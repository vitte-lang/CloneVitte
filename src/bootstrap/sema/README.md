# compiler/src/sema

The semantic analyzer validates the parsed AST before backend emission.

## Contract

- No dependency on `runtime/*`.
- Input is an initialized AST rooted at a module node.
- Type resolution uses `compiler/src/type`.
- Bindings use `compiler/src/symbol` and `compiler/src/scope`.
- Builtin operators and functions come from `compiler/src/builtin`.
- Optional constant evaluation uses `compiler/src/constant_fold`.
- Diagnostics go through `compiler/src/diagnostic`.
- Errors use `compiler/src/api/error.h`.

## Current Checks

- duplicate import declarations and conflicting visible import names
- explicit export targets must resolve to local top-level declarations
- explicit export names must not collide with other exported names
- imported symbol visibility for direct, glob, and module-alias usage
- duplicate top-level `proc` and `const`
- unknown identifiers
- unknown type names
- return value compatibility
- `let` initializer compatibility
- exact and safe conversions are accepted across assignments, arguments,
  fields, returns, pointers and references; lossy conversions require casts
- integer and floating literals are context-typed only when their value is
  representable in the requested destination type
- generic parameter scopes for type, trait, impl, form and procedure bodies
- generic type arguments are resolved in the importing scope, checked for
  arity, interned as nominal instances, and substituted into form fields and
  member access; imported nominal identities remain distinct
- tuple/list declaration and `for` binding patterns
- `if` condition type validity
- callability, call arity, and procedure argument type compatibility
- builtin function argument compatibility
- operator operand compatibility
- canonical logical operator semantics for `and` and `or`

## Current Limits

- generic procedure instantiation and trait-bound enforcement remain a later
  pass; nominal type arguments and their field/member substitutions are
  already validated here
- constructor pattern typing and exhaustiveness for non-boolean subjects remain a later pass
- export clauses are local-only in compiler; re-exporting imported module paths still belongs to a later stage
- imported modules are analyzed for compiler visibility and declaration validity only

## Driver Integration

The compiler driver now runs semantic analysis after AST validation and before
backend/codegen. Syntax-only success is no longer enough to reach C17 emission.

## Bootstrap 0.1.1 diagnostic checks

Contracts are checked in the procedure parameter scope before its body:

- `requires` and `ensures` must have type `bool` (`E0704`, `E0705`).
- `result` is implicit only in `ensures` for a procedure returning a value;
  other contract uses produce `E0707`, without a follow-up operand error.
- Statically false `requires` is contradictory (`E0709`); statically false
  `ensures` is an impossible postcondition (`E0702`). Bounded evaluation
  includes immutable bindings, integer arithmetic and short-circuit logic.
- Calls with known arguments violating a precondition produce `E0701`,
  including forward calls. Known explicit and implicit match returns that
  violate postconditions produce `E0702`.
- Assignment, suspension and error propagation in contracts produce `E0710`.
- Unknown predicates remain runtime checks. Preconditions run before the body;
  postconditions run on normal returns after deferred actions. Return values
  are evaluated once. Parameter bindings are captured at entry (references
  retain reference semantics), so local shadowing cannot change their meaning.
  Checks remain enabled independently of host assertion settings.
- Contract diagnostics preserve the enclosing clause, expression, procedure
  and reason through terminal, JSON, SARIF and LSP output. Runtime failures
  include the public code, procedure, clause location and corrective help.

Structural flow analysis reports `E1101` at the closing brace when a
value-returning procedure has a known fallthrough path. It handles nested
blocks, conditions, loops, loop-local breaks, unsafe blocks and exhaustive
boolean matches. Infinite loops do not require a return. Procedures without a
body are excluded. Statements after a terminating path receive `W0020`.

Static checks also detect integer overflow (`E1502`), division or remainder by
zero (`E1503`), constant dependency cycles (`E1504`), invalid shifts (`E0512`)
and known array index bounds (`E1201`). Statically dead branches and
short-circuited operands do not generate arithmetic or contract failures.

Boolean matches require complete coverage (`E1601`). Inaccessible arms receive
`E1602`: duplicate boolean, integer, identical string/character patterns,
patterns after an unguarded wildcard, and statically false guards. Integer
comparison uses values, including alternate radix spellings. Guarded arms do
not establish coverage. Pattern type mismatches receive `E1603`; constructor
and range coverage remain conservative. Distinct escape spellings are not yet
normalized for duplicate string/character detection.

The bootstrap reference pass tracks direct local and parameter roots, simple
reference aliases, lexical scopes and the last syntactic use. It reports
`E1205` for a local escape or an unknown reference origin, `E1206` for
overlapping incompatible borrows, and `E1207` for an access that conflicts
with a live borrow. Calls compare reference arguments when their roots are
known. Provenance through aggregates, raw pointers, closures, indirect calls
and returned references is not modeled completely, so this remains an
experimental safety check rather than a complete alias or lifetime proof.

`move` is accepted for a whole local binding or parameter. The bootstrap marks
that binding unavailable until it is assigned again, rejects use after move
(`E1209`), and rejects moving a value while a tracked reference is live
(`E1208`). Moving a reference binding transfers its tracked local borrow.
Partial moves and moves through fields or indirection are rejected; C17 keeps
the value representation unchanged because the bootstrap has no destructor or
runtime ownership machinery.

`make test-bootstrap-semantics` verifies invalid and valid semantic cases plus
compiled executable contract behavior using the bootstrap alone.

`make test-diagnostic-contract` covers exact source ranges, codes, phases,
severity, help, contract metadata, independent errors and valid procedures.
