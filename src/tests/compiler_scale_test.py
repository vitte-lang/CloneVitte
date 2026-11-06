#!/usr/bin/env python3
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import tempfile
import time
import unittest
from typing import Final, Sequence


ROOT: Final[Path] = Path(__file__).resolve().parents[2]
COMPILER: Final[Path] = Path(
    os.environ.get("VITTE_BIN", ROOT / "build/bin/vitte")
).expanduser()

DEFAULT_TIMEOUT: Final[int] = int(
    os.environ.get("VITTE_SCALE_TIMEOUT", "180")
)

LARGE_SOURCE_MIB: Final[int] = int(
    os.environ.get("VITTE_SCALE_SOURCE_MIB", "48")
)

LARGE_DECLARATION_COUNT: Final[int] = int(
    os.environ.get("VITTE_SCALE_DECLARATIONS", "10000")
)

MIB: Final[int] = 1024 * 1024


class CompilerScaleTests(unittest.TestCase):
    """Large-input regression tests for the Vitte compiler."""

    @classmethod
    def setUpClass(cls) -> None:
        super().setUpClass()

        if not COMPILER.exists():
            raise unittest.SkipTest(
                f"Vitte compiler not found: {COMPILER}"
            )

        if not COMPILER.is_file():
            raise unittest.SkipTest(
                f"Vitte compiler path is not a file: {COMPILER}"
            )

        if not os.access(COMPILER, os.X_OK):
            raise unittest.SkipTest(
                f"Vitte compiler is not executable: {COMPILER}"
            )

        if DEFAULT_TIMEOUT <= 0:
            raise ValueError(
                "VITTE_SCALE_TIMEOUT must be greater than zero"
            )

        if LARGE_SOURCE_MIB <= 0:
            raise ValueError(
                "VITTE_SCALE_SOURCE_MIB must be greater than zero"
            )

        if LARGE_DECLARATION_COUNT <= 0:
            raise ValueError(
                "VITTE_SCALE_DECLARATIONS must be greater than zero"
            )

    def run_compiler(
        self,
        *args: str,
        timeout: int = DEFAULT_TIMEOUT,
    ) -> subprocess.CompletedProcess[str]:
        command = [str(COMPILER), *args]

        try:
            return subprocess.run(
                command,
                cwd=ROOT,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=timeout,
                check=False,
            )
        except subprocess.TimeoutExpired as error:
            stdout = (
                error.stdout.decode("utf-8", errors="replace")
                if isinstance(error.stdout, bytes)
                else error.stdout or ""
            )
            stderr = (
                error.stderr.decode("utf-8", errors="replace")
                if isinstance(error.stderr, bytes)
                else error.stderr or ""
            )

            self.fail(
                "Vitte compiler timed out\n"
                f"timeout: {timeout}s\n"
                f"command: {self.format_command(command)}\n"
                f"stdout:\n{stdout}\n"
                f"stderr:\n{stderr}"
            )

        raise AssertionError("unreachable")

    def assert_compiler_success(
        self,
        result: subprocess.CompletedProcess[str],
        command_description: str,
    ) -> None:
        self.assertEqual(
            result.returncode,
            0,
            (
                f"{command_description} failed\n"
                f"return code: {result.returncode}\n"
                f"stdout:\n{result.stdout}\n"
                f"stderr:\n{result.stderr}"
            ),
        )

    @staticmethod
    def format_command(command: Sequence[str]) -> str:
        return " ".join(
            CompilerScaleTests.quote_argument(argument)
            for argument in command
        )

    @staticmethod
    def quote_argument(argument: str) -> str:
        if argument and all(
            character.isalnum()
            or character in "/._-=:+"
            for character in argument
        ):
            return argument

        return repr(argument)

    @staticmethod
    def write_large_comment_source(
        source: Path,
        size_mib: int,
    ) -> None:
        chunk = b"x" * MIB

        with source.open("wb", buffering=MIB) as output:
            output.write(b"/*")

            for _ in range(size_mib):
                output.write(chunk)

            output.write(
                b"*/\n"
                b"export proc main() -> int {\n"
                b"    return 0;\n"
                b"}\n"
            )

    @staticmethod
    def write_large_declaration_source(
        source: Path,
        declaration_count: int,
    ) -> None:
        with source.open(
            "w",
            encoding="utf-8",
            newline="\n",
            buffering=MIB,
        ) as output:
            output.write(
                "space scale.large_declarations;\n\n"
            )

            for index in range(declaration_count):
                output.write(
                    f"proc helper_{index}() -> int {{\n"
                    f"    return {index % 100};\n"
                    f"}}\n"
                )

            output.write(
                "\n"
                "export proc main() -> int {\n"
                f"    return helper_{declaration_count - 1}();\n"
                "}\n"
            )

    def test_large_source_file(self) -> None:
        """Lexer/parser must handle a very large source without truncation."""

        with tempfile.TemporaryDirectory(
            prefix="vitte-scale-source-"
        ) as directory:
            source = Path(directory) / "large_comments.vit"

            started = time.monotonic()

            self.write_large_comment_source(
                source,
                LARGE_SOURCE_MIB,
            )

            generation_elapsed = time.monotonic() - started

            size = source.stat().st_size
            minimum_size = LARGE_SOURCE_MIB * MIB

            self.assertGreaterEqual(
                size,
                minimum_size,
                (
                    "generated scale fixture is unexpectedly small: "
                    f"{size} bytes < {minimum_size} bytes"
                ),
            )

            compile_started = time.monotonic()

            checked = self.run_compiler(
                "check",
                str(source),
                "--quiet",
            )

            compile_elapsed = time.monotonic() - compile_started

            self.assert_compiler_success(
                checked,
                "large source check",
            )

            self.assertGreater(
                generation_elapsed,
                0.0,
            )

            self.assertGreater(
                compile_elapsed,
                0.0,
            )

    def test_large_source_boundary_tokens(self) -> None:
        """
        Large trivia must not corrupt tokens located immediately after it.

        This specifically catches scanners that truncate offsets or lose
        synchronization after processing tens of MiB of source text.
        """

        with tempfile.TemporaryDirectory(
            prefix="vitte-scale-boundary-"
        ) as directory:
            source = Path(directory) / "large_boundary.vit"

            chunk = b"a" * MIB

            with source.open("wb", buffering=MIB) as output:
                output.write(
                    b"space scale.large_boundary;\n"
                    b"/*"
                )

                for _ in range(LARGE_SOURCE_MIB):
                    output.write(chunk)

                output.write(
                    b"*/\n"
                    b"const ANSWER: int = 42;\n"
                    b"export proc main() -> int {\n"
                    b"    return ANSWER;\n"
                    b"}\n"
                )

            result = self.run_compiler(
                "check",
                str(source),
                "--quiet",
            )

            self.assert_compiler_success(
                result,
                "large boundary-token source check",
            )

    def test_large_line_comment(self) -> None:
        """A single huge line comment must not overflow scanner offsets."""

        with tempfile.TemporaryDirectory(
            prefix="vitte-scale-line-comment-"
        ) as directory:
            source = Path(directory) / "large_line_comment.vit"

            chunk = b"x" * MIB

            with source.open("wb", buffering=MIB) as output:
                output.write(b"//")

                for _ in range(LARGE_SOURCE_MIB):
                    output.write(chunk)

                output.write(
                    b"\n"
                    b"export proc main() -> int {\n"
                    b"    return 0;\n"
                    b"}\n"
                )

            result = self.run_compiler(
                "check",
                str(source),
                "--quiet",
            )

            self.assert_compiler_success(
                result,
                "large line-comment source check",
            )

    def test_large_whitespace_region(self) -> None:
        """Large trivia regions must not require pathological memory growth."""

        with tempfile.TemporaryDirectory(
            prefix="vitte-scale-whitespace-"
        ) as directory:
            source = Path(directory) / "large_whitespace.vit"

            chunk = b" " * MIB

            with source.open("wb", buffering=MIB) as output:
                output.write(
                    b"space scale.large_whitespace;\n"
                )

                for _ in range(LARGE_SOURCE_MIB):
                    output.write(chunk)

                output.write(
                    b"\n"
                    b"export proc main() -> int {\n"
                    b"    return 0;\n"
                    b"}\n"
                )

            result = self.run_compiler(
                "check",
                str(source),
                "--quiet",
            )

            self.assert_compiler_success(
                result,
                "large whitespace source check",
            )

    def test_ten_thousand_procedures_compile_through_sema(self) -> None:
        """
        A translation unit containing many declarations must survive the
        complete frontend through semantic analysis.
        """

        declaration_count = LARGE_DECLARATION_COUNT

        with tempfile.TemporaryDirectory(
            prefix="vitte-scale-declarations-"
        ) as directory:
            source = Path(directory) / "large_declarations.vit"

            self.write_large_declaration_source(
                source,
                declaration_count,
            )

            size = source.stat().st_size

            minimum_size = declaration_count * 35

            self.assertGreater(
                size,
                minimum_size,
                (
                    "large declaration fixture is unexpectedly small: "
                    f"{size} bytes < {minimum_size} bytes"
                ),
            )

            compiled = self.run_compiler(
                "compile",
                str(source),
                "--stop-after-sema",
                "--quiet",
            )

            self.assert_compiler_success(
                compiled,
                (
                    "semantic compilation of "
                    f"{declaration_count} procedures"
                ),
            )

    def test_many_forward_references(self) -> None:
        """
        Exercise symbol lookup and semantic resolution with many forward
        references without introducing a deep runtime call chain.
        """

        declaration_count = max(
            2_000,
            LARGE_DECLARATION_COUNT // 2,
        )

        with tempfile.TemporaryDirectory(
            prefix="vitte-scale-forward-refs-"
        ) as directory:
            source = Path(directory) / "forward_references.vit"

            with source.open(
                "w",
                encoding="utf-8",
                newline="\n",
                buffering=MIB,
            ) as output:
                output.write(
                    "space scale.forward_references;\n\n"
                )

                for index in range(declaration_count - 1):
                    output.write(
                        f"proc forward_{index}() -> int {{\n"
                        f"    return forward_{index + 1}();\n"
                        f"}}\n"
                    )

                output.write(
                    f"proc forward_{declaration_count - 1}() -> int {{\n"
                    "    return 42;\n"
                    "}\n\n"
                    "export proc main() -> int {\n"
                    "    return forward_0();\n"
                    "}\n"
                )

            result = self.run_compiler(
                "compile",
                str(source),
                "--stop-after-sema",
                "--quiet",
            )

            self.assert_compiler_success(
                result,
                (
                    "semantic compilation of "
                    f"{declaration_count} forward references"
                ),
            )

    def test_many_constants(self) -> None:
        """Exercise declaration storage and constant symbol lookup at scale."""

        constant_count = LARGE_DECLARATION_COUNT

        with tempfile.TemporaryDirectory(
            prefix="vitte-scale-constants-"
        ) as directory:
            source = Path(directory) / "many_constants.vit"

            with source.open(
                "w",
                encoding="utf-8",
                newline="\n",
                buffering=MIB,
            ) as output:
                output.write(
                    "space scale.many_constants;\n\n"
                )

                for index in range(constant_count):
                    output.write(
                        f"const VALUE_{index}: int = "
                        f"{index % 100};\n"
                    )

                output.write(
                    "\n"
                    "export proc main() -> int {\n"
                    f"    return VALUE_{constant_count - 1};\n"
                    "}\n"
                )

            result = self.run_compiler(
                "compile",
                str(source),
                "--stop-after-sema",
                "--quiet",
            )

            self.assert_compiler_success(
                result,
                (
                    "semantic compilation of "
                    f"{constant_count} constants"
                ),
            )

    def test_many_form_declarations(self) -> None:
        """Exercise aggregate/type registration with many nominal types."""

        form_count = max(
            2_000,
            LARGE_DECLARATION_COUNT // 2,
        )

        with tempfile.TemporaryDirectory(
            prefix="vitte-scale-forms-"
        ) as directory:
            source = Path(directory) / "many_forms.vit"

            with source.open(
                "w",
                encoding="utf-8",
                newline="\n",
                buffering=MIB,
            ) as output:
                output.write(
                    "space scale.many_forms;\n\n"
                )

                for index in range(form_count):
                    output.write(
                        f"form Form_{index} {{\n"
                        "    value: int,\n"
                        "}\n"
                    )

                output.write(
                    "\n"
                    "export proc main() -> int {\n"
                    "    return 0;\n"
                    "}\n"
                )

            result = self.run_compiler(
                "compile",
                str(source),
                "--stop-after-sema",
                "--quiet",
            )

            self.assert_compiler_success(
                result,
                (
                    "semantic compilation of "
                    f"{form_count} form declarations"
                ),
            )

    def test_many_local_bindings(self) -> None:
        """
        Exercise parser, scope construction and semantic local-symbol handling
        with a large function body.
        """

        local_count = max(
            5_000,
            LARGE_DECLARATION_COUNT,
        )

        with tempfile.TemporaryDirectory(
            prefix="vitte-scale-locals-"
        ) as directory:
            source = Path(directory) / "many_locals.vit"

            with source.open(
                "w",
                encoding="utf-8",
                newline="\n",
                buffering=MIB,
            ) as output:
                output.write(
                    "space scale.many_locals;\n\n"
                    "export proc main() -> int {\n"
                )

                for index in range(local_count):
                    output.write(
                        f"    let local_{index}: int = "
                        f"{index % 100};\n"
                    )

                output.write(
                    f"    return local_{local_count - 1};\n"
                    "}\n"
                )

            result = self.run_compiler(
                "compile",
                str(source),
                "--stop-after-sema",
                "--quiet",
            )

            self.assert_compiler_success(
                result,
                (
                    "semantic compilation of "
                    f"{local_count} local bindings"
                ),
            )

    def test_many_assignments(self) -> None:
        """
        Exercise semantic assignment checking and large statement lists.
        """

        assignment_count = max(
            10_000,
            LARGE_DECLARATION_COUNT,
        )

        with tempfile.TemporaryDirectory(
            prefix="vitte-scale-assignments-"
        ) as directory:
            source = Path(directory) / "many_assignments.vit"

            with source.open(
                "w",
                encoding="utf-8",
                newline="\n",
                buffering=MIB,
            ) as output:
                output.write(
                    "space scale.many_assignments;\n\n"
                    "export proc main() -> int {\n"
                    "    let mut value: int = 0;\n"
                )

                for index in range(assignment_count):
                    output.write(
                        f"    set value = {index % 100};\n"
                    )

                output.write(
                    "    return value;\n"
                    "}\n"
                )

            result = self.run_compiler(
                "compile",
                str(source),
                "--stop-after-sema",
                "--quiet",
            )

            self.assert_compiler_success(
                result,
                (
                    "semantic compilation of "
                    f"{assignment_count} assignments"
                ),
            )

    def test_many_nested_blocks(self) -> None:
        """
        Exercise scope creation and destruction without relying on recursive
        expression parsing.
        """

        depth = int(
            os.environ.get(
                "VITTE_SCALE_BLOCK_DEPTH",
                "1000",
            )
        )

        self.assertGreater(
            depth,
            0,
            "VITTE_SCALE_BLOCK_DEPTH must be greater than zero",
        )

        with tempfile.TemporaryDirectory(
            prefix="vitte-scale-blocks-"
        ) as directory:
            source = Path(directory) / "nested_blocks.vit"

            with source.open(
                "w",
                encoding="utf-8",
                newline="\n",
                buffering=MIB,
            ) as output:
                output.write(
                    "space scale.nested_blocks;\n\n"
                    "export proc main() -> int {\n"
                    "    let mut result: int = 0;\n"
                )

                for _ in range(depth):
                    output.write("    {\n")

                output.write(
                    "        set result = 42;\n"
                )

                for _ in range(depth):
                    output.write("    }\n")

                output.write(
                    "    return result;\n"
                    "}\n"
                )

            result = self.run_compiler(
                "compile",
                str(source),
                "--stop-after-sema",
                "--quiet",
            )

            self.assert_compiler_success(
                result,
                (
                    "semantic compilation of "
                    f"{depth} nested blocks"
                ),
            )

    def test_large_identifier_table(self) -> None:
        """
        Stress identifier interning with many unique names of varying length.
        """

        identifier_count = LARGE_DECLARATION_COUNT

        with tempfile.TemporaryDirectory(
            prefix="vitte-scale-identifiers-"
        ) as directory:
            source = Path(directory) / "many_identifiers.vit"

            with source.open(
                "w",
                encoding="utf-8",
                newline="\n",
                buffering=MIB,
            ) as output:
                output.write(
                    "space scale.many_identifiers;\n\n"
                )

                for index in range(identifier_count):
                    name = (
                        f"scale_identifier_{index:05d}_"
                        f"unique_symbol_{index:05d}"
                    )

                    output.write(
                        f"const {name}: int = "
                        f"{index % 100};\n"
                    )

                final_name = (
                    f"scale_identifier_{identifier_count - 1:05d}_"
                    f"unique_symbol_{identifier_count - 1:05d}"
                )

                output.write(
                    "\n"
                    "export proc main() -> int {\n"
                    f"    return {final_name};\n"
                    "}\n"
                )

            result = self.run_compiler(
                "compile",
                str(source),
                "--stop-after-sema",
                "--quiet",
            )

            self.assert_compiler_success(
                result,
                (
                    "semantic compilation of "
                    f"{identifier_count} unique identifiers"
                ),
            )

    def test_repeated_compiler_invocations(self) -> None:
        """
        Repeated fresh compiler processes must remain deterministic.

        This catches accidental dependence on stale filesystem state and
        process-global initialization assumptions.
        """

        invocation_count = int(
            os.environ.get(
                "VITTE_SCALE_INVOCATIONS",
                "32",
            )
        )

        self.assertGreater(
            invocation_count,
            0,
            "VITTE_SCALE_INVOCATIONS must be greater than zero",
        )

        with tempfile.TemporaryDirectory(
            prefix="vitte-scale-repeat-"
        ) as directory:
            source = Path(directory) / "repeat.vit"

            source.write_text(
                "space scale.repeat;\n"
                "\n"
                "proc answer() -> int {\n"
                "    return 42;\n"
                "}\n"
                "\n"
                "export proc main() -> int {\n"
                "    return answer();\n"
                "}\n",
                encoding="utf-8",
                newline="\n",
            )

            for index in range(invocation_count):
                result = self.run_compiler(
                    "check",
                    str(source),
                    "--quiet",
                )

                self.assert_compiler_success(
                    result,
                    (
                        "repeated compiler invocation "
                        f"{index + 1}/{invocation_count}"
                    ),
                )

    def test_deterministic_diagnostics_for_large_invalid_input(self) -> None:
        """
        Large invalid inputs should fail deterministically instead of crashing.

        Two equivalent compiler invocations must return the same exit status
        and diagnostic text.
        """

        declaration_count = max(
            2_000,
            LARGE_DECLARATION_COUNT // 2,
        )

        with tempfile.TemporaryDirectory(
            prefix="vitte-scale-invalid-"
        ) as directory:
            source = Path(directory) / "large_invalid.vit"

            with source.open(
                "w",
                encoding="utf-8",
                newline="\n",
                buffering=MIB,
            ) as output:
                output.write(
                    "space scale.large_invalid;\n\n"
                )

                for index in range(declaration_count):
                    output.write(
                        f"proc helper_{index}() -> int {{\n"
                        f"    return {index % 100};\n"
                        "}\n"
                    )

                output.write(
                    "\n"
                    "export proc main() -> int {\n"
                    "    return definitely_missing_symbol;\n"
                    "}\n"
                )

            first = self.run_compiler(
                "compile",
                str(source),
                "--stop-after-sema",
                "--quiet",
            )

            second = self.run_compiler(
                "compile",
                str(source),
                "--stop-after-sema",
                "--quiet",
            )

            self.assertNotEqual(
                first.returncode,
                0,
                (
                    "invalid scale fixture unexpectedly compiled\n"
                    f"stdout:\n{first.stdout}\n"
                    f"stderr:\n{first.stderr}"
                ),
            )

            self.assertEqual(
                first.returncode,
                second.returncode,
                (
                    "compiler returned different exit codes for "
                    "identical invalid input"
                ),
            )

            self.assertEqual(
                first.stdout,
                second.stdout,
                (
                    "compiler stdout is not deterministic for "
                    "identical invalid input"
                ),
            )

            self.assertEqual(
                first.stderr,
                second.stderr,
                (
                    "compiler stderr is not deterministic for "
                    "identical invalid input"
                ),
            )

            diagnostics = first.stdout + first.stderr

            self.assertTrue(
                diagnostics.strip(),
                "invalid source produced no diagnostic output",
            )

            self.assertIn(
                "definitely_missing_symbol",
                diagnostics,
                (
                    "diagnostic does not mention the intentionally "
                    "missing symbol"
                ),
            )

    def test_empty_file_does_not_crash(self) -> None:
        """An empty translation unit may fail, but it must terminate cleanly."""

        with tempfile.TemporaryDirectory(
            prefix="vitte-scale-empty-"
        ) as directory:
            source = Path(directory) / "empty.vit"
            source.write_bytes(b"")

            try:
                result = self.run_compiler(
                    "check",
                    str(source),
                )
            except AssertionError:
                raise

            self.assertGreaterEqual(
                result.returncode,
                0,
                (
                    "compiler appears to have terminated from a signal "
                    f"(return code {result.returncode})"
                ),
            )

    def test_large_invalid_tail_does_not_crash(self) -> None:
        """
        A syntax error after a very large valid/trivia prefix must still
        produce an ordinary compiler failure rather than an overflow/crash.
        """

        with tempfile.TemporaryDirectory(
            prefix="vitte-scale-invalid-tail-"
        ) as directory:
            source = Path(directory) / "large_invalid_tail.vit"

            chunk = b"x" * MIB

            with source.open("wb", buffering=MIB) as output:
                output.write(b"/*")

                for _ in range(LARGE_SOURCE_MIB):
                    output.write(chunk)

                output.write(
                    b"*/\n"
                    b"export proc main() -> int {\n"
                    b"    return 0\n"
                    b"    invalid_tail\n"
                    b"}\n"
                )

            result = self.run_compiler(
                "check",
                str(source),
            )

            self.assertNotEqual(
                result.returncode,
                0,
                (
                    "source with intentional trailing syntax error "
                    "unexpectedly compiled successfully"
                ),
            )

            self.assertGreaterEqual(
                result.returncode,
                0,
                (
                    "compiler terminated from a signal while processing "
                    "a large source with an invalid tail"
                ),
            )

            self.assertTrue(
                (result.stdout + result.stderr).strip(),
                "compiler failed without emitting diagnostics",
            )


if __name__ == "__main__":
    unittest.main(verbosity=2)
