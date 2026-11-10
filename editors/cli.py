"""CLI shared by Geany, Vim, Emacs and standalone tooling."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

from .semantic_tokens.engine import Analysis, format_source, SYMBOL_KINDS
from .snippets.engine import load_catalog, expand, generate

IGNORED = {'.git', '.vitte', 'node_modules', 'build', 'target', '.venv', '.build', '__pycache__'}


def sources(root):
    for base, dirs, files in os.walk(root):
        dirs[:] = sorted(item for item in dirs if item not in IGNORED and not item.startswith('.'))
        for name in sorted(files):
            path = Path(base, name)
            if path.suffix in ['.vit', '.vitl', '.vitte']: yield path


def compiler_run(compiler, command, file, extra=()):
    return subprocess.run([compiler, command, '--no-color', str(Path(file).resolve()), *extra]).returncode


def main(argv=None):
    parser = argparse.ArgumentParser(description='Vitte editor tools: formatting, diagnostics, navigation, snippets and LSP.')
    parser.add_argument('--compiler', default=os.environ.get('VITTE_BINARY', 'vitte'))
    commands = parser.add_subparsers(dest='command', required=True)
    for command in ['check', 'build', 'run', 'lex', 'parse', 'ast', 'test']:
        sub = commands.add_parser(command)
        sub.add_argument('file', type=Path)
    for command in ['format', 'format-check', 'format-write']:
        sub = commands.add_parser(command)
        sub.add_argument('file', nargs='?', default='-')
        sub.add_argument('--indent', type=int, default=4, choices=range(1, 17))
        sub.add_argument('--write', action='store_true')
        sub.add_argument('--check', action='store_true')
    for command in ['project-check', 'project-test', 'project-format', 'symbols', 'tags']:
        sub = commands.add_parser(command)
        sub.add_argument('path', type=Path, nargs='?', default=Path.cwd())
        if command == 'tags': sub.add_argument('--output', type=Path, required=True)
    commands.add_parser('doctor')
    sub = commands.add_parser('lsp')
    sub.add_argument('--stdio', action='store_true')
    sub = commands.add_parser('tokens')
    sub.add_argument('file', type=Path)
    sub = commands.add_parser('snippets')
    sub.add_argument('prefix', nargs='?')
    sub.add_argument('--generate', action='store_true')
    sub.add_argument('--check', action='store_true')
    sub.add_argument('--value', action='append', default=[], metavar='NUMBER=TEXT')
    args = parser.parse_args(argv)
    try:
        if args.command == 'doctor':
            compiler = shutil.which(args.compiler)
            print('Python: ' + sys.version.split()[0])
            print('Compiler: ' + (compiler or 'NOT FOUND: ' + args.compiler))
            print('Editor tools: ' + str(Path(__file__).parent))
            load_catalog()
            if not compiler: return 1
            return subprocess.run([compiler, '--version']).returncode
        if args.command == 'lsp':
            from .semantic_tokens.server import Server
            return Server(compiler=args.compiler).run()
        if args.command == 'tokens':
            source = args.file.read_text(encoding='utf-8')
            print(json.dumps({'data': Analysis(source).semantic_tokens()}, indent=2))
            return 0
        if args.command == 'snippets':
            if args.generate or args.check:
                stale = generate(Path(__file__).parent, check=args.check)
                if stale: print('Stale snippet files: ' + ', '.join(stale), file=sys.stderr)
                return int(bool(stale))
            catalog = load_catalog()
            if args.prefix:
                entry = next((item for item in catalog if item['prefix'] == args.prefix), None)
                if entry is None: raise ValueError('Unknown snippet: ' + args.prefix)
                values = dict(item.split('=', 1) for item in args.value)
                print(expand(entry['body'], values))
            else: print(json.dumps(catalog, indent=2))
            return 0
        if args.command.startswith('format'):
            path = None if args.file == '-' else Path(args.file)
            source = path.read_bytes().decode('utf-8') if path else sys.stdin.read()
            formatted = format_source(source, args.indent)
            write = args.write or args.command == 'format-write'
            check = args.check or args.command == 'format-check'
            if check:
                if formatted != source: print(str(path or 'stdin') + ': indentation requires formatting', file=sys.stderr)
                return int(formatted != source)
            if write:
                if path is None: raise ValueError('--write requires a file')
                if formatted != source:
                    # Preserve existing file identity/permissions, including symlink targets.
                    with path.open('w', encoding='utf-8', newline='') as stream: stream.write(formatted)
            else: sys.stdout.write(formatted)
            return 0
        if args.command.startswith('project-'):
            if not args.path.is_dir(): raise ValueError('Project directory does not exist: ' + str(args.path))
            paths = list(sources(args.path))
            if args.command == 'project-test':
                paths = [path for path in paths if any(token.text == 'test' and token.kind == 'keyword' for token in Analysis(path.read_text(encoding='utf-8')).tokens)]
            if not paths: raise ValueError('No Vitte ' + ('tests' if args.command == 'project-test' else 'sources') + ' found')
            status = 0
            for path in paths:
                print('[vitte-editor] ' + str(path), flush=True)
                if args.command == 'project-format':
                    source = path.read_bytes().decode('utf-8')
                    formatted = format_source(source)
                    if formatted != source:
                        with path.open('w', encoding='utf-8', newline='') as stream: stream.write(formatted)
                else:
                    if args.command == 'project-test' and any(t.text == 'main' and t.declaration and t.kind == 'function' for t in Analysis(path.read_text()).tokens):
                        print(str(path) + ': test file must not define main', file=sys.stderr)
                        status = 1
                        continue
                    status = max(status, compiler_run(args.compiler, 'run' if args.command == 'project-test' else 'check', path))
            return status
        if args.command in ['symbols', 'tags']:
            paths = list(sources(args.path)) if args.path.is_dir() else [args.path]
            tags = []
            for path in paths:
                analysis = Analysis(path.read_text(encoding='utf-8'))
                for item in analysis.symbols:
                    if item.kind in ['parameter', 'variable', 'property'] and item.scope: continue
                    line = item.range(analysis.source)['start']['line'] + 1
                    if args.command == 'symbols': print(f'{path}:{line}: {item.kind} {item.text}')
                    else:
                        kind = {'function': 'f', 'macro': 'd', 'class': 's', 'enum': 'g', 'type': 't', 'interface': 's', 'namespace': 'n'}.get(item.kind, 'v')
                        tags.append(f'{item.text}\t{path.resolve()}\t{line};"\t{kind}\tline:{line}')
            if args.command == 'tags':
                args.output.parent.mkdir(parents=True, exist_ok=True)
                args.output.write_text('!_TAG_FILE_FORMAT\t2\t/extended format/\n!_TAG_FILE_SORTED\t1\t/0=unsorted, 1=sorted/\n' + '\n'.join(sorted(set(tags))) + '\n')
                print('Wrote ' + str(args.output))
            return 0
        command = {'build': 'compile', 'test': 'run', 'ast': 'parse'}.get(args.command, args.command)
        if args.command == 'test':
            analysis = Analysis(args.file.read_text(encoding='utf-8'))
            if not any(t.text == 'test' and t.kind == 'keyword' for t in analysis.tokens): raise ValueError('No test declarations in file')
            if any(t.text == 'main' and t.declaration and t.kind == 'function' for t in analysis.tokens): raise ValueError('Test files must not define main; the compiler generates their test entry point')
        return compiler_run(args.compiler, command, args.file, ['--emit-ast'] if args.command == 'ast' else [])
    except (OSError, ValueError, UnicodeError) as error:
        print('vitte-editor: ' + str(error), file=sys.stderr)
        return 1

if __name__ == '__main__':
    raise SystemExit(main())
