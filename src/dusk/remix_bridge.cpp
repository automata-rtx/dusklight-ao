#include "dusk/remix_bridge.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include <aurora/aurora.h>

#include "dusk/logging.h"
#include "dusk/main.h"
#include "dusk/settings.h"
#include "d/d_kankyo.h"
#include "m_Do/m_Do_graphic.h"
#include "dolphin/pad.h"
#include "d/d_com_inf_game.h"
#include "f_op/f_op_camera_mng.h"
#include "dusk/map_loader_definitions.h"
#include "dusk/action_bindings.h"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>

#include <iterator>
#include <string>

#include <cmath>

#if defined(_WIN32)
#define DUSK_REMIX_BRIDGE_SUPPORTED 1
#ifndef NOMINMAX
#define NOMINMAX
#endif
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

CelestialLightDebug s_celestial = {};
bool s_celestialFlip = false;
bool s_celestialLock = false;

LocalLightsDebug s_localDebug = {};
EffectLightsDebug s_effectDebug = {};

#if DUSK_REMIX_BRIDGE_SUPPORTED
aurora::Module BridgeLog("remix-bridge");

bool s_initAttempted = false;
remixapi_Interface s_interface = {};

// The Remix API can only write config variables. Our settings are edited from
// Remix's own Dusklight tab - the game's debug UI is not drawn at all in D3D9
// mode - so we also have to read them back, which rides a plain export rather
// than remixapi_Interface (whose size is asserted, so extending it would break
// its ABI). Absent on a Remix build older than the tab; the game's own config
// values are the fallback then.
typedef uint32_t(*PFN_getRtxOptionValue)(const char* key, char* outValue, uint32_t valueSize);
PFN_getRtxOptionValue s_getOption = nullptr;

// Reads a Remix option, or returns false if this Remix build has no getter or
// does not know the key.
bool readOption(const char* key, std::string& outValue) {
    if (s_getOption == nullptr) {
        return false;
    }

    char buffer[64];
    const uint32_t size = s_getOption(key, buffer, sizeof(buffer));
    if (size == 0 || size > sizeof(buffer)) {
        return false;
    }

    outValue.assign(buffer);
    return true;
}

bool readOptionBool(const char* key, bool fallback) {
    std::string value;
    if (!readOption(key, value)) {
        return fallback;
    }

    return value == "True" || value == "true" || value == "1";
}

float readOptionFloat(const char* key, float fallback) {
    std::string value;
    if (!readOption(key, value)) {
        return fallback;
    }

    // strtof rather than stof: no exception dependency, and an unparseable value
    // should fall back rather than propagate out of the frame loop.
    char* end = nullptr;
    const float parsed = std::strtof(value.c_str(), &end);
    if (end == value.c_str()) {
        return fallback;
    }

    return parsed;
}

int readOptionInt(const char* key, int fallback) {
    std::string value;
    if (!readOption(key, value)) {
        return fallback;
    }

    char* end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str()) {
        return fallback;
    }

    return static_cast<int>(parsed);
}

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

    s_getOption = reinterpret_cast<PFN_getRtxOptionValue>(
        reinterpret_cast<void*>(GetProcAddress(d3d9, "getRtxOptionValue")));
    if (s_getOption == nullptr) {
        BridgeLog.warn("this Remix build has no getRtxOptionValue export; the Dusklight tab "
                       "cannot drive the game and config.json values are used instead");
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

// The ambient colours are GXColorS10: signed 16 bit fields that the hardware treated as
// signed 10 bit. The environment blend keeps them inside 0..255, but events add colours in
// before clamping, so clamp here rather than trust it.
std::string formatColorS10(const GXColorS10& color) {
    const auto norm = [](s16 value) {
        return std::clamp(static_cast<float>(value), 0.0f, 255.0f) / 255.0f;
    };

    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.5g, %.5g, %.5g", norm(color.r), norm(color.g),
                  norm(color.b));
    return buffer;
}

// Quantized push, for values that drift continuously. The fog distances move every frame while a
// palette blend is in flight, and pushing full precision would re-cross the Remix API lock every
// one of those frames for a change nothing can see.
std::string formatFloatQ(f32 value, f32 quantum) {
    if (quantum > 0.0f) {
        value = std::round(value / quantum) * quantum;
    }
    return formatFloat(value);
}

const char* formatBool(bool value) {
    return value ? "True" : "False";
}

// Whether this area has a sky at all.
//
// The game answers this itself in g_env_light.hide_vrbox, but that flag is written by the sky
// dome actor, so in any stage that has no such actor - which is every interior, the ones the
// question matters most for - it is never updated and holds whatever the last outdoor area left
// there. Recomputing the test it performs is both correct everywhere and one frame fresher.
bool skyIsHidden(const dScnKy_env_light_c* env) {
    const auto sum = [](const GXColorS10& c) {
        return static_cast<int>(c.r) + static_cast<int>(c.g) + static_cast<int>(c.b);
    };

    return (sum(env->vrbox_kasumi_outer_col) + sum(env->vrbox_sky_col) +
            sum(env->vrbox_kumo_top_col)) == 0;
}

// The game leaves its fog distances completely unclamped and its debug menu can drive them into
// the millions, so nothing about the feed is trusted. A ramp needs a positive span to mean
// anything; note that a negative start is normal rather than broken - scripted fog banks set it
// that way deliberately so the ramp is already underway at the camera.
bool fogIsActive(const dScnKy_env_light_c* env) {
    const f32 start = env->mFogNear;
    const f32 end = env->mFogFar;

    return std::isfinite(start) && std::isfinite(end) && end > start && end > 0.0f;
}

// Everything below hands world positions and radiances straight to Remix, which
// puts them in its acceleration structures. Remix validates radius and radiance
// for sign and range but does not check any of it for NaN, and a NaN that gets
// that far takes the renderer down on one of its own worker threads, where the
// crash says nothing about where it came from.
//
// The values come from live actor state read once a frame, so a torn read during
// a scene teardown is exactly the sort of thing that produces one. Cheap to rule
// out here; miserable to diagnose later.
bool isFinite3(const float v[3]) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

// --- Sun/moon distant light -------------------------------------------------
//
// Drives one Remix distant light from the vanilla game's astronomical sun/moon
// angles. Important subtlety: the game's actual shadow-casting "sun" light is a
// LOCAL light hovering near Link, and its selection snaps to lanterns and other
// point lights when Link approaches them (SetBaseLight / lightStatus). A
// distant light must not inherit either property, so the direction is taken
// from setSunpos's orbit (sun_pos/moon_pos), which is pure time-of-day
// geometry: offsets from the camera eye on a fixed 80000-unit arc.
//
// Day/night follows SetBaseLight's window (sun while 67.5 < daytime < 292.5,
// moon otherwise), with a short crossfade at each boundary instead of the
// vanilla hard swap so the light never pops.

constexpr uint64_t kCelestialLightHash = 0xD05C114D00000001ull;

void* s_registeredDevice = nullptr;
// Set when the D3D9 device is replaced (aurora recreates it on resize). Every
// light handle belongs to the old device's scene, so they must be re-created
// rather than destroyed - the object that owned them is already gone.
// One per consumer. These are cleared by whoever reads them, and there is more than one
// reader: a single flag meant whichever light system ran first in tick() swallowed the
// notification and the other kept handles bound to a device that no longer exists.
bool s_lightsNeedRecreate = false;        // the local light mirror
bool s_effectLightsNeedRecreate = false;  // the effect lights
bool s_celestialLightExists = false;
remixapi_LightHandle s_celestialHandle = nullptr;
float s_lastDir[3] = {0.0f, 0.0f, 0.0f};
float s_lastRadiance[3] = {0.0f, 0.0f, 0.0f};
float s_lastAngle = 0.0f;
float s_celestialLockedToBody[3] = {0.0f, 0.0f, 0.0f};
bool s_celestialLockValid = false;

// The Remix API needs the D3D9 device registered before any scene calls.
// Aurora recreates the device on window resize, so re-register on change.
bool ensureDeviceRegistered() {
    void* device = aurora_dx9_get_device();
    if (device == nullptr) {
        s_celestial.deviceRegistered = false;
        return false;
    }

    if (device != s_registeredDevice) {
        // Under Remix the game-facing IDirect3DDevice9 is implemented by the
        // same object as its 9Ex interface, which is what RegisterD3D9Device
        // dynamic_casts back out of this pointer.
        remixapi_ErrorCode err = s_interface.dxvk_RegisterD3D9Device(
            reinterpret_cast<IDirect3DDevice9Ex*>(device));
        if (err != REMIXAPI_ERROR_CODE_SUCCESS) {
            BridgeLog.warn("dxvk_RegisterD3D9Device failed ({})", static_cast<int>(err));
            s_celestial.deviceRegistered = false;
            return false;
        }

        s_registeredDevice = device;
        // External lights live in the device's scene state; recreate them
        // against the new device.
        s_celestialLightExists = false;
        s_lightsNeedRecreate = true;
        s_effectLightsNeedRecreate = true;
        BridgeLog.info("registered D3D9 device with the Remix API");
    }

    s_celestial.deviceRegistered = true;
    return true;
}

