# compiler/src/parser

The parser turns compiler Vitte source into the arena-backed AST used by the
rest of the C17 compiler pipeline.

## Contract

- No dependency on `runtime/*`.
- Input tokens come from `compiler/src/lexer`.
- Nodes are allocated through `compiler/src/ast` and its arena.
- Syntax diagnostics go through `compiler/src/diagnostic`.
- Errors use `compiler/src/api/error.h`.
- Parsing is deterministic and bounded by `max_depth`.

## Entry Points

- `vitte_parser_init`
- `vitte_parser_init_module`
- `vitte_parser_parse_module`
- `vitte_parser_parse_decl`
- `vitte_parser_parse_stmt`
- `vitte_parser_parse_expr`

The normal compiler path is:

1. initialize an empty AST
2. initialize the parser on source text or a loaded module
3. parse a module root
4. validate the AST
5. hand the AST to diagnostics, codegen, and backend stages

## Supported Grammar

Current compiler grammar:

- module:
  - optional `space module/path`
  - zero or more `use` imports:
    - simple path `use math/core`
    - alias `use math::core as core`
    - glob `use math::*`
    - groups `use math::{add, sub as minus, ops::{mul}}`
  - zero or more top-level declarations
- declaration:
  - outer and inner attributes (`#[name]`, `#![name(...)]`) are retained as
    per-declaration token ranges for the qualified name and argument list,
    without changing the stable child layout
  - `export *`
  - `export name`
  - `export name as alias`
  - `export { name, other as alias }`
  - `export proc name(param: type, ...) -> type { ... }`
  - `export proc name(param: type, ...) { ... }`
  - `export const name: type = expr`
  - `export const name = expr`
  - `proc name(param: type, ...) -> type { ... }`
  - `proc name(param: type, ...) { ... }`
  - angle-bracket generic parameter lists (`<T, U: Trait>`) and `where` bounds
    (including `Trait + Debug` and relational predicates) on type, form, pick,
    trait, impl and procedure declarations
  - `trait` and `impl` declaration bodies with nested procedures, constants and
    associated type declarations
  - `const name: type = expr`
  - `const name = expr`
- statement:
  - block `{ ... }`
  - `return expr`
  - `let name: type`
  - `let name: type = expr`
  - `let mut name: type = expr`
  - tuple/list/constructor/struct/range/or binding patterns in `let` and `for`
  - `match` arms with optional `case`, guards, and wildcard/constructor,
    struct, range and alternative patterns
  - `expr;`
  - `if expr stmt`
  - `if expr stmt else stmt`
- expression:
  - integer literal
  - string literal
  - identifier and qualified identifier `a::b`
  - grouped expression `(expr)`
  - call `callee(arg, ...)`
  - unary `+expr` `-expr` `not expr`
  - binary `* / % + - << >> & ^ | < <= > >= == != and or`
  - qualified type names `a::b`

## Vitte Limits

- import paths are normalized to dotted module names in AST/module tracking
- contextual grammar words such as `static`, `defer`, and `move` remain usable as identifiers outside their productions
- direct `use a::b` is locked as a module-path import
- symbol imports must use groups like `use a::{b}`
- when `use a::b` fails as a module import but `a` exports `b`, the driver emits a dedicated diagnostic pointing to `use a::{b}`
- standalone compiler export clauses only target local top-level items
- compiler re-exports such as `export mod::{x}` and `export mod as alias` are explicitly rejected with dedicated diagnostics
- `let` may infer its type from the initializer; an explicit type still
  constrains the initializer through the centralized conversion policy
- `const` without a type relies on simple literal/operator inference
- compatibility-only declarations such as `class`, `union`, `bits`, `flags`,
  and backend configuration declarations remain diagnosed as unsupported
- complex pattern nodes are represented by `VITTE_AST_NODE_PATTERN`; constructor
  heads, ranges and alternatives carry flags so semantic binding does not treat
  type names as locals. Declaration and `for` bindings are registered by
  semantic analysis, while match exhaustiveness remains a later type-checking
  pass

## Recovery

After a syntax error, the parser keeps scanning and synchronizes on:

- `,` or the matching closing delimiter in argument, `asm`, type,
  parameter, array, field, variant, import, export, and match-arm lists
- `,` or the next declaration token after a `where` predicate
- `;` or `}` at the current declaration or statement depth
- the next declaration keyword in the current scope
- the next statement opener inside blocks

Nested delimiters are skipped before selecting a recovery point. Malformed
list items are omitted from the AST; later valid items and declarations remain
available even though the parse reports failure.

## Frontend Synchronisation

`grammar/vitte.ebnf` is the canonical syntax inventory.
`make test-grammar` checks terminal coverage across the native and C lexer
tables and token references in the legacy C parsers. It does not prove that
all frontends accept identical productions or synchronize Tree-sitter,
generated documentation, or editor tables.

## Module Integration

`vitte_parser_init_module` parses a `vitte_module_t` that already owns loaded
source text. On success, the parser:

- attaches the AST to the module
- normalizes `space foo/bar` to module name `foo.bar`
- registers `use` imports into both the AST and `vitte_module_t`
- advances module state to `PARSED`

## Driver Integration

The compiler driver now uses this parser instead of the previous synthetic AST
builder. `check`, `emit-c`, and `build` all pass through the same parse stage.
