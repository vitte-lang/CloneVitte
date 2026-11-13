# Installer build scripts

Installer builders for Vitte release artifacts.

Common options:

- `--dry-run`: print the planned builder action and exit before staging or writing artifacts.
- `--list-targets`: list supported installer target tuples from `build-all-installers.sh`.
- `--print-env`: print effective build environment from `build-all-installers.sh`.
- `--verify-only OUT_DIR`: verify an existing artifact directory.
- `--clean`: remove `target/installer-*`; requires `CONFIRM_CLEAN=YES`.

Common environment:

- `VERSION`: version du paquet. Par défaut, elle provient du fichier racine `VERSION`.
- `OUT_DIR`: output directory for artifacts. Defaults to `pkgout`.
- `ARCH`: target architecture or `all`, depending on the builder.
- `STRICT_PROCESSOR`: when `1`, payload staging refuses host fallback binaries for mismatched architectures.
- `VITTE_BIN`: chemin explicite du compilateur. Par défaut, les scripts utilisent le binaire C natif `build/bin/vitte`, construit avec `make`.
- `STRICT_NATIVE`: when `1`, `verify-installers.sh` requires native Windows and Solaris packages, not only portable kits.
- `STRICT_DMG`: when `1`, macOS installer builds fail if `hdiutil` cannot create the DMG. Release jobs should set this.
- `DMG_PRESENTATION`: when `1` (default), macOS DMGs are staged as a polished Finder window with a dark Vitte layout, an `Install Vitte.pkg` item and an Applications shortcut. Set to `0` for a plain compressed DMG.
- `FAMILY`: `linux`, `portable`, `freebsd`, `bsd`, `macos`, `solaris`, `windows`, or `all`.
- `SIGN`: when `1`, sign macOS/Windows artifacts using platform-specific tools.
- `NOTARIZE`: when `1`, submit signed macOS artifacts to Apple notary service.
- `SBOM`: when `1`, generate SPDX and CycloneDX SBOM files.

Examples:

- Linux: `FAMILY=linux ARCH=amd64 scripts/build-all-installers.sh`
- Portable tarball: `PLATFORM=linux ARCH=amd64 scripts/build-portable-tarball.sh`
- BSD portable: `FAMILY=bsd BSD_FAMILY=openbsd ARCH=amd64 scripts/build-all-installers.sh`
- macOS release: `FAMILY=macos STRICT_DMG=1 SIGN=1 NOTARIZE=1 scripts/build-all-installers.sh`
- macOS preview DMG: `ARCH=arm64 STRICT_DMG=1 scripts/build-macos-installers.sh`
- Mac OS X 2006 / iMac Intel i386 package: `ARCH=macos2006-i386 scripts/build-macos-installers.sh`
- Solaris: `FAMILY=solaris ARCH=i386 scripts/build-all-installers.sh`
- Windows retrocompatibility kits: `FAMILY=windows ARCH=all scripts/build-all-installers.sh`
- Windows professional matrix: `pwsh scripts/build-windows.ps1 -Arch all -WindowsTargets "xp vista 7 8 8.1 10 11"`
- Windows wrapper inspection: `pwsh scripts/build-windows.ps1 -DryRun` (also `-ListTargets`, `-PrintEnv`, `-Help`). The wrapper accepts `-Arch`, `-Version`, `-OutDir`, `-PackageName`, `-WindowsTargets`, `-VitteExe` (alias `-VitteBin`), `-Shell`, `-BuilderPath`, `-Sign`, `-SignCert` (alias `-WindowsSignCert`), and `-StrictNative`; it discovers common MSYS2/Git Bash locations, adds the selected shell's tool directories only for the build, converts Windows paths before invoking Bash, and restores the caller's environment afterward.

Raspberry Pi 1 source installation:

- Use a 32-bit Raspberry Pi OS installation on the Pi 1 itself. The Pi 1 is ARMv6; a generic Debian `armhf` or ARMv7 binary must not be assumed to run on it. See [Raspberry Pi's architecture table](https://www.raspberrypi.com/news/raspberry-pi-os-64-bit/).
- Install build prerequisites first: `sudo apt-get install build-essential python3`.
- Run `scripts/install-raspberry-pi1.sh` from the source checkout. It builds natively with one job by default, installs the compiler and JSON source module via `make install`, then compiles and runs a smoke program. The default prefix is `/usr/local`; the script asks for `sudo` only for the installation step when required.
- For a user-writable installation: `scripts/install-raspberry-pi1.sh --prefix "$HOME/.local"`. Add `$HOME/.local/bin` to `PATH` if needed.
- Inspect without changing the system: `scripts/install-raspberry-pi1.sh --dry-run`; `--print-env` displays the resolved configuration. Use `--destdir /absolute/stage` to stage an installation; `--allow-non-pi` requires `--destdir`, is only for host staging, and does not cross-compile for ARMv6.
- `--full` stages the runtime, standard library, headers, docs, examples, and editor assets. `--verify-only` checks an existing live installation; `--no-verify` skips the default compile-and-run smoke test. `--bootstrap` runs only an explicitly supplied `BOOTSTRAP_CMD`; `--offline` is the default and does not contact the network.
- `--backup-dir DIR` chooses the parent directory for a timestamped backup of replaced files. Live installation commits are rolled back if a file replacement fails. `--log-file FILE`, `--min-memory-kib N`, and `--benchmark` enable append-only logging, a low-memory warning threshold, and elapsed-time reporting.
- A Pi 1 has little memory; keep the default single build job unless the device has enough free memory or swap. Build failures are reported, not replaced by a possibly incompatible prebuilt binary.

Runtime contract checks:

- `VITTE_BIN=/absolute/path/to/vitte scripts/ci/real-install-smoke.sh` checks an installed Unix compiler, compiles a smoke program and executes it. The PowerShell equivalent is `pwsh scripts/ci/real-install-smoke.ps1 -VitteBin C:\path\to\vitte.exe`.
- `OUT_DIR=pkgout scripts/verify-installers.sh` checks release artifacts and checksums. Run the real-install smoke script on each target system as a separate release gate; artifact inspection alone does not prove the installed binary runs there.
- Installed Unix and portable payloads include `vitte-installer-doctor`; Windows payloads include `vitte-installer-doctor.cmd`. The doctor prints the resolved prefix, expected wrapper/payload/share paths, `VITTE_ROOT`, and the exact missing part when an installation is incomplete.
- Windows `cmd.exe`/PowerShell and macOS Terminal coverage is represented by the real-platform smoke scripts and package shell-profile contract; release CI must execute those scripts on the real target systems.
- Do not publish an architecture-specific artifact without install + compile + run evidence from that target architecture.

Exit codes:

- `0`: requested operation completed.
- `1`: validation/build failure.
- `2`: invalid command-line usage in target listing helpers.

Release macOS builds should run with `STRICT_DMG=1` so a missing DMG is not silently accepted.
