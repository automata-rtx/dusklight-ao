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

// What kind of thing the effect is, read out of the effect's own name. See
// docs/effect-lights.md section 3.1.
//
// WHAT THIS ACTUALLY FEEDS - the five readers are the whole list:
//
//   1. two gates. Excluded refuses a candidate outright, and Burst is skipped unless
//      Params::bursts is on. Nothing else here decides whether a light exists; that is the
//      blend-mode-and-colour rule, written inline in collectEmitters and collectSimple.
//   2. the vertical offset (classOffset). Fire, Lantern and Burst take fireOffset, Glow and
//      Spark take glowOffset, and Lava, Other and Excluded take nothing.
//   3. the merge tie-break (classWeight). Within one site, the highest-weight member donates
//      the position, the colour and the effect id.
//   4. site identity across frames. A pending site only matches last frame's site if the
//      class agrees, so a class that changed would start a new site - and a new Remix light
//      hash, losing that light's temporal history.
//   5. ONE class, Lantern, can be given its own reach, radius and intensity, entirely
//      separately from every other light - Params::lanternSeparate. Off by default, and off
//      means the lantern is solved exactly like any other fire.
//
// IT STILL DOES NOT SELECT THE FALLBACK RADIUS OR REACH for any other class. That keys on
// whether a vanilla light was adopted - derivedRadius when one was, undeterminedReach and
// undeterminedRadius when none was - and neither branch reads the class. That is deliberate
// and it is the survey's headline finding: NOTHING THE ARTISTS AUTHORED IS PHOTOMETRIC. The
// JPA blocks carry colour, extent, rate and lifetime; not one of them is a brightness, and
// inventing a brightness from rate x lifetime would be exactly the plausible-mechanism-as-
// finding this project has recorded three times. Hue, extent and persistence come from the
// authored data; radiance comes from the game's own LIGHT_INFLUENCE::mPow and from settings.
//
// At stock settings glowOffset is 0.0, the same as the offset Other gets, so Glow, Spark and
// Other currently behave identically in every one of the five. Worth knowing before spending
// time on which of the three a name lands in: today that question cannot move a pixel.
enum class Class : uint8_t {
    Other = 0,
    Fire,
    // Link's lantern, and only it. "kantera" (カンテラ) matches five names in the game's whole
    // effect table, two of which are the lantern's still and swung flames and three of which
    // have no caller anywhere in the tree - so this is a clean identification, not a heuristic.
    // It has to cover BOTH of Link's, because the game destroys one emitter and creates the
    // other every time he swings the lamp; a class that changed on the swing would mint a new
    // site id and a new Remix light hash every time.
    Lantern,
    Glow,
    // Sparkle and twinkle - kirakira, and the "spark" effects that are not explosions. Split
    // off Glow so that a group of sub-second, high-frequency effects can be recognised in the
    // report and damped later without touching steady glows. Today it takes the same offset
    // and the same merge weight as Glow, so the split moves nothing by itself.
    Spark,
    Lava,
    Burst,
    // Named by the game as a substance that is never a light source - drool, body fluid.
    // Unlike every other class this one DOES decide: an Excluded effect never earns a light.
    // It is still recorded in the classification report, so a wrong exclusion is visible
    // rather than silent. See docs/effect-lights.md section 4.
    Excluded,
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

    // --- the three global multipliers ----------------------------------------------------
    //
    // ONE per value the system derives from the game, all defaulting to 1.0, all applied at
    // the same point in the chain to both the derived and the undetermined branch. They exist
    // so that a value the game authored can be corrected without a rebuild when it comes out
    // too weak or too strong. The full chain, authored value to final radiance, is written
    // out once in docs/effect-lights.md section 5 - and nowhere else, deliberately.
    float intensity = 1.0f;              // radiance
    float reachScale = 1.0f;             // reach
    float radiusScale = 1.0f;            // sphere radius

    // How much a light grows with the amount of fire standing at it. Reach is multiplied by
    // the site's mass raised to this power; 0 disables it exactly, 0.5 makes radiance
    // proportional to mass because radiance goes as reach squared. See docs/effect-lights.md.
    float massExponent = 0.5f;

