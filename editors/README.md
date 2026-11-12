# Vitte editor and shell support

All editor helpers use Python 3's standard library and the installed `vitte`
compiler. The shared command, formatter, symbol index and validated snippet
catalog live here. The compiler's canonical command table generates the Bash,
Zsh, Fish and Vim completions.

## Geany

Geany receives a dedicated filetype, highlighting, file/project actions,
snippets, compiler diagnostics with clickable locations, a tag generator and a
bundled LSP server. The installer discovers and enables the Geany LSP Client
plugin when installed. See [Geany setup](geany/README.md) for features,
configuration, and reversible installation options.

## Vim / Neovim

Add `editors/vim` to `runtimepath`, then enable filetype plugins, indent and
syntax. The integration supports `.vit`, `.vitl` and `.vitte`, syntax, nested
block indentation, completion, quickfix diagnostics, build/run/test commands,
tag generation and lossless indentation formatting. Set `g:vitte_binary` or
`g:vitte_editor` for custom executable paths. Use `:VitteLsp` with an installed
LSP client when desired.

## Emacs

Load `editors/emacs/vitte-mode.el` and require `vitte-mode`. The mode supports
syntax, imenu, completion, indentation, formatting, check/build/run/test/parse
commands, and optional Eglot LSP startup (`C-c C-l`). Customize `vitte-binary`
and `vitte-editor-binary` for custom paths. Snippets are in
`editors/emacs/snippets/vitte-mode`.

## Nano and Tree-sitter

Nano highlighting is in `nano/vitte.nanorc`. Tree-sitter ships a grammar,
highlighting and injection queries, generated parser and bindings for C, Node,
Python, Rust, Go and Swift. See [Tree-sitter details](tree-sitter/README.md).

## Completions

Use `completions/bash/vitte`, `completions/zsh/_vitte` or
`completions/fish/vitte.fish`. Vim's generated completion word list is in
`completions/vim/complete.vim`. The source of truth is
`src/bootstrap/cli/command_spec.def`:

```sh
python3 tools/generate_completions.py
python3 tools/generate_completions.py --check
```

The shells complete real compiler commands/options, enum values, source files,
output paths and flags with `=` syntax.

## Install and verify

`scripts/install-unix-integrations.sh` stages the Vim, Emacs, Nano, Geany and
shell files under `$PREFIX`; `DESTDIR` supports package builds. `geany`
has its own per-user install/uninstall script. Both installers support dry
runs or a temporary destination for verification.

```sh
python3 editors/tests/check.py
python3 editors/tests/test_tools.py
python3 tools/generate_completions.py --check
npm --prefix editors/tree-sitter test
npm --prefix editors/tree-sitter run test:node
cargo test --manifest-path editors/tree-sitter/Cargo.toml
```
