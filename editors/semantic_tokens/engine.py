"""Lossless Vitte lexer, scope-aware symbols, highlighting and formatting."""
from dataclasses import dataclass
import json
from pathlib import Path
import re

GRAMMAR = json.loads((Path(__file__).resolve().parents[1] / 'grammar/vitte_highlight_grammar.json').read_text())
KEYWORDS = set(GRAMMAR['keywords'])
TYPES = set(GRAMMAR['types'])
LITERALS = set(GRAMMAR['literals'])
TOKEN_TYPES = ['namespace', 'type', 'class', 'enum', 'interface', 'parameter', 'variable', 'property', 'enumMember', 'function', 'macro', 'keyword', 'modifier', 'comment', 'string', 'number', 'operator']
TOKEN_MODIFIERS = ['declaration', 'readonly', 'static', 'async', 'defaultLibrary']
DECLARATIONS = {'space': 'namespace', 'form': 'class', 'pick': 'enum', 'trait': 'interface', 'type': 'type', 'opaque': 'type', 'proc': 'function', 'intrinsic': 'function', 'macro': 'macro', 'test': 'function', 'let': 'variable', 'const': 'variable', 'static': 'variable', 'for': 'variable', 'as': 'namespace'}
SYMBOL_KINDS = {'namespace': 3, 'class': 5, 'enum': 10, 'interface': 11, 'type': 26, 'function': 12, 'macro': 12, 'variable': 13, 'parameter': 13, 'property': 7, 'enumMember': 22}
NUMBER = re.compile(GRAMMAR['number_regex'])
IDENTIFIER = re.compile(r'[^\W\d]\w*', re.UNICODE)


def utf16(text):
    return len(text.encode('utf-16-le')) // 2


def offset_at(text, position):
    lines = text.splitlines(keepends=True)
    line = position['line']
    if line >= len(lines): return len(text)
    start = sum(map(len, lines[:line]))
    target = position['character']
    count = 0
    for index, char in enumerate(lines[line]):
        if count >= target: return start + index
        count += utf16(char)
    return start + len(lines[line])


def position_at(text, offset):
    prefix = text[:offset]
    return {'line': prefix.count('\n'), 'character': utf16(prefix.rsplit('\n', 1)[-1])}


@dataclass
class Token:
    text: str
    start: int
    end: int
    kind: str
    declaration: bool = False
    scope: tuple = ()

    def range(self, source):
        return {'start': position_at(source, self.start), 'end': position_at(source, self.end)}


def lex(source):
    tokens = []
    index = 0
    while index < len(source):
        start = index
        c = source[index]
        if c.isspace(): index += 1; continue
        if source.startswith('//', index):
            end = source.find('\n', index)
            index = len(source) if end < 0 else end
            kind = 'comment'
        elif source.startswith('/*', index):
            index += 2
            depth = 1
            while index < len(source) and depth:
                if source.startswith('/*', index): depth += 1; index += 2
                elif source.startswith('*/', index): depth -= 1; index += 2
                else: index += 1
            kind = 'comment'
        elif c in '\"\'':
            quote = c
            index += 1
            while index < len(source):
                if source[index] == '\\': index = min(len(source), index + 2)
                elif source[index] == quote: index += 1; break
                else: index += 1
            kind = 'string'
        elif c.isdigit() and (match := NUMBER.match(source, index)):
            index = match.end()
            kind = 'number'
        elif match := IDENTIFIER.match(source, index):
            index = match.end()
            word = match.group()
            kind = 'keyword' if word in KEYWORDS or word in LITERALS else 'type' if word in TYPES else 'variable'
        else:
            match = re.match(r'(?:<<=|>>=|\.\.=|::|->|=>|==|!=|<=|>=|&&|\|\||\?\?|\.\.|[+*/%&|^<>!-]=|.)', source[index:])
            index += len(match.group())
            kind = 'operator'
        tokens.append(Token(source[start:index], start, index, kind))
    return tokens


