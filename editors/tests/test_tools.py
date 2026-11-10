"""End-to-end regressions for language tools, protocol and shell completions."""
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from editors.semantic_tokens.engine import Analysis, lex, format_source, offset_at, position_at, utf16
from editors.semantic_tokens.diagnostics import compiler_diagnostics
from editors.semantic_tokens.server import Server
from editors.snippets.engine import load_catalog, expand, generate


class LanguageTools(unittest.TestCase):
    def test_lossless_formatting(self):
        source = 'proc main() {\nlet url = "https://x/{";\n/* { /* nested */ } */\ncall(\n1,\n2\n);\n}\n'
        formatted = format_source(source)
        self.assertEqual([t.text for t in lex(source)], [t.text for t in lex(formatted)])
        self.assertEqual(formatted, format_source(formatted))
        self.assertIn('        2\n    );', formatted)
        self.assertIn('    let url', formatted)

    def test_multiline_literals_preserved(self):
        source = 'proc main() {\nlet a = "first\n literal content\nlast";\n/* comment\n   original alignment\n*/\n}\n'
        self.assertIn('"first\n literal content\nlast"', format_source(source))
        self.assertIn('/* comment\n   original alignment\n*/', format_source(source))

    def test_utf16(self):
        source = 'a😀é\n第二行'
        for offset in range(len(source) + 1):
            self.assertEqual(offset_at(source, position_at(source, offset)), offset)
        self.assertEqual(utf16('😀'), 2)
        self.assertEqual(position_at(source, 2), {'line': 0, 'character': 3})

    def test_symbols_and_shadowing(self):
        source = 'proc foo(x: i32) { let y = x; if true { let y = 2; use_it(y); } use_it(y); }'
        analysis = Analysis(source)
        ys = [t for t in analysis.tokens if t.text == 'y']
        self.assertEqual(len(ys), 4)
        self.assertIs(analysis.resolve(ys[2]), ys[1])
        self.assertIs(analysis.resolve(ys[3]), ys[0])
        xs = [t for t in analysis.tokens if t.text == 'x']
        self.assertEqual(xs[0].kind, 'parameter')
        self.assertIs(analysis.resolve(xs[1]), xs[0])
        self.assertEqual(len(analysis.semantic_tokens()) % 5, 0)

    def test_comments_and_strings_have_no_symbols(self):
        analysis = Analysis('// proc fake() {}\nproc real() { let s = "let fake = 2"; /* form Fake {} */ }')
        self.assertEqual([t.text for t in analysis.symbols], ['real', 's'])

    def test_snippets_generated_and_expansion(self):
        self.assertFalse(generate(ROOT / 'editors', check=True))
        self.assertGreater(len(load_catalog()), 20)
        geany = (ROOT / 'editors/geany/snippets.vitte.conf').read_text()
        self.assertIn('proc name%cursor%(', geany)
        self.assertIn('value%cursor%', geany)
        for entry in load_catalog():
            self.assertNotIn('${', expand(entry['body']))
            self.assertNotIn('$0', expand(entry['body']))
        self.assertEqual(expand('${1:name}($1)$0', {'1': 'hello'}), 'hello(hello)')

    def test_cli_format_roundtrip_and_status(self):
        with tempfile.TemporaryDirectory(prefix='vitte format ') as directory:
            path = Path(directory, 'file with spaces.vit')
            path.write_text('proc main() {\nlet x = 1;\n}\n')
            helper = [sys.executable, str(ROOT / 'editors/vitte-editor')]
            self.assertEqual(subprocess.run(helper + ['format-check', str(path)], capture_output=True).returncode, 1)
            self.assertEqual(subprocess.run(helper + ['format-write', str(path)], capture_output=True).returncode, 0)
            self.assertEqual(subprocess.run(helper + ['format-check', str(path)], capture_output=True).returncode, 0)
            self.assertIn('    let', path.read_text())

    @unittest.skipUnless(shutil.which('vitte'), 'Compiler unavailable')
    def test_compiler_diagnostics_unsaved_buffer(self):
        with tempfile.TemporaryDirectory(prefix='vitte diagnostic ') as directory:
            path = Path(directory, 'unsaved file.vit')
            path.write_text('proc main() {}')
            diagnostics = compiler_diagnostics('proc main() { let x = ; }', path)
            self.assertTrue(diagnostics)
            self.assertEqual(diagnostics[0]['code'], 'E0203')
            self.assertEqual(path.read_text(), 'proc main() {}')
            self.assertEqual(list(Path(directory).glob('.vitte-editor-*')), [])
            self.assertEqual(compiler_diagnostics('proc main() {}', path), [])

    def test_lsp_incremental_delta_and_navigation(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory, 'main.vit')
            uri = path.as_uri()
            server = Server(writer=io.BytesIO())
            try:
                capabilities = server.dispatch('initialize', {'rootUri': Path(directory).as_uri(), 'initializationOptions': {'diagnostics': False}, 'capabilities': {'textDocument': {'completion': {'completionItem': {'snippetSupport': True}}}}})
                self.assertEqual(capabilities['capabilities']['positionEncoding'], 'utf-16')
                source = 'proc main() { let value = "😀"; use_it(value); }'
                server.dispatch('textDocument/didOpen', {'textDocument': {'uri': uri, 'text': source, 'version': 1}})
                params = {'textDocument': {'uri': uri}}
                first = server.dispatch('textDocument/semanticTokens/full', params)
                same = server.dispatch('textDocument/semanticTokens/full/delta', dict(params, previousResultId=first['resultId']))
                self.assertEqual(same['edits'], [])
                reference_pos = position_at(source, source.rindex('value'))
                definition = server.dispatch('textDocument/definition', dict(params, position=reference_pos))
                self.assertEqual(definition['range']['start'], position_at(source, source.index('value')))
                refs = server.dispatch('textDocument/references', dict(params, position=reference_pos, context={'includeDeclaration': True}))
                self.assertEqual(len(refs), 2)
                completions = server.dispatch('textDocument/completion', dict(params, position=reference_pos))
                self.assertTrue(any(item.get('insertTextFormat') == 2 for item in completions['items']))
                start = source.index('😀')
                server.dispatch('textDocument/didChange', {'textDocument': {'uri': uri, 'version': 2}, 'contentChanges': [{'range': {'start': position_at(source, start), 'end': position_at(source, start + 1)}, 'text': 'é'}]})
                self.assertEqual(server.documents[uri]['text'], source.replace('😀', 'é'))
                delta = server.dispatch('textDocument/semanticTokens/full/delta', dict(params, previousResultId=first['resultId']))
                self.assertTrue(delta['edits'])
                server.dispatch('textDocument/didClose', params)
                self.assertNotIn(uri, server.documents)
            finally: server.pool.shutdown(wait=True)

    def test_signature_help(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory, 'main.vit')
            uri = path.as_uri()
            server = Server(writer=io.BytesIO())
            try:
                capabilities = server.dispatch('initialize', {'rootUri': Path(directory).as_uri()})['capabilities']
                self.assertEqual(capabilities['signatureHelpProvider']['triggerCharacters'], ['(', ','])
                source = 'proc sum(left: i32, right: i32) -> i32 { give left; }\nproc main() { sum(1, 2); }'
                server.dispatch('textDocument/didOpen', {'textDocument': {'uri': uri, 'text': source, 'version': 1}})
                position = position_at(source, source.index('2)') + 1)
                result = server.dispatch('textDocument/signatureHelp', {'textDocument': {'uri': uri}, 'position': position})
                self.assertEqual(result['activeParameter'], 1)
                self.assertEqual(len(result['signatures'][0]['parameters']), 2)
                label = result['signatures'][0]['label']
                first = result['signatures'][0]['parameters'][0]['label']
                self.assertEqual(label[slice(*first)], 'left: i32')
            finally: server.pool.shutdown(wait=True)

    def test_completion_scope_and_workspace_symbols(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / 'main.vit'
            (root / 'lib.vit').write_text('proc external_api() {}\n')
            uri = path.as_uri()
            server = Server(writer=io.BytesIO())
            try:
                server.dispatch('initialize', {'rootUri': root.as_uri()})
                source = 'proc main() {\n    let visible: i32 = 1;\n    if true { let nested: i32 = 2; }\n    visible;\n}\n'
                server.dispatch('textDocument/didOpen', {'textDocument': {'uri': uri, 'text': source, 'version': 1}})
                result = server.dispatch('textDocument/completion', {'textDocument': {'uri': uri}, 'position': position_at(source, source.rindex('visible'))})
                labels = {item['label'] for item in result['items']}
                self.assertIn('visible', labels)
                self.assertNotIn('nested', labels)
                self.assertIn('external_api', labels)
            finally: server.pool.shutdown(wait=True)

    def test_stdio_protocol(self):
        messages = [{'jsonrpc': '2.0', 'id': 1, 'method': 'initialize', 'params': {'initializationOptions': {'diagnostics': False}}}, {'jsonrpc': '2.0', 'id': 2, 'method': 'shutdown'}, {'jsonrpc': '2.0', 'method': 'exit'}]
        payload = b''
        for item in messages:
            raw = json.dumps(item).encode()
            payload += f'Content-Length: {len(raw)}\r\n\r\n'.encode() + raw
        result = subprocess.run([sys.executable, str(ROOT / 'editors/vitte-editor'), 'lsp', '--stdio'], input=payload, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        reader = Server(reader=io.BytesIO(result.stdout), writer=io.BytesIO())
        try:
            self.assertEqual(reader.read()['id'], 1)
            self.assertEqual(reader.read(), {'jsonrpc': '2.0', 'id': 2, 'result': None})
            self.assertIsNone(reader.read())
        finally: reader.pool.shutdown(wait=True)


class Completions(unittest.TestCase):
    def bash(self, words):
        with tempfile.TemporaryDirectory(prefix='vitte complete ') as directory:
            Path(directory, 'with spaces.vit').touch()
            Path(directory, 'other.txt').touch()
            env = dict(os.environ, VITTE_COMPLETION=str(ROOT / 'completions/bash/vitte'), WORDS=json.dumps(words))
            # Pass words as positional arguments, preserving spaces and metacharacters.
            script = 'source "$VITTE_COMPLETION"; COMP_WORDS=("$@"); COMP_CWORD=$((${#COMP_WORDS[@]}-1)); _vitte_complete; printf "%s\\n" "${COMPREPLY[@]}"'
            result = subprocess.run(['bash', '-c', script, 'bash', *words], cwd=directory, env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            return result.stdout.splitlines()

    def test_current_commands(self):
        words = self.bash(['vitte', ''])
        self.assertIn('compile', words)
        self.assertIn('run', words)
        self.assertNotIn('doctor', words)
        self.assertNotIn('build', words)

    def test_enum_options(self):
        self.assertEqual(self.bash(['vitte', 'check', '--diagnostic=j']), ['--diagnostic=json'])
        self.assertEqual(self.bash(['vitte', 'check', '--diagnostic', '=', 'j']), ['json'])
        self.assertEqual(self.bash(['vitte', '--no-color', 'completion', 'f']), ['fish'])
        self.assertEqual(self.bash(['vitte', 'compile', '--backend', 'c']), ['c17'])

    def test_paths_with_spaces_and_forward_args(self):
        self.assertIn('with spaces.vit', self.bash(['vitte', 'check', 'with']))
        self.assertIn('with spaces.vit', self.bash(['vitte', 'compile', '-o', 'with']))
        self.assertNotIn('other.txt', self.bash(['vitte', 'check', '']))
        self.assertIn('other.txt', self.bash(['vitte', 'run', 'with spaces.vit', '--', 'other']))

    def test_generation_and_syntax(self):
        subprocess.run([sys.executable, 'tools/generate_completions.py', '--check'], cwd=ROOT, check=True, capture_output=True)
        for command in [['bash', '-n', 'completions/bash/vitte'], ['zsh', '-n', 'completions/zsh/_vitte'], ['fish', '-n', 'completions/fish/vitte.fish']]:
            if shutil.which(command[0]): subprocess.run(command, cwd=ROOT, check=True, capture_output=True)

if __name__ == '__main__': unittest.main(verbosity=2)