    float derivedIntensity = 19.0f;      // sites that adopted a vanilla light
    float derivedRadius = 10.0f;
    // NOTE there is no derivedReach: the derived branch's reach IS the game's authored
    // LIGHT_INFLUENCE::mPow, and reachScale above is what scales it. Until 2026-08-13 that
    // multiplier existed separately, applied to this branch only, and defaulted to the same
    // 1.0 - so widening it to both branches changed no default and no behaviour.
    float undeterminedIntensity = 1.0f;  // sites with nothing to copy
    float undeterminedReach = 400.0f;
    float undeterminedRadius = 8.0f;

    // --- what the artists authored -------------------------------------------------------
    //
    // Both read the loaded JPA blocks through public accessors, with no .jpa parsing, and
    // both are IMMUTABLE for the session - which is the point. The live emitter fields beside
    // them are overwritten every frame by key blocks and by several hundred actor setter
    // calls, so a value read from the live side can animate, and an animating value that
    // reaches radiance re-creates the Remix light every frame and costs its temporal history.
    bool authoredColor = true;   // hue from the authored colour ramp, not the live register
    bool authoredRadius = false; // grow the sphere to the authored extent where that is bigger

    // --- the lantern ---------------------------------------------------------------------
    //
    // Off: Class::Lantern is solved exactly like Class::Fire, global multipliers included.
    // On: the three values below replace the branch's, RAW - the global multipliers and the
    // mass boost do not apply, which is what "its own settings" has to mean to be useful.
    // The colour is not overridden either way; the lantern adopts the game's own lamp colour.
    bool lanternSeparate = false;
    float lanternIntensity = 1.0f;
    float lanternReach = 400.0f;
    float lanternRadius = 8.0f;

    float fireOffset = 15.0f;            // upward offset, world units, per class
    float glowOffset = 0.0f;

    float mergeRadius = 60.0f;           // two emitters closer than this are one site
    float adoptRadius = 250.0f;          // a vanilla light closer than this may be adopted

    int maxLights = 32;
    float maxDistance = 12000.0f;

    bool bursts = false;                 // include one-shot effects
    int orphanPolicy = 0;                // 0 none, 1 unadopted only, 2 all - see Stats::orphans

    // --- Class::Spark, the 29 glitter and creature-spark effects ---------------------------
    //
    // THE DEFAULT IS TODAY'S BEHAVIOUR, not a new light. Every Spark name was admitted before
    // this gate existed - 21 of them as Class::Spark and the 8 shadow-insect names as
    // Class::Other, and NEITHER class was ever gated. `sparks` is therefore an UNDO switch: it
    // is on because that is what the game already did, and it exists so that a result nobody
    // likes is one checkbox rather than a rebuild. Turning it off is a visible change; leaving
    // it on is not.
    bool sparks = true;

    // Extra frames a Spark site is held after its last live emitter, ON TOP of the six-frame
    // grace every class gets (kSiteGraceFrames).
    //
    // This is the periodicity answer, and it is about the RENDERER rather than about the look.
    // The shadow insect does not spark on a timer - it sparks in windows driven by its state
    // machine, and the shortest of them is genuinely brief: daE_YM_c::initFireFly arms a spark
    // of cM_rndF-chosen 5 to 15 frames on a wall bounce (d_a_e_ym.cpp:2624), i.e. 0.17-0.5 s at
    // the 30 Hz sim pace (game_clock.h:8). A site that ends and restarts inside that gap is
    // DESTROYED AND RE-CREATED: site identity is matched on class and proximity, a new site
    // takes a new id, and a new id is a new Remix light hash with no RTXDI temporal history.
    // So the cost of a gap is not one dark frame, it is the light re-accumulating its reuse
    // from nothing every time the bug bounces.
    //
    // Holding the site across the gap keeps ONE light alive with ONE hash. It does not make the
    // light brighter and it does not fill the gap in with anything - the site is held with the
    // values it last had, exactly as the base grace period already does for effects the game
    // re-sets every few frames.
    //
    // 12 frames = 0.4 s, chosen to bridge the 5-15 frame window rather than to be generous.
    // A BRIEF SPARK IS STILL A BRIEF LIGHT: this bridges the gaps WITHIN a spark burst, it does
    // not turn a burst into a steady lamp - when the bug stops sparking for good, the site
    // still goes out, 18 frames later instead of 6. Set it to 0 for exactly the old behaviour.
    int sparkHold = 12;

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