class Analysis:
    def __init__(self, source):
        self.source = source
        self.tokens = lex(source)
        code = [token for token in self.tokens if token.kind != 'comment']
        stack = []
        pending_parameters = []
        parameter_depth = 0
        in_parameters = False
        procedure_pending = False
        declaration_kind = None
        for index, token in enumerate(code):
            word = token.text
            prev = code[index - 1].text if index else ''
            nxt = code[index + 1].text if index + 1 < len(code) else ''
            token.scope = tuple(stack)
            if word == '{':
                stack.append(token.start)
                for param in pending_parameters: param.scope = tuple(stack)
                pending_parameters = []
                procedure_pending = False
            elif word == '}':
                if stack: stack.pop()
            elif word == ';' and not in_parameters:
                pending_parameters = []
                procedure_pending = False
            if token.kind not in ['variable', 'type'] or word in TYPES:
                if word in DECLARATIONS:
                    declaration_kind = DECLARATIONS[word]
                    if word in ['proc', 'intrinsic', 'macro']: procedure_pending = True
                elif word == 'mut' and declaration_kind:
                    pass
                elif word not in ['pub', 'export', 'async', 'extern']:
                    declaration_kind = None
            elif declaration_kind:
                token.kind = declaration_kind
                token.declaration = True
                declaration_kind = None
            elif in_parameters and nxt == ':' and prev in ['(', ',']:
                token.kind = 'parameter'
                token.declaration = True
                pending_parameters.append(token)
            elif nxt == ':' and prev in ['{', ',', ';']:
                token.kind = 'property'
                token.declaration = True
            elif prev == '.': token.kind = 'property'
            elif nxt == '(': token.kind = 'function'
            if procedure_pending and word == '(':
                in_parameters = True
                parameter_depth += 1
            elif in_parameters and word == ')':
                parameter_depth -= 1
                if parameter_depth == 0: in_parameters = False
        self.symbols = [token for token in code if token.declaration]
        for token in code:
            if token.kind == 'variable' and not token.declaration:
                declaration = self.resolve(token)
                if declaration: token.kind = declaration.kind

    def token_at(self, position):
        offset = offset_at(self.source, position)
        return next((token for token in self.tokens if token.start <= offset < token.end), None)

    def scope_at(self, offset):
        """Return the lexical brace scope containing a source offset."""
        stack = []
        for token in self.tokens:
            if token.start >= offset: break
            if token.kind in ['comment', 'string']: continue
            if token.text == '{': stack.append(token.start)
            elif token.text == '}' and stack: stack.pop()
        return tuple(stack)

    def resolve(self, token):
        candidates = [item for item in self.symbols if item.text == token.text and token.scope[:len(item.scope)] == item.scope and (item.start <= token.start or item.kind in ['function', 'type', 'class', 'enum', 'interface', 'namespace'])]
        return max(candidates, key=lambda item: (len(item.scope), item.start), default=None)

    def semantic_tokens(self):
        data = []
        previous_line = previous_column = 0
        for token in self.tokens:
            position = position_at(self.source, token.start)
            for part in token.text.splitlines(keepends=True):
                value = part.rstrip('\r\n')
                if value:
                    line, column = position['line'], position['character']
                    modifiers = int(token.declaration)
                    if token.text in TYPES: modifiers |= 1 << 4
                    data.extend([line - previous_line, column - previous_column if line == previous_line else column, utf16(value), TOKEN_TYPES.index(token.kind), modifiers])
                    previous_line, previous_column = line, column
                position = {'line': position['line'] + 1, 'character': 0}
        return data

    def document_symbols(self):
        return [{'name': t.text, 'kind': SYMBOL_KINDS[t.kind], 'range': t.range(self.source), 'selectionRange': t.range(self.source)} for t in self.symbols if t.kind != 'parameter']


def format_source(source, width=4):
    """Normalize leading indentation only; preserve strings/comments verbatim."""
    tokens = lex(source)
    lines = source.splitlines(keepends=True)
    stack = []
    output = []
    offset = 0
    token_index = 0
    for line in lines:
        first = offset + len(line) - len(line.lstrip(' \t'))
        while token_index < len(tokens) and tokens[token_index].end <= offset: token_index += 1
        covering = tokens[token_index] if token_index < len(tokens) else None
        inside = covering and covering.start < offset < covering.end and covering.kind in ['string', 'comment']
        stripped = line.lstrip(' \t')
        closing = stripped[:1] in ['}', ')', ']']
        level = max(0, len(stack) - int(closing))
        if inside: output.append(line)
        elif stripped.strip(): output.append(' ' * (level * width) + stripped)
        else: output.append(line.lstrip(' \t'))
        index = token_index
        while index < len(tokens) and tokens[index].start < offset + len(line):
            token = tokens[index]
            if token.kind == 'operator':
                if token.text in ['{', '(', '[']: stack.append(token.text)
                elif token.text in ['}', ')', ']'] and stack: stack.pop()
            index += 1
        offset += len(line)
    return ''.join(output)
