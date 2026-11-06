# Native Vitte compiler driver

`driver.vitte` defines the compiler driver contract. Its legacy public C ABI
declarations remain separate from the native pipeline. The native entry
`vitte_driver_run_native_frontend` runs the Vitte lexer, parser, semantic
analyzer, HIR/IR lowering, and C17 emission. Compile, check, run, and `--emit-ir`
share the native pipeline; check stops after semantic analysis, while
`--emit-ir` serializes the validated native IR.

The root `make` target links this Vitte-generated frontend with a small set of
C boundary objects for the public ABI, file and process APIs, and OS entry
point. The filesystem object supplies only the public file and path ABI; it
contains no compiler phases or CLI dispatch. The link omits the historical C
compiler objects. The C bootstrap is
still used as the seed that translates the Vitte sources into the frontend
archive, but that seed is a separate executable. Native `compile` and `run`
pass generated C17 to the host-tool contract declared in `host_tools.vitte`.
Vitte owns temporary-file lifecycle, compiler argument construction, stage
ordering, and failure classification. A small C host adapter reserves OS
temporary paths and launches the host compiler or generated executable. The
adapter uses native temporary-directory and process APIs on Windows and
Unix-like hosts.

The Vitte CLI dispatches commands directly. `install` is implemented by
`compiler::package::installer`: it selects a compatible stable release,
verifies the archive SHA-256, stages extraction, and updates project records
with rollback on failure. `curl` and `tar` are host tools; the package policy
and hash implementation are Vitte code. Unsupported commands and option
combinations report an error; the process entry does not fall back to the
historical C driver.
`check` returns after native semantic analysis. `--emit-ir` writes the native
IR dump; inspection, diagnostic rendering, and host system calls remain behind
their respective interfaces.

`api_compile_source` is implemented by `compiler/api/api.vitte`; its
`ApiResult` owns the generated C17 bytes and formatted diagnostics. Object
generation, linking, and execution are sequenced by the Vitte driver. The
historical C compiler remains available as a separate bootstrap executable and
is not part of the normal `make` link.

## Remaining C bootstrap contract

The sections below describe the historical bootstrap driver behavior and
contracts that still need a native Vitte implementation. They do not describe
the current native `.vitte` module as complete.

## Lifecycle

1. Initialize options with `vitte_driver_options_init`.
2. Create a `vitte_driver_t` with `vitte_driver_init`.
3. Create an input with `vitte_driver_input_from_buffer` or
   `vitte_driver_input_from_file`.
4. Run `vitte_driver_check`, `vitte_driver_emit_c`, `vitte_driver_build`, or
   `vitte_driver_run`.
5. Inspect `vitte_driver_result_t` and `vitte_driver_diagnostics`.
6. Destroy owned input with `vitte_driver_input_destroy`.
7. Shut down the session with `vitte_driver_shutdown`.

## Pipeline

Stages are explicit and stable:

- `init`
- `load-config`
- `load-source`
- `lex`
- `parse`
- `build-ast`
- `validate-ast`
- `constants`
- `semantic`
- `backend`
- `codegen-c`
- `compile-link`
- `diagnostics`
- `cleanup`

The current compiler driver runs the real compiler frontend:

- parser over the source text or module wrapper
- AST validation
- semantic analysis
- HIR and IR lowering
- C17 emission or native C compilation

When parsing through `vitte_module_t`, the driver also registers and resolves
imports before semantic analysis. Global imports use configured search paths,
and sibling modules are discoverable from the input file directory.
Imported module graphs are semantically checked before the root module. Backend
flattening only carries exported imported declarations into the lowered module.
That export surface now includes `export *`, explicit local export items, and
local export aliases in addition to inline `export proc/const`.
Flattening now assigns stable internal lowered names to imported declarations,
so different imported modules can expose the same public Vitte symbol without
colliding during backend lowering.

The native frontend build includes `modules/core/src` on `VITTE_MODULE_PATH`
and tracks both its Vitte sources and `modules/core/Vitte.toml`. A configured
root at a package's `src` directory accepts the package name as a prefix, so
`core::num::checked` resolves to `modules/core/src/num/checked.vit`. The loader
passes the resolved Vitte source through the native parser, semantic analyzer,
and emitter; it does not substitute a C17 implementation of a `core` module.
During stage0, the CLI bootstrap host still discovers paths and assembles the
source graph before handing it to that frontend. Moving filesystem discovery
and graph assembly into the Vitte `compiler::import` implementation remains
separate work.

## Options

`vitte_driver_options_t` carries input/output paths, module metadata, sysroot,
target triple, C compiler, emit kind, optimization level, diagnostic behavior,
debug toggles, and resource limits. Limits default to the config layer defaults.

Supported emit kinds:

- `VITTE_DRIVER_EMIT_CHECK`
- `VITTE_DRIVER_EMIT_AST`
- `VITTE_DRIVER_EMIT_C`
- `VITTE_DRIVER_EMIT_BINARY`

`VITTE_DRIVER_EMIT_OBJECT` is reserved by the public enum and currently follows
the binary compile path until object-specific backend plumbing exists.

## Diagnostics

All user-facing failures are recorded in `vitte_diagnostic_bag_t`:

- missing input
- source size limit exceeded
- empty source
- invalid configuration
- import resolver initialization failure
- missing imported module
- AST allocation or validation failure
- semantic failure
- HIR or IR backend lowering failure
- C17 codegen failure
- C compiler/link failure

The driver uses `compiler/src/api/error.h` for status and last-error state. It
does not depend on `runtime/*`.

## Output

`check` validates the pipeline through semantic analysis and backend HIR/IR
validation. For file inputs it also verifies that declared imports can be
resolved on disk.

`emit-c` writes to the requested file path or to the result output buffer when
the path is `NULL`.

`build` emits a sibling generated C file named `<output>.c` and invokes the
configured C compiler with strict C17 flags.

## Minimal Example

```c
vitte_driver_t driver;
vitte_driver_options_t options;
vitte_driver_input_t input;
vitte_driver_result_t result;

vitte_driver_options_init(&options);
vitte_driver_init(&driver, NULL, &options);
vitte_driver_input_from_buffer(&input, "memory.vit", "return 0", 0u);
vitte_driver_check(&driver, &input, &result);
vitte_driver_input_destroy(&input);
vitte_driver_shutdown(&driver);
```
