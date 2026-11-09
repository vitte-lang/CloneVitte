# Vitte Diagnostics

The diagnostic layer stores and formats compiler diagnostics for parser, semantic analysis, driver, and CLI integration.

Invariants:
- No dependency on `runtime/*`.
- Storage is caller-provided and bounded.
- Diagnostic order is stable.
- Severity and origin enums are validated before storage; emission requires a
  non-empty message.
- Spans are optional and copied from `vitte_ast_span_t` when valid.
- With a source manager in `vitte_diagnostic_options_t`, diagnostics render the
  source line and underline the primary half-open byte range.
- `vitte_diagnostic_add_location` attaches declaration, origin, or other
  related locations to the most recently added diagnostic.
- Every diagnostic records a structured compiler phase. Pass an explicit origin
  to `vitte_diagnostic_emit`, or set it on the object before
  `vitte_diagnostic_add`. `VITTE_DIAGNOSTIC_ORIGIN_UNKNOWN` uses the registered
  code's origin; an explicit origin is preserved.
- Causes form an ordered chain and may point at source spans. Notes and help
  are distinct annotations. Suggestions include a replacement range, text,
  and `machine-applicable`, `maybe-applicable`, or `manual` applicability.
- Counts remain coherent with stored diagnostics.
- `warnings_as_errors` stores warnings as errors.
- When capacity or max diagnostics is reached, new diagnostics are suppressed and reported through `last_error`.
- A failed compiler phase with no error diagnostic receives `E0012`; if the bag
  is full of non-errors, `vitte_diagnostic_ensure_failure` replaces the last one.

Infrastructure codes (`E0000`–`E0099` are reserved):
- `E0000` fallback, `E0001` internal error, `E0002` invariant, `E0003` impossible state.
- `E0004` resource allocation, `E0005` unreadable source, `E0006` invalid UTF-8,
  `E0007` source map, `E0008` diagnostic construction, `E0009` internal limit.
- `E0010` incompatible file format, `E0011` configuration, `E0012` phase failure,
  `E0013` component failure. Each code has a fixed symbolic name in `diagnostic.h`.
- Registered public IDs are assigned explicitly in `registry.c`, never hashed.
  Both symbolic identifiers and public codes (for example `E0302` or `W0003`)
  are accepted by the emission API. Unregistered identifiers retain their
  symbolic code and receive `E????`, `W????`, `N????`, or `H????` as a display
  fallback; register a code to give it a stable public identity.
- Set `VITTE_BACKTRACE=1` to append a best-effort native backtrace to `E0001`,
  `E0002`, `E0003`, and `E0013` on macOS/Linux.

Severities:
- note
- help
- warning
- error
- fatal

Code, severity and origin are independent: promoting `W0003` to an error keeps
its public code and `import` origin. Both `error` and `fatal` count as failures.
Origins include `lexer`, `parser`, `import`, `name-resolution`, `type-check`,
`constant-eval`, `linker`, and the remaining backend/infrastructure phases.

`cascade.c` assigns error diagnostics a primary root or an earlier causal
parent. Explicit relationships are preserved; fallback classification scores
phase dependencies with span, symbol, type and context evidence. Suppressed
diagnostics remain stored. The native counterpart is
`src/compiler/diagnostic/cascade.vitte`, which the native diagnostic engine
uses. Both phase graphs include scanner and code-generation stages. The C ABI
stays in `cascade.h`; the Vitte module exports namespaced operations over the
native diagnostic bag representation.

The origin is exposed as `phase` in output for compatibility with existing
consumers:

| Format | Public code | Exact Vitte severity | Origin |
| --- | --- | --- | --- |
| Terminal | `[E0302]` | `error` before the code | `phase: import` |
| JSON | `code` | `severity` | `phase` |
| LSP | `code` | `data.severity` | `data.phase` |
| SARIF | `ruleId` | `properties.severity` | `properties.phase` |

LSP's numeric `severity` and SARIF's `level` keep their protocol mappings.
The additional metadata preserves distinctions such as `fatal` versus `error`,
and SARIF's `help` versus `note`.

Lifecycle:
- Initialize options with `vitte_diagnostic_options_init`.
- Provide a storage array to `vitte_diagnostic_bag_init`.
- Add diagnostics with `vitte_diagnostic_add`.
- Inspect counts with `vitte_diagnostic_bag_counts`.
- Format one diagnostic or write all diagnostics to `FILE *`.
- Format with default behavior or explicit options using `vitte_diagnostic_format_one_ex`.
- Merge one bag into another with `vitte_diagnostic_merge`.
- Use `vitte_diagnostic_status` to convert stored errors into a compiler status.
- Reset with `vitte_diagnostic_bag_reset`.

