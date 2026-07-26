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

}  // namespace remix
}  // namespace dusk