// 0..1 ramp over `width` daytime units inside the window [begin, end].
float windowFade(float daytime, float begin, float end, float width) {
    if (daytime <= begin || daytime >= end) {
        return 0.0f;
    }

    const float edge = std::min(daytime - begin, end - daytime);
    return std::min(edge / width, 1.0f);
}

// setSunpos's piecewise remap of time of day (0..360) onto the orbit angle.
// The two branches make the body cross the sky faster around noon/midnight
// than it does near the horizon.
float celestialOrbitAngle(float time) {
    if (time >= 90.0f && time <= 270.0f) {
        // get_parcent(270, 90, time)
        return ((time - 90.0f) / 180.0f) * 150.0f + 105.0f;
    }

    float angle = time;
    if (angle < 90.0f) {
        angle += 360.0f;
    }

    // get_parcent(450, 270, angle)
    angle = ((angle - 270.0f) / 180.0f) * 210.0f + 255.0f;
    if (angle > 360.0f) {
        angle -= 360.0f;
    }
    return angle;
}

// Unit vector from the scene toward the sun/moon, straight from time of day.
//
// setSunpos places the body at eye + (sin(a)*R, -cos(a)*R, -cos(a)*R*ratio). The eye term and R
// both cancel under normalization, leaving only the tilt ratio. Deriving the vector here rather
// than differencing sun_pos against the camera keeps this a pure direction - no position, no
// arc, no camera, nothing for a distant light to misinterpret - and it stays correct in the
// stages where setSunpos declines to update sun_pos at all.
void celestialDirectionTo(float time, float outDir[3]) {
    const float radians = celestialOrbitAngle(time) * (3.14159265358979323846f / 180.0f);
    const float sinA = std::sin(radians);
    const float cosA = std::cos(radians);

    // The same tilt setSunpos places the visible body on, so the light and the thing you can see
    // in the sky cannot disagree. docs/sun-elevation.md.
    const float orbitZRatio = dKy_celestial_orbit_z_ratio();

    float dir[3] = {sinA, -cosA, -cosA * orbitZRatio};
    const float invLength =
        1.0f / std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);

    outDir[0] = dir[0] * invLength;
    outDir[1] = dir[1] * invLength;
    outDir[2] = dir[2] * invLength;
}

void updateCelestialLight() {
    s_celestial.active = false;

    if (s_interface.CreateLight == nullptr || s_interface.DrawLightInstance == nullptr ||
        s_interface.dxvk_RegisterD3D9Device == nullptr) {
        return;
    }

    if (!readOptionBool("rtx.dusklight.game.sunMoonLight",
                        getSettings().game.remixSunMoonLight.getValue()) ||
        !dusk::IsGameLaunched) {
        return;
    }

    // Outdoor stages with a sky only; false in twilight, interiors and the
    // handful of special stages. Deliberately NOT the shadow-light selection.
    if (dKy_SunMoon_Light_Check() != TRUE) {
        return;
    }

    if (!ensureDeviceRegistered()) {
        return;
    }

    const float daytime = dKy_getEnvlight()->getDaytime();

    // SetBaseLight's sun window, with a crossfade over 7.5 daytime units
    // (about 30 in-game minutes) at each boundary.
    const bool isDay = daytime > 67.5f && daytime < 292.5f;
    float fade;
    float orbitTime;

    if (isDay) {
        fade = windowFade(daytime, 67.5f, 292.5f, 7.5f);
        orbitTime = daytime;
    } else {
        // Night window wraps midnight: 292.5 -> 360/0 -> 67.5.
        const float sinceDusk = daytime >= 292.5f ? daytime - 292.5f : daytime + 67.5f;
        fade = windowFade(sinceDusk, 0.0f, 135.0f, 7.5f);
        // The moon rides the same orbit half a day out of phase.
        orbitTime = daytime >= 180.0f ? daytime - 180.0f : daytime + 180.0f;
    }

    if (fade <= 0.0f) {
        return;
    }

    float toBody[3];
    celestialDirectionTo(orbitTime, toBody);

    // Compass bearing and height, purely for the debug readout: this is the
    // quickest way to see whether the sun is holding still while the player
    // moves, which is the question that comes up every time the lighting looks
    // like it is following someone around.
    constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;
    s_celestial.elevation = std::asin(std::clamp(toBody[1], -1.0f, 1.0f)) * kRadToDeg;
    s_celestial.azimuth = std::atan2(toBody[0], toBody[2]) * kRadToDeg;

    // Pinned for diagnosis: the direction below depends on nothing but time of
    // day, so if the lighting still swings around while this is on, whatever is
    // moving it is downstream of us.
    const bool lockDirection =
        readOptionBool("rtx.dusklight.game.celestialLock", s_celestialLock);

    if (lockDirection && s_celestialLockValid) {
        toBody[0] = s_celestialLockedToBody[0];
        toBody[1] = s_celestialLockedToBody[1];
        toBody[2] = s_celestialLockedToBody[2];
    } else {
        s_celestialLockedToBody[0] = toBody[0];
        s_celestialLockedToBody[1] = toBody[1];
        s_celestialLockedToBody[2] = toBody[2];
        s_celestialLockValid = true;
    }

    // A distant light is defined purely by the direction its light travels,
    // which is the reverse of the direction to the body.
    float dir[3] = {-toBody[0], -toBody[1], -toBody[2]};
    if (readOptionBool("rtx.dusklight.game.celestialFlip", s_celestialFlip)) {
        dir[0] = -dir[0];
        dir[1] = -dir[1];
        dir[2] = -dir[2];
    }

    // Vanilla's sun diffuse for actors is the constant warm (126,110,89);
    // normalized against red that is the day tint. The moon tint is our own
    // cool counterpart (the vanilla night look comes from ambients).
    const float sunColor[3] = {1.0f, 0.873f, 0.706f};
    const float moonColor[3] = {0.55f, 0.65f, 0.95f};

    const float intensity =
        fade * (isDay ? readOptionFloat("rtx.dusklight.game.sunIntensity",
                                        getSettings().game.remixSunIntensity.getValue())
                      : readOptionFloat("rtx.dusklight.game.moonIntensity",
                                        getSettings().game.remixMoonIntensity.getValue()));
    const float* color = isDay ? sunColor : moonColor;
    const float radiance[3] = {color[0] * intensity, color[1] * intensity, color[2] * intensity};
    const float angle = readOptionFloat("rtx.dusklight.game.celestialAngle",
                                        getSettings().game.remixCelestialAngle.getValue());

    // Re-creating with the same hash is the API's update mechanism; only do it
    // when something moved beyond quantization noise.
    const float kDirEps = 0.001f;   // ~0.06 degrees
    const float kRadEps = 0.005f;
    const bool changed = !s_celestialLightExists ||
                         std::fabs(dir[0] - s_lastDir[0]) > kDirEps ||
                         std::fabs(dir[1] - s_lastDir[1]) > kDirEps ||
                         std::fabs(dir[2] - s_lastDir[2]) > kDirEps ||
                         std::fabs(radiance[0] - s_lastRadiance[0]) > kRadEps ||
                         std::fabs(radiance[1] - s_lastRadiance[1]) > kRadEps ||
                         std::fabs(radiance[2] - s_lastRadiance[2]) > kRadEps ||
                         std::fabs(angle - s_lastAngle) > 0.01f;

    if (!isFinite3(dir) || !isFinite3(radiance) || !std::isfinite(angle)) {
        BridgeLog.warn("sun/moon light had non-finite values this frame; skipping it");
        return;
    }

    if (changed) {
        remixapi_LightInfoDistantEXT distant = {};
        distant.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISTANT_EXT;
        distant.direction = {dir[0], dir[1], dir[2]};
        distant.angularDiameterDegrees = angle;
        distant.volumetricRadianceScale = 1.0f;

        remixapi_LightInfo info = {};
        info.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO;
        info.pNext = &distant;
        info.hash = kCelestialLightHash;
        info.radiance = {radiance[0], radiance[1], radiance[2]};

        remixapi_LightHandle handle = nullptr;
        remixapi_ErrorCode err = s_interface.CreateLight(&info, &handle);
        if (err != REMIXAPI_ERROR_CODE_SUCCESS) {
            if (s_celestialLightExists) {
                BridgeLog.warn("CreateLight(sun/moon) failed ({})", static_cast<int>(err));
            }
            s_celestialLightExists = false;
            return;
        }

        s_celestialHandle = handle;
        s_celestialLightExists = true;
        s_lastDir[0] = dir[0];
        s_lastDir[1] = dir[1];
        s_lastDir[2] = dir[2];
        s_lastRadiance[0] = radiance[0];
        s_lastRadiance[1] = radiance[1];
        s_lastRadiance[2] = radiance[2];
        s_lastAngle = angle;
    }

    // The active-light list is cleared by Remix every frame; an undrawn light
    // simply doesn't exist that frame.
    remixapi_ErrorCode err = s_interface.DrawLightInstance(s_celestialHandle);
    if (err != REMIXAPI_ERROR_CODE_SUCCESS) {
        return;
    }

    s_celestial.active = true;
    s_celestial.isDay = isDay;
    s_celestial.fade = fade;
    s_celestial.direction[0] = dir[0];
    s_celestial.direction[1] = dir[1];
    s_celestial.direction[2] = dir[2];
    s_celestial.radiance[0] = radiance[0];
    s_celestial.radiance[1] = radiance[1];
    s_celestial.radiance[2] = radiance[2];
}