Terminal format:
```text
error[E0702]: postcondition failed
  phase: contract | category: contract
  procedure: clamp
  --> src/math.vit:31:9
   31 | ensures result >= minimum;
      |         ^^^^^^^^^^^^^^^^^ condition evaluated to false
  contract: ensures `result >= minimum`
  note: `minimum`: 0
  note: `maximum`: 100
  note: `result`: -1
  caused by: returned value produced the violation
  help: every successful return from `clamp` must satisfy this condition
```

The CLI uses the structured diagnostic model for lexer, parser, semantic-analysis,
driver, and source-loading failures. `--diagnostic=terminal` renders annotated
source excerpts, `--diagnostic=json` emits one versioned diagnostics document,
and `--diagnostic=sarif` emits a SARIF 2.1.0 log. Diagnostics collected during
the same compiler phase are emitted together, so machine-readable output remains
one valid document rather than concatenated per-error fragments.
When a diagnostic code exists in the registry, the renderer supplies its stable
code, title, explanation, and compiler phase; source spans are rendered through
the registered source manager. Error and warning diagnostics also receive
contextual help from `help.c` when no explicit help annotation is attached.
Every registered error and warning has a dedicated remediation policy, enforced
by a coverage test. Actual types, source excerpts, expected parser tokens,
contract conditions and available failure details specialize that policy.
Name resolution suggests a name only when it is a unique visible candidate one
edit away (including a transposition); ambiguous, short and non-ASCII names do
not trigger a spelling guess. Call diagnostics report the exact expected and
supplied argument counts. These are advisory hints, not automatic edits.

The CLI and direct renderers share the same help formatter. JSON carries help
annotations, SARIF carries `result.properties.help`, and LSP carries `data.help`.
The four CLI formats preserve the public code, phase, severity, type context,
causes, related locations, and source byte spans. LSP additionally exposes the
converted protocol `range`; its `data.position` and `data.labels` entries
retain the original source name, line/column values and byte offsets so
clients can compare or remap the range without losing source fidelity.
Explicit subsystem guidance takes precedence. Unknown codes retain the actual
failure and cause and explicitly say that no verified correction is registered.
No model, network service or external inference dependency is required.

`vitte explain E0501` and `vitte explain E0501 --diagnostic=json` use the same
per-code remediation policies. `vitte_diagnostic_write_all_format` dispatches
to the real terminal, JSON and SARIF renderers. For LSP, a bag with a source
manager uses the built-in byte-to-position converter automatically. A bag
without source bytes still requires the dedicated renderer and an explicit
converter; the generic entry point returns `VITTE_STATUS_ERROR_INVALID_STATE`
rather than inventing UTF-16 positions.

Run `make test` for coverage, real CLI examples, format consistency and
bounded rendering checks. `make test-diagnostic-contract` runs the C bootstrap
against 42 source cases, eight valid procedure cases and one invalid-UTF-8 byte case, asserting public code,
phase, severity, exact byte range, primary label and actionable help. It also
checks cross-module privacy with a secondary declaration location and import
cycles with the originating `use` range. The invalid-UTF-8 case requires the
offending byte to be underlined.
`make test-diagnostic-help` runs the contextual CLI suite. Meanwhile,
`make test-diagnostic-parity` compares terminal, JSON, SARIF and LSP output and
checks the complete human-readable and JSON `explain` contract for all 154
registered codes in both the C bootstrap and native frontend. Registry-wide
`explain` coverage proves that every code is documented; the source-case
matrix separately records which current bootstrap failures have executable
emission witnesses.

Without a span:
```text
error[E0501]: type incompatible
  phase: type-check | category: type
  type: expected `int`, found `string`
  context: argument 1 of procedure call
```

Limitations:
- Color output is available in terminal format; use `--no-color` for stable
  snapshots and non-interactive text capture.
- Recovery from a process crash and portable native backtraces are not yet available.

Formatting options:
- `show_codes` toggles `[VITTE_CODE]`.
- `show_details` toggles detail lines.
- `color_enabled` wraps diagnostics in ANSI color sequences.
- `show_source_line` enables source excerpts and underlines when source text is registered.

Summary:
- `vitte_diagnostic_format_summary` reports total diagnostics, errors, warnings, and suppressed diagnostics.

The bootstrap public C API stores diagnostic identity and text in owned buffers
before adding them to the diagnostic bag. Its textual results use the same
terminal renderer as the CLI, preserving public codes, phases and source
locations instead of reducing failures to an unnamed compact diagnostic.
