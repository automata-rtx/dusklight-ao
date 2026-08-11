#!/usr/bin/env python3
"""Check that docs/japanese-naming.md only names game symbols that exist.

The glossary's whole value is that a session can trust it instead of guessing at
a romanized name. An entry naming a symbol the port has since renamed or dropped
is worse than no entry at all: it is a wrong answer delivered with the authority
of a glossary, and a glossary is exactly the kind of document nobody re-reads.
So the symbols are machine-checked. The Japanese readings are ours and cannot be
checked by anything here.

Also verifies the locale claim the document leads with, because that claim is the
reason anyone finds the game's own labels at all.

Run with no arguments from anywhere in the repo. Exit 0 = consistent.

    python3 tools/check_japanese_naming.py
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DOC = "docs/japanese-naming.md"

# Where the game code lives. Not src/dusk or extern: those are ours, and this
# glossary is about the code we read rather than the code we write.
HAYSTACKS = ["src", "include", "libs"]

# Backticked tokens in the document that are prose, not game symbols. Kept short
# and explicit: a token silently exempted is a glossary entry that stops being
# checked.
NON_SYMBOLS = {
    "camelCase",
    "snake_case",
    "rg",
    "grep",
    "Grep",
    "locale",
    "TARGET_PC",
    "DEBUG",
    "MAnn",
    "MA00",
    "MA01",
    "MA03",
    "MA04",
    "MA06",
    "MA09",
    "MA16",
    "Gake",
    "Kusa",
    "Enkei",
    "Nami",
    "Nigori",
    "kasan",
    "minami",
    "nami",
    "ese",
    "nama",
}

# Prefixes owned by the other two repos or by our own port code, which this
# checkout either cannot see or deliberately excludes from the haystack.
FOREIGN_PREFIXES = ("rtx_", "dxvk_", "aurora_", "hub_", "er_")

# Suffixes worth checking as whole filenames; anything else backticked with a dot
# in it (docs, field accesses like g_env_light.mMoyaCount) is prose as far as
# this check is concerned.
FILE_SUFFIXES = (".cpp", ".h", ".inc")

failures: list[str] = []


def fail(message: str) -> None:
    failures.append(message)


def tokens_from_doc(text: str) -> list[str]:
    """Split the document's backticked tokens into identifiers and filenames.

    They need different checks: an identifier has to appear in some file's text,
    a filename has to *be* a file. Searching a filename as content only works by
    accident, when a header comment happens to repeat it.
    """
    idents: list[str] = []
    files: list[str] = []
    for token in dict.fromkeys(re.findall(r"`([^`\n]+)`", text)):
        if token in NON_SYMBOLS or token.startswith(FOREIGN_PREFIXES):
            continue
        if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", token):
            idents.append(token)
        elif token.endswith(FILE_SUFFIXES) and re.fullmatch(
            r"[A-Za-z_][A-Za-z0-9_]*\.[a-z]+", token
        ):
            files.append(token)
    return idents, files


def tracked_basenames() -> set[str]:
    proc = subprocess.run(
        ["git", "-C", str(REPO), "ls-files", *HAYSTACKS],
        capture_output=True,
        text=True,
    )
    if proc.returncode != 0:
        return set()
    return {Path(line).name for line in proc.stdout.splitlines() if line}


def check_symbols() -> int:
    """Every game symbol the document names must exist in the tree.

    Substring search, deliberately: the file also cites bare prefixes (dKyw_,
    fopAcM_) whose whole point is that they are not complete identifiers.
    """
    path = REPO / DOC
    if not path.is_file():
        fail(f"{DOC} missing - it is the canonical naming reference")
        return 0

    idents, files = tokens_from_doc(path.read_text(encoding="utf-8", errors="replace"))
    if not idents:
        fail(f"{DOC} names no checkable symbols - has the format changed?")
        return 0

    present = [d for d in HAYSTACKS if (REPO / d).is_dir()]
    if not present:
        fail(f"none of {HAYSTACKS} found under {REPO} - is this the game repo?")
        return 0

    for token in idents:
        proc = subprocess.run(
            ["rg", "--fixed-strings", "--quiet", "--", token, *present],
            cwd=REPO,
            capture_output=True,
            text=True,
        )
        if proc.returncode != 0:
            fail(f"{DOC} names `{token}`, which is not in {'/'.join(present)}")

    basenames = tracked_basenames()
    for token in files:
        if basenames and token not in basenames:
            fail(f"{DOC} names the file `{token}`, which is not in {'/'.join(present)}")
    return len(idents) + len(files)


def check_locale_claim() -> None:
    """The document's headline operational claim, re-measured rather than trusted.

    Two things must hold or section 6 is telling sessions something false:
    ripgrep must find Japanese under whatever locale this shell has, and the
    tree must actually contain some.
    """
    if not (REPO / "src").is_dir():
        return
    proc = subprocess.run(
        ["rg", "-l", r"\p{Hiragana}|\p{Katakana}|\p{Han}", "src", "include"],
        cwd=REPO,
        capture_output=True,
        text=True,
        env={**os.environ, "LC_ALL": "POSIX"},
    )
    count = len([ln for ln in proc.stdout.splitlines() if ln])
    if count == 0:
        fail(
            "ripgrep found no kana/kanji under LC_ALL=POSIX. Section 6 claims it is "
            "locale-independent; either that stopped being true or the game's "
            "Japanese labels are gone."
        )


def main() -> int:
    checked = check_symbols()
    check_locale_claim()

    if failures:
        print(f"{DOC}: {len(failures)} problem(s)\n", file=sys.stderr)
        for message in failures:
            print(f"  {message}", file=sys.stderr)
        return 1

    print(f"{DOC}: {checked} symbols checked, all present; locale claim holds")
    return 0


if __name__ == "__main__":
    sys.exit(main())
