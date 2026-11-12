# Shared Vitte snippets

`catalog.json` is the validated source for the Geany, Emacs, UltiSnips, VS Code
and LSP snippet collections. Regenerate the native files with:

```sh
editors/vitte-editor snippets --generate
editors/vitte-editor snippets --check
editors/vitte-editor snippets main
```

The engine validates prefixes and placeholders and expands values with the
same numbered stop points used by the editor formats.
