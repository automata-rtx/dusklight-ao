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


def main() -> int:
    check_conflict_markers()
    check_settings_consistency()
    check_protocol()
    check_aurora_pin_is_real()

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