// --- Local point lights ------------------------------------------------------
//
// Mirrors the game's live point lights (g_env_light.pointlight and efplight, everything
// registered through dKy_plight_set: torches, braziers, lanterns, campfires, Midna, bomb
// flashes, dungeon lights) into Remix sphere lights. Tested in game 2026-07-29.
//
// This is not a nicety. Aurora's D3D9 backend deliberately does not forward GX lights to D3D9 -
// the GC light model does not survive the translation and Remix relights everything anyway - so
// Remix sees no game light of its own. Outdoors the sun/moon distant light covers that; indoors
// and at night nothing does, and the scene is left on Remix's fallback light. These are the
// lights that were meant to carry those scenes.
//
// Intensity is derived with Remix's own legacy-light conversion rather than a tuning constant,
// so these land in the same range as lights in any other Remix title - see localLightRadiance().

constexpr uint64_t kLocalLightHashBase = 0xD05C114E00000000ull;

struct TrackedLocalLight {
    uint64_t hash;
    remixapi_LightHandle handle;
    const LIGHT_INFLUENCE* source;
    float position[3];
    float radiance[3];
    float radius;
    bool seen;
};

std::vector<TrackedLocalLight> s_localLights;

// Stable per-light identity. The LIGHT_INFLUENCE lives inside its actor, so the
// address holds still for as long as the light does. If an actor is freed and
// another lands on the same address the hash is reused, which is harmless: the
// tracking below simply sees the parameters change and re-creates it.
uint64_t localLightHash(const LIGHT_INFLUENCE* influence) {
    uint64_t value = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(influence));

    // Standard 64-bit finalizer; the low bits of an allocation address are the
    // least distinctive, so mix before truncating.
    value ^= value >> 33;
    value *= 0xff51afd7ed558ccdull;
    value ^= value >> 33;
    value *= 0xc4ceb9fe1a85ec53ull;
    value ^= value >> 33;

    return kLocalLightHashBase | (value & 0xffffffffull);
}

// Radiance for a sphere light standing in for one of the game's point lights.
//
// Deliberately the same maths Remix applies to a legacy D3D9 light
// (LightUtils::calculateIntensity): work out how far the original light was
// meant to reach, then solve for the radiance a sphere light of a fixed radius
// needs to still be perceptible at that distance.
//
//   radiance = reach^2 * kNewLightEndValue / (pi * radius^2)
//
// The reach used below is LIGHT_INFLUENCE::mPow, the game's own influence radius -
// dKy_light_influence_id treats "closer than mPow" as "inside this light". Deriving from that
// rather than a tuning constant is what puts these in the same intensity range as the lights of
// every other Remix title.
//
// mPow is not where the light actually ends, though, and the difference is ~19x. The game loads
// into GX with dKy_GXInitLightDistAttn(info, mPow * 0.001f, 0.99999f, GX_DA_STEEP), i.e.
//   attenuation(D) = 1 / (1 + 10 * D^2 / mPow^2)
// so mPow is where the light falls to 1/11 of peak. Applying Remix's own end threshold (1/255)
// to that curve gives reach = mPow * sqrt((maxColorByte - 1) / 10), which is 4.3x mPow for a
// torch (colour AF5D00) and so ~19x the radiance.
//
// game.remixLocalLightIntensity carries that factor: it defaults to 19, tested in game
// 2026-07-29 alongside a radius of 10, and the two were settled together - the radiance is
// solved to reach the same distance, so a larger radius needs less of it. Change one and
// re-test both. docs/remix-open-issues.md.
bool localLightRadiance(const LIGHT_INFLUENCE& influence, float radius, float scale,
                        float outRadiance[3]) {
    const float channel[3] = {static_cast<float>(influence.mColor.r),
                              static_cast<float>(influence.mColor.g),
                              static_cast<float>(influence.mColor.b)};
    const float brightest = std::max(channel[0], std::max(channel[1], channel[2]));

    if (brightest <= 0.0f || influence.mPow <= 0.01f || radius <= 0.0f) {
        return false;
    }

    // Remix's threshold for "still perceptible" (kNewLightEndValue in
    // rtx_lights.h). Kept as a literal because it is part of Remix's
    // conversion, not a knob of ours.
    constexpr float kNewLightEndValue = 0.01f;
    constexpr float kPi = 3.14159265358979323846f;

    const float reach = influence.mPow;
    const float intensity =
        (reach * reach) * kNewLightEndValue / (kPi * radius * radius) * scale;

    for (int i = 0; i < 3; i++) {
        outRadiance[i] = (channel[i] / brightest) * intensity;
    }

    return true;
}

// --- Effect lights -----------------------------------------------------------
//
// The replacement for the mirror above, and the reason it now defaults off. Instead of copying
// the game's registered lights - which reproduces every faked placement the original shading
// model got away with, because a GameCube point light casts no shadow and could sit anywhere -
// this puts a sphere light at the origin of the effect that actually draws the fire, and takes
// only the game's colour and reach from whatever light was authored nearby.
//
// The decision is dusk::effect_lights; this half only owns the Remix API calls. Design,
// citations and the exclusion policy: docs/effect-lights.md.

constexpr uint64_t kEffectLightHashBase = 0xE55C114E00000000ull;

struct TrackedEffectLight {
    uint32_t siteId;
    remixapi_LightHandle handle;
    float position[3];
    float radiance[3];
    float radius;
    bool seen;
};

std::vector<TrackedEffectLight> s_effectLights;

uint64_t effectLightHash(uint32_t siteId) {
    return kEffectLightHashBase | static_cast<uint64_t>(siteId);
}

void destroyEffectLight(TrackedEffectLight& light) {
    if (light.handle != nullptr && s_interface.DestroyLight != nullptr) {
        s_interface.DestroyLight(light.handle);
        s_effectDebug.destroys++;
    }

    light.handle = nullptr;
}

void releaseEffectLights() {
    for (TrackedEffectLight& light : s_effectLights) {
        destroyEffectLight(light);
    }

    s_effectLights.clear();
    s_effectDebug.tracked = 0;
    s_effectDebug.drawn = 0;
}

void destroyLocalLight(TrackedLocalLight& light) {
    if (light.handle != nullptr && s_interface.DestroyLight != nullptr) {
        s_interface.DestroyLight(light.handle);
        s_localDebug.destroys++;
    }

    light.handle = nullptr;
}

void releaseLocalLights() {
    for (TrackedLocalLight& light : s_localLights) {
        destroyLocalLight(light);
    }

    s_localLights.clear();
    s_localDebug.tracked = 0;
    s_localDebug.drawn = 0;
}

// ---------------------------------------------------------------------------
// Controls
// ---------------------------------------------------------------------------
//
// The overlay shows and drives the game's action binds, and the game owns every part of the
// decision: it captures the press, resolves the conflict, and pushes back the table that resulted.
// The overlay only ever displays strings the game sent it.
//
// That split is deliberate rather than incidental. The overlay cannot see the whole input picture -
// it only knows the binds it is handed - so a check made there would happily allow a conflict with
// something outside the rebindable list. Validating in both places would be worse still: two rules
// that can disagree today and will certainly drift the first time one of them is edited.
//
// Capture works while the overlay is open because PADGetNativeButtonPressed and SDL's keyboard
// state read the device directly, without consulting the flag PADBlockInput sets. So the input the
// overlay is busy blocking from the game is still visible to the one piece of the game that needs
// it, and no carve-out in the blocking was required.
constexpr ActionBinds kBindActions[] = {
    ActionBinds::FIRST_PERSON_CAMERA,
    ActionBinds::CALL_MIDNA,
    ActionBinds::OPEN_MAP_SCREEN,
    ActionBinds::TOGGLE_MINIMAP,
    ActionBinds::OPEN_DUSKLIGHT_MENU,
    ActionBinds::TURBO_SPEED_BUTTON,
};
constexpr int kBindActionCount = static_cast<int>(std::size(kBindActions));

// The stored value means different things per port: an SDL scancode where the port is driven by a
// keyboard, a native gamepad button otherwise. Both spell "unbound" as -1.
bool portUsesKeyboard(u32 port) {
    u32 count = 0;
    return PADGetKeyButtonBindings(port, &count) != nullptr;
}

std::string bindDisplayName(int value, bool keyboard) {
    if (value == PAD_KEY_INVALID) {
        return "Not Bound";
    }

    if (keyboard) {
        switch (value) {
        case PAD_KEY_MOUSE_LEFT:   return "Mouse Left";
        case PAD_KEY_MOUSE_MIDDLE: return "Mouse Middle";
        case PAD_KEY_MOUSE_RIGHT:  return "Mouse Right";
        case PAD_KEY_MOUSE_X1:     return "Mouse X1";
        case PAD_KEY_MOUSE_X2:     return "Mouse X2";
        default: break;
        }
        if (value < 0) {
            return "Unknown";
        }
        const char* name = SDL_GetScancodeName(static_cast<SDL_Scancode>(value));
        return (name != nullptr && name[0] != '\0') ? name : "Unknown";
    }

    if (value < 0) {
        return "Unknown";
    }
    const char* name = SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(value));
    return (name != nullptr && name[0] != '\0') ? name : "Unknown";
}

