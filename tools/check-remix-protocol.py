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


def declared(path):
    with open(path, encoding="utf-8") as handle:
        return {f"{m.group(1)}.{m.group(2)}" for m in DECL.finditer(handle.read())}


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

    print(f"protocol check: {len(read)} options read, {len(push)} readouts pushed, "
          f"{len(known)} declared in the fork")

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
