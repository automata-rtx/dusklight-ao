#!/usr/bin/env python3
"""Cross-check the game/fork option protocol.

The game reads Remix options by *string name* and pushes readouts back the same
way, so a typo on either side is invisible to both compilers: the read silently
falls back to config.json forever, and the push lands on a key nothing displays.
That is the failure this project's rule 2 exists to prevent, and no build catches
it. This does.

Checks, against dxvk-remix's RTX_OPTION declarations:

  1. every "rtx.dusklight.*" name the game reads is declared in the fork
  2. every "rtx.dusklight.env.*" readout the game pushes is declared in the fork
  3. every env readout the fork declares is actually pushed by the game
  4. the protocol number the game pushes equals the fork's kRequiredProtocol,
     and every doc on both sides that states the CURRENT number agrees with it
  5. every "rtx.dusklight.*" option name the DOCS mention is declared in the
     fork. A doc naming a switch that does not exist sends the reader looking
     through the overlay and rtx.conf for it - which is how effectLightOrphanPolicy
     sat in a settings table for a day. Nothing else can catch it: the game
     never reads that name, so checks 1-3 never see it.

Check 4 exists because the number is written down in eight places across three
repos and drifts silently: a doc saying 6 when the wire is at 7 sends the next
session debugging a skew that is not there. Only present-tense statements are
checked - "landed at protocol 5" is history and is left alone.

Usage:  tools/check-remix-protocol.py [path-to-dxvk-remix]
Defaults to ../dxvk-remix. Skips (exit 0) if the fork is not found, because a
dusklight checkout on its own is a legitimate state.
"""

import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

DECL = re.compile(r'RTX_OPTION(?:_ARGS|_FLAG)?\(\s*"([^"]+)"\s*,\s*[^,]+,\s*(\w+)\s*,')

# Present-tense statements of the current protocol number, in prose. Deliberately
# does NOT match "landed 2026-07-29, protocol 6" or "arrived at protocol 2", which
# are history. Bold markers around the digit are optional and common.
DOC_PROTOCOL = re.compile(
    r'(?:protocol is at|protocol is|wire has since advanced to|'
    r'protocol\s*[-—]\s*currently|single protocol\s*[-—]\s*currently)'
    r'\s*\**(\d+)\**',
    re.IGNORECASE)


# Every fork file that declares an rtx.dusklight.* option. Checks 1-3 only ever needed
# the two option surfaces, but check 5 reads the docs, and the docs legitimately name
# atmosphere, grade, emissive and matrep options too - so the authority has to be all of
# them or every such mention reads as a missing switch.
#
# Globbed rather than listed, because a hand-maintained list is wrong exactly when it
# matters. This was a fixed tuple of six until 2026-08-11, and it went stale twice
# without anyone noticing: rtx_dusklight_texrep.h existed only on the fork's
# Fixed-Function-dev, and rtx_dusklight_catrep.h was added after the tuple was written.
# Merging both together made this script report four options as undeclared that the fork
# declares perfectly well - a false alarm, which is the failure mode that teaches people
# to ignore a checker. The rtx_dusklight_* naming convention is what makes the glob
# correct; a fork option surface that does not follow it will be missed, so follow it.
OPTION_SOURCE_GLOBS = (
    "src/dxvk/rtx_render/rtx_dusklight_*.h",
    "src/dxvk/rtx_render/rtx_dusklight_*.cpp",
)
OPTION_SOURCES = (
    "src/d3d9/d3d9_rtx_matrep.h",
)


def declared(path):
    with open(path, encoding="utf-8") as handle:
        return {f"{m.group(1)}.{m.group(2)}" for m in DECL.finditer(handle.read())}


def option_source_paths(fork):
    paths = []
    for pattern in OPTION_SOURCE_GLOBS:
        paths.extend(sorted(glob.glob(os.path.join(fork, pattern))))
    paths.extend(os.path.join(fork, rel) for rel in OPTION_SOURCES)
    return [p for p in paths if os.path.isfile(p)]


def declared_all(fork):
    names = set()
    for path in option_source_paths(fork):
        names |= declared(path)
    return names


def read_text(path):
    try:
        with open(path, encoding="utf-8") as handle:
            return handle.read()
    except OSError:
        return None


def doc_paths(fork):
    return [os.path.join(ROOT, p) for p in (
        "CLAUDE.md", "docs/kankyo-remix.md", "docs/kankyo-fog.md",
        "docs/remix-open-issues.md", "docs/dx9-fixed-function.md",
        "docs/remix-test-playbook.md", "docs/effect-lights.md",
    )] + [os.path.join(fork, p) for p in (
        "CLAUDE.md", "documentation/DusklightOverlay.md",
        "documentation/DusklightAtmosphere.md",
    )]


