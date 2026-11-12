# Vitte support for Geany

This integration provides syntax highlighting, file associations for `.vit`,
`.vitl`, and `.vitte`, snippets, project-aware build commands, diagnostics,
lexing, AST inspection, and symbol extraction through Geany's standard filetype system.

## Install

From the repository:

```sh
./editors/geany/install_geany.sh
```

The installer detects Linux, macOS, and Windows-compatible Geany config paths.
Use `GEANY_HOME=/path/to/geany` to target one configuration explicitly.

Useful options:

```sh
./editors/geany/install_geany.sh --dry-run
./editors/geany/install_geany.sh --wd=project
./editors/geany/install_geany.sh --no-backup
./editors/geany/uninstall_geany.sh
```

On macOS, pass the application executable when Geany is not on `PATH` so the
installer can find and enable its bundled LSP Client plugin:

```sh
./editors/geany/install_geany.sh \
  --geany "/Applications/Geany.app/Contents/MacOS/geany"
```

Restart Geany after installation so it reloads filetype definitions.

## Commands

The Vitte filetype adds current-file actions for check, compile, run, lex,
AST inspection, and parse. Project actions use Geany's `%p` project directory and
file actions use `%d`, so paths containing spaces are handled safely.

The compiler executable is resolved through the environment inherited by
Geany. If `vitte` is not on that PATH, configure Geany's PATH or invoke the
installer from a desktop environment with the same PATH as Geany.

The per-file Test action runs a file's `test` declarations through the compiler.
Project Test finds and runs files with tests. Project Format applies the bundled
indentation formatter to Vitte sources in the project.

## Full editor features

The Geany installer deploys a private Python 3 editor service and registers
`.vit`, `.vitl` and `.vitte` under one Vitte filetype. It uses Geany's C lexer
and active color scheme for syntax highlighting. It adds per-file Check, Build,
Run, Test, Format, Lex, Parse and AST actions, plus project checks, tests,
formatting, symbols and tag generation. The build menu grows to expose them
all. Formatting uses the bundled indentation formatter. Snippet placeholders
become cursor stops that Geany can step through.

When the Geany LSP Client plugin is installed, the installer detects and enables
it, then configures semantic highlighting, live diagnostics, hover, signature
help, completion with snippets, document/workspace symbols, definitions,
reference highlighting and formatting. The LSP server is launched from the
installed editor service, so Python dependencies are not required.
On macOS, an application outside `/Applications` can be found with
`--geany /path/to/Geany.app/Contents/MacOS/geany`. On Linux, usual Geany plugin
locations are detected. Restart Geany after installation.

The installer records original files/settings. Uninstall restores unchanged
files byte-for-byte and preserves files edited after installation. `--dry-run`
changes nothing.
