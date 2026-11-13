# BSD installer compatibility entry points

The scripts in `scripts/` are compatibility wrappers. They locate the Vitte
repository and delegate to the canonical builders in its top-level `scripts/`
directory; they do not carry a second copy of the build or staging logic.

- FreeBSD delegates to `scripts/build-freebsd-packages.sh` and creates a native
  `pkg(8)` package.
- DragonFly, OpenBSD, NetBSD, GhostBSD, and other listed BSD families delegate
  to `scripts/build-bsd-installers.sh` and create portable `tar.xz` installers.
  These artifacts are not represented as native OS packages.
- The generic `build-bsd-packages.sh` selects the family from `uname` or an
  explicit `BSD_FAMILY` environment value. Set `ARCH` and `BSD_RELEASE` to
  override the defaults.

Examples:

```sh
sh scripts/build-freebsd-packages.sh --dry-run
BSD_FAMILY=openbsd ARCH=amd64 sh scripts/build-bsd-packages.sh --dry-run
sh scripts/build-ghostbsd-packages.sh --list-targets
```

For supported targets, use the canonical scripts directly. Real package
installation and compiler smoke tests must still be run on the target OS.
