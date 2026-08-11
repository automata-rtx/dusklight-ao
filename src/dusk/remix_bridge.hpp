#pragma once

#include <cstdint>
#include <string>
#include <vector>

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

// State of the game's local point lights (torches, braziers, lanterns, Midna,
// dungeon lights - everything registered through dKy_plight_set) mirrored into
// Remix as sphere lights. Aurora does not forward GX lights to D3D9, so without
// this Remix sees no game light at all indoors or at night.
struct LocalLightsDebug {
    bool enabled;
    int found;           // lights the game had registered, before any filtering of ours
    int tracked;         // lights with a live Remix handle
    int drawn;           // drawn into the scene this frame
    uint64_t creates;    // cumulative CreateLight calls
    uint64_t destroys;   // cumulative DestroyLight calls
};

const LocalLightsDebug& localLightsDebug();

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
