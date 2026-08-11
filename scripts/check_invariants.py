#!/usr/bin/env python3
"""Facts this repo states in more than one place, checked mechanically.

Each check exists because a merge that git reported as clean, or a conflict
whose resolution looked obvious, left two places disagreeing:

  * settings.h declares a ConfigVar, settings.cpp initialises it with a
    designated initialiser, and Register() lists it. Two branches adding a
    setting collide here every time, and a resolution that keeps only one side,
    or keeps both in a different order in each file, is a compile error at best
    and a silently unregistered setting at worst. C++20 requires designated
    initialisers to appear in declaration order.
  * The protocol number lives in remix_bridge.cpp and in several documents.
    Two branches bumped it to 7 independently; git conflicted, and both sides
    said 7, so the obvious resolution shipped two features on one version.
  * docs/japanese-naming.md glosses the game's romanized-Japanese symbols, and
    says up front that every symbol it names was checked to exist. A glossary
    is exactly the kind of document nobody re-reads, so that promise is only
    worth anything if something enforces it.

Run with no arguments. Exit 0 = consistent. CI runs it on every push with no
path filter, because doc-only changes are exactly when these drift.
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

failures: list[str] = []
skipped: list[str] = []
checks_run = 0


def fail(check: str, message: str) -> None:
    failures.append(f"[{check}] {message}")


def read(rel: str) -> str | None:
    p = REPO / rel
    return p.read_text(encoding="utf-8", errors="replace") if p.is_file() else None


def tracked_files() -> list[str]:
    out = subprocess.run(
        ["git", "-C", str(REPO), "ls-files"], capture_output=True, text=True, check=False
    )
    return out.stdout.splitlines() if out.returncode == 0 else []


# ---------------------------------------------------------------------------


def check_conflict_markers() -> None:
    global checks_run
    checks_run += 1
    starts = "<" * 7
    ends = ">" * 7
    suffixes = {".md", ".h", ".hpp", ".cpp", ".c", ".py", ".yml", ".json"}
    for rel in tracked_files():
        if Path(rel).suffix not in suffixes or rel == "scripts/check_invariants.py":
            continue
        if rel.startswith("extern/"):
            continue
        text = read(rel)
        if text is None:
            continue
        for n, line in enumerate(text.splitlines(), 1):
            if line.startswith(starts) or line.startswith(ends):
                fail("conflict-markers", f"{rel}:{n} leftover merge marker")


def parse_settings_groups(header: str) -> dict[str, list[str]]:
    """{group name: [field names in declaration order]} from UserSettings."""
    body = header[header.index("struct UserSettings {") :]
    groups: dict[str, list[str]] = {}
    for m in re.finditer(r"struct\s*\{(.*?)\}\s*(\w+);", body, re.S):
        fields = re.findall(
            r"^\s*(?:ConfigVar<[^>]*>|std::array<[^>]*>)\s+(\w+)\s*[;{]", m.group(1), re.M
        )
        if fields:
            groups[m.group(2)] = fields
    return groups


def check_settings_consistency() -> None:
    """Declaration, initialisation and registration must agree, in order."""
    global checks_run
    checks_run += 1

    header = read("src/dusk/settings.h")
    source = read("src/dusk/settings.cpp")
    if header is None or source is None:
        fail("settings", "src/dusk/settings.{h,cpp} missing")
        return

    groups = parse_settings_groups(header)
    if not groups:
        fail("settings", "could not parse any group out of struct UserSettings")
        return

    init_block = source[source.index("UserSettings g_userSettings = {") :]
    # Register() also takes an optional on-change callback, so the call may span
    # lines and carry further arguments - matching on a closing paren here
    # reported pauseOnFocusLost as unregistered when it is not.
    registered = set(re.findall(r"Register\(\s*g_userSettings\.(\w+)\.(\w+)\s*[,)]", source))

    for group, declared in groups.items():
        gm = re.search(r"\." + group + r"\s*=\s*\{(.*?)\n    \},", init_block, re.S)
        if not gm:
            continue  # group is initialised some other way; not this check's business
        initialised = re.findall(r"^\s*\.(\w+)\s*[{(]", gm.group(1), re.M)

        missing = [f for f in declared if f not in initialised]
        if missing:
            fail(
                "settings",
                f"{group}: declared in settings.h but never initialised in settings.cpp: "
                f"{', '.join(missing)} - a merge that kept only one side of a conflict",
            )

        # C++20: designated initialisers must appear in declaration order.
        common = [f for f in initialised if f in declared]
        expected = [f for f in declared if f in initialised]
        if common != expected:
            first = next(
                (a for a, b in zip(common, expected) if a != b), common[-1] if common else "?"
            )
            fail(
                "settings",
                f"{group}: initialiser order in settings.cpp does not match declaration order "
                f"in settings.h (diverges at '{first}'). C++20 requires them to agree, so this "
                f"is a compile error on MSVC, not a style point",
            )

        unregistered = [f for f in declared if (group, f) not in registered]
        if unregistered and len(unregistered) != len(declared):
            # A wholly unregistered group is deliberate (transient state);
            # a few missing out of many is a dropped merge hunk.
            fail(
                "settings",
                f"{group}: declared and initialised but never passed to Register(): "
                f"{', '.join(unregistered)} - such a setting silently never loads or saves",
            )


def check_protocol() -> None:
    """The pushed protocol literal must match every document that states it."""
    global checks_run
    checks_run += 1

    bridge = read("src/dusk/remix_bridge.cpp")
    if bridge is None:
        fail("protocol", "src/dusk/remix_bridge.cpp missing")
        return

    pushes = re.findall(r'push\(\s*"rtx\.dusklight\.env\.protocol"\s*,\s*"(\d+)"\s*\)', bridge)
    if not pushes:
        fail("protocol", "no rtx.dusklight.env.protocol push found in remix_bridge.cpp")
        return
    if len(set(pushes)) > 1:
        fail("protocol", f"remix_bridge.cpp pushes more than one protocol value: {set(pushes)}")
        return
    code_value = int(pushes[0])

    for rel in ["CLAUDE.md"] + [f for f in tracked_files() if f.startswith("docs/") and f.endswith(".md")]:
        text = read(rel)
        if text is None:
            continue
        for n, line in enumerate(text.splitlines(), 1):
            for m in re.finditer(r"[Pp]rotocol is at \*{0,2}(\d+)\*{0,2}", line):
                if int(m.group(1)) != code_value:
                    fail(
                        "protocol",
                        f"{rel}:{n} says protocol {m.group(1)} but remix_bridge.cpp pushes "
                        f"{code_value}",
                    )


def check_aurora_pin_is_real() -> None:
    """The extern/aurora gitlink must name a commit that actually exists.

    A pin written by hand from a short hash points at nothing, and every CI job
    then fails at checkout rather than at build - which reads as a broken
    toolchain rather than a bad pin. This has happened.
    """
    global checks_run
    checks_run += 1

    # The index, not HEAD: the point is to catch a hand-typed pin before it is
    # committed and pushed, at which point every CI job fails at checkout and
    # looks like a broken runner rather than a bad hash.
    out = subprocess.run(
        ["git", "-C", str(REPO), "ls-files", "-s", "extern/aurora"],
        capture_output=True,
        text=True,
        check=False,
    )
    m = re.search(r"160000 ([0-9a-f]{40})", out.stdout)
    if not m:
        return  # no submodule recorded; nothing to check
    sha = m.group(1)

    # Any clone that could know the commit: the submodule itself, or a sibling
    # checkout of aurora, which is how these repos are usually laid out side by
    # side during a session. Without the fallback this check silently skips in
    # exactly the working copy where the bad pin gets written.
    candidates = [REPO / "extern" / "aurora", REPO.parent / "aurora-ao"]
    for cand in candidates:
        if not (cand / ".git").exists():
            continue
        exists = subprocess.run(
            ["git", "-C", str(cand), "cat-file", "-e", sha + "^{commit}"],
            capture_output=True,
            check=False,
        )
        if exists.returncode == 0:
            return
        break
    else:
        skipped.append(
            "aurora-pin: no aurora clone available to verify the pin against "
            "(checked extern/aurora and ../aurora-ao); CI's submodule checkout is the real test"
        )
        return

    if True:
        fail(
            "aurora-pin",
            f"extern/aurora is pinned to {sha[:12]}, which does not exist in the submodule. "
            f"Set it with 'git -C extern/aurora rev-parse HEAD', never by typing a hash",
        )


# Backticked tokens in japanese-naming.md that are prose, not game symbols.
# Kept short and explicit: a token silently exempted is a glossary entry that
# stops being checked.
NAMING_NON_SYMBOLS = {"camelCase"}

# Identifiers owned by the other two repos, which this checkout cannot see. The
# document cites a few when explaining what our code does differently from the
# game's; they are not glossary entries and there is nothing here to check them
# against.
NAMING_FOREIGN_PREFIXES = ("rtx_", "dxvk_", "d3d9_", "dx9_")

# Suffixes worth checking as whole filenames; anything else backticked with a
# dot in it (docs, options, field accesses like g_env_light.mMoyaCount) is prose
# as far as this check is concerned.
NAMING_FILE_SUFFIXES = (".cpp", ".h", ".inc")


def check_japanese_naming_symbols() -> None:
    """Every game symbol docs/japanese-naming.md names must exist in the tree.

    The document's value is that a session can trust it instead of guessing at a
    romanized name, so an entry naming a symbol the port has since renamed or
    dropped is worse than no entry at all - it is a wrong answer delivered with
    the authority of a glossary. The readings are ours and cannot be checked;
    the symbols can be, so they are.

    Substring search, deliberately: the file also cites bare prefixes (dKyw_,
    fopAcM_) whose whole point is that they are not complete identifiers.
    """
    global checks_run
    checks_run += 1

    rel = "docs/japanese-naming.md"
    text = read(rel)
    if text is None:
        fail("japanese-naming", f"{rel} missing - it is the canonical naming reference")
        return

    # Search the game code and the libraries it was decompiled with. Not
    # src/dusk or extern: those are ours, and this glossary is about the code we
    # read rather than the code we write.
    haystacks = ["src", "include", "libs"]

    tokens: list[str] = []
    for token in dict.fromkeys(re.findall(r"`([^`\n]+)`", text)):
        if token in NAMING_NON_SYMBOLS or token.startswith(NAMING_FOREIGN_PREFIXES):
            continue
        if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", token):
            tokens.append(token)
        elif token.endswith(NAMING_FILE_SUFFIXES) and re.fullmatch(
            r"[A-Za-z_][A-Za-z0-9_]*\.[a-z]+", token
        ):
            tokens.append(token)

    if not tokens:
        fail("japanese-naming", f"{rel} names no checkable symbols - has the format changed?")
        return

    # A filename may exist without any file's *text* mentioning it - d_resorce.cpp
    # is included as "d/d_resorce.h" and nowhere names itself - so check the file
    # list as well as the contents. Caught exactly that on the first run.
    tracked = tracked_files()
    basenames = {Path(f).name for f in tracked}

    for token in tokens:
        if token in basenames:
            continue
        found = subprocess.run(
            ["git", "-C", str(REPO), "grep", "--quiet", "--fixed-strings", token, "--", *haystacks],
            capture_output=True,
            check=False,
        )
        if found.returncode != 0:
            fail(
                "japanese-naming",
                f"{rel} cites `{token}`, which no longer appears anywhere in "
                f"{'/, '.join(haystacks)}/. Either the port renamed it - in which case fix the "
                f"glossary row rather than deleting it, the reading is still useful - or the "
                f"entry was written from memory",
            )


# Claims established as WRONG, and the phrasing each was stated in. Mirrors
# RETIRED_CLAIMS in dxvk-remix/scripts/check_dusklight_invariants.py - both repos'
# documents restate these facts, so both need the guard.
#
# The failure it exists for: on 2026-08-10 the kasumi haze bands were corrected from
# "on the sun's side" / "away from the sun" to front/back. The option descriptions and
# the .md files were fixed; the fork's sky SHADER kept the wrong comment and the blend
# built on it for another day. Correcting the sentence that is wrong and finding the code
# built on the wrong belief are two different jobs, and only the second one changes pixels.
#
# The mechanical slice is narrow but real: once a claim is retired, its words must not
# reappear. This cannot tell whether prose is true - only that a sentence we have already
# decided is false has been re-typed by someone who half-remembered it.
RETIRED_CLAIMS: list[tuple[str, str]] = [
    (
        "on the sun's side",
        "the kasumi bands are front/back, not sun-relative - nothing in the game relates "
        "either to sun position, and 'outer' is the NEAR band. Retired 2026-08-10; the "
        "fork's shader was still saying it on 2026-08-11. docs/japanese-naming.md section 6",
    ),
    (
        "away from the sun",
        "the other half of the same retired kasumi claim. Say 'the far (back) band' rather "
        "than describing either band by where the sun is",
    ),
    (
        "lit cloud colour",
        "kumoTop is the UPPER cloud band - a position in a distance gradient, not a lighting "
        "term. The game labels it upper-cloud (d_kankyo.cpp:6302) and lerps top->bottom by "
        "horizontal distance (d_kankyo_rain.cpp:5026-5039). Retired 2026-08-11",
    ),
    (
        "shaded cloud underside",
        "kumoBottom is the LOWER cloud band, the far end of that same gradient, not a shaded "
        "underside. Retired 2026-08-11",
    ),
]

RETIRED_CLAIM_EXEMPT = {
    "scripts/check_invariants.py",  # this file, which must name them to ban them
}


def _in_fenced_block(lines: list[str], index0: int) -> bool:
    """True if this line sits inside a ``` fence.

    Structural, not a keyword guess: a fenced block in markdown is quoted or verbatim
    material by construction - a transcript, a prompt kept as the record of what was
    asked, a paste of the code being discussed. It is never the document asserting
    something in its own voice, which is the only thing this check is trying to stop.
    """
    fences = 0
    for line in lines[:index0]:
        if line.lstrip().startswith("```"):
            fences += 1
    return fences % 2 == 1