const char* bindActionName(int index) {
    if (index < 0 || index >= kBindActionCount) {
        return "?";
    }
    auto& binds = getActionBinds();
    auto it = binds.find(kBindActions[index]);
    return it != binds.end() ? it->second.actionName.c_str() : "?";
}

int bindGetButton(int index, u32 port) {
    auto& binds = getActionBinds();
    auto it = binds.find(kBindActions[index]);
    if (it == binds.end() || it->second.configVars == nullptr) {
        return PAD_KEY_INVALID;
    }
    return it->second.configVars->at(port).getValue();
}

void bindSetButton(int index, u32 port, int value) {
    auto& binds = getActionBinds();
    auto it = binds.find(kBindActions[index]);
    if (it == binds.end() || it->second.configVars == nullptr) {
        return;
    }
    it->second.configVars->at(port).setValue(value);
}

// Nothing held anywhere. Waited for after arming, so the click or keypress that armed the capture
// is not itself captured as the new bind.
bool bindInputNeutral(u32 port, bool keyboard) {
    if (keyboard) {
        int keyCount = 0;
        const bool* keys = SDL_GetKeyboardState(&keyCount);
        if (keys != nullptr) {
            for (int i = 0; i < keyCount; ++i) {
                if (keys[i]) {
                    return false;
                }
            }
        }
        if (SDL_GetMouseState(nullptr, nullptr) != 0) {
            return false;
        }
        return true;
    }

    return PADGetNativeButtonPressed(port) < 0;
}

// The first thing held, or PAD_KEY_INVALID for nothing yet.
int bindPollPress(u32 port, bool keyboard) {
    if (keyboard) {
        int keyCount = 0;
        const bool* keys = SDL_GetKeyboardState(&keyCount);
        if (keys != nullptr) {
            for (int i = 0; i < keyCount; ++i) {
                if (keys[i]) {
                    return i;
                }
            }
        }

        const u32 mouse = SDL_GetMouseState(nullptr, nullptr);
        if ((mouse & SDL_BUTTON_LMASK) != 0) { return PAD_KEY_MOUSE_LEFT; }
        if ((mouse & SDL_BUTTON_MMASK) != 0) { return PAD_KEY_MOUSE_MIDDLE; }
        if ((mouse & SDL_BUTTON_RMASK) != 0) { return PAD_KEY_MOUSE_RIGHT; }
        if ((mouse & SDL_BUTTON_X1MASK) != 0) { return PAD_KEY_MOUSE_X1; }
        if ((mouse & SDL_BUTTON_X2MASK) != 0) { return PAD_KEY_MOUSE_X2; }
        return PAD_KEY_INVALID;
    }

    return PADGetNativeButtonPressed(port);
}

// Displace rather than reject: the new bind is always applied, and whatever else held that button
// on this port loses it. Rejecting would leave someone pressing a key and watching nothing happen,
// with no indication of why; displacing is visible, and the action that lost its bind is named in
// the status line and shows as Not Bound in the list immediately.
std::string bindApply(int actionIndex, u32 port, int button, bool keyboard) {
    std::string displaced;

    for (int i = 0; i < kBindActionCount; ++i) {
        if (i == actionIndex) {
            continue;
        }
        if (bindGetButton(i, port) == button) {
            bindSetButton(i, port, PAD_KEY_INVALID);
            if (!displaced.empty()) {
                displaced += ", ";
            }
            displaced += bindActionName(i);
        }
    }

    bindSetButton(actionIndex, port, button);

    std::string status = std::string("Bound ") + bindActionName(actionIndex) + " to " +
                         bindDisplayName(button, keyboard);
    if (!displaced.empty()) {
        status += " (displaced " + displaced + ")";
    }
    return status;
}

void updateControls() {
    // Same shape as the warp and clock commits: act on the counter changing, and latch the first
    // value seen without acting, so a game restarting under a Remix that kept running does not
    // arm a capture nobody asked for.
    static int s_captureCommit = 0;
    static bool s_capturePrimed = false;
    static int s_clearCommit = 0;
    static bool s_clearPrimed = false;

    static bool s_capturing = false;
    static bool s_sawNeutral = false;
    static int s_captureAction = 0;
    static u32 s_capturePort = 0;
    static std::string s_status = "Idle";

    const u32 port = static_cast<u32>(std::clamp(readOptionInt("rtx.dusklight.bind.port", 0), 0, PAD_CHANMAX - 1));
    const int action = std::clamp(readOptionInt("rtx.dusklight.bind.actionIndex", 0), 0, kBindActionCount - 1);
    const bool keyboard = portUsesKeyboard(port);

    std::string actionNames;
    std::string buttonNames;
    for (int i = 0; i < kBindActionCount; ++i) {
        if (i > 0) {
            actionNames += '|';
            buttonNames += '|';
        }
        actionNames += bindActionName(i);
        buttonNames += bindDisplayName(bindGetButton(i, port), keyboard);
    }
    push("rtx.dusklight.env.bindActions", actionNames);
    push("rtx.dusklight.env.bindButtons", buttonNames);
    push("rtx.dusklight.env.bindKeyboard", formatBool(keyboard));

    const int clearCommit = readOptionInt("rtx.dusklight.bind.clearCommit", 0);
    if (!s_clearPrimed) {
        s_clearCommit = clearCommit;
        s_clearPrimed = true;
    } else if (clearCommit != s_clearCommit) {
        s_clearCommit = clearCommit;
        s_capturing = false;
        bindSetButton(action, port, PAD_KEY_INVALID);
        s_status = std::string("Cleared ") + bindActionName(action);
    }

    const int captureCommit = readOptionInt("rtx.dusklight.bind.captureCommit", 0);
    if (!s_capturePrimed) {
        s_captureCommit = captureCommit;
        s_capturePrimed = true;
    } else if (captureCommit != s_captureCommit) {
        s_captureCommit = captureCommit;
        s_capturing = true;
        s_sawNeutral = false;
        s_captureAction = action;
        s_capturePort = port;
        s_status = std::string("Press a key or button for ") + bindActionName(action) +
                   " - Escape to unbind";
    }

    if (s_capturing) {
        const bool captureKeyboard = portUsesKeyboard(s_capturePort);

        if (!s_sawNeutral) {
            // The press that armed this is almost certainly still down.
            if (bindInputNeutral(s_capturePort, captureKeyboard)) {
                s_sawNeutral = true;
            }
        } else {
            const int pressed = bindPollPress(s_capturePort, captureKeyboard);

            if (captureKeyboard && pressed == SDL_SCANCODE_ESCAPE) {
                s_capturing = false;
                bindSetButton(s_captureAction, s_capturePort, PAD_KEY_INVALID);
                s_status = std::string("Cleared ") + bindActionName(s_captureAction);
            } else if (pressed != PAD_KEY_INVALID) {
                s_capturing = false;
                s_status = bindApply(s_captureAction, s_capturePort, pressed, captureKeyboard);
                BridgeLog.info("bind: {}", s_status);
            }
        }
    }

    push("rtx.dusklight.env.bindCapturing", formatBool(s_capturing));
    push("rtx.dusklight.env.bindStatus", s_status);
}

