#!/usr/bin/env python3
"""Install isolated, reversible Geany support and its self-contained tools."""
import argparse
import datetime
import json
import hashlib
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

EDITOR_ROOT = Path(__file__).resolve().parents[1]


def ini_get(text, section, key):
    current = None
    for line in text.splitlines():
        match = re.match(r'\s*\[([^]]+)\]\s*$', line)
        if match: current = match.group(1)
        elif current == section:
            match = re.match(r'\s*' + re.escape(key) + r'\s*=(.*)$', line)
            if match: return match.group(1)
    return None


def ini_set(text, section, key, value):
    lines = text.splitlines(keepends=True)
    current = None
    begin = end = None
    for index, line in enumerate(lines):
        match = re.match(r'\s*\[([^]]+)\]\s*$', line)
        if match:
            if current == section and end is None: end = index
            current = match.group(1)
            if current == section: begin = index
        elif current == section and re.match(r'\s*' + re.escape(key) + r'\s*=', line):
            lines[index] = key + '=' + value + '\n' if value is not None else ''
            return ''.join(lines)
    if value is None: return text
    if begin is None:
        return text.rstrip('\n') + ('\n\n' if text else '') + '[' + section + ']\n' + key + '=' + value + '\n'
    index = end if end is not None else len(lines)
    if index and not lines[index - 1].endswith('\n'): lines[index - 1] += '\n'
    lines.insert(index, key + '=' + value + '\n')
    return ''.join(lines)


def atomic_write(path, text, executable=False):
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, name = tempfile.mkstemp(prefix='.vitte-', dir=path.parent)
    try:
        with os.fdopen(descriptor, 'w', encoding='utf-8', newline='') as stream: stream.write(text)
        os.chmod(name, 0o755 if executable else 0o644)
        os.replace(name, path)
    finally:
        Path(name).unlink(missing_ok=True)


