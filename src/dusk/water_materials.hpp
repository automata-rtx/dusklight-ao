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

// Whether the name carries an MAxx tag at all, which is a different question from what that
// tag's number is: MA00 is a real tag whose number is 0, so "the tag parsed" and "the tag is
// nonzero" are not the same test. Split out so a caller wanting the first one cannot
// accidentally write the second - see the layer gate in noteDusklightWaterMaterial
// (libs/JSystem/src/J3DGraphBase/J3DMaterial.cpp), which did exactly that.
//
// Same three facts dKy_bg_MAxx_proc and waterRoleForMaterialName read: the convention puts
// the tag at offset 3, so anything shorter than 7 characters cannot carry one.
inline bool materialNameHasTag(const char* name, int nameLength) {
    if (name == nullptr || nameLength < 7) {
        return false;
    }
    if (name[3] != 'M' || name[4] != 'A') {
        return false;
    }
    return name[5] >= '0' && name[5] <= '9' && name[6] >= '0' && name[6] <= '9';
}

// The MAxx tag as a number: 9 for MA09, 0 if the name carries none. Fed to the renderer
// beside the role, because a body of water is drawn as several surfaces and telling them
// apart is the only way to keep one of them - overlapping refracting interfaces are not
// water, and Remix does not blend overlapping normal maps either.
//
// 0 is ambiguous by construction - it is both "no tag" and "MA00" - so anything deciding
// whether a name is tagged must ask materialNameHasTag rather than compare this to 0.
inline u32 waterTagForMaterialName(const char* name, int nameLength) {
    if (!materialNameHasTag(name, nameLength)) {
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
//
// Unlike its two siblings this has no early-out - it is up to 14 whole-name strstr passes - so
// the caller gates it on the MAxx tag having parsed. See noteDusklightWaterMaterial
// (libs/JSystem/src/J3DGraphBase/J3DMaterial.cpp), which runs per material per frame.
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
// Deduplicated by a hash of the name's TEXT, not by its address. It was the address, and an
// address is not a safe key here: names live in the model's archive data, which is freed on a
// room change, so an address retired with one room and later handed to a *different* material
// would be treated as already seen and silently dropped - a hole with no counter, in the one
// log written to catch a name nobody expected. No such drop has been observed; this is the
// mechanism, not a finding, and it is the reason for the key rather than a bug report. The
// hash is also strictly better on the case that is certain: one spelling reached through
// several addresses now burns one of the slots below instead of several.
//
// What that deliberately gives up is the old behaviour of "the same name in a second room
// reports again". A distinct spelling now reports exactly once for the run. Counting rooms was
// never this line's job - enumerating distinct names is, and a silent omission defeats that. If
// per-room repetition is wanted later, clear the set at a room boundary rather than relying on
// the allocator to do it by accident.
//
// Capped, with a notice at the cap so a truncated list is never read as a short one.
//
// The cap was 192 and it bit: the 2026-08-08 22:38 session hit it in Hyrule Field, so Lake
// Hylia - the one place the report was wanted - was entirely past the end of the list, and
// "no water reported there" meant nothing. One Hyrule Field plus one warp is ~200 distinct
// names, so this is sized for a session that visits several areas.
inline constexpr std::size_t kMaxReportedNames = 512;

// FNV-1a, 64-bit. A local implementation rather than a dependency: this is a dedup key for a
// diagnostic, never a texture or material hash that has to agree with anything else.
inline u64 materialNameHash(const char* name) {
    u64 h = 14695981039346656037ull;
    for (const char* p = name; *p != '\0'; ++p) {
        h ^= (u64)(unsigned char)*p;
        h *= 1099511628211ull;
    }
    return h;
}

inline void reportMaterialName(const char* name, u32 role, u32 tag, u32 layer) {
    static std::unordered_set<u64> s_seen;
    static bool s_truncated = false;

    if (name == nullptr) {
        return;
    }

    const u64 key = materialNameHash(name);
    if (s_seen.count(key) != 0) {
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

    s_seen.insert(key);
    DuskLog.info("dusk.matname name={} role={} tag=MA{:02} layer={}", name, waterRoleName(role),
                 tag, waterLayerName(layer));
}

}  // namespace water
}  // namespace dusk