// Warp, driven from the Remix overlay.
//
// The destination table lives here, not there, and duplicating it would guarantee the two drift.
// So the overlay sends indices and this pushes back the names for whatever those indices select -
// which means the picker over there can list "Hyrule Field" rather than F_SP121 without either
// side owning a copy of the other's data.
void updateWarp() {
    // Remembered so a commit counter that is already non-zero when the game connects - a game
    // restart under a Remix that kept running - latches instead of firing a warp nobody asked for.
    static int s_lastCommit = 0;
    static bool s_commitPrimed = false;

    const int regionCount = static_cast<int>(gameRegions.size());
    if (regionCount == 0) {
        return;
    }

    const int regionIdx = std::clamp(readOptionInt("rtx.dusklight.warp.regionIndex", 0), 0, regionCount - 1);
    const RegionEntry& region = gameRegions[regionIdx];

    const int mapCount = static_cast<int>(region.maps.size());
    const int mapIdx = mapCount > 0
        ? std::clamp(readOptionInt("rtx.dusklight.warp.mapIndex", 0), 0, mapCount - 1)
        : 0;

    std::string regionNames;
    for (int i = 0; i < regionCount; i++) {
        if (i > 0) {
            regionNames += '|';
        }
        regionNames += gameRegions[i].regionName != nullptr ? gameRegions[i].regionName : "?";
    }
    push("rtx.dusklight.env.warpRegions", regionNames);

    std::string mapNames;
    for (int i = 0; i < mapCount; i++) {
        if (i > 0) {
            mapNames += '|';
        }
        mapNames += region.maps[i].mapName != nullptr ? region.maps[i].mapName : "?";
    }
    push("rtx.dusklight.env.warpMaps", mapNames);

    if (mapCount == 0) {
        push("rtx.dusklight.env.warpRooms", "");
        push("rtx.dusklight.env.warpPoints", "");
        return;
    }

    const MapEntry& map = region.maps[mapIdx];
    const int roomCount = static_cast<int>(map.mapRooms.size());
    const int roomIdx = roomCount > 0
        ? std::clamp(readOptionInt("rtx.dusklight.warp.roomIndex", 0), 0, roomCount - 1)
        : 0;

    std::string roomNames;
    for (int i = 0; i < roomCount; i++) {
        if (i > 0) {
            roomNames += '|';
        }
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(map.mapRooms[i].roomNo));
        roomNames += buffer;
    }
    push("rtx.dusklight.env.warpRooms", roomNames);

    std::string pointNames;
    int pointCount = 0;
    if (roomCount > 0) {
        const RoomEntry& room = map.mapRooms[roomIdx];
        pointCount = static_cast<int>(room.roomPoints.size());
        for (int i = 0; i < pointCount; i++) {
            if (i > 0) {
                pointNames += '|';
            }
            char buffer[16];
            std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(room.roomPoints[i]));
            pointNames += buffer;
        }
    }
    push("rtx.dusklight.env.warpPoints", pointNames);

    // The stage name is what the warp actually travels on, so it is reported too - the overlay
    // shows it next to the plain English one as confirmation of where a press will land.
    push("rtx.dusklight.env.warpStage", map.mapFile != nullptr ? map.mapFile : "");

    const int commit = readOptionInt("rtx.dusklight.warp.commit", 0);

    if (!s_commitPrimed) {
        s_lastCommit = commit;
        s_commitPrimed = true;
        return;
    }

    if (commit == s_lastCommit) {
        return;
    }

    s_lastCommit = commit;

    if (roomCount == 0 || pointCount == 0 || map.mapFile == nullptr) {
        BridgeLog.warn("warp requested to an incomplete destination; ignoring");
        return;
    }

    const RoomEntry& room = map.mapRooms[roomIdx];
    const int pointIdx = std::clamp(readOptionInt("rtx.dusklight.warp.pointIndex", 0), 0, pointCount - 1);
    // Same bounds the game's own warp menu uses. -1 means "you pick", which is the default and
    // nearly always right; the game folds anything at or above 15 into the same thing, so 14 is
    // the last layer that means itself.
    const int layer = std::clamp(readOptionInt("rtx.dusklight.warp.layer", -1), -1, 14);

    BridgeLog.info("warping to {} (room {}, point {}, layer {})", map.mapFile,
                   static_cast<int>(room.roomNo), static_cast<int>(room.roomPoints[pointIdx]), layer);

    dComIfGp_setNextStage(map.mapFile, room.roomPoints[pointIdx],
                          static_cast<s8>(room.roomNo), static_cast<s8>(layer));
}

void updateLocalLights() {
    s_localDebug.drawn = 0;
    s_localDebug.found = 0;
    s_localDebug.enabled = false;

    if (s_interface.CreateLight == nullptr || s_interface.DrawLightInstance == nullptr ||
        s_interface.dxvk_RegisterD3D9Device == nullptr) {
        return;
    }

    if (!dusk::IsGameLaunched) {
        releaseLocalLights();
        return;
    }

    // Counted ahead of every gate below, so it stays truthful whichever one turns us back. A count
    // of lights the game has registered, next to a count of lights we submitted, is what separates
    // "there is nothing here" from "we are dropping them" - and those two are indistinguishable
    // from a drawn count alone, which is what made the first report of this impossible to narrow.
    const dScnKy_env_light_c* envForCount = dKy_getEnvlight();

    for (int i = 0; i < 100; i++) {
        if (envForCount->pointlight[i] != nullptr) {
            s_localDebug.found++;
        }
    }

    for (int i = 0; i < 5; i++) {
        if (envForCount->efplight[i] != nullptr) {
            s_localDebug.found++;
        }
    }

    if (!readOptionBool("rtx.dusklight.game.localLights",
                        getSettings().game.remixLocalLights.getValue())) {
        releaseLocalLights();
        return;
    }

    if (!ensureDeviceRegistered()) {
        return;
    }

    s_localDebug.enabled = true;

    if (s_lightsNeedRecreate) {
        // Drop the handles without destroying them: they refer to a device that
        // no longer exists, and its light manager went with it.
        s_localLights.clear();
        s_lightsNeedRecreate = false;
    }

    const float radius = std::max(
        readOptionFloat("rtx.dusklight.game.localLightRadius",
                        getSettings().game.remixLocalLightRadius.getValue()), 0.01f);
    const float scale = std::max(
        readOptionFloat("rtx.dusklight.game.localLightIntensity",
                        getSettings().game.remixLocalLightIntensity.getValue()), 0.0f);

    for (TrackedLocalLight& tracked : s_localLights) {
        tracked.seen = false;
    }

    const dScnKy_env_light_c* env = envForCount;

    // The game keeps its lights in two arrays, not one. pointlight is the big one that torches,
    // braziers, candles and campfires register into; efplight is a separate five slot list used by
    // chests, a couple of NPCs and the effect system. Only reading the first misses the second
    // entirely, which is a quiet way to lose lights in exactly the rooms that have the fewest.
    const LIGHT_INFLUENCE* candidates[105];
    int candidateCount = 0;

    for (int i = 0; i < 100; i++) {
        if (env->pointlight[i] != nullptr) {
            candidates[candidateCount++] = env->pointlight[i];
        }
    }

    for (int i = 0; i < 5; i++) {
        if (env->efplight[i] != nullptr) {
            candidates[candidateCount++] = env->efplight[i];
        }
    }

    for (int c = 0; c < candidateCount; c++) {
        const LIGHT_INFLUENCE* influence = candidates[c];

        float radiance[3];
        if (!localLightRadiance(*influence, radius, scale, radiance)) {
            continue;
        }

        const float position[3] = {influence->mPosition.x, influence->mPosition.y,
                                   influence->mPosition.z};
        if (!isFinite3(position) || !isFinite3(radiance)) {
            continue;
        }

        const uint64_t hash = localLightHash(influence);

        TrackedLocalLight* tracked = nullptr;
        for (TrackedLocalLight& candidate : s_localLights) {
            if (candidate.hash == hash) {
                tracked = &candidate;
                break;
            }
        }

        if (tracked != nullptr && tracked->source != influence && tracked->seen) {
            // Two live lights hashed to the same value. Vanishingly unlikely
            // (~1e-7 for a roomful), but without this the two would fight over
            // one Remix light and re-create it twice a frame forever, which
            // costs far more than the light is worth. The incumbent keeps it.
            continue;
        }

        if (tracked == nullptr) {
            s_localLights.push_back(TrackedLocalLight {hash, nullptr, influence, {}, {}, 0.0f, false});
            tracked = &s_localLights.back();
        }

        tracked->seen = true;
        tracked->source = influence;

        // Torches move with the actor carrying them and brighten as they catch,
        // so re-create on any real change - but not on float noise, since every
        // re-create crosses the API lock and re-enters the light manager.
        constexpr float kPositionEpsilon = 0.5f;   // world units, ~6mm at TP's scale
        constexpr float kRadianceEpsilon = 0.01f;

        const bool changed =
            tracked->handle == nullptr ||
            std::fabs(position[0] - tracked->position[0]) > kPositionEpsilon ||
            std::fabs(position[1] - tracked->position[1]) > kPositionEpsilon ||
            std::fabs(position[2] - tracked->position[2]) > kPositionEpsilon ||
            std::fabs(radiance[0] - tracked->radiance[0]) > kRadianceEpsilon ||
            std::fabs(radiance[1] - tracked->radiance[1]) > kRadianceEpsilon ||
            std::fabs(radiance[2] - tracked->radiance[2]) > kRadianceEpsilon ||
            std::fabs(radius - tracked->radius) > 0.001f;

        if (changed) {
            remixapi_LightInfoSphereEXT sphere = {};
            sphere.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT;
            sphere.position = {position[0], position[1], position[2]};
            sphere.radius = radius;
            sphere.shaping_hasvalue = 0;
            sphere.volumetricRadianceScale = 1.0f;

            remixapi_LightInfo info = {};
            info.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO;
            info.pNext = &sphere;
            info.hash = hash;
            info.radiance = {radiance[0], radiance[1], radiance[2]};

            remixapi_LightHandle handle = nullptr;
            if (s_interface.CreateLight(&info, &handle) != REMIXAPI_ERROR_CODE_SUCCESS) {
                tracked->handle = nullptr;
                continue;
            }

            tracked->handle = handle;
            tracked->position[0] = position[0];
            tracked->position[1] = position[1];
            tracked->position[2] = position[2];
            tracked->radiance[0] = radiance[0];
            tracked->radiance[1] = radiance[1];
            tracked->radiance[2] = radiance[2];
            tracked->radius = radius;
            s_localDebug.creates++;
        }

        if (s_interface.DrawLightInstance(tracked->handle) == REMIXAPI_ERROR_CODE_SUCCESS) {
            s_localDebug.drawn++;
        }
    }

    // Lights whose actor is gone. Remix keeps an entry per created handle until
    // it is destroyed, so dropping them here is what stops the light manager's
    // map growing for the whole session as rooms load and unload.
    for (size_t i = s_localLights.size(); i-- > 0;) {
        if (!s_localLights[i].seen) {
            destroyLocalLight(s_localLights[i]);
            s_localLights.erase(
                s_localLights.begin() +
                static_cast<std::vector<TrackedLocalLight>::difference_type>(i));
        }
    }

    s_localDebug.tracked = static_cast<int>(s_localLights.size());
}