def _framing_window(lines: list[str], index0: int, suffix: str) -> list[str]:
    """The text allowed to mark a retired phrase as a quotation rather than a claim.

    Deliberately different for prose and for code, because they are shaped differently
    and a single fixed window is wrong for both.

    Prose frames at paragraph level - a sentence introduces the old wording and the quote
    follows a line or two later - so for markdown the window is the enclosing paragraph,
    bounded by blank lines.

    Code does not. Comments sit hard against what they describe, and option descriptions
    are packed one after another, so a generous window lets one historical note at the top
    of a block excuse every claim below it. That is not hypothetical: a 20-line window was
    tried first, and a "Corrected 2026-08-11:" comment above three option declarations
    silently suppressed the check for all three - the guard reported clean while an option
    a few lines down asserted the retired claim outright. Two lines either way, so the
    framing has to be next to the thing it frames.
    """
    if suffix == ".md":
        start = index0
        while start > 0 and lines[start - 1].strip():
            start -= 1
        end = index0
        while end + 1 < len(lines) and lines[end + 1].strip():
            end += 1
        return lines[start:end + 1]
    return lines[max(0, index0 - 2):index0 + 3]


def check_retired_claims() -> None:
    """A claim established as false must not be restated anywhere in the tree."""
    global checks_run
    checks_run += 1

    # Documents whose subject IS the correction quote the old wording to explain it, which
    # is legitimate - but only where the quote is visibly framed as history rather than
    # asserted as fact. Prose wraps, so look at a few lines either side.
    history_markers = (
        "retired", "corrected", "was wrong", "used to", "previously", "no longer",
        "old wording", "old description", "described", "describes", "the old ", "mistake",
        "misreading", "instead", "rather than", "not fixed", "was not", "wrongly",
        "banned", "must not", "stopped there", "premise", "contradicts", "otherwise",
        "claim", "verbatim", "quotation", "do not re-run",
    )

    for rel in tracked_files():
        if rel in RETIRED_CLAIM_EXEMPT:
            continue
        if Path(rel).suffix not in {".h", ".hpp", ".c", ".cpp", ".inc", ".md"}:
            continue
        text = read(rel)
        if text is None:
            continue
        lowered = text.lower()
        for phrase, correction in RETIRED_CLAIMS:
            if phrase not in lowered:
                continue
            lines = text.splitlines()
            for n, line in enumerate(lines, 1):
                if phrase not in line.lower():
                    continue
                if Path(rel).suffix == ".md" and _in_fenced_block(lines, n - 1):
                    continue
                window = " ".join(_framing_window(lines, n - 1, Path(rel).suffix)).lower()
                if any(marker in window for marker in history_markers):
                    continue
                fail(
                    "retiredclaims",
                    f"{rel}:{n} restates a retired claim, \"{phrase}\" - {correction}. "
                    f"If this is a quotation of the old wording, say so within a line or "
                    f"two so a reader can tell history from assertion",
                )


def main() -> int:
    check_conflict_markers()
    check_settings_consistency()
    check_protocol()
    check_aurora_pin_is_real()
    check_japanese_naming_symbols()
    check_retired_claims()

    for s_ in skipped:
        print(f"SKIPPED {s_}")
    if skipped:
        print()

    if failures:
        print(f"{len(failures)} inconsistency/ies across {checks_run} checks:\n")
        for f in failures:
            print(f"  {f}")
        print(
            "\nThese are facts stated in more than one place that no longer agree. "
            "A clean git merge does not mean they do - see CLAUDE.md, "
            "'Merges that succeed and are still wrong'."
        )
        return 1

    print(f"dusklight invariants: {checks_run - len(skipped)} checks passed, {len(skipped)} skipped")
    return 0


if __name__ == "__main__":
    sys.exit(main())
