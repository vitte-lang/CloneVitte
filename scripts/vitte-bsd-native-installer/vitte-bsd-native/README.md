# Vitte — native BSD package builder

This is a **build-from-source-tree** entry point, modeled on `scripts/build-linux-debs.sh`. It invokes the same `scripts/stage-installer-payload.sh` interface, rather than asking for a prebuilt `PAYLOAD`. Run it **on the target BSD system**; no cross-compilation.

## Integration

Copy `scripts/build-bsd-packages.sh` to the `scripts/` directory of the Vitte repository.

```sh
cd ~/Documents/GitHub/vitte
chmod +x scripts/build-bsd-packages.sh
MODE=full VERSION=0.1.0 sh scripts/build-bsd-packages.sh
```

Default native prefixes: `/usr/local` on FreeBSD, OpenBSD and DragonFly; `/usr/pkg` on NetBSD. The builder stages the compiler and editor assets in `target/installer-bsd-<OS>-<arch>` and writes packages under `pkgout/bsd/<OS>/<arch>`.

### Bootstrap and regression tests

Bootstrap is opt-in and **fails closed** unless the real project command is provided:

```sh
BOOTSTRAP=1 \
BOOTSTRAP_CMD='sh scripts/YOUR-REAL-BOOTSTRAP.sh' \
VERIFY_CMD='"$VITTE_BIN" compile games/game.vit -o /tmp/vitte-game-test' \
sh scripts/build-bsd-packages.sh
```

The bootstrap command shown is a placeholder; replace it with the real build procedure. Never run this with an unverified bootstrap command that installs into the live prefix. The staging script must support the detected BSD OS and architecture, and produce the executable under the selected prefix.

## Platform support and limitations

- **FreeBSD and DragonFly:** build staged payload and invoke `pkg create`; the manifest and resulting packages require validation on those hosts. Native package installation uses `pkg add /path/to/package.pkg`.
- **OpenBSD and NetBSD:** build staged payload and verify the binary, but **do not yet generate native packages**. The `pkg_create` staging behavior and package metadata must be validated on real hosts first. No fallback tarball is misrepresented as a native package.
- **Editors:** Vim runtime, Emacs Lisp and Nano syntax are staged into shared paths. Geany and Tree-sitter source integrations are included, but their per-user activation is not automatic. User configuration files are never modified by package hooks.
- **Rollback:** package management should be handled through native package-manager procedures with a retained previous package. This builder never overwrites the installed compiler itself.

This bundle is an implementation starting point, **not a tested four-BSD production installer**. In particular, the existing Vitte staging script's OS mappings and the compiler's actual BSD support must be checked.
