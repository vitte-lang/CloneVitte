# Vitte JSON module

Pour la documentation complète et humanisée, consulter
[DOCUMENTATION.md](DOCUMENTATION.md). Elle couvre l’installation, les imports,
le modèle `JsonValue`, le lexer, le parser, l’encodeur, les diagnostics,
l’Unicode, les nombres, la sécurité et les tests.

Le guide de consultation est aussi disponible sur le site :
[Documentation du module JSON](https://vitte-lang.org/reference/json/).
Les éditions PDF multilingues jointes à l’issue
[#17](https://github.com/vitte-lang/vitte/issues/17) sont des ressources
complémentaires ; vérifier leurs signatures et commandes contre le code de
cette révision avant de les utiliser comme référence d’API.

`json` is a Vitte source package laid out under the `json` namespace. Module
paths use `::`, as required by the Vitte grammar. For example:

```vitte
use json::error::position::{JsonPosition, start};

let origin: JsonPosition = start();
```

The source tree includes value, lexer, parser, encoder, and error/position
modules. The package currently has no native C ABI or native runtime files;
`runtime/numeric.vit` is Vitte source.

## Diagnostics for beginners

Lexer results expose `error.message`; parser results expose `report.message`.
Both include `suggestion` for an optional, beginner-friendly correction. A
suggestion is intentionally empty when the module cannot infer a safe fix.
Suggestions describe the correction; they do not modify the input
automatically. If lexing fails during `parse` or `parse_with_limits`, the
parser preserves the lexer message, detail, suggestion, and span, and sets
`report.error` to `JsonParserError.InvalidTokenStream`. Call `lex` or
`lex_with_limits` directly when the specific `JsonLexerError` is needed.

For untrusted or size-constrained input, configure both lexer and parser
budgets with `new_lexer_limits`, `new_parser_limits`, and
`parse_strict_with_limits`. The parser limits cover nesting depth, total
values, array elements, object members, key length, and duplicate-key policy;
the lexer limits cover source size, token count, string length, and number
length. The detailed guide includes an example and the default values.

## Build and install

From the repository root:

```sh
make -C modules/json check
make -C modules/json test-vitte
make install PREFIX="$HOME/.local"
```

`test-vitte` stages a compiler installation, then compiles and runs a consumer
that imports the parser and encoder, parses a JSON object, and checks its
compact encoding from the installed source tree.
On MSYS2 in this checkout, run that smoke test with the bootstrap compiler:

```sh
make -C modules/json VITTE=../../build/bin/vitte-bootstrap.exe test-vitte
```

Normal `make install` installs the compiler and JSON source tree under
`$PREFIX/share/vitte/modules/json`; `make uninstall` removes both. A staged
installation can be made with `DESTDIR`, for example:

```sh
make install PREFIX=/usr/local DESTDIR="$PWD/modules/json/build/stage"
```

An installed compiler searches the module directory beside its own `bin`
directory. Additional module roots can be provided through `VITTE_MODULE_PATH`,
separated by the host path-list separator (`:` on Unix and `;` on Windows). This
is useful for development or a compiler invoked via a custom wrapper.

## Current compiler compatibility

The current bootstrap is Vitte 0.1.1. `make test-vitte` stages the package,
then compiles and runs a consumer of the installed parser and encoder API.
`make test-parse` compiles and runs the parser suite, including configurable
resource limits.

The checkout retains unexported parser drafts in `parser/array.vit`,
`parser/object.vit`, and `parser/value.vit`, along with a duplicate escaping
draft in `encode/escape.vit`. They remain outside source checks and package
installs; `json::parser::parser` and `json::encode::encoder` are the maintained
APIs. `make test-string-concat` verifies dynamic string concatenation with the
current bootstrap.

The root `make install` and Raspberry Pi installer install the JSON source
module through the module install target.

`package.toml` records package identity and layout. The compiler does not yet
consume this manifest or provide a general package manager.
