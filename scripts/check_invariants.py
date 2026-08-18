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
  * docs/japanese-naming-remix.md glosses the game's romanized-Japanese symbols, and
    says up front that every symbol it names was checked to exist. A glossary
    is exactly the kind of document nobody re-reads, so that promise is only
    worth anything if something enforces it.
  * The effect-light classifier matches words against the game's own effect
    names, which are romanized Japanese. A keyword spelled the way the reader
    expects rather than the way the game spelled it matches nothing, silently,
    forever - which is what Class::Lava did. Ten such words exist today, and the
    fact that they are dead is invisible unless something states it.

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


# --- the effect-light classifier's word lists ------------------------------------------
#
# src/dusk/effect_lights.cpp classifies each particle effect by looking for words in the
# effect's own name, and the names are romanized Japanese (docs/japanese-naming-remix.md). A
# keyword that matches none of the game's 3205 effect names is inert: it costs nothing and
# it is invisible, so nobody finds out until they build on it. Class::Lava was exactly that
# for however long - lava/magma/youdo match nothing, the game spells it yogan/yougan, and
# every lava column was classified Other until 2026-08-07.
#
# This check replays the lists over src/d/d_particle_name.cpp and pins the result in BOTH
# directions, which is the shape CLAUDE.md's side-channel checks settled on:
#
#   * a keyword matching zero names that is not recorded below is a NEW dead word - either
#     a typo, or a word from a different game;
#   * a word recorded below that starts matching means the negative is stale and the
#     comments in effect_lights.cpp and docs/effect-lights.md that rest on it are wrong.
#
# It also holds classifyByName and classKeyword together. classKeyword is a second copy of
# the same lists in the same order, used to print which word claimed a name in the
# classification report; effect_lights.cpp says it "must match classifyByName exactly - if
# it drifts, the report lies about the class it is printing beside", and nothing enforced
# that.
#
# THIS CHECK CHANGES NO BEHAVIOUR AND ASKS FOR NONE. The ten dead words below are
# deliberately still in the source: at stock settings effectLightGlowOffset is 0.0, the
# same as Other's offset, so most of what these words would decide cannot move a pixel,
# and removing a word is a change to a shipping classifier for no gain.
#
# READ THIS BEFORE "FIXING" ANY OF THEM: they are NOT romanization misses
# (docs/japanese-naming-remix.md section 3). Every one was re-checked in both kunrei-shiki and
# Hepburn, and in the obvious alternatives, and every spelling matches zero. The game
# simply used English, or a different Japanese word. Adding spellings would not help.
EFFECT_LIGHT_DEAD_KEYWORDS: dict[str, str] = {
    "lava": "English. The game spells 溶岩 yogan (18) and yougan (3), disjoint sets - "
            "which is why both are in the list",
    "magma": "English, and the game never uses it. Same substitution as 'lava'",
    "youdo": "no spelling the game uses; 'yodo' is zero too. Same substitution as 'lava'",
    "bakuha": "爆発 bakuhatsu, explosion. bakuhatsu/bakuhatu are zero as well; the game "
              "names these bomb (171), explo (7) and baku (2, 爆炎 bakuen)",
    "honoo": "炎, flame. honou and homura are zero too; the game uses fire (181), "
             "火炎 kaen (4) and flame (2)",
    "hono": "the same word clipped, and zero for the same reason - no name contains "
            "those four letters anywhere",
    "taimatsu": "松明, torch. taimatu is zero as well; the game uses torch (1), "
                "カンテラ kantera (5) and 薪 maki (8)",
    "kagarib": "篝火 kagaribi, brazier. kagari and kagaribi are both zero; the game uses "
               "the same three words as 'taimatsu'",
    "pika": "ぴか, sparkle. pikari and pikapika are zero too; the game uses kira (9), "
            "spark (33) and glow (59)",
    "shine": "English. shain and shiny are zero as well; kira, spark and glow cover it",
}


def _strip_line_comments(text: str) -> str:
    """Remove // comments, leaving string literals alone.

    Both files this check parses need it, for different reasons.
    d_particle_name.cpp:1732 is `"\\x81\\x60\\x00",  // "~"` - a shift-JIS glyph quoted in
    a comment - so a naive scan for quoted strings counts 3206 names instead of 3205, and
    the extra one is not an effect. effect_lights.cpp's comments name Class:: values and
    quote keywords while explaining them.
    """
    out: list[str] = []
    for line in text.splitlines(keepends=True):
        i = 0
        in_str = False
        while i < len(line):
            c = line[i]
            if in_str:
                if c == "\\":
                    i += 2
                    continue
                if c == '"':
                    in_str = False
            elif c == '"':
                in_str = True
            elif c == "/" and line[i + 1 : i + 2] == "/":
                break
            i += 1
        # Line numbers have to survive: a comment is replaced by nothing, never by
        # nothing-plus-a-newline, or every offset reported from here on is wrong.
        out.append(line if i >= len(line) else line[:i] + ("\n" if line.endswith("\n") else ""))
    return "".join(out)


def _effect_names(src: str) -> list[str]:
    """Every name in dPa_name::jpaName - the whole space classifyByName can see.

    effectName() masks the id with kIdMask and getName bounds-checks against
    ID_PARTICLE_MAX, so this table is exactly the set of names, no more.
    """
    m = re.search(r"jpaName\[\]\s*=\s*\{(.*?)\n\};", src, re.S)
    if not m:
        return []
    body = _strip_line_comments(m.group(1))
    return [n.lower() for n in re.findall(r'"((?:[^"\\]|\\.)*)"', body)]


