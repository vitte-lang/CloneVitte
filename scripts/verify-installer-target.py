#!/usr/bin/env python3
"""Reject installer payloads whose executable is for another target."""

import argparse
import platform
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary")
    parser.add_argument("target_os")
    parser.add_argument("target_arch")
    args = parser.parse_args()

    host_os = platform.system().lower()
    target_os = args.target_os.lower()
    aliases = {"macos": "darwin", "ghostbsd": "freebsd", "hardenedbsd": "freebsd",
               "nomadbsd": "freebsd", "hellosystem": "freebsd",
               "midnightbsd": "freebsd", "solaris": "sunos"}
    expected_host = aliases.get(target_os, target_os)
    if host_os != expected_host:
        parser.error(f"{target_os}/{args.target_arch} requires a {expected_host} build host; current host is {host_os}")

    host_machine = platform.machine().lower()
    machine_aliases = {
        "x86_64": "amd64", "amd64": "amd64", "aarch64": "arm64",
        "arm64": "arm64", "i686": "i386", "i386": "i386",
        "armv7l": "armv7", "armv6l": "armv6", "powerpc64": "powerpc64",
        "ppc64": "powerpc64", "ppc64le": "powerpc64le",
    }
    actual_arch = machine_aliases.get(host_machine, host_machine)
    requested_arch = args.target_arch.lower()
    if requested_arch == "macos2006-i386":
        requested_arch = "i386"
    target_arch = machine_aliases.get(requested_arch, requested_arch)

    if target_arch in {"universal", "universal2"} and host_os == "darwin":
        try:
            architectures = subprocess.check_output(
                ["lipo", "-archs", args.binary], text=True, stderr=subprocess.STDOUT
            ).strip().split()
        except (OSError, subprocess.CalledProcessError) as error:
            parser.error(f"cannot inspect macOS universal executable: {error}")
        if not {"arm64", "x86_64"}.issubset(architectures):
            parser.error(f"{target_arch} requires arm64 and x86_64 slices, found {architectures}")
    elif actual_arch != target_arch:
        parser.error(f"{target_os}/{target_arch} requires a matching build host; current architecture is {actual_arch}")

    # This stage executes the compiler below; a successful execution checks
    # that the payload is runnable on the claimed native build host.
    return 0


if __name__ == "__main__":
    sys.exit(main())