def configure(home, args):
    home = home.expanduser().resolve()
    if args.dry_run:
        print('[geany] would ' + ('uninstall from ' if args.uninstall else 'install into ') + str(home))
        return
    state_path = home / 'plugins/vitte/install-state.json'
    state = json.loads(state_path.read_text()) if state_path.exists() else {'keys': {}, 'files': []}
    stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    backed_up = set()
    state.setdefault('original_files', {})
    state.setdefault('installed_hashes', {})

    def backup(path):
        if not args.no_backup and path.is_file() and path not in backed_up:
            shutil.copy2(path, str(path) + '.bak.' + stamp)
            backed_up.add(path)

    def update(relative, section, key, value):
        path = home / relative
        if relative not in state['original_files']:
            state['original_files'][relative] = path.read_text() if path.exists() else None
            if relative not in state['files']: state['files'].append(relative)
        text = path.read_text() if path.exists() else ''
        identity = relative + '\0' + section + '\0' + key
        if identity not in state['keys']:
            state['keys'][identity] = {'previous': ini_get(text, section, key), 'installed': value}
        else: state['keys'][identity]['installed'] = value
        new = ini_set(text, section, key, value)
        if new != text:
            backup(path)
            atomic_write(path, new)
        state['installed_hashes'][relative] = hashlib.sha256(path.read_bytes()).hexdigest() if path.exists() else None

    if args.uninstall:
        restored = set()
        for relative in state['files']:
            path = home / relative
            if not path.is_file(): continue
            current = hashlib.sha256(path.read_bytes()).hexdigest()
            if current != state['installed_hashes'].get(relative): continue
            backup(path)
            original = state['original_files'].get(relative)
            if original is None: path.unlink()
            else: atomic_write(path, original)
            restored.add(relative)
        for identity, values in state['keys'].items():
            relative, section, key = identity.split('\0')
            if relative in restored: continue
            path = home / relative
            if not path.exists(): continue
            text = path.read_text()
            if ini_get(text, section, key) == values['installed']:
                backup(path)
                atomic_write(path, ini_set(text, section, key, values['previous']))
        payload_root = home / 'plugins/vitte/editors'
        if payload_root.exists():
            for directory in sorted(payload_root.rglob('__pycache__'), reverse=True): shutil.rmtree(directory)

        state_path.unlink(missing_ok=True)
        # Legacy installer files, created only for this language.
        for name in ['filetypes.Vitte.conf', 'filetypes.vitte.conf']:
            path = home / 'filedefs' / name
            if str(path.relative_to(home)) not in state['original_files'] and path.is_file() and '# Vitte language support' in path.read_text():
                backup(path)
                path.unlink()
        print('[geany] removed Vitte support from ' + str(home))
        return

    payload = ['__init__.py', 'cli.py', 'vitte-editor', 'grammar/vitte_highlight_grammar.json', 'semantic_tokens/__init__.py', 'semantic_tokens/engine.py', 'semantic_tokens/server.py', 'semantic_tokens/diagnostics.py', 'snippets/__init__.py', 'snippets/engine.py', 'snippets/catalog.json']
    for relative in payload:
        target = 'plugins/vitte/editors/' + relative
        if target not in state['original_files']:
            state['original_files'][target] = (home / target).read_text() if (home / target).is_file() else None
        atomic_write(home / target, (EDITOR_ROOT / relative).read_text(), relative == 'vitte-editor')
        state['installed_hashes'][target] = hashlib.sha256((home / target).read_bytes()).hexdigest()
        if target not in state['files']: state['files'].append(target)
    helper = home / 'plugins/vitte/editors/vitte-editor'
    command = shlex.quote(sys.executable) + ' ' + shlex.quote(str(helper))
    compiler = args.compiler or shutil.which(os.environ.get('VITTE_BINARY', 'vitte')) or 'vitte'
    command += ' --compiler ' + shlex.quote(compiler)
    # Key-file strings require literal backslashes to be escaped.
    def keyfile(value): return value.replace('\\', '\\\\')
    config = (EDITOR_ROOT / 'geany/filetypes.vitte.conf').read_text().replace('@VITTE_EDITOR@', keyfile(command))
    config = re.sub(r'^((?:FT|EX)_\d\d_WD)=@FILE_WD@$', lambda m: m[1] + '=' + {'file': '%d', 'project': '%p', 'current': ''}[args.wd], config, flags=re.M)
    relative = 'filedefs/filetypes.Vitte.conf'
    backup(home / relative)
    if relative not in state['original_files']:
        state['original_files'][relative] = (home / relative).read_text() if (home / relative).is_file() else None
    atomic_write(home / relative, config)
    state['installed_hashes'][relative] = hashlib.sha256((home / relative).read_bytes()).hexdigest()
    if relative not in state['files']: state['files'].append(relative)
    # Only the capitalized filetype is registered, avoiding duplicate menu entries.
    update('filetype_extensions.conf', 'Extensions', 'Vitte', '*.vit;*.vitl;*.vitte;')
    old_lower = ini_get((home / 'filetype_extensions.conf').read_text(), 'Extensions', 'vitte')
    if old_lower is not None: update('filetype_extensions.conf', 'Extensions', 'vitte', None)
    import configparser
    snippets = configparser.ConfigParser(interpolation=None)
    snippets.optionxform = str
    snippets.read(EDITOR_ROOT / 'geany/snippets.vitte.conf')
    for key, value in snippets['Vitte'].items(): update('snippets.conf', 'Vitte', key, value)
    lsp_path = 'plugins/lsp/lsp.conf'
    update(lsp_path, 'all', 'enable_by_default', 'true')
    # Auto-activate the Geany LSP Client when this installation ships the plugin.
    plugin_candidates = []
    geany = os.environ.get('VITTE_GEANY_EXECUTABLE') or shutil.which('geany')
    if geany:
        try:
            lines = subprocess.run([geany, '--print-prefix'], capture_output=True, text=True, timeout=3).stdout.splitlines()
            if len(lines) > 2:
                prefix = Path(lines[2])
                plugin_candidates += [prefix / 'geany/lsp.so', prefix / 'geany/lsp.dylib', prefix / 'geany/lsp.dll', prefix / 'geany-plugins/lsp/lsp.so']
        except (OSError, subprocess.SubprocessError): pass
        executable = Path(geany).resolve()
        for base in [*executable.parents, *Path(geany).absolute().parents]:
            plugin_candidates += [base / 'Resources/lib/geany/lsp.so', base / 'Resources/lib/geany/lsp.dylib', base / 'lib/geany/lsp.so', base / 'lib/geany/lsp.dylib', base / 'lib/geany-plugins/lsp/lsp.so']
            plugin_candidates += list((base / 'lib').glob('*/geany/lsp.so'))
            plugin_candidates += list((base / 'lib').glob('*/geany/lsp.dll'))
            plugin_candidates += list((base / 'lib').glob('*/geany/lsp.dylib'))
    plugin = next((candidate.resolve() for candidate in plugin_candidates if candidate.is_file()), None)
    if plugin:
        config_text = (home / 'geany.conf').read_text() if (home / 'geany.conf').exists() else ''
        plugin_values = [value for value in (ini_get(config_text, 'plugins', 'active_plugins') or '').split(';') if value]
        if str(plugin) not in plugin_values: plugin_values.append(str(plugin))
        update('geany.conf', 'plugins', 'load_plugins', 'true')
        update('geany.conf', 'plugins', 'active_plugins', ';'.join(plugin_values) + ';')
        update('geany.conf', 'plugins', 'custom_plugin_path', str(plugin.parent))
    # Geany loads only the first three file actions unless this preference is raised.
    existing_count = ini_get((home / 'geany.conf').read_text() if (home / 'geany.conf').exists() else '', 'build-menu', 'number_ft_menu_items')
    update('geany.conf', 'build-menu', 'number_ft_menu_items', str(max(16, int(existing_count or 0))))
    settings = {'cmd': keyfile(command + ' lsp --stdio'), 'use_without_project': 'true', 'use_outside_project_dir': 'true', 'project_root_marker_patterns': 'vitte.toml;.git;Makefile', 'lang_id_mappings': 'vitte;*.vit;vitte;*.vitl;vitte;*.vitte', 'autocomplete_enable': 'true', 'autocomplete_use_snippets': 'true', 'diagnostics_enable': 'true', 'hover_enable': 'true', 'signature_enable': 'true', 'goto_enable': 'true', 'highlighting_enable': 'true', 'document_symbols_enable': 'true', 'document_symbols_tab_label': 'Vitte Symbols', 'semantic_tokens_enable': 'true', 'semantic_tokens_types': 'type;class;enum;interface;struct;namespace', 'formatting_options': '{"tabSize":4,"insertSpaces":true}', 'format_on_save': 'false'}
    for key, value in settings.items(): update(lsp_path, 'Vitte', key, value)
    atomic_write(state_path, json.dumps(state, indent=2) + '\n')
    print('[geany] installed Vitte into ' + str(home))
    print('[geany] compiler: ' + compiler)
    print('[geany] restart Geany; LSP client activated for diagnostics, completion and navigation' if plugin else '[geany] restart Geany; install the Geany LSP Client plugin for live diagnostics, completion and navigation')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dry-run', action='store_true', default=os.environ.get('VITTE_GEANY_DRY_RUN') == '1')
    parser.add_argument('--uninstall', action='store_true', default=os.environ.get('VITTE_GEANY_UNINSTALL') == '1')
    parser.add_argument('--no-backup', action='store_true', default=os.environ.get('VITTE_GEANY_BACKUP') == '0')
    parser.add_argument('--wd', choices=['file', 'project', 'current'], default=os.environ.get('VITTE_GEANY_WD_MODE', 'file'))
    parser.add_argument('--compiler', help='Compiler executable; defaults to the executable found on PATH')
    parser.add_argument('--geany', help='Geany executable used to locate and activate the installed LSP Client')
    parser.add_argument('--config', type=Path, help='Geany configuration directory (also GEANY_HOME)')
    parser.add_argument('--render-filetype', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('--helper', default='vitte-editor', help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.render_filetype:
        text = (EDITOR_ROOT / 'geany/filetypes.vitte.conf').read_text()
        command = shlex.quote(args.helper)
        if args.compiler: command += ' --compiler ' + shlex.quote(args.compiler)
        print(text.replace('@VITTE_EDITOR@', command.replace('\\', '\\\\')).replace('@FILE_WD@', '%d'), end='')
        return 0
    if args.geany: os.environ['VITTE_GEANY_EXECUTABLE'] = args.geany
    homes = [args.config] if args.config else [Path(os.environ['GEANY_HOME'])] if os.environ.get('GEANY_HOME') else []
    if not homes:
        if sys.platform == 'win32': homes = [Path(os.environ.get('APPDATA', str(Path.home() / 'AppData/Roaming'))) / 'geany']
        else:
            homes = [Path(os.environ.get('XDG_CONFIG_HOME', str(Path.home() / '.config'))) / 'geany']
            mac_home = Path.home() / 'Library/Application Support/geany'
            if sys.platform == 'darwin' and mac_home.exists(): homes.append(mac_home)
    try:
        for home in dict.fromkeys(homes): configure(home, args)
    except (OSError, ValueError) as error:
        print('[geany] ' + str(error), file=sys.stderr)
        return 1
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
