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

Check 4 exists because the number is written down in eight places across three
repos and drifts silently: a doc saying 6 when the wire is at 7 sends the next
session debugging a skew that is not there. Only present-tense statements are
checked - "landed at protocol 5" is history and is left alone.

Usage:  tools/check-remix-protocol.py [path-to-dxvk-remix]
Defaults to ../dxvk-remix. Skips (exit 0) if the fork is not found, because a
dusklight checkout on its own is a legitimate state.
"""

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


def declared(path):
    with open(path, encoding="utf-8") as handle:
        return {f"{m.group(1)}.{m.group(2)}" for m in DECL.finditer(handle.read())}


def read_text(path):
    try:
        with open(path, encoding="utf-8") as handle:
            return handle.read()
    except OSError:
        return None


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

    docs = [os.path.join(ROOT, p) for p in (
        "CLAUDE.md", "docs/kankyo-remix.md", "docs/kankyo-fog.md",
        "docs/remix-open-issues.md", "docs/dx9-fixed-function.md",
        "docs/remix-test-playbook.md", "docs/effect-lights.md",
    )] + [os.path.join(fork, p) for p in (
        "CLAUDE.md", "documentation/DusklightOverlay.md",
        "documentation/DusklightAtmosphere.md",
    )]

    for path in docs:
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

    known = declared(game_h) | declared(env_h)

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
