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

#include <cstddef>
#include <cstring>
#include <unordered_set>

#include <dolphin/gx/GXAurora.h>

#include "dusk/logging.h"

namespace dusk {
namespace water {

// The water materials, by the four characters dKy_bg_MAxx_proc keys on, and which of the
// two roles each one plays. Read from that function (d_kankyo.cpp:11418-11479) rather than
// guessed - it is the game's own dispatch over these same names.
//
//   MA03, MA09, MA17, MA19   the water surface. dKy_bg_MAxx_proc drives their fog type and,
//                            for MA09, the shine rate (mWaterSurfaceShineRate).
//   MA06                     the murky body (dKy_murky_set)
//   MA02, MA10               NOT the surface. dKy_bg_MAxx_proc calls dComIfGd_setListInvisisble
//                            and then installs a C_MTXLightPerspective built from the live
//                            camera fovy and aspect as the material's texture matrix - a
//                            screen-projected fake reflection drawn over the water.
//
// Deliberately excluded entirely: MA00/MA01/MA04/MA16, the water-*in* fog overlay rather
// than a water surface - they are what the camera looks through while submerged, and making
// them refractive would put a second water surface in front of the eye.
//
// Why the roles are separate rather than one "is water" answer: a body of water arrives as
// two or three coincident draws, and only one of them is the surface. Making all of them
// refracting interfaces stacks sheets of glass where there should be one surface, which is
// what the 2026-08-08 23:47 session looked like - 11 water materials reached Remix, 4 of
// them the projected layer, and the water did not read as one continuous surface.
inline u32 waterRoleForMaterialName(const char* name, int nameLength) {
    // The convention places the tag at offset 3, which is why dKy_bg_MAxx_proc reads
    // name[3] and name[4] before comparing. Anything shorter cannot carry one.
    if (name == nullptr || nameLength < 7) {
        return GX_AURORA_DUSKLIGHT_WATER_NONE;
    }

    if (name[3] != 'M' || name[4] != 'A') {
        return GX_AURORA_DUSKLIGHT_WATER_NONE;
    }

    // The camera-projected overlay. Kept as its own role rather than dropped here, so the
    // decision of what to do with it lives in the renderer and stays switchable.
    if (std::memcmp(&name[3], "MA02", 4) == 0 || std::memcmp(&name[3], "MA10", 4) == 0) {
        return GX_AURORA_DUSKLIGHT_WATER_PROJECTED;
    }

    static const char* const kSurfaceTags[] = {
        "MA06", "MA09", "MA17", "MA19",
    };

    for (const char* tag : kSurfaceTags) {
        if (std::memcmp(&name[3], tag, 4) == 0) {
            return GX_AURORA_DUSKLIGHT_WATER_SURFACE;
        }
    }

    // MA03 is the one tag that is not water on its own. dKy_bg_MAxx_proc gives it the same
    // fog and shine treatment as MA09, so the game does not distinguish - but the names do,
    // and the 2026-08-08 20:35 session showed why it matters: cc_MA03_Sunbeam_v is a light
    // shaft, and turning a light shaft into refracting water is worse than leaving it alone.
    // The water ones in that session named themselves: cd_MA03_Funsui_v (fountain),
    // ce_MA03_FunsuiKasan_v_x and ce_MA03_WaterKasan_v_x.
    //
    // An MA03 rejected here still reports through dusk.matname with role=none, so a water
    // surface named some third way shows up as a name to add rather than as absent water.
    if (std::memcmp(&name[3], "MA03", 4) == 0) {
        const bool isWater =
            std::strstr(name, "Water") != nullptr || std::strstr(name, "Funsui") != nullptr;
        return isWater ? GX_AURORA_DUSKLIGHT_WATER_SURFACE : GX_AURORA_DUSKLIGHT_WATER_NONE;
    }

    return GX_AURORA_DUSKLIGHT_WATER_NONE;
}

// The MAxx tag as a number: 9 for MA09, 0 if the name carries none. Fed to the renderer
// beside the role, because a body of water is drawn as several surfaces and telling them
// apart is the only way to keep one of them - overlapping refracting interfaces are not
// water, and Remix does not blend overlapping normal maps either.
inline u32 waterTagForMaterialName(const char* name, int nameLength) {
    if (name == nullptr || nameLength < 7) {
        return 0;
    }
    if (name[3] != 'M' || name[4] != 'A') {
        return 0;
    }
    if (name[5] < '0' || name[5] > '9' || name[6] < '0' || name[6] > '9') {
        return 0;
    }
    return (u32)((name[5] - '0') * 10 + (name[6] - '0'));
}

// Which layer of a body of water this material is, read from the game's own vocabulary.
//
// Twilight Princess is a Japanese production and this decompilation preserves its naming,
// so these words are the developers' own labels for the passes rather than anything
// invented here. A lake is drawn as several of them stacked:
//
//   mera      shimmer / heat-haze      cc_MA09_mera_v, cd_MA09_MeraWater_v
//   nami      waves                    cc_MA06_nami_v_x
//   mizugiwa  the water's edge         cc_MA06_mizugiwa_v_x
//   nigori    the murky body           cc_MA06_NigoriWater_v_x
//   funsui    a fountain               cd_MA03_Funsui_v
//   kasan     an ADDITIVE pass         ce_MA03_WaterKasan_v_x, ce_MA03_FunsuiKasan_v_x
//   indirect  the warp used to fake refraction   cc_MA02_IndirectWater_v
//
// The MAxx tag cannot do this job: three of those - nami, mizugiwa and nigori - are all
// MA06, so hiding that tag would delete a lake's waves and shoreline to be rid of its murk.
// That was tried and recommended before the names were read properly.
//
// "kasan" is worth knowing on its own: it is the Japanese for *addition*, and every
// material carrying it measured SRC_ALPHA,ONE in the 2026-08-09 blend report. The name said
// what the blend state said, a session earlier.
//
// UNKNOWN is the safe answer and the default: a name nobody has classified stays visible.
inline u32 waterLayerForMaterialName(const char* name) {
    if (name == nullptr) {
        return GX_AURORA_DUSKLIGHT_WATER_LAYER_UNKNOWN;
    }

    // Both spellings, because the convention capitalises a word when it follows another
    // ("MeraWater", "FunsuiKasan") and lowercases it when it follows the tag ("_mera").
    // Matching "_mera" rather than bare "mera" keeps a longer word that merely contains it
    // from being caught - "minami" would otherwise read as "nami".
    struct LayerWord { const char* lower; const char* upper; u32 layer; };
    static const LayerWord kWords[] = {
        // Pass words first: they describe how a surface is drawn, which is what decides
        // whether it should be kept, and they win over the object word in a compound like
        // "FunsuiKasan" (a fountain's additive pass).
        {"_indirect", "Indirect", GX_AURORA_DUSKLIGHT_WATER_LAYER_INDIRECT},
        {"_kasan",    "Kasan",    GX_AURORA_DUSKLIGHT_WATER_LAYER_ADDITIVE},
        {"_nigori",   "Nigori",   GX_AURORA_DUSKLIGHT_WATER_LAYER_MURK},
        {"_mizugiwa", "Mizugiwa", GX_AURORA_DUSKLIGHT_WATER_LAYER_SHORELINE},
        {"_nami",     "Nami",     GX_AURORA_DUSKLIGHT_WATER_LAYER_WAVES},
        {"_mera",     "Mera",     GX_AURORA_DUSKLIGHT_WATER_LAYER_SHIMMER},
        {"_funsui",   "Funsui",   GX_AURORA_DUSKLIGHT_WATER_LAYER_FOUNTAIN},
    };

    for (const LayerWord& w : kWords) {
        if (std::strstr(name, w.lower) != nullptr || std::strstr(name, w.upper) != nullptr) {
            return w.layer;
        }
    }

    return GX_AURORA_DUSKLIGHT_WATER_LAYER_UNKNOWN;
}

inline const char* waterLayerName(u32 layer) {
    switch (layer) {
    case GX_AURORA_DUSKLIGHT_WATER_LAYER_SHIMMER:   return "shimmer";
    case GX_AURORA_DUSKLIGHT_WATER_LAYER_WAVES:     return "waves";
    case GX_AURORA_DUSKLIGHT_WATER_LAYER_SHORELINE: return "shoreline";
    case GX_AURORA_DUSKLIGHT_WATER_LAYER_MURK:      return "murk";
    case GX_AURORA_DUSKLIGHT_WATER_LAYER_FOUNTAIN:  return "fountain";
    case GX_AURORA_DUSKLIGHT_WATER_LAYER_ADDITIVE:  return "additive";
    case GX_AURORA_DUSKLIGHT_WATER_LAYER_INDIRECT:  return "indirect";
    default:                                        return "unknown";
    }
}

inline const char* waterRoleName(u32 role) {
    switch (role) {
    case GX_AURORA_DUSKLIGHT_WATER_SURFACE:   return "surface";
    case GX_AURORA_DUSKLIGHT_WATER_PROJECTED: return "projected";
    default:                                  return "none";
    }
}

// One line per distinct material name, with the verdict.
//
// This exists because the first attempt at marking water produced no water at all, and the
// two explanations - the hook never running, and the names not being what was expected -
// are indistinguishable from the outside. Zero lines means the former; lines with no
// role=surface among them means the latter, and says what the names actually are.
//
// Deduplicated by name pointer. Names live in the model's archive data, so the same
// material re-drawn every frame reports once, while the same name in a second room reports
// again - which is wanted, since that is how a room whose water is named differently shows
// up. Capped, with a notice at the cap so a truncated list is never read as a short one.
//
// The cap was 192 and it bit: the 2026-08-08 22:38 session hit it in Hyrule Field, so Lake
// Hylia - the one place the report was wanted - was entirely past the end of the list, and
// "no water reported there" meant nothing. One Hyrule Field plus one warp is ~200 distinct
// names, so this is sized for a session that visits several areas.
inline constexpr std::size_t kMaxReportedNames = 512;

inline void reportMaterialName(const char* name, u32 role, u32 tag, u32 layer) {
    static std::unordered_set<const void*> s_seen;
    static bool s_truncated = false;

    if (name == nullptr || s_seen.count(name) != 0) {
        return;
    }
    if (s_seen.size() >= kMaxReportedNames) {
        if (!s_truncated) {
            s_truncated = true;
            DuskLog.info("dusk.matname.trunc cap={} - further distinct material names not reported",
                         kMaxReportedNames);
        }
        return;
    }

    s_seen.insert(name);
    DuskLog.info("dusk.matname name={} role={} tag=MA{:02} layer={}", name, waterRoleName(role),
                 tag, waterLayerName(layer));
}

}  // namespace water
}  // namespace dusk
