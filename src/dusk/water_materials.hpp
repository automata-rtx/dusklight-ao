#pragma once

// Which J3D materials are water.
//
// Twilight Princess does not have a water material; it has a naming convention. Room and
// object models name their environment-driven materials "***MAxx*", and dKy_bg_MAxx_proc
// (d_kankyo.cpp) dispatches on those names every frame to drive fog, shine and the
// projected reflection layer. That naming is the game's own answer to "is this water", so
// it is what gets translated to the renderer rather than a list of texture hashes - this
// game reuses water textures on non-water draws, and a hash list would be wrong there.
//
// Naming it also survives water moving. Dungeon water levels rise and fall, and neither the
// material's name nor its identity changes when they do, so the water stays water across
// the whole animation.
//
// Consumed at draw time by J3DMaterial::load, which is the point where the material's GX
// state is programmed and therefore the only place that brackets exactly its own draws.

#include <cstring>
#include <unordered_set>

#include "dusk/logging.h"

namespace dusk {
namespace water {

// The water surfaces, by the four characters dKy_bg_MAxx_proc keys on.
//
//   MA02, MA10  the projected reflection layer (dComIfGd_setListInvisisble + an effect
//               matrix built from the camera, d_kankyo.cpp)
//   MA03, MA09  the water surface, including the shine rate the environment drives
//   MA06        the murky body (dKy_murky_set)
//   MA17, MA19  further surface variants handled beside MA03/MA09
//
// Deliberately excluded: MA00/MA01/MA04/MA16, which are the water-*in* fog overlay rather
// than a water surface - they are what the camera looks through while submerged, and making
// them refractive would put a second water surface in front of the eye.
inline bool isWaterMaterialName(const char* name, int nameLength) {
    // The convention places the tag at offset 3, which is why dKy_bg_MAxx_proc reads
    // name[3] and name[4] before comparing. Anything shorter cannot carry one.
    if (name == nullptr || nameLength < 7) {
        return false;
    }

    if (name[3] != 'M' || name[4] != 'A') {
        return false;
    }

    static const char* const kWaterTags[] = {
        "MA02", "MA03", "MA06", "MA09", "MA10", "MA17", "MA19",
    };

    for (const char* tag : kWaterTags) {
        if (std::memcmp(&name[3], tag, 4) == 0) {
            return true;
        }
    }

    return false;
}

// One line per distinct material name, with the verdict.
//
// This exists because the first attempt at marking water produced no water at all, and the
// two explanations - the hook never running, and the names not being what was expected -
// are indistinguishable from the outside. Zero lines means the former; lines without a
// water=1 among them means the latter, and says what the names actually are.
//
// Deduplicated by name pointer. Names live in the model's archive data, so the same
// material re-drawn every frame reports once, while the same name in a second room reports
// again - which is wanted, since that is how a room whose water is named differently shows
// up. Capped, with a notice at the cap so a truncated list is never read as a short one.
inline void reportMaterialName(const char* name, bool isWater) {
    static std::unordered_set<const void*> s_seen;
    static bool s_truncated = false;

    if (name == nullptr || s_seen.count(name) != 0) {
        return;
    }
    if (s_seen.size() >= 192) {
        if (!s_truncated) {
            s_truncated = true;
            DuskLog.info("dusk.matname.trunc cap=192 - further distinct material names not reported");
        }
        return;
    }

    s_seen.insert(name);
    DuskLog.info("dusk.matname name={} water={}", name, isWater ? 1 : 0);
}

}  // namespace water
}  // namespace dusk