void updateEffectLights() {
    s_effectDebug.enabled = false;
    s_effectDebug.drawn = 0;

    if (s_interface.CreateLight == nullptr || s_interface.DrawLightInstance == nullptr ||
        s_interface.dxvk_RegisterD3D9Device == nullptr) {
        return;
    }

    const auto& game = getSettings().game;

    if (!dusk::IsGameLaunched ||
        !readOptionBool("rtx.dusklight.game.effectLights", game.effectLights.getValue())) {
        releaseEffectLights();
        dusk::effect_lights::reset();
        dusk::effect_lights::Params off;
        off.enable = false;
        dusk::effect_lights::collect(off);
        s_effectDebug.stats = dusk::effect_lights::stats();
        return;
    }

    if (!ensureDeviceRegistered()) {
        return;
    }

    s_effectDebug.enabled = true;

    if (s_effectLightsNeedRecreate) {
        // Drop the handles without destroying them: they refer to a device that no longer
        // exists, and its light manager went with it. The sites go too - holding them would
        // carry a grace period across a discontinuity it was never meant to span.
        s_effectLights.clear();
        dusk::effect_lights::reset();
        s_effectLightsNeedRecreate = false;
    }

    dusk::effect_lights::Params params;
    params.enable = true;
    params.intensity = std::max(
        readOptionFloat("rtx.dusklight.game.effectLightIntensity",
                        game.effectLightIntensity.getValue()), 0.0f);
    params.derivedIntensity = std::max(
        readOptionFloat("rtx.dusklight.game.effectLightDerivedIntensity",
                        game.effectLightDerivedIntensity.getValue()), 0.0f);
    params.derivedReach = std::max(
        readOptionFloat("rtx.dusklight.game.effectLightDerivedReach",
                        game.effectLightDerivedReach.getValue()), 0.0f);
    params.derivedRadius = std::max(
        readOptionFloat("rtx.dusklight.game.effectLightDerivedRadius",
                        game.effectLightDerivedRadius.getValue()), 0.01f);
    params.undeterminedIntensity = std::max(
        readOptionFloat("rtx.dusklight.game.effectLightUndeterminedIntensity",
                        game.effectLightUndeterminedIntensity.getValue()), 0.0f);
    params.undeterminedReach = std::max(
        readOptionFloat("rtx.dusklight.game.effectLightUndeterminedReach",
                        game.effectLightUndeterminedReach.getValue()), 0.0f);
    params.undeterminedRadius = std::max(
        readOptionFloat("rtx.dusklight.game.effectLightUndeterminedRadius",
                        game.effectLightUndeterminedRadius.getValue()), 0.01f);
    params.fireOffset = readOptionFloat("rtx.dusklight.game.effectLightFireOffset",
                                        game.effectLightFireOffset.getValue());
    params.glowOffset = readOptionFloat("rtx.dusklight.game.effectLightGlowOffset",
                                        game.effectLightGlowOffset.getValue());
    params.mergeRadius = std::max(
        readOptionFloat("rtx.dusklight.game.effectLightMergeRadius",
                        game.effectLightMergeRadius.getValue()), 0.0f);
    params.adoptRadius = std::max(
        readOptionFloat("rtx.dusklight.game.effectLightAdoptRadius",
                        game.effectLightAdoptRadius.getValue()), 0.0f);
    params.maxLights = readOptionInt("rtx.dusklight.game.effectLightMaxLights",
                                     game.effectLightMaxLights.getValue());
    params.maxDistance = readOptionFloat("rtx.dusklight.game.effectLightMaxDistance",
                                         game.effectLightMaxDistance.getValue());
    params.bursts = readOptionBool("rtx.dusklight.game.effectLightBursts",
                                   game.effectLightBursts.getValue());
    params.minChroma = readOptionFloat("rtx.dusklight.game.effectLightMinChroma",
                                       game.effectLightMinChroma.getValue());
    params.minLuma = readOptionFloat("rtx.dusklight.game.effectLightMinLuma",
                                     game.effectLightMinLuma.getValue());

    // Not part of the decision, so it never reaches effect_lights: it is a per light multiplier
    // Remix applies in the volumetrics passes only, so above 1 a flame hazes the air around it
    // without getting any brighter on surfaces.
    const float volumetric = std::max(
        readOptionFloat("rtx.dusklight.game.effectLightVolumetric",
                        game.effectLightVolumetric.getValue()), 0.0f);

    const camera_class* camera = dComIfGp_getCamera(0);
    if (camera != nullptr) {
        params.cameraPos[0] = camera->view.lookat.eye.x;
        params.cameraPos[1] = camera->view.lookat.eye.y;
        params.cameraPos[2] = camera->view.lookat.eye.z;
        params.cameraValid = isFinite3(params.cameraPos);
    }

    // The report is an action, so act on the counter changing and latch the first value seen
    // without acting - otherwise connecting to a Remix that outlived a game restart dumps a
    // report nobody asked for. Same shape as the warp and clock commits.
    {
        static int s_reportCommit = 0;
        static bool s_reportLatched = false;
        const int commit = readOptionInt("rtx.dusklight.game.effectLightReportCommit", 0);
        if (!s_reportLatched) {
            s_reportCommit = commit;
            s_reportLatched = true;
        } else if (commit != s_reportCommit) {
            s_reportCommit = commit;
            dusk::effect_lights::requestReport();
        }
    }

    // Hand over the counters this side owns, so one report can answer cost questions too.
    // These have been counted since the system landed and printed nowhere - `creates` in
    // particular is the number that says whether an animating light is expensive, because the
    // bridge re-creates a light every time its radiance moves more than 2%.
    dusk::effect_lights::setBridgeCounters(s_effectDebug.creates, s_effectDebug.destroys,
                                           s_effectDebug.drawn);

    const std::vector<dusk::effect_lights::Site>& sites = dusk::effect_lights::collect(params);
    s_effectDebug.stats = dusk::effect_lights::stats();

    for (TrackedEffectLight& tracked : s_effectLights) {
        tracked.seen = false;
    }

    for (const dusk::effect_lights::Site& site : sites) {
        if (!isFinite3(site.position) || !isFinite3(site.radiance)) {
            continue;
        }

        const uint64_t hash = effectLightHash(site.id);

        TrackedEffectLight* tracked = nullptr;
        for (TrackedEffectLight& candidate : s_effectLights) {
            if (candidate.siteId == site.id) {
                tracked = &candidate;
                break;
            }
        }

        if (tracked == nullptr) {
            s_effectLights.push_back(TrackedEffectLight {site.id, nullptr, {}, {}, 0.0f, false});
            tracked = &s_effectLights.back();
        }

        tracked->seen = true;

        // Re-create on a real change but not on float noise. Each re-create crosses the API lock
        // and re-enters the light manager, so this is worth doing on cost alone.
        //
        // It used to be worth much more than that: re-creating a light reset its entry in Remix's
        // light manager and with it its place in the RTXDI index map, costing one frame of
        // temporal reuse for every pixel the light touched - so a fire whose radiance was re-sent
        // every tick never accumulated any and was visibly noisier than a static one. Our fork
        // fixes that at the source: LightManager::addExternalLight carries the buffer index across
        // the overwrite (rtx_light_manager.cpp), the way the game-light path already did. Against
        // a STOCK Remix runtime the old cost is back, and this epsilon is the only thing between
        // an animating flame and permanent temporal noise.
        //
        // The radiance test is RELATIVE, unlike the local light mirror's. Radiance here is solved
        // from a reach and a radius and routinely lands in the hundreds, so a fixed 0.01 would
        // trip on the colour animation of every flame, every frame.
        constexpr float kPositionEpsilon = 0.5f;   // world units
        // static, because std::max binds its arguments by const reference and MSVC will not
        // let a capture-less lambda odr-use a function-local constexpr.
        static constexpr float kRadianceRelative = 0.02f; // 2 percent
        static constexpr float kRadianceFloor = 0.01f;

        const auto radianceChanged = [](float now, float before) {
            const float scale = std::max(std::fabs(now), std::fabs(before));
            return std::fabs(now - before) > std::max(kRadianceFloor, scale * kRadianceRelative);
        };

        const bool changed =
            tracked->handle == nullptr ||
            std::fabs(site.position[0] - tracked->position[0]) > kPositionEpsilon ||
            std::fabs(site.position[1] - tracked->position[1]) > kPositionEpsilon ||
            std::fabs(site.position[2] - tracked->position[2]) > kPositionEpsilon ||
            radianceChanged(site.radiance[0], tracked->radiance[0]) ||
            radianceChanged(site.radiance[1], tracked->radiance[1]) ||
            radianceChanged(site.radiance[2], tracked->radiance[2]) ||
            std::fabs(site.radius - tracked->radius) > 0.001f;

        if (changed) {
            remixapi_LightInfoSphereEXT sphere = {};
            sphere.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT;
            sphere.position = {site.position[0], site.position[1], site.position[2]};
            sphere.radius = site.radius;
            sphere.shaping_hasvalue = 0;
            sphere.volumetricRadianceScale = volumetric;

            remixapi_LightInfo info = {};
            info.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO;
            info.pNext = &sphere;
            info.hash = hash;
            info.radiance = {site.radiance[0], site.radiance[1], site.radiance[2]};

            remixapi_LightHandle handle = nullptr;
            if (s_interface.CreateLight(&info, &handle) != REMIXAPI_ERROR_CODE_SUCCESS) {
                tracked->handle = nullptr;
                continue;
            }

            // No DestroyLight here, even though this is an update rather than a first create.
            // The handle Remix returns is the hash itself (rtx_remix_api.cpp: the handle is a
            // reinterpret_cast of info.hash), and our hash is stable per site - so the "old"
            // handle and the new one are the same value, and destroying it would erase the light
            // that was just written. The failure that causes is nasty and invisible in the
            // counters: DrawLightInstance still returns success, so "drawn" keeps incrementing
            // while the light is gone for every frame in which it changed.
            tracked->handle = handle;
            for (int i = 0; i < 3; i++) {
                tracked->position[i] = site.position[i];
                tracked->radiance[i] = site.radiance[i];
            }
            tracked->radius = site.radius;
            s_effectDebug.creates++;
        }

        if (s_interface.DrawLightInstance(tracked->handle) == REMIXAPI_ERROR_CODE_SUCCESS) {
            s_effectDebug.drawn++;
        }
    }

    // Sites that ended. Remix keeps an entry per created handle until it is destroyed, so
    // dropping them here is what stops the light manager's map growing for the whole session
    // as rooms load and unload.
    for (size_t i = s_effectLights.size(); i-- > 0;) {
        if (!s_effectLights[i].seen) {
            destroyEffectLight(s_effectLights[i]);
            s_effectLights.erase(
                s_effectLights.begin() +
                static_cast<std::vector<TrackedEffectLight>::difference_type>(i));
        }
    }

    s_effectDebug.tracked = static_cast<int>(s_effectLights.size());
}

