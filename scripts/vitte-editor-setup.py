#!/usr/bin/env python3
"""Per-user Vitte editor integration. Never silently overwrite user files."""
import argparse
import datetime
import json
import os
from pathlib import Path
import shutil
import sys

HOME = Path.home()
STATE = HOME / '.local/state/vitte/editor-setup'

def source_root(value):
    p = Path(value).expanduser().resolve()
    if (p / 'editors').is_dir():
        return p / 'editors'
    if (p / 'geany').is_dir() and (p / 'vim').is_dir():
        return p
    raise SystemExit(f'Editor resources not found: {p}')

def file_copy(src, dest, records, dry):
    if not src.is_file():
        print(f'SKIP missing {src}')
        return
    dest = Path(dest)
    if dest.is_file() and dest.read_bytes() == src.read_bytes():
        print(f'OK {dest}')
        return
    backup = None
    if dest.exists() or dest.is_symlink():
        if not dest.is_file() or dest.is_symlink():
            print(f'SKIP non-regular destination {dest}')
            return
        backup = f'{len(records):04d}.bak'
    print(f'{"WOULD INSTALL" if dry else "INSTALL"} {src} -> {dest}')
    if not dry:
        dest.parent.mkdir(parents=True, exist_ok=True)
        if backup:
            shutil.copy2(dest, STATE / 'pending' / backup)
        shutil.copy2(src, dest)
    records.append({'path': str(dest), 'backup': backup, 'installed_sha256': sha(dest) if not dry else None})

def sha(p):
    import hashlib
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def append_block(path, block, records, dry):
    path = Path(path)
    old = path.read_text() if path.is_file() else ''
    if block.strip() in old:
        print(f'OK {path}')
        return
    backup = f'{len(records):04d}.bak' if path.exists() else None
    print(f'{"WOULD UPDATE" if dry else "UPDATE"} {path}')
    if not dry:
        path.parent.mkdir(parents=True, exist_ok=True)
        if backup:
            shutil.copy2(path, STATE / 'pending' / backup)
        with path.open('a') as out:
            if old and not old.endswith('\n'):
                out.write('\n')
            out.write(block + '\n')
    records.append({'path': str(path), 'backup': backup, 'installed_sha256': sha(path) if not dry else None})

def install(root, dry):
    if not dry and (STATE / 'pending').exists():
        raise SystemExit('Pending transaction exists; inspect '+str(STATE / 'pending'))
    records = []
    if not dry:
        (STATE / 'pending').mkdir(parents=True, exist_ok=False)
    try:
        for editor, target in [('vim', HOME/'.vim'), ('nvim', HOME/'.config/nvim')]:
            for sub in ('after', 'colors', 'compiler', 'ftdetect', 'ftplugin', 'indent', 'syntax'):
                folder = root/'vim'/sub
                if folder.is_dir():
                    for f in folder.rglob('*'):
                        if f.is_file():
                            file_copy(f, target/f.relative_to(root/'vim'), records, dry)
            file_copy(root/'vim/ultisnips/vitte.snippets', target/'UltiSnips/vitte.snippets', records, dry)
        for f in ('vitte-mode.el', 'vitte-indent.el'):
            file_copy(root/'emacs'/f, HOME/'.emacs.d/lisp'/f, records, dry)
        folder = root/'emacs/snippets'
        if folder.is_dir():
            for f in folder.rglob('*'):
                if f.is_file():
                    file_copy(f, HOME/'.emacs.d/snippets'/f.relative_to(folder), records, dry)
        append_block(HOME/'.emacs.d/init.el',
            ';; Vitte editor setup\n(add-to-list \'load-path (expand-file-name "~/.emacs.d/lisp"))\n(autoload \'vitte-mode "vitte-mode" nil t)\n(dolist (ext \'("\\\\.vit\\\\\'" "\\\\.vitl\\\\\'" "\\\\.vitte\\\\\'"))\n  (add-to-list \'auto-mode-alist (cons ext \'vitte-mode)))', records, dry)
        file_copy(root/'nano/vitte.nanorc', HOME/'.nano/vitte.nanorc', records, dry)
        append_block(HOME/'.nanorc', 'include "'+str(HOME/'.nano/vitte.nanorc')+'"', records, dry)
        # Geany custom filetypes use filetypes.<name>.conf, case-sensitive on Linux.
        file_copy(root/'geany/filetypes.vitte.conf', HOME/'.config/geany/filedefs/filetypes.vitte.conf', records, dry)
        # Preserve existing Geany associations: add only a dedicated section entry when absent.
        geany_ext = HOME/'.config/geany/filetype_extensions.conf'
        current = geany_ext.read_text() if geany_ext.is_file() else ''
        if '[Extensions]' in current:
            import re
            if not re.search(r'(?m)^Vitte\s*=', current):
                print('MANUAL Geany: add Vitte=*.vit;*.vitl;*.vitte; under [Extensions] in', geany_ext)
        else:
            append_block(geany_ext, '[Extensions]\nVitte=*.vit;*.vitl;*.vitte;', records, dry)
        # Geany snippets must be merged according to their INI sections; do not clobber snippets.conf.
        file_copy(root/'geany/snippets.vitte.conf', HOME/'.local/share/vitte/geany/snippets.vitte.conf', records, dry)
        print('Geany snippets staged for manual section-aware merge; existing snippets.conf untouched.')
        # Tree-sitter is editor/version specific; stage sources, do not claim parser registration.
        ts = root/'tree-sitter'
        for rel in ('grammar.js', 'queries/highlights.scm', 'queries/injections.scm'):
            file_copy(ts/rel, HOME/'.local/share/vitte/tree-sitter'/rel, records, dry)
        if not dry:
            stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
            dest = STATE/stamp
            (STATE/'pending'/'manifest.json').write_text(json.dumps(records, indent=2))
            (STATE/'pending').rename(dest)
            print('Transaction:', dest)
    except Exception:
        if not dry:
            print('Installation interrupted. Run --rollback pending after inspection.', file=sys.stderr)
            (STATE/'pending'/'manifest.json').write_text(json.dumps(records, indent=2))
        raise

def rollback(transaction, dry):
    p = STATE/transaction
    if not p.is_dir() or not (p/'manifest.json').is_file():
        raise SystemExit(f'Unknown transaction: {transaction}')
    records = json.loads((p/'manifest.json').read_text())
    for r in reversed(records):
        dest = Path(r['path'])
        if not dest.is_file() or sha(dest) != r['installed_sha256']:
            print('SKIP changed since install:', dest)
            continue
        print('RESTORE' if r['backup'] else 'REMOVE', dest)
        if not dry:
            if r['backup']:
                shutil.copy2(p/r['backup'], dest)
            else:
                dest.unlink()
    if not dry:
        (p/'rolled-back').touch()

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--source', default='.')
    ap.add_argument('--dry-run', action='store_true')
    ap.add_argument('--rollback', metavar='TRANSACTION')
    args = ap.parse_args()
    if args.rollback:
        rollback(args.rollback, args.dry_run)
    else:
        install(source_root(args.source), args.dry_run)
if __name__ == '__main__':
    main()
