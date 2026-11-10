"""Validated snippet catalog, expansion and native editor exporters."""
import json
from pathlib import Path
import re

CATALOG = Path(__file__).with_name('catalog.json')
PLACEHOLDER = re.compile(r'\$\{(\d+)(?::([^{}]*))?\}|\$(\d+)')


def load_catalog():
    entries = json.loads(CATALOG.read_text())
    seen = set()
    for entry in entries:
        if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', entry['prefix']) or entry['prefix'] in seen:
            raise ValueError('Duplicate or invalid snippet prefix: ' + entry['prefix'])
        seen.add(entry['prefix'])
        if not isinstance(entry['body'], str) or not entry['body']:
            raise ValueError('Empty snippet: ' + entry['prefix'])
        remainder = PLACEHOLDER.sub('', entry['body'])
        if '${' in remainder: raise ValueError('Unsupported nested placeholder: ' + entry['prefix'])
    return entries


def expand(body, values=None):
    values = {str(key): str(value) for key, value in (values or {}).items()}
    defaults = {match.group(1): match.group(2) or '' for match in PLACEHOLDER.finditer(body) if match.group(1)}
    return PLACEHOLDER.sub(lambda match: values.get(match.group(1) or match.group(3), defaults.get(match.group(1) or match.group(3), '')), body)


def completion_items():
    return [{'label': entry['prefix'], 'kind': 15, 'detail': entry['description'], 'insertText': entry['body'], 'insertTextFormat': 2} for entry in load_catalog()]


def generated_files(root):
    catalog = load_catalog()
    result = {}
    ulti = ['# Generated from editors/snippets/catalog.json.']
    geany = ['# Generated from editors/snippets/catalog.json.', '[Vitte]']
    vscode = {}
    for entry in catalog:
        prefix, description, body = entry['prefix'], entry['description'], entry['body']
        ulti += ['', f'snippet {prefix} "{description}"', body, 'endsnippet']
        yas = f'# -*- mode: snippet -*-\n# name: {description}\n# key: {prefix}\n# --\n{body}\n'
        result[root / 'emacs/snippets/vitte-mode' / prefix] = yas
        # Geany snippets support cursor stops but not linked placeholder fields.
        # Preserve each field's default text and make it a stop so users can step
        # through every part of the generated declaration with Geany's snippet
        # navigation keybinding.
        geany_body = PLACEHOLDER.sub(
            lambda m: (m.group(2) or '') + '%cursor%', body)
        geany.append(prefix + '=' + geany_body.replace('\\', '\\\\').replace('\t', '\\t').replace('\n', '\\n'))
        vscode[description] = {'prefix': prefix, 'body': body.splitlines(), 'description': description}
    result[root / 'vim/ultisnips/vitte.snippets'] = '\n'.join(ulti) + '\n'
    result[root / 'geany/snippets.vitte.conf'] = '\n'.join(geany) + '\n'
    result[root / 'snippets/vitte.code-snippets'] = json.dumps(vscode, indent=2) + '\n'
    return result


def generate(root, check=False):
    stale = []
    for path, content in generated_files(root).items():
        if check:
            if not path.is_file() or path.read_text() != content: stale.append(str(path))
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
    return stale
