#include "dusk/remix_bridge.hpp"

#include <cstdio>

#include <aurora/aurora.h>

#include "dusk/logging.h"
#include "dusk/settings.h"
#include "m_Do/m_Do_graphic.h"

#if defined(_WIN32)
#define DUSK_REMIX_BRIDGE_SUPPORTED 1
#include <windows.h>
#include <remix/remix_c.h>
#else
#define DUSK_REMIX_BRIDGE_SUPPORTED 0
#endif

namespace dusk {
namespace remix {

namespace {

BridgeStatus s_status = BridgeStatus::Uninitialized;
uint64_t s_totalPushes = 0;

std::vector<PushedVar> s_vars;

#if DUSK_REMIX_BRIDGE_SUPPORTED
aurora::Module BridgeLog("remix-bridge");

bool s_initAttempted = false;
remixapi_Interface s_interface = {};

void initialize() {
    s_initAttempted = true;
    s_status = BridgeStatus::NotUnderRemix;

    // Aurora imports d3d9.dll statically, so if we are running under Remix its DLL is already
    // in the process. Stock Microsoft d3d9.dll has no remixapi export, which makes this probe
    // double as the "are we under Remix at all?" check.
    HMODULE d3d9 = GetModuleHandleW(L"d3d9.dll");
    if (d3d9 == NULL) {
        BridgeLog.info("d3d9.dll not loaded; kankyo bridge disabled");
        return;
    }

    auto initializeLibrary = reinterpret_cast<PFN_remixapi_InitializeLibrary>(
        reinterpret_cast<void*>(GetProcAddress(d3d9, "remixapi_InitializeLibrary")));
    if (initializeLibrary == nullptr) {
        BridgeLog.info("d3d9.dll is not RTX Remix; kankyo bridge disabled");
        return;
    }

    remixapi_InitializeLibraryInfo info = {};
    info.sType = REMIXAPI_STRUCT_TYPE_INITIALIZE_LIBRARY_INFO;
    info.version = REMIXAPI_VERSION_MAKE(REMIXAPI_VERSION_MAJOR, REMIXAPI_VERSION_MINOR,
                                         REMIXAPI_VERSION_PATCH);

    remixapi_ErrorCode err = initializeLibrary(&info, &s_interface);
    if (err != REMIXAPI_ERROR_CODE_SUCCESS) {
        // While the API's major version is 0 every minor bump is breaking, so a mismatch is
        // the expected failure mode when the Remix DLL and our vendored header drift apart.
        s_status = err == REMIXAPI_ERROR_CODE_INCOMPATIBLE_VERSION ? BridgeStatus::VersionMismatch
                                                                   : BridgeStatus::InitFailed;
        BridgeLog.warn("remixapi_InitializeLibrary failed ({}); kankyo bridge disabled",
                       static_cast<int>(err));
        return;
    }

    if (s_interface.SetConfigVariable == nullptr) {
        s_status = BridgeStatus::InitFailed;
        BridgeLog.warn("remixapi interface has no SetConfigVariable; kankyo bridge disabled");
        return;
    }

    s_status = BridgeStatus::Active;
    BridgeLog.info("RTX Remix detected; kankyo bridge active (remixapi {}.{}.{})",
                   REMIXAPI_VERSION_MAJOR, REMIXAPI_VERSION_MINOR, REMIXAPI_VERSION_PATCH);
}

// Diff-cached push: Remix's SetConfigVariable takes a global lock and re-parses the value
// string on every call, so only values that actually changed go through.
void push(const char* key, const std::string& value) {
    for (PushedVar& var : s_vars) {
        if (var.key == key) {
            if (var.value == value) {
                return;
            }

            remixapi_ErrorCode err = s_interface.SetConfigVariable(key, value.c_str());
            if (err != REMIXAPI_ERROR_CODE_SUCCESS) {
                // Unknown key: the Remix DLL predates this option. Warn once, then track the
                // value anyway so the warning doesn't repeat every change.
                if (var.pushes == 0) {
                    BridgeLog.warn("SetConfigVariable({}) failed ({})", key,
                                   static_cast<int>(err));
                }
            }

            var.value = value;
            var.pushes++;
            s_totalPushes++;
            return;
        }
    }

    remixapi_ErrorCode err = s_interface.SetConfigVariable(key, value.c_str());
    if (err != REMIXAPI_ERROR_CODE_SUCCESS) {
        BridgeLog.warn("SetConfigVariable({}) failed ({})", key, static_cast<int>(err));
        s_vars.push_back(PushedVar {key, value, 0});
        return;
    }

    s_vars.push_back(PushedVar {key, value, 1});
    s_totalPushes++;
}

std::string formatFloat(float value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.5g", value);
    return buffer;
}

std::string formatColor(u8 r, u8 g, u8 b) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.5g, %.5g, %.5g", r / 255.0f, g / 255.0f,
                  b / 255.0f);
    return buffer;
}

const char* formatBool(bool value) {
    return value ? "True" : "False";
}

void pushKankyoState() {
    mDoGph_gInf_c::bloom_c* bloom = mDoGph_gInf_c::getBloom();

    // Bloom parameters. setLight() refreshes these every frame from the environment palettes
    // (including the twilight and senses tables), whatever the in-game bloom mode is set to,
    // so they are always current here. dusk::ApplyBloomOverride has already run too, so the
    // in-game bloom debug window overrides flow through to Remix as well.
    const GXColor blend = *bloom->getBlendColor();
    const GXColor mono = *bloom->getMonoColor();

    push("rtx.dusklight.env.enable", "True");
    push("rtx.dusklight.env.bloomEnable", formatBool(bloom->getEnable() != 0));
    push("rtx.dusklight.env.bloomThreshold", formatFloat(bloom->getPoint() / 255.0f));
    push("rtx.dusklight.env.bloomBlurSize", formatFloat(bloom->getBlureSize()));
    push("rtx.dusklight.env.bloomBlurRatio", formatFloat(bloom->getBlureRatio()));
    push("rtx.dusklight.env.bloomTint", formatColor(blend.r, blend.g, blend.b));
    push("rtx.dusklight.env.bloomBaseWeight", formatFloat(blend.a / 255.0f));
    push("rtx.dusklight.env.bloomScreenBlend", formatBool(bloom->mMode == 1));
    push("rtx.dusklight.env.monoColor", formatColor(mono.r, mono.g, mono.b));
    push("rtx.dusklight.env.monoAmount", formatFloat(mono.a / 255.0f));
}
#endif  // DUSK_REMIX_BRIDGE_SUPPORTED

}  // namespace