def _parse_keyword_lists(body: str) -> list[tuple[str, list[str]]]:
    """[(class, words)] from classifyByName's if-chain, in precedence order."""
    lists: list[tuple[str, list[str]]] = []
    pending: list[str] = []
    for m in re.finditer(
        r'nameHas\(\s*name\s*,\s*"([^"]+)"\s*\)|return\s+Class::(\w+)\s*;', body
    ):
        if m.group(1) is not None:
            pending.append(m.group(1))
        elif pending:
            lists.append((m.group(2), pending))
            pending = []
    return lists


def _parse_report_lists(src: str) -> list[tuple[str, list[str]]]:
    """[(class, words)] from classKeyword's static arrays, in kLists order."""
    arrays = {
        m.group(1): [w for w in re.findall(r'"([^"]+)"', m.group(2))]
        for m in re.finditer(
            r"static const char\* const k(\w+)\[\]\s*=\s*\{(.*?)\};", src, re.S
        )
    }
    order = re.search(r"const\* const kLists\[\]\s*=\s*\{(.*?)\};", src, re.S)
    if not order:
        return []
    return [
        (n, arrays[n]) for n in re.findall(r"\bk(\w+)\b", order.group(1)) if n in arrays
    ]


def check_effect_light_keywords() -> None:
    """No classifier keyword may silently match nothing."""
    global checks_run
    checks_run += 1

    cpp = read("src/dusk/effect_lights.cpp")
    table = read("src/d/d_particle_name.cpp")
    if cpp is None or table is None:
        fail("effect-light-keywords", "src/dusk/effect_lights.cpp or src/d/d_particle_name.cpp missing")
        return

    names = _effect_names(table)
    if len(names) < 3000:
        # The table is game data and does not change; a short read means the parse broke,
        # and reporting 30 dead keywords would be a confident lie.
        fail(
            "effect-light-keywords",
            f"parsed only {len(names)} names out of dPa_name::jpaName - expected ~3205. "
            f"Fix this parse before trusting anything below it",
        )
        return

    stripped = _strip_line_comments(cpp)
    fn = re.search(r"\nClass classifyByName\([^)]*\)\s*\{(.*?)\n\}", stripped, re.S)
    if not fn:
        fail("effect-light-keywords", "could not find classifyByName in src/dusk/effect_lights.cpp")
        return

    lists = _parse_keyword_lists(fn.group(1))
    if not lists:
        fail("effect-light-keywords", "parsed no keyword lists out of classifyByName")
        return

    live: set[str] = set()
    for cls, words in lists:
        for word in words:
            live.add(word)
            hits = sum(1 for n in names if word in n)
            if hits == 0 and word not in EFFECT_LIGHT_DEAD_KEYWORDS:
                fail(
                    "effect-light-keywords",
                    f"classifyByName's Class::{cls} list contains \"{word}\", which matches "
                    f"none of the {len(names)} effect names in d_particle_name.cpp. Before "
                    f"changing the spelling, try the other romanization "
                    f"(docs/japanese-naming-remix.md section 3) and the English word - the ten "
                    f"already-dead keywords are dead in every spelling. If it is meant to "
                    f"stay inert, record it in EFFECT_LIGHT_DEAD_KEYWORDS with the reason",
                )
            elif hits > 0 and word in EFFECT_LIGHT_DEAD_KEYWORDS:
                fail(
                    "effect-light-keywords",
                    f"\"{word}\" is recorded as matching zero effect names, and now matches "
                    f"{hits}. The negative is stale: fix EFFECT_LIGHT_DEAD_KEYWORDS, the "
                    f"comment above the Class::{cls} branch in effect_lights.cpp, and "
                    f"docs/effect-lights.md section 3.1, which all rest on it",
                )

    for word in EFFECT_LIGHT_DEAD_KEYWORDS:
        if word not in live:
            fail(
                "effect-light-keywords",
                f"EFFECT_LIGHT_DEAD_KEYWORDS records \"{word}\", which classifyByName no "
                f"longer uses. Drop the row - a stale exemption is how a real dead keyword "
                f"gets waved through later",
            )

    report = _parse_report_lists(stripped)
    if report and report != lists:
        first = next(
            (f"{a} {a_w} vs {b} {b_w}" for (a, a_w), (b, b_w) in zip(lists, report) if a_w != b_w or a != b),
            f"{len(lists)} lists in classifyByName, {len(report)} in classKeyword",
        )
        fail(
            "effect-light-keywords",
            f"classKeyword's lists no longer match classifyByName's, in content or in order, "
            f"so the classification report names the wrong word beside a class. First "
            f"divergence: {first}",
        )


# Backticked tokens in japanese-naming-remix.md that are prose, not game symbols.
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
    """Every game symbol docs/japanese-naming-remix.md names must exist in the tree.

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

    rel = "docs/japanese-naming-remix.md"
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


def main() -> int:
    check_conflict_markers()
    check_settings_consistency()
    check_protocol()
    check_aurora_pin_is_real()
    check_effect_light_keywords()
    check_japanese_naming_symbols()

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
            "'A clean git merge is not a correct merge'."
        )
        return 1

    print(f"dusklight invariants: {checks_run - len(skipped)} checks passed, {len(skipped)} skipped")
    return 0


if __name__ == "__main__":
    sys.exit(main())
