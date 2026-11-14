#!/usr/bin/env python3
"""Check EBNF terminal coverage against the Vitte and C lexer tables."""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GRAMMAR = ROOT / "grammar" / "vitte.ebnf"
C_FRONTENDS = (
    (
        "src/src",
        ROOT / "src" / "src" / "lexer" / "lexer.c",
        ROOT / "src" / "src" / "parser" / "parser.c",
    ),
    (
        "src/bootstrap",
        ROOT / "src" / "bootstrap" / "lexer" / "lexer.c",
        ROOT / "src" / "bootstrap" / "parser" / "parser.c",
    ),
)
NATIVE_LEXER = ROOT / "src" / "compiler" / "lexer" / "lexer.vitte"


def without_comments(text: str) -> str:
    return re.sub(r"\(\*.*?\*\)", "", text, flags=re.DOTALL)


def c_lexer_entries(source: str, macro: str) -> list[tuple[str, str]]:
    return re.findall(
        rf'{macro}\("([^"\\]+)",\s*(VITTE_TOKEN_[A-Z0-9_]+)\)',
        source,
    )


def native_lexer_entries(source: str) -> tuple[list[str], list[str]]:
    keyword_function = re.search(
        r"(?ms)^proc lexer_keyword\(text: string\) -> TokenKind \{(.*?)^\}",
        source,
    )
    operators = re.search(
        r"(?s)LEXER_OPERATORS:\s*\[string\]\s*=\s*\[(.*?)\];",
        source,
    )
    if keyword_function is None or operators is None:
        raise ValueError("could not locate native lexer keyword or operator table")
    keywords = re.findall(r'text\s*==\s*"([^"]+)"', keyword_function.group(1))
    operator_texts = re.findall(r'"([^"]+)"', operators.group(1))
    return keywords, operator_texts


def main() -> int:
    grammar = without_comments(GRAMMAR.read_text(encoding="utf-8"))
    native_lexer = NATIVE_LEXER.read_text(encoding="utf-8")

    terminals = set(re.findall(r'"([^"\\]*(?:\\.[^"\\]*)*)"', grammar))
    errors: list[str] = []
    all_keywords: set[str] = set()
    all_operators: set[str] = set()

    for label, lexer_path, parser_path in C_FRONTENDS:
        lexer = lexer_path.read_text(encoding="utf-8")
        parser = parser_path.read_text(encoding="utf-8")
        keywords = c_lexer_entries(lexer, "VITTE_KW")
        operators = c_lexer_entries(lexer, "VITTE_OP")

        for token_label, entries in (("keyword", keywords), ("operator", operators)):
            texts = [text for text, _ in entries]
            duplicates = sorted({text for text in texts if texts.count(text) > 1})
            errors.extend(
                f"duplicate {label} lexer {token_label}: {text}"
                for text in duplicates
            )

        all_keywords.update(text for text, _ in keywords)
        all_operators.update(text for text, _ in operators)

        for text, _ in keywords:
            if text not in terminals:
                errors.append(f"{label} lexer keyword missing from EBNF: {text}")
        for text, _ in operators:
            if text not in terminals:
                errors.append(f"{label} lexer operator missing from EBNF: {text}")

        for text, token_kind in keywords + operators:
            if token_kind not in parser:
                errors.append(
                    f"{label} lexer token is not referenced by parser: "
                    f"{token_kind} ({text})"
                )

    native_keywords, native_operators = native_lexer_entries(native_lexer)
    for label, values in (
        ("keyword", native_keywords),
        ("operator", native_operators),
    ):
        duplicates = sorted({value for value in values if values.count(value) > 1})
        errors.extend(f"duplicate native lexer {label}: {value}" for value in duplicates)
        missing = sorted(set(values) - terminals)
        errors.extend(f"native lexer {label} missing from EBNF: {value}" for value in missing)

    all_keywords.update(native_keywords)
    all_operators.update(native_operators)

    if errors:
        print("[ebnf-sync] FAILED")
        for error in errors:
            print(f"- {error}")
        return 1

    print(
        f"[ebnf-sync] OK: {len(all_keywords)} keyword spellings, "
        f"{len(all_operators)} operator spellings covered across 3 lexers; "
        "legacy C parser token references verified"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
