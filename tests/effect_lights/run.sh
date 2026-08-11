#!/bin/sh
# Behavioural harness for src/dusk/effect_lights.cpp.
#
# The module is compiled against the stub headers beside this script rather than against the
# game's own, because the real build is a Windows cross-build with a precompiled header and does
# not come up in a Linux container. The stubs reproduce the signatures the module uses, all of
# which were read out of the game's headers - so this checks the module's behaviour and its
# syntax, and it CANNOT catch a stub that has drifted from the real declaration. Full statement
# of what is and is not verified: docs/effect-lights.md section 10.
#
# Usage: tests/effect_lights/run.sh        (from the repo root)
set -e
here=$(dirname "$0")
root=$here/../..
out=${TMPDIR:-/tmp}/dusk_effect_lights_test

g++ -std=c++20 -w -g -fsanitize=address,undefined \
    -I"$here/stub_logging" -I"$here/stub_headers" -I"$root/src" \
    "$here/test_effect_lights.cpp" "$root/src/dusk/effect_lights.cpp" -lfmt \
    -o "$out"

"$out"
