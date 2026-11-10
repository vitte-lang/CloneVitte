"""Compiler-backed diagnostics for saved files and unsaved editor buffers."""
import json
import os
from pathlib import Path
import subprocess
import tempfile


def compiler_diagnostics(source, path, compiler='vitte', timeout=20):
    path = Path(path)
    directory = path.parent if path.parent.is_dir() else Path.cwd()
    descriptor, temporary = tempfile.mkstemp(prefix='.vitte-editor-', suffix=path.suffix or '.vit', dir=directory)
    try:
        with os.fdopen(descriptor, 'w', encoding='utf-8', newline='') as stream: stream.write(source)
        result = subprocess.run([compiler, 'check', '--no-color', '--diagnostic=lsp', temporary], cwd=directory, capture_output=True, text=True, timeout=timeout)
        if not result.stdout.strip() and result.returncode == 0: return []
        try: items = json.loads(result.stderr.strip() or result.stdout.strip() or '[]')
        except json.JSONDecodeError as error: raise RuntimeError((result.stderr or result.stdout or 'Compiler returned no diagnostics').strip()) from error
        if not isinstance(items, list): raise RuntimeError('Compiler returned an invalid LSP diagnostic list')
        diagnostics = []
        for item in items:
            origin = item.get('data', {}).get('position', {}).get('source')
            if origin and Path(origin).resolve() != Path(temporary).resolve(): continue
            item['message'] = item['message'].replace(temporary, str(path))
            # Internal metadata may reference the temporary path; clients only need LSP fields.
            item.pop('data', None)
            diagnostics.append(item)
        return diagnostics
    finally:
        Path(temporary).unlink(missing_ok=True)