# Names the docs mention on purpose that the fork does not declare. Each needs a reason,
# and adding one has to be a deliberate act - an allowlist that grows by reflex is how a
# check stops finding anything. A name here is NOT a switch anybody can set.
DOC_NAME_ALLOWED = {
    # Design intent in kankyo-remix.md's push table, phase 4, explicitly not built (the
    # shipped rows carry a tick, these do not).
    "rtx.dusklight.env.hazeColor": "kankyo-remix.md push table, phase 4, unbuilt",
    "rtx.dusklight.env.darkworld": "kankyo-remix.md push table, phase 4, unbuilt",
    "rtx.dusklight.env.sensesStrength": "kankyo-remix.md push table, phase 4, unbuilt",
    # Named in remix-open-issues.md for the express purpose of recording that it was
    # renamed to emissive.brightness and that RtxOptions.md still carries the old row.
    "rtx.dusklight.emissive.intensity": "recorded as renamed to emissive.brightness",
    # The local point-light mirror, REMOVED at protocol 17 (2026-08-16) on both sides once
    # the A/B it was kept for had been decided. Both repos' notes record its retirement by
    # name, which is worth keeping - a reader who finds the key in an old rtx.conf needs to
    # be able to search for what happened to it. It is history, not a switch anybody can set.
    # NOTE the cost of this entry: it also stops the check catching a doc that tells a reader
    # to SET this key. Nothing does today; if you add a settings table row for it, that is a
    # mistake this script will no longer find for you.
    "rtx.dusklight.game.localLights": "removed at protocol 17; named only as history",
}


def check_doc_option_names(fork, known, failures):
    """Check 5. Any rtx.dusklight.<ns>.<name> the docs name must be declared."""
    pattern = re.compile(r'rtx\.dusklight\.(?:game|env|emissive|atmosphere|grade|texrep|warp)'
                         r'\.([A-Za-z_][A-Za-z0-9_]*)')
    for path in doc_paths(fork):
        text = read_text(path)
        if text is None:
            continue
        rel = os.path.relpath(path, ROOT)
        for match in pattern.finditer(text):
            name = match.group(0)
            # A trailing "*" in prose ("the rtx.dusklight.game.effectLight* options")
            # is a family, not a name. The regex stops before it, so check the source text.
            if text[match.end():match.end() + 1] == "*":
                continue
            if name not in known and name not in DOC_NAME_ALLOWED:
                line = text[:match.start()].count("\n") + 1
                failures.append(f"{rel}:{line} names option '{name}', which the fork "
                                f"does not declare (a reader will hunt for a switch "
                                f"that does not exist)")


def check_protocol_number(fork, bridge, failures):
    """Check 4. Returns the wire number, or None if it could not be determined."""
    pushed = re.search(r'push\(\s*"rtx\.dusklight\.env\.protocol"\s*,\s*"(\d+)"', bridge)
    imgui = read_text(os.path.join(fork, "src/dxvk/imgui/dxvk_imgui.cpp")) or ""
    required = re.search(r'kRequiredProtocol\s*=\s*(\d+)', imgui)

    if not pushed:
        failures.append("the game does not push rtx.dusklight.env.protocol at all")
        return None
    if not required:
        failures.append("could not find kRequiredProtocol in the fork's dxvk_imgui.cpp")
        return None

    wire = int(pushed.group(1))
    if wire != int(required.group(1)):
        failures.append(f"the game pushes protocol {wire} but the fork requires "
                        f"{required.group(1)} - one side is unbuilt")
        return wire

    for path in doc_paths(fork):
        text = read_text(path)
        if text is None:
            continue
        for match in DOC_PROTOCOL.finditer(text):
            if int(match.group(1)) != wire:
                line = text[:match.start()].count("\n") + 1
                rel = os.path.relpath(path, ROOT)
                failures.append(f"{rel}:{line} says the protocol is "
                                f"{match.group(1)}, but the wire is at {wire}")
    return wire


def main():
    fork = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "..", "dxvk-remix")
    game_h = os.path.join(fork, "src/dxvk/rtx_render/rtx_dusklight_game.h")
    env_h = os.path.join(fork, "src/dxvk/rtx_render/rtx_dusklight_env.h")

    if not (os.path.isfile(game_h) and os.path.isfile(env_h)):
        print(f"protocol check skipped: no dxvk-remix at {fork}")
        return 0

    known = declared_all(fork)

    with open(os.path.join(ROOT, "src/dusk/remix_bridge.cpp"), encoding="utf-8") as handle:
        bridge = handle.read()

    read = set(re.findall(r'readOption(?:Bool|Float|Int)\(\s*"([^"]+)"', bridge))
    push = set(re.findall(r'push\(\s*"(rtx\.dusklight\.env\.[^"]+)"', bridge))

    failures = []
    for name in sorted(read - known):
        failures.append(f"game reads '{name}', which the fork does not declare "
                        f"(it will silently use the config.json fallback forever)")
    for name in sorted(push - known):
        failures.append(f"game pushes '{name}', which the fork does not declare "
                        f"(nothing will ever display it)")
    for name in sorted(n for n in known if n.startswith("rtx.dusklight.env.") and n not in push):
        failures.append(f"fork declares readout '{name}', which the game never pushes "
                        f"(it will read as its default)")

    wire = check_protocol_number(fork, bridge, failures)
    check_doc_option_names(fork, known, failures)

    print(f"protocol check: {len(read)} options read, {len(push)} readouts pushed, "
          f"{len(known)} declared in the fork"
          + (f", wire at protocol {wire}" if wire is not None else ""))

    if failures:
        print()
        for line in failures:
            print(f"  MISMATCH: {line}")
        print("\nprotocol check FAILED")
        return 1

    print("protocol check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
