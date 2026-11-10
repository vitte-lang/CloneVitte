"""Dependency-free stdio Language Server Protocol implementation for Vitte."""
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import sys
import threading
from urllib.parse import unquote, urlparse

from .diagnostics import compiler_diagnostics
from .engine import Analysis, KEYWORDS, TYPES, LITERALS, SYMBOL_KINDS, TOKEN_TYPES, TOKEN_MODIFIERS, format_source, offset_at, position_at, utf16
from ..snippets.engine import completion_items


def uri_path(uri):
    parsed = urlparse(uri)
    if parsed.scheme != 'file': raise ValueError('Only file:// documents are supported')
    path = unquote(parsed.path)
    if os.name == 'nt' and path.startswith('/') and len(path) > 2 and path[2] == ':': path = path[1:]
    return Path(path)


class Server:
    def __init__(self, reader=None, writer=None, compiler='vitte'):
        self.reader = reader or sys.stdin.buffer
        self.writer = writer or sys.stdout.buffer
        self.compiler = compiler
        self.documents = {}
        self.root = Path.cwd()
        self.shutdown = False
        self.exit_requested = False
        self.lock = threading.RLock()
        self.pool = ThreadPoolExecutor(max_workers=1)
        self.semantic_cache = {}
        self.analysis_cache = {}
        self.diagnostics_enabled = True
        self.snippets_enabled = False

    def send(self, message):
        data = json.dumps(message, ensure_ascii=False).encode('utf-8')
        with self.lock:
            self.writer.write(f'Content-Length: {len(data)}\r\n\r\n'.encode() + data)
            self.writer.flush()

    def notify(self, method, params):
        self.send({'jsonrpc': '2.0', 'method': method, 'params': params})

    def read(self):
        headers = {}
        while True:
            line = self.reader.readline()
            if not line: return None
            if line in (b'\r\n', b'\n'): break
            name, value = line.decode('ascii').split(':', 1)
            headers[name.lower()] = value.strip()
        length = int(headers.get('content-length', '0'))
        if length <= 0 or length > 16 * 1024 * 1024: raise ValueError('Invalid LSP Content-Length')
        data = bytearray()
        while len(data) < length:
            part = self.reader.read(length - len(data))
            if not part: raise ValueError('Truncated LSP message')
            data.extend(part)
        return json.loads(data)

    def document(self, uri):
        if uri in self.documents: return self.documents[uri]
        return {'text': uri_path(uri).read_text(encoding='utf-8'), 'version': None}

    def analysis(self, uri):
        if uri in self.documents:
            source = self.documents[uri]['text']
            cached = self.analysis_cache.get(uri)
            if cached and cached[0] == source: return cached[1]
            result = Analysis(source)
            self.analysis_cache[uri] = (source, result, None)
            return result
        path = uri_path(uri)
        info = path.stat()
        signature = (info.st_mtime_ns, info.st_size)
        cached = self.analysis_cache.get(uri)
        if cached and cached[2] == signature: return cached[1]
        source = path.read_text(encoding='utf-8')
        result = Analysis(source)
        self.analysis_cache[uri] = (source, result, signature)
        return result

    def completions(self, uri, analysis, position):
        """Offer keywords, visible local declarations, workspace symbols and snippets."""
        offset = offset_at(analysis.source, position)
        scope = analysis.scope_at(offset)
        selected = {}
        kinds = {'namespace': 9, 'class': 7, 'enum': 13, 'interface': 8, 'type': 7,
                 'function': 3, 'macro': 21, 'variable': 6, 'parameter': 6,
                 'property': 10, 'enumMember': 20}

        def add_symbol(item, owner, local):
            if local:
                if item.scope and (scope[:len(item.scope)] != item.scope or item.start > offset): return
            elif item.scope:
                return
            priority = 0 if local else 1
            rank = (priority, -len(item.scope), -item.start)
            existing = selected.get(item.text)
            if existing and existing[0] <= rank: return
            selected[item.text] = (rank, {'label': item.text, 'kind': kinds.get(item.kind, 6),
                                         'detail': item.kind + (' · ' + Path(uri_path(owner)).name if owner != uri else '')})

        for item in analysis.symbols: add_symbol(item, uri, True)
        for other_uri, other in self.workspace():
            if other_uri == uri: continue
            for item in other.symbols: add_symbol(item, other_uri, False)

        items = [{'label': word, 'kind': 14 if word in KEYWORDS else 7 if word in TYPES else 21,
                  'sortText': '2' + word} for word in sorted(KEYWORDS | TYPES | LITERALS)]
        items.extend(dict(value[1], sortText=str(value[0][0]) + value[1]['label'])
                     for value in selected.values())
        if self.snippets_enabled:
            items.extend(dict(item, sortText='3' + item['label']) for item in completion_items())
        items.sort(key=lambda item: (item.get('sortText', item['label']), item['label']))
        return {'isIncomplete': False, 'items': items}

    def schedule_diagnostics(self, uri):
        if not self.diagnostics_enabled: return
        document = self.documents.get(uri)
        if document is None: return
        snapshot = document.copy()
        def check():
            if self.documents.get(uri) != snapshot: return
            try:
                items = compiler_diagnostics(snapshot['text'], uri_path(uri), self.compiler)
            except Exception as error:
                if self.documents.get(uri) == snapshot:
                    self.notify('window/logMessage', {'type': 1, 'message': 'Vitte diagnostics: ' + str(error)})
                return
            with self.lock:
                if self.documents.get(uri) == snapshot:
                    self.notify('textDocument/publishDiagnostics', {'uri': uri, 'version': snapshot['version'], 'diagnostics': items})
        self.pool.submit(check)

    def workspace(self):
        # Bound disk work and exclude dependency/build trees. Open buffers win.
        count = 0
        ignored = {'.git', 'node_modules', '.vitte', 'build', 'target', '.venv', '.build'}
        seen = set()
        for base, dirs, files in os.walk(self.root):
            dirs[:] = sorted(d for d in dirs if d not in ignored and not d.startswith('.'))
            for filename in sorted(files):
                path = Path(base, filename)
                if path.suffix not in ['.vit', '.vitl', '.vitte']: continue
                uri = path.absolute().as_uri()
                seen.add(uri)
                try:
                    yield uri, self.analysis(uri)
                except (OSError, UnicodeError): continue
                count += 1
                if count >= 1000: return
        for uri in list(self.documents):
            if uri not in seen: yield uri, self.analysis(uri)

    @staticmethod
    def signature_help(analysis, position):
        """Return a local procedure signature and the active argument, if any."""
        source = analysis.source
        cursor = offset_at(source, position)
        pairs = {'(': ')', '[': ']', '{': '}'}
        stack = []
        for index, token in enumerate(analysis.tokens):
            if token.start >= cursor: break
            if token.kind in ['comment', 'string']: continue
            if token.text in pairs: stack.append((token.text, index))
            elif token.text in pairs.values() and stack and pairs[stack[-1][0]] == token.text:
                stack.pop()
        opening = next((index for char, index in reversed(stack) if char == '('), None)
        if opening is None: return None

        tokens = analysis.tokens
        name_index = opening - 1
        while name_index >= 0 and tokens[name_index].text in ['>', ']', ')']:
            name_index -= 1
        if name_index < 0: return None
        name = tokens[name_index]
        if name.kind in ['keyword', 'operator', 'comment', 'string']: return None
        function = next((item for item in analysis.symbols if item.text == name.text and item.kind == 'function' and item.declaration), None)
        if function is None: return None

        declaration_index = next((i for i, token in enumerate(tokens) if token.start == function.start), None)
        if declaration_index is None: return None
        declaration_open = next((i for i in range(declaration_index + 1, len(tokens)) if tokens[i].text == '('), None)
        if declaration_open is None: return None
        declaration_stack = 0
        declaration_close = None
        for i in range(declaration_open, len(tokens)):
            token = tokens[i]
            if token.kind in ['comment', 'string']: continue
            if token.text == '(': declaration_stack += 1
            elif token.text == ')':
                declaration_stack -= 1
                if declaration_stack == 0:
                    declaration_close = i
                    break
        if declaration_close is None: return None

        # Include a trailing return type while stopping before the function body.
        end_offset = tokens[declaration_close].end
        nesting = 0
        for token in tokens[declaration_close + 1:]:
            if token.kind in ['comment', 'string']: continue
            if token.text in ['(', '[', '<']: nesting += 1
            elif token.text in [')', ']', '>']: nesting = max(0, nesting - 1)
            elif nesting == 0 and token.text in ['{', ';']:
                break
            end_offset = token.end
        label = source[function.start:end_offset]

        # Convert top-level comma-separated parameter ranges to LSP UTF-16 offsets.
        parameter_ranges = []
        segment_start = tokens[declaration_open].end
        nesting = []
        close_offset = tokens[declaration_close].start
        delimiter_pairs = {'(': ')', '[': ']', '{': '}', '<': '>'}
        for token in tokens[declaration_open + 1:declaration_close]:
            if token.kind in ['comment', 'string']: continue
            if token.text in delimiter_pairs: nesting.append(token.text)
            elif token.text in delimiter_pairs.values() and nesting and delimiter_pairs[nesting[-1]] == token.text:
                nesting.pop()
            if token.text == ',' and not nesting:
                left = source[segment_start:token.start].strip()
                if left:
                    start = source.find(left, segment_start, token.start)
                    parameter_ranges.append([utf16(source[function.start:start]), utf16(source[function.start:start + len(left)])])
                segment_start = token.end
        last = source[segment_start:close_offset].strip()
        if last:
            start = source.find(last, segment_start, close_offset)
            parameter_ranges.append([utf16(source[function.start:start]), utf16(source[function.start:start + len(last)])])

        active = 0
        nesting = []
        for token in tokens[opening + 1:]:
            if token.start >= cursor: break
            if token.kind in ['comment', 'string']: continue
            if token.text in delimiter_pairs: nesting.append(token.text)
            elif token.text in delimiter_pairs.values() and nesting and delimiter_pairs[nesting[-1]] == token.text:
                nesting.pop()
            elif token.text == ',' and not nesting: active += 1
        if parameter_ranges: active = min(active, len(parameter_ranges) - 1)
        return {'signatures': [{'label': label, 'parameters': [{'label': value} for value in parameter_ranges]}],
                'activeSignature': 0, 'activeParameter': active}

    def dispatch(self, method, params):
        if method == 'initialize':
            root = params.get('rootUri') or next(iter(params.get('workspaceFolders') or []), {}).get('uri')
            if root: self.root = uri_path(root)
            options = params.get('initializationOptions') or {}
            self.compiler = options.get('compiler', self.compiler)
            self.diagnostics_enabled = options.get('diagnostics', True)
            self.snippets_enabled = params.get('capabilities', {}).get('textDocument', {}).get('completion', {}).get('completionItem', {}).get('snippetSupport', False)
            return {'serverInfo': {'name': 'vitte-editor', 'version': '1.0.0'}, 'capabilities': {
                'positionEncoding': 'utf-16', 'textDocumentSync': {'openClose': True, 'change': 2, 'save': {'includeText': True}},
                'semanticTokensProvider': {'legend': {'tokenTypes': TOKEN_TYPES, 'tokenModifiers': TOKEN_MODIFIERS}, 'full': {'delta': True}, 'range': False},
                'completionProvider': {'triggerCharacters': ['.', ':']}, 'hoverProvider': True, 'definitionProvider': True,
                'referencesProvider': True, 'documentSymbolProvider': True, 'workspaceSymbolProvider': True,
                'documentFormattingProvider': True,
                'signatureHelpProvider': {'triggerCharacters': ['(', ','], 'retriggerCharacters': [',']}}}
        if method == 'shutdown': self.shutdown = True; return None
        if method == 'exit': self.exit_requested = True; return None
        if method in ['initialized', '$/cancelRequest', '$/setTrace']: return None
        if self.shutdown: raise ValueError('Server has shut down')
        if method == 'workspace/didChangeConfiguration':
            settings = params.get('settings', {}).get('vitte', {})
            self.compiler = settings.get('compiler', self.compiler)
            self.diagnostics_enabled = settings.get('diagnostics', self.diagnostics_enabled)
            return None
        if method == 'workspace/symbol':
            query = params.get('query', '').lower()
            return [{'name': t.text, 'kind': SYMBOL_KINDS[t.kind], 'location': {'uri': uri, 'range': t.range(a.source)}} for uri, a in self.workspace() for t in a.symbols if t.kind != 'parameter' and query in t.text.lower()][:500]
        uri = params.get('textDocument', {}).get('uri')
        if method == 'textDocument/didOpen':
            doc = params['textDocument']
            self.documents[uri] = {'text': doc['text'], 'version': doc.get('version')}
            self.schedule_diagnostics(uri)
            return None
        if method == 'textDocument/didChange':
            doc = self.documents[uri]
            version = params['textDocument'].get('version')
            if doc['version'] is not None and version is not None and version <= doc['version']: return None
            text = doc['text']
            for change in params['contentChanges']:
                if 'range' in change:
                    start, end = (offset_at(text, change['range'][side]) for side in ['start', 'end'])
                    text = text[:start] + change['text'] + text[end:]
                else: text = change['text']
            self.documents[uri] = {'text': text, 'version': version}
            self.schedule_diagnostics(uri)
            return None
        if method == 'textDocument/didSave':
            if 'text' in params: self.documents[uri]['text'] = params['text']
            self.schedule_diagnostics(uri)
            return None
        if method == 'textDocument/didClose':
            self.documents.pop(uri, None)
            self.semantic_cache.pop(uri, None)
            self.analysis_cache.pop(uri, None)
            self.notify('textDocument/publishDiagnostics', {'uri': uri, 'diagnostics': []})
            return None
        if not uri: raise NotImplementedError(method)
        analysis = self.analysis(uri)
        if method.startswith('textDocument/semanticTokens/full'):
            data = analysis.semantic_tokens()
            result_id = hashlib.sha256(json.dumps(data).encode()).hexdigest()[:16]
            previous = self.semantic_cache.get(uri)
            self.semantic_cache[uri] = (result_id, data)
            if method.endswith('/delta') and previous and params.get('previousResultId') == previous[0]:
                edits = [] if previous[1] == data else [{'start': 0, 'deleteCount': len(previous[1]), 'data': data}]
                return {'resultId': result_id, 'edits': edits}
            return {'resultId': result_id, 'data': data}
        if method == 'textDocument/documentSymbol': return analysis.document_symbols()
        if method == 'textDocument/formatting':
            options = params.get('options', {})
            formatted = format_source(analysis.source, max(1, min(16, options.get('tabSize', 4))))
            return [] if formatted == analysis.source else [{'range': {'start': {'line': 0, 'character': 0}, 'end': position_at(analysis.source, len(analysis.source))}, 'newText': formatted}]
        if method == 'textDocument/completion':
            return self.completions(uri, analysis, params.get('position', {'line': 0, 'character': 0}))
        if method == 'textDocument/signatureHelp':
            return self.signature_help(analysis, params.get('position', {'line': 0, 'character': 0}))
        token = analysis.token_at(params.get('position', {'line': 0, 'character': 0}))
        if method == 'textDocument/hover':
            if not token or token.kind in ['operator', 'comment', 'string', 'number']: return None
            target = analysis.resolve(token)
            description = f'{target.kind} {target.text}' if target else f'{token.kind} {token.text}'
            return {'contents': {'kind': 'plaintext', 'value': description}, 'range': token.range(analysis.source)}
        if method == 'textDocument/definition':
            if not token: return None
            target = analysis.resolve(token)
            if target: return {'uri': uri, 'range': target.range(analysis.source)}
            for target_uri, candidate in self.workspace():
                for item in candidate.symbols:
                    if item.text == token.text and not item.scope:
                        return {'uri': target_uri, 'range': item.range(candidate.source)}
            return None
        if method == 'textDocument/references':
            if not token: return []
            target = analysis.resolve(token)
            include = params.get('context', {}).get('includeDeclaration', False)
            return [{'uri': uri, 'range': item.range(analysis.source)} for item in analysis.tokens if item.text == token.text and (include or not item.declaration) and (target is None or analysis.resolve(item) is target)]
        raise NotImplementedError(method)

    def run(self):
        try:
            while not self.exit_requested:
                message = self.read()
                if message is None: break
                request_id = message.get('id')
                try:
                    result = self.dispatch(message['method'], message.get('params') or {})
                    if 'id' in message: self.send({'jsonrpc': '2.0', 'id': request_id, 'result': result})
                except Exception as error:
                    if 'id' in message:
                        self.send({'jsonrpc': '2.0', 'id': request_id, 'error': {'code': -32601 if isinstance(error, NotImplementedError) else -32602, 'message': str(error)}})
                    else: self.notify('window/logMessage', {'type': 1, 'message': str(error)})
        finally:
            self.documents.clear()
            self.pool.shutdown(wait=True, cancel_futures=True)
        return 0 if self.shutdown else 1
