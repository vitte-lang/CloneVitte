# Bootstrap filesystem boundary

`src/bootstrap/filesystem` is the compiler's ownership-explicit C17 boundary
for files, directories and paths. Compiler layers call this API instead of
opening files or using host directory functions directly.

## Contract

- File reads and writes are binary safe and report complete-operation errors.
- Read buffers, directory lists and returned paths have explicit destroy
  functions.
- Reads and path operations enforce bounded sizes and checked arithmetic.
- Directory enumeration is sorted by entry name for deterministic builds.
- Symlinks can be queried separately from their target on POSIX; Windows
  reparse points are handled through the native file API when available.
- Statistics are thread local and do not create shared compiler state.

## Paths and hosts

`join` and `normalize` accept `/` and `\\`, drive-letter paths and UNC paths,
and return `/` separators. `canonicalize` resolves an existing path through
the host API. POSIX calls (`stat`, `open`, `mkdir`, directory iteration and
`realpath`) are isolated in this translation unit; Windows uses the wide CRT
and Win32 equivalents behind the same API. UTF-8 paths are converted at that
boundary, so filenames are not interpreted through the active ANSI code page.

The public API does not expose `PATH_MAX`, `DIR`, `HANDLE` or platform error
codes. Callers receive `vitte_filesystem_error_t` plus the optional native
error number.

## Validation

`make test-robustness` exercises file size and path limits, atomic writes,
metadata queries, directory creation and enumeration, rename, and cleanup.
`make compiler-binary-ownership` inventories which filesystem C symbols enter
the standard compiler and which unresolved imports the Vitte frontend still
uses. The compiler power gate runs both checks:

```sh
make MODE=release test-robustness
make MODE=release compiler-binary-ownership
make MODE=release compiler-power-gate
```

The current standard binary still links this C object. The ownership audit
reports 27 exported filesystem symbols, of which 8 satisfy active Vitte
frontend imports. Porting those remaining imports to Vitte is required before
the object can be removed from the standard link.