bool isActive() {
    return s_status == BridgeStatus::Active;
}

BridgeStatus status() {
    return s_status;
}

const char* statusString() {
    switch (s_status) {
    case BridgeStatus::Uninitialized:
        return "uninitialized";
    case BridgeStatus::NotUnderRemix:
        return "not under RTX Remix";
    case BridgeStatus::VersionMismatch:
        return "remixapi version mismatch";
    case BridgeStatus::InitFailed:
        return "remixapi init failed";
    case BridgeStatus::Disabled:
        return "disabled (game.remixKankyoBridge)";
    case BridgeStatus::Active:
        return "active";
    }
    return "unknown";
}

void tick() {
#if DUSK_REMIX_BRIDGE_SUPPORTED
    if (aurora_get_backend() != BACKEND_D3D9) {
        return;
    }

    const bool wantEnabled = getSettings().game.remixKankyoBridge.getValue();

    if (!s_initAttempted) {
        if (!wantEnabled) {
            // Do not probe until the user actually wants the bridge; state stays
            // Uninitialized so enabling it later still initializes.
            return;
        }
        initialize();
    }

    if (s_status == BridgeStatus::Active && !wantEnabled) {
        s_status = BridgeStatus::Disabled;
        return;
    }
    if (s_status == BridgeStatus::Disabled && wantEnabled) {
        s_status = BridgeStatus::Active;
        // Drop the diff cache so every value is re-pushed after re-enabling: the user may
        // have hand-edited options in the Remix UI in between.
        s_vars.clear();
    }

    if (s_status != BridgeStatus::Active) {
        return;
    }

    pushKankyoState();
#endif
}

const std::vector<PushedVar>& debugVars() {
    return s_vars;
}

uint64_t totalPushes() {
    return s_totalPushes;
}

}  // namespace remix
}  // namespace dusk
