# tree-sitter-vitte

Tree-sitter grammar and highlighting queries for Vitte, with generated C parser
and bindings for Node, Rust, Python, Go, Swift and C.

## Node

```sh
npm ci
npm test
npm run test:node
```

```js
const Parser = require('tree-sitter');
const Vitte = require('tree-sitter-vitte');
const parser = new Parser();
parser.setLanguage(Vitte);
const tree = parser.parse('proc main() {}');
```

The package exports the native language binding. `grammar.js` is used only to
regenerate the parser (`npm run generate`). A native build toolchain is required
when no prebuilt binding exists.

## Rust

`cargo test` validates loading and the example. The binding uses Tree-sitter
0.22's `Language` API; its dependency is constrained accordingly. Highlight and
injection queries are exported as `HIGHLIGHTS_QUERY` and `INJECTIONS_QUERY`.

## Scope

The corpus covers the forms in the repository's EBNF grammar. Passing it does
not establish compatibility with every experimental or bootstrap source file.