// The bridge's diff cache assumed nothing else ever touched rtx.dusklight.env.*.
// That is wrong: those options are NoSave, so anything that rebuilds Remix's user
// layer - saving settings from its UI, a config reload - drops them back to their
// defaults, and the cache then happily never pushes them again. The symptom is
// brutal and silent: Remix says the bridge is not connected while the game is
// convinced it is, forever.
//
// So verify rather than assume. Reading the heartbeat back costs one call a frame
// and recovers on the next one. Where the getter is missing (an older Remix), fall
// back to re-pushing everything periodically, which is slower to notice but needs
// nothing from the other side.
uint32_t s_framesSinceFullPush = 0;

void resyncIfDropped() {
    constexpr uint32_t kBlindResyncFrames = 120;

    bool dropped = false;

    std::string heartbeat;
    if (readOption("rtx.dusklight.env.enable", heartbeat)) {
        dropped = heartbeat != "True";
    } else if (++s_framesSinceFullPush >= kBlindResyncFrames) {
        dropped = true;
    }

    if (!dropped) {
        return;
    }

    // Dropping the cache is what forces every value through again; they are all
    // re-pushed by the callers below in the same frame.
    s_vars.clear();
    s_framesSinceFullPush = 0;
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
    // Bumped whenever the game gains something the Remix tab depends on, so the tab
    // can say "your game build is older than this Remix build" instead of leaving
    // controls that quietly do nothing.
    push("rtx.dusklight.env.protocol", "9");
    push("rtx.dusklight.env.bloomEnable", formatBool(bloom->getEnable() != 0));
    push("rtx.dusklight.env.bloomThreshold", formatFloat(bloom->getPoint() / 255.0f));
    push("rtx.dusklight.env.bloomBlurSize", formatFloat(bloom->getBlureSize()));
    push("rtx.dusklight.env.bloomBlurRatio", formatFloat(bloom->getBlureRatio()));
    push("rtx.dusklight.env.bloomTint", formatColor(blend.r, blend.g, blend.b));
    push("rtx.dusklight.env.bloomBaseWeight", formatFloat(blend.a / 255.0f));
    push("rtx.dusklight.env.bloomScreenBlend", formatBool(bloom->mMode == 1));
    push("rtx.dusklight.env.monoColor", formatColor(mono.r, mono.g, mono.b));
    push("rtx.dusklight.env.monoAmount", formatFloat(mono.a / 255.0f));

    // Ambient colours - the single biggest carrier of the game's time-of-day, weather and area
    // mood, and nothing consumes them under Remix because the path tracer lights the scene
    // itself. The fork's grade stage puts their colour back over the final image; the design is
    // docs/kankyo-remix.md IV.3.
    //
    // Already the fully blended per-frame values: setLight() ran the four-way palette blend,
    // folded in the event add-colours and applied the global ratios before we read them. BG
    // layer 0 is the main room layer, the one the game itself reuses when it needs "the"
    // background ambient (e.g. mirror reflections).
    const dScnKy_env_light_c* env = dKy_getEnvlight();

    push("rtx.dusklight.env.actorAmbient", formatColorS10(env->actor_amb_col));
    push("rtx.dusklight.env.bgAmbient", formatColorS10(env->bg_amb_col[0]));

    // The clock, so the overlay's slider can follow the game while nobody is holding it and a
    // frozen scene can say what it is frozen at. Quantized to a quarter of a degree - one
    // in-game minute, since the whole day is 360 degrees - because this changes every frame and
    // every push that gets through takes the Remix API's global lock.
    push("rtx.dusklight.env.daytime", formatFloatQ(env->daytime, 0.25f));

    // Fog and sky, pushed together because the game authors them together - one palette entry,
    // same time-of-day and weather indices, same blend call. The fog colour is approximately the
    // sky colour at any moment, which is why distant terrain dissolves into the sky; a consumer
    // that sources the two separately loses that. docs/kankyo-fog.md.
    //
    // As with the ambients these are the settled per-frame values (event add-colours, the ratio
    // lightning pulses, fog-bank tag overrides and the second "gather" blend all folded in), so
    // reading the outputs rather than the palette tables avoids reimplementing any of it.
    //
    // Fog is D3D9-captured as well, per draw, but the game sets it per object and Remix keeps
    // only the first state it sees in a frame - so the captured value is decided by submission
    // order. These are the room's actual answer.
    push("rtx.dusklight.env.fogActive", formatBool(fogIsActive(env)));
    push("rtx.dusklight.env.fogColor", formatColorS10(env->fog_col));
    push("rtx.dusklight.env.fogStartZ", formatFloatQ(env->mFogNear, 1.0f));
    push("rtx.dusklight.env.fogEndZ", formatFloatQ(env->mFogFar, 1.0f));

    push("rtx.dusklight.env.skyHidden", formatBool(skyIsHidden(env)));
    push("rtx.dusklight.env.skyColor", formatColorS10(env->vrbox_sky_col));
    push("rtx.dusklight.env.kasumiInner", formatColorS10(env->vrbox_kasumi_inner_col));
    push("rtx.dusklight.env.kasumiOuter", formatColorS10(env->vrbox_kasumi_outer_col));
    push("rtx.dusklight.env.kumoTop", formatColorS10(env->vrbox_kumo_top_col));
    push("rtx.dusklight.env.kumoBottom", formatColorS10(env->vrbox_kumo_bottom_col));
    push("rtx.dusklight.env.kumoShadow", formatColorS10(env->vrbox_kumo_shadow_col));

    char sceneBuffer[16];
    std::snprintf(sceneBuffer, sizeof(sceneBuffer), "%d", static_cast<int>(env->wether_pat1));
    push("rtx.dusklight.env.colpat", sceneBuffer);
    std::snprintf(sceneBuffer, sizeof(sceneBuffer), "%d", static_cast<int>(env->mMoyaMode));
    push("rtx.dusklight.env.moyaMode", sceneBuffer);
    // The haze counter is decremented past zero as it winds down, which would read as a negative
    // strength on the other side.
    push("rtx.dusklight.env.moyaCount",
         formatFloat(static_cast<f32>(std::max(env->mMoyaCount, 0))));
}

