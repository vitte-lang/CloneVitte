#!/usr/bin/env python3
"""Integration smoke tests, isolated from the user's editor configuration."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class Editors(unittest.TestCase):
    def run_command(self, args, **kwargs):
        result = subprocess.run(args, cwd=ROOT, text=True, capture_output=True, **kwargs)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    @unittest.skipUnless(shutil.which('vim'), 'Vim unavailable')
    def test_vim(self):
        with tempfile.TemporaryDirectory() as directory:
            env = dict(os.environ, VITTE_EDITOR_ROOT=str(ROOT / 'editors'), VITTE_EDITOR_TMP=directory)
            result = subprocess.run(['vim', '-Nu', 'NONE', '-n', '-es', '-S', str(ROOT / 'editors/tests/vim.vim')], env=env, capture_output=True, text=True)
            errors = Path(directory, 'vim-errors')
            self.assertEqual(result.returncode, 0, errors.read_text() if errors.exists() else result.stdout + result.stderr)

    @unittest.skipUnless(shutil.which('emacs'), 'Emacs unavailable')
    def test_emacs(self):
        self.run_command(['emacs', '--batch', '-Q', '-L', 'editors/emacs', '-l', 'editors/tests/emacs.el'])

    def test_geany_lifecycle(self):
        with tempfile.TemporaryDirectory(prefix='vitte editors ') as directory:
            home = Path(directory, 'geany')
            prefix = Path(directory, 'Geany Prefix')
            plugin = prefix / 'lib/geany/lsp.so'
            plugin.parent.mkdir(parents=True)
            plugin.touch()
            executable = Path(directory, 'fake geany')
            executable.write_text('#!/usr/bin/env python3\nprint("' + str(prefix) + '")\nprint("' + str(prefix / 'share') + '")\nprint("' + str(prefix / 'lib') + '")\nprint("' + str(prefix / 'share/locale') + '")\n')
            executable.chmod(0o755)
            env = dict(os.environ, GEANY_HOME=str(home))
            installer = ['bash', 'editors/geany/install_geany.sh']
            self.run_command(installer + ['--dry-run'], env=env)
            self.assertFalse(home.exists(), 'dry-run created configuration')
            home.mkdir()
            Path(home, 'filetype_extensions.conf').write_text('[Extensions]\nPython=*.py;\n')
            Path(home, 'snippets.conf').write_text('[Python]\nhello=world\n')
            self.run_command(installer + ['--no-backup', '--geany', str(executable)], env=env)
            snapshot = {p.relative_to(home): p.read_bytes() for p in home.rglob('*') if p.is_file()}
            self.run_command(installer + ['--dry-run'], env=env)
            self.assertEqual(snapshot, {p.relative_to(home): p.read_bytes() for p in home.rglob('*') if p.is_file()})
            self.run_command(installer + ['--no-backup'], env=env)
            self.assertEqual(Path(home, 'filetype_extensions.conf').read_text().count('Vitte='), 1)
            self.assertEqual(Path(home, 'snippets.conf').read_text().count('[Vitte]'), 1)
            self.assertTrue(Path(home, 'plugins/vitte/editors/vitte-editor').is_file())
            self.assertIn('number_ft_menu_items=16', Path(home, 'geany.conf').read_text())
            geany_config = Path(home, 'geany.conf').read_text()
            self.assertIn('load_plugins=true', geany_config)
            self.assertIn(str(plugin), geany_config)
            lsp_config = Path(home, 'plugins/lsp/lsp.conf').read_text()
            for feature in ['autocomplete_enable=true', 'signature_enable=true', 'highlighting_enable=true', 'semantic_tokens_enable=true']:
                self.assertIn(feature, lsp_config)
            self.assertIn('lsp --stdio', lsp_config)
            filetype = Path(home, 'filedefs/filetypes.Vitte.conf').read_text()
            self.assertIn('[styling=C]', filetype)
            self.assertIn('secondary=void never unit bool char', filetype)
            self.run_command(['python3', str(Path(home, 'plugins/vitte/editors/vitte-editor')), 'snippets'])
            self.run_command(installer + ['--uninstall', '--no-backup'], env=env)
            self.assertIn('Python=*.py;', Path(home, 'filetype_extensions.conf').read_text())
            self.assertIn('hello=world', Path(home, 'snippets.conf').read_text())
            self.assertNotIn('Vitte=', Path(home, 'filetype_extensions.conf').read_text())

    def test_unix_installer(self):
        with tempfile.TemporaryDirectory(prefix='vitte stage ') as directory:
            stage = Path(directory)
            share = stage / 'usr/local/share/vitte'
            share.mkdir(parents=True)
            shutil.copytree(ROOT / 'editors', share / 'editors', ignore=shutil.ignore_patterns('node_modules', 'target', 'build', '.build', '.venv'))
            shutil.copytree(ROOT / 'completions', share / 'completions')
            env = dict(os.environ, DESTDIR=str(stage), PREFIX='/usr/local')
            self.run_command(['sh', 'scripts/install-unix-integrations.sh'], env=env)
            expected = ['vim/vimfiles/compiler/vitte.vim', 'vim/vimfiles/indent/vitte.vim', 'vim/vimfiles/autoload/vitte/complete.vim', 'vim/vimfiles/autoload/vitte.vim', 'emacs/site-lisp/vitte-indent.el']
            for relative in expected:
                self.assertTrue((stage / 'usr/local/share' / relative).is_file(), relative)
            self.run_command([str(stage / 'usr/local/bin/vitte-editor'), 'snippets'])
            self.assertNotIn('@VITTE_EDITOR@', (stage / 'usr/local/share/geany/filedefs/filetypes.Vitte.conf').read_text())
            self.run_command(['sh', 'scripts/install-unix-integrations.sh', 'uninstall'], env=env)
            for relative in expected:
                self.assertFalse((stage / 'usr/local/share' / relative).exists(), relative)


if __name__ == '__main__':
    unittest.main(verbosity=2)
