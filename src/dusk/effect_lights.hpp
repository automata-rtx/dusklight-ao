#pragma once

#include <cstdint>
#include <vector>

// Turns the game's own particle effects into light sources for the path tracer.
//
// The game decides every frame where fire and glow exist and whether they are on, and it
// expresses that decision as JPA emitters. This walks the emitter table, keeps the ones that
// are emitting light rather than covering what is behind them, groups the several emitters
// that make up one visible fire into a single site, and hands the result to the Remix bridge
// to be drawn as sphere lights.
//
// Nothing here talks to Remix. The bridge owns the API; this owns the decision. Design and
// citations: docs/effect-lights.md.

namespace dusk {
namespace effect_lights {

// What kind of thing the effect is. Decides only the vertical offset and which fallback
// radius/reach applies when the game has no light of its own to copy - never whether a light
// is made at all. See docs/effect-lights.md section 3.1.
enum class Class : uint8_t {
    Other = 0,
    Fire,
    Glow,
    Lava,
    Burst,
    Count,
};

const char* className(Class cls);

// One place in the world that should be lit, after the emitters that make up a single visible
// effect have been merged together.
struct Site {
    // Persistent across frames for as long as the site exists. The bridge hashes this into the
    // Remix light hash, and Remix keys a light's temporal history on that hash - so it has to
    // survive a frame in which an individual emitter came or went.
    uint32_t id;

    float position[3];   // world space, offset already applied
    float radiance[3];   // linear, ready for remixapi_LightInfo::radiance
    float radius;        // sphere light emitter radius, world units

    Class cls;
    bool derived;        // reach came from a light the game authored
    bool colorFromGame;  // colour came from one - a wider set, see docs/effect-lights.md
    uint16_t effectId;   // the effect that won the site, for the report
    int members;         // emitters merged into it
};

// Everything the caller configures. Filled from rtx.dusklight.game.* with the game's own
// config.json as the fallback; see docs/effect-lights.md section 9.
struct Params {
    bool enable = true;

    float intensity = 1.0f;              // master multiplier over every light made here

    float derivedIntensity = 19.0f;      // sites that adopted a vanilla light
    float derivedRadius = 10.0f;

    float undeterminedIntensity = 1.0f;  // sites with nothing to copy
    float undeterminedReach = 400.0f;
    float undeterminedRadius = 8.0f;

    float fireOffset = 15.0f;            // upward offset, world units, per class
    float glowOffset = 0.0f;

    float mergeRadius = 60.0f;           // two emitters closer than this are one site
    float adoptRadius = 250.0f;          // a vanilla light closer than this may be adopted

    int maxLights = 32;
    float maxDistance = 12000.0f;

    bool bursts = false;                 // include one-shot effects
    int orphanPolicy = 0;                // 0 none, 1 unadopted only, 2 all - see Stats::orphans

    // "reads as a glow" - saturated OR near white hot. Same shape and same defaults as the
    // fork's material self-illumination rule, which asks the same question of a surface.
    float minChroma = 0.50f;
    float minLuma = 0.70f;
    float minAlpha = 0.08f;

    // Camera position, for the distance cull. The caller supplies it because this module
    // deliberately knows nothing about the camera's own accessors.
    float cameraPos[3] = {0.0f, 0.0f, 0.0f};
    bool cameraValid = false;
};

// Per-frame counters. Every one of these exists because the alternative was asking the owner
// to describe what they saw; docs/effect-lights.md section 7 says which question each answers.
struct Stats {
    int emitters = 0;    // alive in the manager
    int considered = 0;  // reached the rule (drawn, in a 3D group)
    int candidates = 0;  // passed the rule
    int sites = 0;       // after clustering
    int derived = 0;        // sites whose reach came from a vanilla light
    int colorFromGame = 0;  // sites whose colour did (always >= derived)
    int orphans = 0;     // vanilla lights no site adopted
    int culled = 0;      // dropped by distance or budget

    // Split by which of the game's two light registries they came from. These exist to settle
    // one thing we could not settle by reading: whether the spot list's per frame "in use" flag
    // is still set by the time the bridge runs, which depends on where the kankyo process falls
    // in the frame relative to the actors. If vanillaSpot is always zero while torches are lit,
    // kankyo runs last and clears the flags before we see them - the failure is silent and
    // safe (those sites fall back to the configured defaults rather than being mispositioned),
    // but it costs the game's own colour for every BossLight torch. See docs/effect-lights.md.
    int vanillaPoint = 0;  // pointlight[] + efplight[]
    int vanillaSpot = 0;   // the BOSS_LIGHT spot list, flagged live this frame

    bool ran = false;
};

// Walks the emitter table and produces this frame's sites. Must be called from the thread that
// runs the game's frame, after the actors have executed - the emitters carry last update's
// positions otherwise. Safe to call when there is no particle system yet; it produces nothing.
const std::vector<Site>& collect(const Params& params);

const Stats& stats();

// Drops every tracked site, so the next collect() starts from nothing. Call it whenever the
// continuity between frames has genuinely been broken - a Remix device reset, the system being
// switched off - rather than letting the grace period below hold stale sites across it.
void reset();

// --- the simple effect side channel ---------------------------------------------------
//
// Torches and candles do not get an emitter each. The game shares ONE emitter per effect id
// and teleports it around the world once per frame, standing in for every instance
// (dPa_simpleEcallBack, src/d/d_particle.cpp:811) - so by the time anything can sweep the
// emitter table, only the last instance's position survives. dPa_simpleEcallBack::set is the
// one point where each instance is still its own thing, so it hands us a copy there.
//
// Cheap and inert when the system is off: recordSimple returns immediately unless collect()
// has armed it, and the buffer is a fixed size array that saturates rather than growing.
void setRecording(bool enabled);
void recordSimple(uint16_t effectId, const void* emitter, float x, float y, float z,
                  const float prm[3], const float env[3]);

// Asks for the classification report to be written to the log on the next collect(). One line
// per distinct effect seen since the last request, capped, with the rule's inputs and verdict -
// this is what turns the "additive means emissive" inference into a measurement.
void requestReport();

}  // namespace effect_lights
}  // namespace dusk
