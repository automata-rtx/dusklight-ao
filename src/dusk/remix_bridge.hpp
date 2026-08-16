#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "dusk/effect_lights.hpp"

// Pushes the game's environment (kankyo) state into RTX Remix through the Remix API, so the
// path tracer's Dusklight features track time of day, weather and area palettes. Design and
// rationale: docs/kankyo-remix.md.
//
// The bridge is inert unless all of these hold: the D3D9 backend is active, d3d9.dll is
// actually RTX Remix (detected by its remixapi export), the API version matches, and
// game.remixKankyoBridge is enabled.

namespace dusk {
namespace remix {

enum class BridgeStatus {
    Uninitialized,   // tick() not called yet
    NotUnderRemix,   // d3d9.dll has no remixapi export (stock D3D9), or not the D3D9 backend
    VersionMismatch, // Remix DLL speaks a different remixapi version than our vendored header
    InitFailed,      // remixapi_InitializeLibrary returned an error
    Disabled,        // available, but game.remixKankyoBridge is off
    Active,
};

// True when the bridge initialized against Remix and is enabled; the game's own EFB bloom
// draw is skipped in this state because Remix renders the bloom instead.
bool isActive();

BridgeStatus status();
const char* statusString();

// Call once per frame from the main loop, after the frame's game logic and kankyo update
// (setLight) have run and before the frame is presented. Must be called from the thread
// that drives D3D9 - the Remix API's config path is not synchronized against other threads.
void tick();

// Introspection for the debug UI.
struct PushedVar {
    const char* key;
    std::string value;
    uint32_t pushes;
};

const std::vector<PushedVar>& debugVars();
uint64_t totalPushes();

// State of the sun/moon distant light driven through the Remix light API (see
// docs/kankyo-remix.md, Phase 4). The direction follows the vanilla game's
// astronomical sun/moon angles (setSunpos), NOT the game's shadow-casting
// light selection - that one snaps to nearby local lights, which a distant
// light must never do.
struct CelestialLightDebug {
    bool deviceRegistered;
    bool active;      // drawn this frame
    bool isDay;
    float fade;       // 0..1 crossfade near the day/night boundary
    float direction[3];
    float radiance[3];
    // Where the body sits in the world, in degrees, for eyeballing whether the
    // sun is actually holding still. Azimuth is a compass bearing about the
    // world's up axis (0 = +Z, 90 = +X); elevation is height above the horizon.
    // Taken before the debug flip, so these are the game's own astronomy.
    float azimuth;
    float elevation;
};

const CelestialLightDebug& celestialDebug();

// State of the effect light system: sphere lights placed at the origin of the game's own fire
// and glow effects rather than at the positions of the game's registered lights. This replaced
// the local point-light mirror, which was removed at protocol 17 (2026-08-16) once the A/B it
// was kept for had been decided; see docs/effect-lights.md.
struct EffectLightsDebug {
    bool enabled;
    int tracked;         // sites with a live Remix handle
    int drawn;           // drawn into the scene this frame
    uint64_t creates;    // cumulative CreateLight calls
    uint64_t destroys;   // cumulative DestroyLight calls
    effect_lights::Stats stats;  // the decision side's own counters
};

const EffectLightsDebug& effectLightsDebug();

// State of the room's authored lights - dScnKy_env_light_c::dungeonlight, fed every frame from
// the current room's LightVec stage data. A different registry from either the effect emitters
// or the pointlight[]/efplight[] list the effect lights read, and the only one in the game that
// carries a cone. Off by default; see docs/effect-lights.md section 8.1.
struct RoomLightsDebug {
    bool enabled;
    int found;           // slots the game itself considers live this room, before our filtering
    int tracked;         // slots holding a live Remix handle
    int drawn;           // drawn into the scene this frame
    int shaped;          // of those, how many carried a cone
    int unshapeable;     // GX_SP_RING1/RING2, which Remix's shaping cannot express at all
    uint64_t creates;    // cumulative CreateLight calls
    uint64_t destroys;   // cumulative DestroyLight calls
};

const RoomLightsDebug& roomLightsDebug();

// Session-only debug toggle: negates the pushed light direction, for quickly
// diagnosing a handedness mismatch between game and Remix world space.
bool& celestialFlipDirection();

// Session-only debug toggle: pins the light direction at whatever it was when
// the lock was switched on. The direction the bridge computes depends on
// nothing but time of day, so if shadows still swing about while this is on,
// whatever is moving them is downstream of the bridge - the space Remix reads
// the direction in, not the direction itself.
bool& celestialLockDirection();

// Diagnostic only - drives no rendering. Called by the horse on every frame it is
// dashing, and read-and-cleared once per push, so a frame that does not call it reads as
// not dashing. That way the flag cannot stick on when the player dismounts, which a plain
// setter would do because the horse stops being updated at all rather than reporting
// false. Surfaces in Remix's log as dusklight.mark, beside the material report, so a
// material that first appears mid dash can be attributed to the dash without correlating
// two logs by wall clock.
void noteHorseDashing();

}  // namespace remix
}  // namespace dusk