// Reports what the lights are actually doing, so Remix's Dusklight tab can show
// it. This is the only place any of it is visible: the game's own debug window
// is never drawn in the D3D9 mode this feature exists for.
//
// Pushed after the lights have run, so a frame's readout matches the frame that
// produced it. The diff cache means a steady scene costs nothing.
void pushLightStatus() {
    push("rtx.dusklight.env.deviceRegistered", formatBool(s_celestial.deviceRegistered));
    push("rtx.dusklight.env.sunActive", formatBool(s_celestial.active));
    push("rtx.dusklight.env.sunIsDay", formatBool(s_celestial.isDay));
    push("rtx.dusklight.env.sunFade", formatFloat(s_celestial.fade));

    // Rounded to a tenth of a degree: these change continuously as time passes,
    // and pushing full precision would re-cross the API lock every frame for a
    // readout nobody can read that finely anyway.
    const auto roundTenth = [](float value) {
        return std::round(value * 10.0f) / 10.0f;
    };

    push("rtx.dusklight.env.sunAzimuth", formatFloat(roundTenth(s_celestial.azimuth)));
    push("rtx.dusklight.env.sunElevation", formatFloat(roundTenth(s_celestial.elevation)));

    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%d", s_localDebug.found);
    push("rtx.dusklight.env.localLightsFound", buffer);
    // Whether we got past every gate and actually ran the submit loop. Without this, an option
    // that reads false and an area with no lights look identical from the other side.
    push("rtx.dusklight.env.localLightsRunning", formatBool(s_localDebug.enabled));
    std::snprintf(buffer, sizeof(buffer), "%d", s_localDebug.drawn);
    push("rtx.dusklight.env.localLightsDrawn", buffer);
    std::snprintf(buffer, sizeof(buffer), "%d", s_localDebug.tracked);
    push("rtx.dusklight.env.localLightsTracked", buffer);

    // Effect lights. Every one of these exists because the alternative was asking the owner to
    // describe what they saw; docs/effect-lights.md section 7 says which question each answers.
    // In particular "orphans" is what decides whether refusing to forward a vanilla light that
    // no effect corroborates is the right default - it counts exactly the lights that policy
    // is throwing away.
    push("rtx.dusklight.env.effLightsRunning", formatBool(s_effectDebug.enabled));
    std::snprintf(buffer, sizeof(buffer), "%d", s_effectDebug.stats.emitters);
    push("rtx.dusklight.env.effLightsEmitters", buffer);
    std::snprintf(buffer, sizeof(buffer), "%d", s_effectDebug.stats.considered);
    push("rtx.dusklight.env.effLightsConsidered", buffer);
    std::snprintf(buffer, sizeof(buffer), "%d", s_effectDebug.stats.candidates);
    push("rtx.dusklight.env.effLightsCandidates", buffer);
    std::snprintf(buffer, sizeof(buffer), "%d", s_effectDebug.stats.sites);
    push("rtx.dusklight.env.effLightsSites", buffer);
    std::snprintf(buffer, sizeof(buffer), "%d", s_effectDebug.drawn);
    push("rtx.dusklight.env.effLightsDrawn", buffer);
    std::snprintf(buffer, sizeof(buffer), "%d", s_effectDebug.stats.derived);
    push("rtx.dusklight.env.effLightsDerived", buffer);
    std::snprintf(buffer, sizeof(buffer), "%d", s_effectDebug.stats.orphans);
    push("rtx.dusklight.env.effLightsOrphans", buffer);
    std::snprintf(buffer, sizeof(buffer), "%d", s_effectDebug.stats.culled);
    push("rtx.dusklight.env.effLightsCulled", buffer);
    std::snprintf(buffer, sizeof(buffer), "%d", s_effectDebug.stats.excluded);
    push("rtx.dusklight.env.effLightsExcluded", buffer);
    std::snprintf(buffer, sizeof(buffer), "%d/%d", s_effectDebug.stats.vanillaPoint,
                  s_effectDebug.stats.vanillaSpot);
    push("rtx.dusklight.env.effLightsVanilla", buffer);
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

    // Remix's Dusklight tab owns this once the bridge has connected; before that (and on a
    // Remix build without the getter) the game's own config value decides.
    const bool wantEnabled = readOptionBool("rtx.dusklight.game.bridgeEnable",
                                            getSettings().game.remixKankyoBridge.getValue());

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

    // Settings that are not part of a light but still have to be reachable from Remix's tab,
    // because the game cannot draw its own UI in this mode. Mirrored into the game's own config
    // var rather than read at the use site, so the code that consumes it stays free of any
    // knowledge of Remix - and so it keeps working on the backends where the bridge is inert.
    {
        auto& game = getSettings().game;

        const bool culling =
            readOptionBool("rtx.dusklight.game.disableFrustumCulling",
                           game.disableFrustumCulling.getValue());
        if (culling != game.disableFrustumCulling.getValue()) {
            game.disableFrustumCulling.setValue(culling);
        }

        const bool hideSky =
            readOptionBool("rtx.dusklight.game.hideSkyBillboards",
                           game.remixHideSkyBillboards.getValue());
        if (hideSky != game.remixHideSkyBillboards.getValue()) {
            game.remixHideSkyBillboards.setValue(hideSky);
        }

        const bool hideVrbox = readOptionBool("rtx.dusklight.game.hideVrbox",
                                              game.remixHideVrbox.getValue());
        if (hideVrbox != game.remixHideVrbox.getValue()) {
            game.remixHideVrbox.setValue(hideVrbox);
        }

        // The game's flat circular shadows under rupees, hearts, pots and the like. Remix
        // traces a real shadow for each of those objects, so the painted disc lands on top of
        // a correct one. Suppressed at registration (dDlst_shadowControl_c::setSimple), so no
        // draw call is issued rather than one being hidden downstream.
        const bool blobShadows = readOptionBool("rtx.dusklight.game.blobShadows",
                                                game.remixBlobShadows.getValue());
        if (blobShadows != game.remixBlobShadows.getValue()) {
            game.remixBlobShadows.setValue(blobShadows);
        }

        // Grass: one draw per blade instead of one batch per room. Costs draw calls, and buys
        // Remix a stable hash for each blade - see dGrass_packet_c::draw.
        const bool perBladeGrass = readOptionBool("rtx.dusklight.game.perBladeGrass",
                                                  game.remixPerBladeGrass.getValue());
        if (perBladeGrass != game.remixPerBladeGrass.getValue()) {
            game.remixPerBladeGrass.setValue(perBladeGrass);
        }

        // The game's own recording mode. Its settings screen is never drawn in this rendering
        // mode, so config.json was previously the only way to reach it - and a one way trip,
        // since nothing in the running game could turn it back off.
        const bool recording = readOptionBool("rtx.dusklight.game.recordingMode",
                                              game.recordingMode.getValue());
        if (recording != game.recordingMode.getValue()) {
            game.recordingMode.setValue(recording);
        }


        const float noonElevation =
            readOptionFloat("rtx.dusklight.game.celestialNoonElevation",
                            game.celestialNoonElevation.getValue());
        if (noonElevation != game.celestialNoonElevation.getValue()) {
            game.celestialNoonElevation.setValue(noonElevation);
        }

        // Clock control. Both are mirrored rather than read at the use site, so setDaytime()
        // stays free of any knowledge of Remix and keeps working on the backends where this
        // bridge is inert. The requested time is deliberately carried as a plain value rather
        // than a commit counter: scrubbing a slider has to be continuous, and setDaytime()
        // already acts on the change rather than the value.
        const bool freeze = readOptionBool("rtx.dusklight.game.freezeTime",
                                           game.freezeTime.getValue());
        if (freeze != game.freezeTime.getValue()) {
            game.freezeTime.setValue(freeze);
        }

        const float requestedTime = readOptionFloat("rtx.dusklight.game.timeOfDay",
                                                    game.timeOfDay.getValue());
        if (requestedTime != game.timeOfDay.getValue()) {
            game.timeOfDay.setValue(requestedTime);
        }

        // Mirrored after the value it refers to, so the two can never be seen half applied: by
        // the time a new count is visible the time it asks for already is.
        const int timeCommit = readOptionInt("rtx.dusklight.game.timeCommit",
                                             game.timeCommit.getValue());
        if (timeCommit != game.timeCommit.getValue()) {
            game.timeCommit.setValue(timeCommit);
        }
    }

    // Remix's own input blocking sends a message across the 32 bit bridge, which a 64 bit game
    // loading its DLL directly never receives - so an open overlay has always let input straight
    // through to the game. Aurora already has the switch for it, and it suppresses the held state
    // on release so nothing is left stuck down; it just needed telling.
    PADBlockInput(readOptionBool("rtx.dusklight.uiActive", false));

    resyncIfDropped();
    pushKankyoState();
    updateCelestialLight();
    updateLocalLights();
    updateEffectLights();
    updateWarp();
    updateControls();
    pushLightStatus();
#endif
}

const std::vector<PushedVar>& debugVars() {
    return s_vars;
}

uint64_t totalPushes() {
    return s_totalPushes;
}

const CelestialLightDebug& celestialDebug() {
    return s_celestial;
}

bool& celestialFlipDirection() {
    return s_celestialFlip;
}

bool& celestialLockDirection() {
    return s_celestialLock;
}

const EffectLightsDebug& effectLightsDebug() {
    return s_effectDebug;
}

const LocalLightsDebug& localLightsDebug() {
    return s_localDebug;
}

}  // namespace remix
}  // namespace dusk
