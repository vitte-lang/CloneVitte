# Vitte semantic editor service

`engine.py` provides a lossless lexer, UTF-16 source ranges, scope-aware local
symbol indexing, token classification, source formatting, and document symbols.
`diagnostics.py` invokes the Vitte compiler on a temporary copy of unsaved
buffers, reads its native LSP diagnostics and removes the temporary file.
`server.py` implements a dependency-free stdio LSP server. It supports full text
sync, compiler diagnostics, full/delta semantic tokens, completion, hover,
definition, references, document/workspace symbols and formatting.

Start it with `editors/vitte-editor lsp --stdio`. Initialize options accept a
`compiler` executable and `diagnostics` boolean. The server uses Tree-sitter
for editor parsing separately; compiler diagnostics always come from `vitte`.

The index handles local declarations, parameters, lexical nesting and shadowing.
Cross-file name lookup covers module-level declarations. The compiler remains
the source of truth for type errors and semantic validation.
