#!/bin/sh
#
# Cross-compile syntax check for the Remix-facing game code, using MinGW so the
# _WIN32 half is actually compiled.
#
# WHY THIS EXISTS. The Remix bridge is wrapped in
#   #if defined(_WIN32) ... #define DUSK_REMIX_BRIDGE_SUPPORTED 1
# so a native Linux g++ compiles *none* of it - every light system, every option
# read, every API call is preprocessed away. A harness that runs on Linux and
# reports success therefore says nothing at all about the code that matters, and
# on 2026-08-06 that is exactly what happened: four compile errors in
# remix_bridge.cpp reached CI because the local check had never seen the file.
# MinGW defines _WIN32, so this compiles the real path.
#
# WHAT IT DOES NOT DO. It is `-fsyntax-only` against GCC, not a build against
# MSVC. It will not catch:
#   - MSVC-specific rules GCC is lenient about (though GCC does catch the
#     capture-less-lambda-odr-uses-a-local-constexpr case that bit us);
#   - anything that only appears at link time;
#   - the CheckRtInstanceSize / hashStructByMemory guards, which live in the fork;
#   - the fork half entirely - dxvk-remix has no cross-compilable harness and CI
#     is still its only syntax check.
# CI remains the authority. This exists to stop the cheap failures.
#
# Third-party headers that are fetched at build time (SDL3, Tracy) are stubbed in
# tests/effect_lights/stub_thirdparty. Stubs cover only what these files use, so
# a new dependency may need a line adding there.
#
# Requires: g++-mingw-w64-x86-64  (apt-get install g++-mingw-w64-x86-64)
# Usage:    tools/syntax-check-remix.sh          (from the repo root)

set -e

root=$(cd "$(dirname "$0")/.." && pwd)
aurora=$root/extern/aurora
stub=$root/tests/effect_lights/stub_thirdparty

# The aurora submodule may be unpopulated in a bare checkout; fall back to a
# sibling clone, which is how the three repos usually sit next to each other.
if [ ! -f "$aurora/include/aurora/aurora.h" ]; then
    if [ -f "$root/../aurora-ao/include/aurora/aurora.h" ]; then
        aurora=$root/../aurora-ao
    else
        echo "aurora headers not found - init the submodule or clone aurora-ao alongside" >&2
        exit 1
    fi
fi

CXX=${CXX_MINGW:-x86_64-w64-mingw32-g++}
if ! command -v "$CXX" >/dev/null 2>&1; then
    echo "$CXX not found - apt-get install g++-mingw-w64-x86-64" >&2
    exit 1
fi

# aurora's logging header is included as <aurora/lib/logging.hpp> but lives at
# <aurora>/lib/logging.hpp, and it needs fmt. Build a shim include root rather
# than guessing at how the real build wires it up. fmt matters: it is what makes
# the format strings in the effect-light report compile-time checked, and a
# mismatched brace count there is exactly the sort of thing CI would otherwise
# find for us.
shim=${TMPDIR:-/tmp}/dusk_syntax_shim
rm -rf "$shim"
mkdir -p "$shim/aurora" "$shim/fmt"
ln -s "$aurora/lib" "$shim/aurora/lib"
for h in "$aurora"/include/aurora/*; do ln -s "$h" "$shim/aurora/$(basename "$h")"; done

fmtdir=""
for d in /usr/include/fmt /usr/local/include/fmt; do
    [ -f "$d/format.h" ] && fmtdir=$d && break
done
if [ -z "$fmtdir" ]; then
    echo "fmt headers not found - apt-get install libfmt-dev" >&2
    exit 1
fi
for h in "$fmtdir"/*; do ln -s "$h" "$shim/fmt/$(basename "$h")"; done
# fmt/base.h is fmt 10+; on older fmt it is called core.h.
[ -f "$shim/fmt/base.h" ] || echo '#pragma once
#include <fmt/core.h>' > "$shim/fmt/base.h"

# -fpermissive and the neutered __declspec are for the game's own dllimport
# annotations, which MSVC accepts on definitions and GCC does not. They relax
# nothing about the code under test.
check() {
    printf '  %-44s' "$1"
    if out=$("$CXX" -fsyntax-only -std=c++20 -w -fpermissive \
            -DTARGET_PC=1 -DWIDESCREEN_SUPPORT=1 '-D__declspec(x)=' \
            -include global.h -include helpers/endian.h -include dolphin/os/OSRtc.h \
            -I"$stub" -I"$shim" -I"$root/include" -I"$root/src" -I"$root/libs/JSystem/include" \
            -I"$aurora/include" -I"$aurora/include/dolphin" \
            -I"$root/libs/dolphin/include" -I"$root/libs/dolphin/include/dolphin" \
            -I"$root/libs/revolution/include" \
            "$root/$1" 2>&1); then
        echo "ok"
    else
        echo "FAILED"
        echo "$out" | grep -E "error" | head -20
        failed=1
    fi
}

failed=0
echo "MinGW syntax check ($CXX):"
check src/dusk/effect_lights.cpp
check src/dusk/remix_bridge.cpp
check src/d/d_particle.cpp
# Added 2026-08-11 with the vrkumo draw counter. Everything this file does that matters to
# Remix sits inside #if TARGET_PC - the same reason a native Linux g++ is worse than useless
# on remix_bridge.cpp, since it preprocesses the interesting half away and then reports
# success. Cross-compiling is the only way the counter gets checked before CI.
check src/d/d_kankyo_rain.cpp

if [ "$failed" = 1 ]; then
    echo
    echo "syntax check FAILED"
    exit 1
fi

echo
echo "syntax check passed"

# The other half of the protocol is string-matched at runtime, where no compiler
# can see it. Skips itself if the fork is not checked out alongside.
echo
"$root/tools/check-remix-protocol.py" || exit 1