    // Passed the additive+glow rule and were then refused because the game names the substance
    // as something that is never a light source (Class::Excluded). Watch this rather than trust
    // it: a non-zero count in a room that reads under-lit is the signal the list is too wide,
    // and the classification report names every one of them.
    int excluded = 0;

    // Split by which of the game's two light registries they came from. These exist to settle
    // one thing we could not settle by reading: whether the spot list's per frame "in use" flag
    // is still set by the time the bridge runs, which depends on where the kankyo process falls
    // in the frame relative to the actors. If vanillaSpot is always zero while torches are lit,
    // kankyo runs last and clears the flags before we see them - the failure is silent and
    // safe (those sites fall back to the configured defaults rather than being mispositioned),
    // but it costs the game's own colour for every BossLight torch. See docs/effect-lights.md.
    int vanillaPoint = 0;  // pointlight[] + efplight[]
    int vanillaSpot = 0;   // the BOSS_LIGHT spot list, flagged live this frame

    // Candidates addCandidate refused because its fixed array was full, and sites the tracker
    // refused because kMaxSites was reached. Both were silent, and both fire only in a crowded
    // scene - which is exactly what a combat test produces. A light going missing while every
    // printed counter looked healthy was reachable before these existed.
    int droppedCandidates = 0;
    int droppedSites = 0;

    // Where the values each site was solved from actually came from, counted per frame. These
    // are the "authored versus defaulted" question asked of the whole frame at once, so it can
    // be answered from the overlay without pressing the report button: authoredColor counts
    // sites whose hue came from the effect's authored ramp, authoredRadius counts sites whose
    // sphere grew to the authored extent, and lantern counts sites solved from the lantern's
    // own settings rather than the shared ones.
    int authoredColor = 0;
    int authoredRadius = 0;
    int lantern = 0;

    // Sites this frame per Class, indexed by the enum. The report names every class it prints;
    // this is the same split without a log, and it is what makes a classification change
    // visible the moment it happens rather than at the next report.
    int byClass[static_cast<int>(Class::Count)] = {};

    // Class::Spark, counted at the two points that actually answer the question - and they are
    // DIFFERENT questions, which is why one number would not have done.
    //
    // sparkSeen is every Spark emitter that reached the additive-and-glow rule: the game was
    // drawing it, so the shadow insect WAS sparking in front of the camera. sparkLit is how
    // many of those passed. seen>0 with lit==0 is the whole negative result in one line - the
    // bug sparked and the rule refused it - and it is the one outcome that no amount of
    // classification work can fix, because the refusal is the .jpa's blend mode or its colour.
    // seen==0 means we never saw one at all, which is a different failure and points at the
    // draw-group filter rather than at the rule.
    int sparkSeen = 0;
    int sparkLit = 0;

    bool ran = false;
};

// The high-water mark of each counter since the last report, kept alongside the per-frame
// values.
//
// The counters are a single-frame snapshot, and the first real report proved why that is not
// enough: the owner pressed at a calm moment and five of the nine numbers read zero, including
// culled - while the trace showed 22 concurrent sites against a 32-light budget in the same
// session. Relabelling honest zeros leaves them zero. This is what makes one press describe the
// session rather than the instant.
struct StatsPeak {
    int emitters = 0;
    int considered = 0;
    int candidates = 0;
    int sites = 0;
    int culled = 0;
    int excluded = 0;
    int orphans = 0;
    int vanillaPoint = 0;
    int vanillaSpot = 0;
    int droppedCandidates = 0;
    int droppedSites = 0;
    int sparkSeen = 0;
    int sparkLit = 0;
    uint32_t frames = 0;   // frames the system ran since the last report - the denominator
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

// The bridge's own counters, handed over so the report can print them. Call once per frame
// before collect(); they are display-only and nothing here reads them for a decision.
void setBridgeCounters(int creates, int destroys, int drawn);

}  // namespace effect_lights
}  // namespace dusk
