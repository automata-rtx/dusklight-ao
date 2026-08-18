#include "dusk/effect_lights.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "dusk/logging.h"

#include "JSystem/JParticle/JPABaseShape.h"
#include "JSystem/JParticle/JPAEmitter.h"
#include "JSystem/JParticle/JPAEmitterManager.h"
#include "JSystem/JParticle/JPAKeyBlock.h"
#include "JSystem/JParticle/JPADynamicsBlock.h"
#include "JSystem/JParticle/JPAResource.h"
#include "d/d_com_inf_game.h"
#include "d/d_kankyo.h"
#include "d/d_particle.h"
#include "d/d_particle_name.h"

// See docs/effect-lights.md. The short version:
//
//   The game already decides, every frame, where fire and glow exist and whether they are on,
//   and it says so by creating JPA emitters. We read the emitters rather than the actors (800
//   of them, fire is a detail in each) or the particles (dozens per fire, recycled constantly,
//   a light each would strobe). The emitter is the long-lived thing that sits at the point the
//   effect is generated from - which is exactly where the light belongs.
//
// Two sources have to be read, because the game spawns effects two different ways:
//
//   1. Level and one-shot emitters, one JPABaseEmitter per instance. Swept from the emitter
//      manager's in-use lists. Bonfires, Link's lantern, arrows.
//   2. "Simple" effects, where ONE shared emitter is teleported around the world once per
//      frame to stand in for every instance (dPa_simpleEcallBack). Torches and candles are
//      this path, and a sweep sees exactly one of them - the last one drawn. So those are
//      recorded at the point the actor asks for them, via recordSimple() below.

namespace dusk {
namespace effect_lights {

namespace {

aurora::Module Log("effect-lights");

constexpr float kPi = 3.14159265358979323846f;

// Remix's own "still perceptible" threshold (kNewLightEndValue in the runtime's rtx_lights.h).
// A literal because it belongs to Remix's conversion, not to us.
constexpr float kNewLightEndValue = 0.01f;

// The game's effect ids are one flat namespace; bit 0x8000 only selects which resource bank
// the effect was loaded into (dPa_RM / dPa_control_c::getRM_ID, src/d/d_particle.cpp:1199).
// Strip it to index anything keyed by effect.
constexpr uint16_t kIdMask = 0x1FFF;

// Groups 14..18 are the 2D and menu draw passes (dPa_control_c::draw's group argument maps
// straight onto JPAEmitterManager::draw, src/d/d_particle.cpp:1365). 13 is drawFogScreen, a
// full screen effect. None of those has a world position worth lighting from.
constexpr uint8_t kFirstScreenGroup = 13;

// Bounded by construction: 250 emitters is the manager's hard cap (src/d/d_particle.cpp:1215),
// and the simple recorder has its own cap below.
constexpr int kMaxCandidates = 512;
constexpr int kMaxSimpleRecords = 192;
constexpr int kMaxSites = 128;
constexpr int kMaxReportEntries = 256;

// The report's other four sections. All fixed-size and all saturating with a stated truncation
// notice, per the logging rule in CLAUDE.md: a log that fills a disk is worse than no log.
constexpr int kMaxVanillaReport = 64;
constexpr int kMaxSiteReport = 64;
constexpr int kMaxTraceEntries = 512;

// A single-frame position move past this is worth counting. A light that walks with Link moves
// a few units a frame; a light that swaps which emitter it is standing on jumps by up to
// mergeRadius at once. 12 units sits well above the first and well below the second, so the
// counter separates "the light is following something" from "the light teleported".
constexpr float kJumpNotable = 12.0f;

// A site's radiance must move by more than this fraction to earn a trace line. Deliberately
// the bridge's own update threshold (remix_bridge.cpp, kRadianceRelative): a change too small
// to make the bridge re-create the light is too small to be worth a line.
constexpr float kTraceRelative = 0.02f;

// A site whose members all vanish is kept this many frames before its light is dropped. Some
// effects are re-set every few frames rather than continuously, and without the grace period
// those would create and destroy a Remix light in a loop.
constexpr int kSiteGraceFrames = 6;

// Below this the emitter's particles have no visible size, so it is drawing nothing however
// healthy its other state looks. Not a tuning value: it is a "is this exactly zero" test with
// room for the float ramp that gets it there (d_a_e_db.cpp:1871-1877 uses cLib_addCalc towards
// 0.0, which approaches rather than arrives).
constexpr float kMinParticleScale = 0.001f;

// The largest sphere the authored extent is allowed to ask for, in world units. Deliberately
// the same 64 that bounds the two configured radii in the overlay, so switching the authored
// radius on cannot leave the envelope those were tuned inside. An emitter whose authored volume
// is larger than this is a wide area effect - a burning bridge, a lava field - and a 64 unit
// sphere is already at the point where a light inside a wall sconce clips through the geometry.
constexpr float kMaxAuthoredRadius = 64.0f;

// Where a site's hue came from, in the order the solve prefers them. Printed by name in the
// site report - a reader with only the log has to be able to tell an adopted lamp colour from
// an authored ramp from the live register, because those three fail in different ways.
enum ColorSource : uint8_t {
    kColorGame = 0,      // a light the game itself registered near the effect
    kColorAuthored = 1,  // the effect's own authored colour ramp - stable for the session
    kColorLive = 2,      // the emitter's current registers - can animate, and can be tinted
};

const char* colorSourceName(uint8_t v) {
    switch (v) {
    case kColorGame: return "game";
    case kColorAuthored: return "authored";
    default: return "live";
    }
}

struct Candidate {
    float pos[3];
    float color[3];   // 0..1, the effect's own colour
    // How much fire this candidate is worth, for scaling the site's output. NOT the lead
    // score - the lead is decided from class and effect id, deliberately without any animating
    // term, because a position that depends on an animating value snaps between emitters.
    // Sweep path: the emitter's global alpha, so a fading fire contributes less as it fades.
    // Simple path: 1.0 flat - a simple record is one visible effect instance, and the emitter
    // behind it is shared between instances so its alpha says nothing about this one.
    float weight;
    Class cls;
    uint16_t effectId;

    // The authored side, carried alongside the live side rather than instead of it. `color`
    // above is the LIVE colour and stays that way, because the accept test and the report both
    // have to keep asking their question of what is actually being drawn; this is what the
    // light's hue is taken from when Params::authoredColor is on.
    float authoredColor[3];
    bool hasAuthoredColor;
    float extent;        // emitter-local units, 0 when the resource said nothing
    bool hasExtent;
};

struct SimpleRecord {
    uint16_t effectId;
    float pos[3];
    float prm[3];
    float env[3];
    const JPABaseEmitter* emitter;
};

struct TrackedSite {
    // Last frame's position, and the largest single-frame jump this site has ever made.
    // A light that teleports is invisible in every other section: the sites list is one frame,
    // and the trace only writes a line when radiance moves, so a pure position jump at constant
    // brightness wrote nothing at all. That is the exact signature of a stuttering shadow.
    float prevPos[3];
    bool hasPrevPos;
    float maxJump;
    uint32_t jumps;      // frames this site moved more than kJumpNotable

    Site site;
    int missingFrames;
    bool seen;
    // Report-only, deliberately not on the public Site: the caller has no use for either, and
    // widening the API for a log would be the wrong trade.
    float reach;
    float vanillaDistance;
    float mass;        // summed emitter alpha merged into this site
    float massBoost;   // what that multiplied reach by
    // Which source each value was actually taken from this frame. Report-only, and the answer
    // to "authored or defaulted" for this particular light.
    uint8_t colorSource;
    bool radiusAuthored;
    bool lantern;
};

// One press of Log Effect Classification Report has to answer every open question at once, so
// the report is five sections rather than one table. The shape is deliberate: a reader who has
// only the log, and not the source, should be able to work out why any given light does or does
// not exist. See docs/effect-lights.md section 7.
struct ReportEntry {
    uint16_t effectId;
    uint8_t blendMode;
    uint8_t blendSrc;
    uint8_t blendDst;
    uint8_t prm[3];
    uint8_t env[3];
    Class cls;
    bool additive;
    bool glow;
    bool simple;

    // The measured inputs to readsAsGlow, so a refusal can be priced rather than guessed at:
    // "what would minChroma have to be for this to pass" is arithmetic once these are printed.
    float chroma;
    float luma;

    // Which keyword put it in its class, or "-" for Other. This turns the keyword lists from
    // something reviewed by eye into something checkable from data - the lists have been wrong
    // twice (lava matched nothing, smokeLight classified as Glow).
    const char* keyword;

    // The animation configuration, all of it queryable at runtime with no .jpa parsing. This is
    // what says whether an effect's colour can animate at all: the emitter's mPrmClr is only
    // re-sampled per frame when glblClrAnm && prmAnm, and two of the five anmType values pin
    // the key frame at 0 and never move.
    bool hasRes;
    bool glblClrAnm;
    bool prmAnm;
    bool envAnm;
    uint8_t anmType;
    int16_t anmMaxFrm;
    int32_t maxFrame;   // emission window; 0 = continuous
    int16_t lifeTime;   // PARTICLE life, and keyed - this is the current value, not authored
    uint32_t age;       // mTick at the moment it was first seen
    int particles;
    float baseSizeX;
    float baseSizeY;
    float globalScale;
    uint16_t keyIds;    // bitmask of JPAKeyBlock IDs present, 0 = no key blocks

    // What the artists authored, as opposed to what the emitter currently holds. These are the
    // values the system now derives from, so the report has to print them beside the live ones
    // - a hue that reads wrong in game is then one subtraction away from being explained.
    bool hasAuthoredColor;
    uint8_t authoredColor[3];
    bool hasExtent;
    float extent;       // emitter-local units; baseSize, or the spawn volume when that is larger
    bool persistent;    // AUTHORED maxFrame == 0, which the live maxFrame column often is not

    // The effect's authored user-work word, printed raw and NOT interpreted.
    //
    // This is the only per-effect authored ground truth within reach, and the game itself cuts
    // real decisions on it: dPa_group_id_change (d_particle.cpp:213-218) routes bit 0x80 to
    // group 13 (drawFogScreen), 0x1000 to group 12 (drawDarkworld) and 0x2000 to group 14
    // (draw2Dgame), per d_particle.h:406-424 - so an effect with 0x2000 is drawn in a 2D pass
    // and would plausibly reach Remix looking like UI. Bits 0x400/0x800 attach the gen_b/gen_d
    // Light8 callbacks and 0x20/0x40 select a kankyo tint source, but ONLY on the branch the
    // Light8 test does not take (d_particle.cpp:1550-1620), so bit 0x20 means different things
    // depending on which other bits are set.
    //
    // That gating is exactly why nothing here decodes it. Print the word, replay it against a
    // log, decide afterwards. A legend shipped on an unverified reading is how this project has
    // recorded inference as finding before.
    uint32_t userWork;
};

// One line per light the game itself registered, and who took it. This is what settles whether
// a short-lived effect steals a torch's light: adoption is exclusive and resolved in cluster
// order, not priority order, so a bomb inside adoptRadius of a torch can take it for the whole
// explosion and leave the torch on the undetermined fallback - 19x dimmer.
struct VanillaReportEntry {
    float pos[3];
    uint8_t color[3];
    float reach;
    bool spot;
    uint16_t adoptedByEffect;  // effect id that took it, 0 = nobody
    float adoptDistance;       // to the effect that took it, or -1
};

// One line per live site, taken at the end of collect(). Answers the merge question (members),
// the adoption hinge (vanillaDistance - the one unverified number the whole burst design turns
// on), and what the light actually ended up being.
struct SiteReportEntry {
    uint32_t id;
    Class cls;
    uint16_t effectId;
    float pos[3];
    int members;
    bool derived;
    bool colorFromGame;
    float reach;
    float radius;
    float radiance;
    float vanillaDistance;  // to the adopted light, or -1 if none
    // How violently this site has moved. maxJump is the largest single-frame displacement it
    // has ever made; jumps counts the frames it exceeded kJumpNotable. A site that follows an
    // actor shows a small maxJump and zero jumps; a site swapping which emitter it stands on
    // shows a maxJump near mergeRadius and a jumps count that climbs every second.
    float maxJump;
    uint32_t jumps;
    float mass;
    float massBoost;
    // Authored or defaulted, per value, for this one light. The three of them together are the
    // answer to "did the new derivation actually run here", which nothing else in the log can
    // give: a site whose colour reads live and whose radius reads default was solved exactly
    // the way it was before any of this landed.
    uint8_t colorSource;
    bool radiusAuthored;
    bool lantern;
};

// A retrospective ring of site state over time. Retrospective is the point: you throw the bomb
// and THEN press the button, rather than having to arm a trace and hope the timing lands.
//
// A line is recorded only when a site appears, disappears, or its radiance moves by more than
// the bridge's own 2% update threshold - so an animating explosion produces a dense trace and a
// steady torch produces one line and then nothing. That is what keeps a 512-entry ring covering
// minutes of play rather than eight frames.
struct TraceEntry {
    uint32_t frame;
    uint32_t siteId;
    Class cls;
    uint16_t effectId;
    float radiance;
    float reach;
    float radius;
    uint8_t color[3];
    int members;
    bool derived;
    uint8_t event;  // 0 = changed, 1 = appeared, 2 = gone
};

bool s_recordingEnabled = false;

SimpleRecord s_simple[kMaxSimpleRecords];
int s_simpleCount = 0;
bool s_simpleOverflowed = false;

// Records refused because s_simple was full, accumulated across the actor pass and folded into
// s_stats.droppedSimple where the records are consumed. It cannot be counted straight into
// s_stats: collect() assigns a fresh Stats at its head and recordSimple runs after that, so the
// count would be wiped before accumulatePeak ever saw it.
int s_simpleDroppedPending = 0;

// 0->1 edge latch on the overflow warning. The counter above is the record of how bad and for
// how long; this only exists so the first occurrence is visible in a log where nobody pressed
// the report button.
bool s_simpleOverflowWarned = false;

std::vector<Site> s_sites;
std::vector<TrackedSite> s_tracked;
uint32_t s_nextSiteId = 1;

Stats s_stats;

StatsPeak s_peak;
ReportEntry s_report[kMaxReportEntries];
int s_reportCount = 0;
bool s_reportOverflowed = false;
bool s_reportRequested = false;

VanillaReportEntry s_vanillaReport[kMaxVanillaReport];
int s_vanillaReportCount = 0;

SiteReportEntry s_siteReport[kMaxSiteReport];
int s_siteReportCount = 0;

// The trace ring. Overwrites oldest-first and never allocates; s_traceWritten is the total
// ever written, so the emitter can say how many lines were lost rather than pretending the
// ring is the whole history.
TraceEntry s_trace[kMaxTraceEntries];
int s_traceHead = 0;
uint32_t s_traceWritten = 0;
uint32_t s_frameCounter = 0;

// Last radiance reported per site, for the trace's change test. Parallel to s_tracked by site
// id rather than by index, because indices move when a site is erased.
struct TraceMemory {
    uint32_t siteId;
    float radiance;
};
TraceMemory s_traceMemory[kMaxSites];
int s_traceMemoryCount = 0;

// Counters the bridge owns. They exist today and are incremented every frame, and until now
// nothing has ever printed them - which is a rule 4 gap in shipped code: we count how many
// times a light is re-created and have no way to see it.
int s_bridgeCreates = 0;
int s_bridgeDestroys = 0;
int s_bridgeDrawn = 0;

// Name derived class, resolved once per effect id. 0 means "not resolved yet".
uint8_t s_classCache[kIdMask + 1] = {};

bool isFinite3(const float v[3]) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

float dist2(const float a[3], const float b[3]) {
    const float dx = a[0] - b[0];
    const float dy = a[1] - b[1];
    const float dz = a[2] - b[2];
    return dx * dx + dy * dy + dz * dz;
}

// dPa_name::getName indexes its table with the unmasked id while bounds-checking a masked one
// (src/d/d_particle_name.cpp:30), so it reads out of bounds for every room pack effect. Mask
// here rather than reaching into the game's table.
const char* effectName(uint16_t id) {
    const char* name = dPa_name::getName(static_cast<uint32_t>(id & kIdMask));
    return name != nullptr ? name : "";
}

bool nameHas(const char* haystack, const char* needle) {
    // Case insensitive, no locale, no allocation. The names are ASCII romaji.
    for (const char* h = haystack; *h != '\0'; h++) {
        const char* a = h;
        const char* b = needle;
        while (*b != '\0') {
            char ca = *a;
            char cb = *b;
            if (ca >= 'A' && ca <= 'Z') {
                ca = static_cast<char>(ca - 'A' + 'a');
            }
            if (cb >= 'A' && cb <= 'Z') {
                cb = static_cast<char>(cb - 'A' + 'a');
            }
            if (ca != cb) {
                break;
            }
            a++;
            b++;
        }
        if (*b == '\0') {
            return true;
        }
    }
    return false;
}

// The name decides what KIND of thing this is. Class feeds four things and no others - two
// gates, the vertical offset, the merge tie-break and cross-frame site identity; it does NOT
// pick the fallback radius or reach, which keys on whether a vanilla light was adopted. The
// full statement is on the enum in effect_lights.hpp. Apart from the Excluded gate it never
// decides whether a light exists; that is the additive-and-glow rule, which is written out
// INLINE TWICE - once in collectEmitters and once in collectSimple. There is no classify()
// function and there never was; two comments pointed at one until 2026-08-13.
//
// Reading the effect's own name is not "tagging" in the sense the project's
// rules forbid: the name is the game's own identity for the effect, shipped in the game's own
// table, at the granularity the game itself uses.
Class classifyByName(uint16_t id) {
    const char* name = effectName(id);
    if (name[0] == '\0') {
        return Class::Other;
    }

    // ORDER IS LOAD-BEARING. Names collide - a great many carry two of these words - so the
    // order is what actually decides, and each step of it was derived by replaying the lists
    // over all 3205 names in d_particle_name.cpp rather than chosen by ear:
    //
    //   Lava > all       "yoganshibuki" is lava SPLASH. Splash is exactly what a negative list
    //                    wants, and this one is molten. Lava naming the substance as hot is the
    //                    strongest claim available, so nothing may override it. (No name
    //                    currently collides, but this is the invariant that keeps a future
    //                    addition to the negative list from putting out the lava.)
    //   Excluded > Burst 3 names: ZI_S_bq_bombdamageYodare_a/b/c, drool off a bomb-damaged
    //                    creature. That is drool, not an explosion.
    //   Burst > Fire     10 names, e.g. ZF_S_bombRoom00_fire, ZF_S_HBomb02_fire00. Pre-dates
    //                    this ordering and is deliberate: a light that lives a handful of
    //                    frames is a flash, which is sometimes right and sometimes a flicker
    //                    artefact, so it is held apart from Fire and off by default. Moving
    //                    Fire above Burst turns every explosion into a persistent fire; the
    //                    test suite catches it, which is how this comment came to exist.
    //   Lantern > Fire   ALL FIVE kantera names contain "fire" too - ZI_J_kantera_fire and
    //                    ZI_J_kantera_swingFire are Link's, the other three have no caller in
    //                    the tree. Below Fire the branch would be unreachable and the report
    //                    would go on printing keyword "fire" for the lantern, which is exactly
    //                    the gap that made a lantern-only setting unbuildable before.
    //   Burst > Lantern  no name collides today (no kantera name contains bomb/baku/explo),
    //                    so this ordering is free; it is stated so that the invariant "one-shot
    //                    violence outranks every steady flame" survives the new class.
    //   Spark < Burst    THE 20 ZM_*_BombInsectSpark* NAMES STAY Class::Burst, and the reason
    //                    RECORDED HERE UNTIL 2026-08-13 WAS FACTUALLY WRONG. It said they are
    //                    "the electric bugs". They are not. They belong to d_a_nbomb - the
    //                    BOMBLING, Link's crawling bomb-bug - and are its fuse spark:
    //                    d_a_nbomb.cpp:488 `static u16 enemyBombID[] = {0xA0D..0xA11}`, set in
    //                    daNbomb_c::setEffect and pinned to the bomb's own animation matrix.
    //                    That is the ONLY reference to any of the 20 anywhere in src/ or
    //                    include/; the other 15 (ZM_S_* and both SparkTornado sets) have no
    //                    caller at all. So the conclusion survives for a better reason than the
    //                    one given: a bomb's fuse belongs with the bomb, and Burst is where a
    //                    bomb goes. Nothing here was reclassified.
    //
    //                    THE TWILIGHT BUG'S SPARK IS ZI_S_ym_elecAt_a..d (0x393-0x396), spawned
    //                    by daE_YM_c::setElecEffect1/2 (d_a_e_ym.cpp:247-286) - 闇虫 yami mushi,
    //                    the Shadow Insect. It was NEVER in Burst and was never gated: it sat in
    //                    Class::Other, which is admitted on the additive-and-glow rule alone. It
    //                    is in Spark below for its NAME, not to change whether it lights - see
    //                    the Spark branch.
    //
    // Re-run over all 3205 names in d_particle_name.cpp on 2026-08-13, comparing every name's
    // class before and after this revision: exactly 26 names move, 5 from Fire to Lantern and
    // 21 from Glow to Spark, and NOT ONE name enters or leaves Excluded, Burst, Lava or Other.
    // That is the whole blast radius of the vocabulary change.
    //
    // The negative list collides with Lava, Fire and Glow zero times today. If that ever stops
    // being true the order has to be revisited - re-run the collision scan, do not guess.
    //
    // TEN OF THESE THIRTY KEYWORDS MATCH NOTHING, and are deliberately left in place. Counted
    // over all 3205 names on 2026-08-11: lava, magma, youdo, bakuha, honoo, hono, taimatsu,
    // kagarib, pika and shine. An earlier version of this comment said three, which was the
    // Lava row only. scripts/check_invariants.py (check_effect_light_keywords) now owns the
    // list, replays every keyword over d_particle_name.cpp on each push, and fails in BOTH
    // directions - a new keyword that matches nothing, and one of these ten starting to match.
    //
    // THEY ARE NOT ROMANIZATION MISSES, so do not "fix" them by adding spellings. Each was
    // re-checked in kunrei-shiki and Hepburn and in the obvious variants, and every spelling
    // is zero: taimatsu/taimatu, kagarib/kagari/kagaribi, honoo/honou/homura, pika/pikari/
    // pikapika, bakuha/bakuhatsu/bakuhatu, youdo/yodo. The game used English (fire 181, torch
    // 1, glow 59, spark 33, bomb 171) or a different Japanese word (maki 8, kantera 5, kaen 4,
    // kira 9) - see docs/japanese-naming-remix.md section 3. Removing them is not worth doing
    // either: a keyword that matches nothing classifies nothing, so deleting all ten would
    // change not one effect's class - a diff against a shipping classifier that buys a
    // shorter list. The check is what stops the next dead word going unnoticed.

    // "lava"/"magma"/"youdo" match ZERO of the 3205 names - this game spells it yogan (18
    // names) and yougan (3), so Class::Lava was unreachable dead code until 2026-08-07 and
    // every lava column was classified Other. The two spellings are disjoint sets, not one
    // containing the other; both are needed.
    if (nameHas(name, "lava") || nameHas(name, "magma") || nameHas(name, "youdo") ||
        nameHas(name, "yogan") || nameHas(name, "yougan")) {
        return Class::Lava;
    }

    // Substances that are never a light source. Deliberately NARROW: two words, 48 names, and
    // both of them unambiguous. It exists because the Deku Baba was observed lighting the room
    // in game on 2026-08-07 from its drool emitters, which is the case that showed additive
    // blending alone does not mean "emits light" - a wet surface is authored additively too,
    // to read as glossy. Widening this is a decision to take with the classification report in
    // hand, not from a list of plausible words: "smoke" alone would be 161 names.
    //
    // AND SIZE ANY CANDIDATE IN BOTH ROMANIZATIONS BEFORE ADDING IT. This is the one list
    // where a name puts a light out, so a word counted in one spelling excludes half its
    // effects and silently leaves the rest lighting the room. Droplet is the live example:
    // shizuku matches 29 names, sizuku 26, and the two sets are disjoint - 55 together.
    // docs/japanese-naming-remix.md section 3, and docs/effect-lights.md section 4.
    //
    // SAND AND DUST joined it on 2026-08-13, from a reported defect: the sand worm in Gerudo
    // Desert and the first room of Arbiter's Grounds carried an "insanely bright" light that the
    // original never had. The actor is daE_SW_c (d_a_e_sw.cpp) - sw is 砂 worm, and the enum at
    // the top of that file aliases its effects to misleading names like ZLM_SAND00_IA. Masking
    // those IDs with kIdMask gives 0x36F-0x380, which d_particle_name.h names in full:
    // ZM_S_SandWormDive00..03, SandWormJump00/01, SandWormRun00..02, SandWormStruggle00,
    // SandWormHide00..02 and SandWormAttackSign00/01. Not one of them matches any positive
    // keyword, so every one landed in Class::Other - which is admitted on the additive-and-glow
    // rule alone, and a cloud of sun-lit sand is exactly the thing that rule cannot tell from a
    // flame. INFERENCE, not finding: the .jpa assets are not in this repo, so the blend mode and
    // colour that made the rule say yes have not been read. The exclusion makes it moot either
    // way, which is why it is the fix rather than a threshold change.
    //
    // Sized over all 3206 names in d_particle_name.h before adding, as this list requires:
    // sandworm 15, sand 121, suna 1, dust 14 - 136 names between them, and the intersection with
    // Lava, the existing exclusions, Burst, Lantern, Fire, Spark and Glow is ZERO for all seven.
    // So the entire blast radius is 136 names moving Other -> Excluded.
    //
    // Three candidates were measured and REJECTED, and the reasons are worth keeping:
    //   tsubu  2 - it_jn_arwg_tsubu00 and it_jn_takara_tsubu. 宝 takara is treasure; a treasure
    //              sparkle is a thing that plausibly should glow. Grain is not worth taking it.
    //   iwa    2 - both are ak_jn_uchiwawind, and 団扇 uchiwa is a FAN. Zero of the two are 岩
    //              rock. A two-name keyword that matches nothing it means is the exact trap this
    //              file's ten dead words document from the other direction.
    //   smoke  161 - the existing comment already rules it out on size, and it stays ruled out.
    if (nameHas(name, "yoda") || nameHas(name, "taieki") ||
        nameHas(name, "sandworm") || nameHas(name, "sand") || nameHas(name, "suna") ||
        nameHas(name, "dust")) {
        return Class::Excluded;
    }

    // One-shot violence. Kept apart from Fire because a light that lives for a handful of
    // frames is a flash, which is sometimes exactly right and sometimes a flicker artefact.
    if (nameHas(name, "bakuha") || nameHas(name, "explo") || nameHas(name, "bomb") ||
        nameHas(name, "baku")) {
        return Class::Burst;
    }

    // Link's lantern, and nothing else in the game. カンテラ kantera is a loanword, so unlike
    // most of this vocabulary there is no kunrei/Hepburn variant to miss - and every English
    // alternative was sized before settling on it: "lantern" and "ranpu" match zero names and
    // "lamp" matches four, all of them ZF_S_k_lampWater*, which is water.
    if (nameHas(name, "kantera")) {
        return Class::Lantern;
    }

    if (nameHas(name, "fire") || nameHas(name, "honoo") || nameHas(name, "hono") ||
        nameHas(name, "kaen") || nameHas(name, "flame") || nameHas(name, "taimatsu") ||
        nameHas(name, "maki") || nameHas(name, "torch") ||
        nameHas(name, "ablaze") || nameHas(name, "kagarib")) {
        return Class::Fire;
    }

    // きらきら kirakira, glitter, and the non-explosive "spark" effects. Split off Glow because
    // they are a different light: sub-second, high-frequency and tiny, where a glow is steady
    // and soft. They keep Glow's offset and Glow's merge weight, so the split is visible in the
    // report and inert in the picture until someone decides otherwise.
    //
    // "elecat" and "yb_elec" are the two SHADOW INSECT sparks, added 2026-08-13. Blast radius
    // replayed over all 3205 names in d_particle_name.cpp before and after: EXACTLY 8 names
    // move, all Other -> Spark, and not one name enters or leaves Excluded, Burst, Lava, Fire,
    // Lantern or Glow.
    //
    //   elecat   4  ZI_S_ym_elecAt_a..d   0x393-0x396  daE_YM_c   (d_a_e_ym.cpp:257-282)
    //   yb_elec  4  ZI_S_yb_elec_a..d     0x630-0x633  daE_YMB_c  (d_a_e_ymb.cpp:146-149)
    //
    // BOTH SPELLINGS ARE DELIBERATELY NARROW, and bare "elec" is deliberately NOT used. "elec"
    // takes 15 names: the 8 above plus ZI_S_dk_elec_a..f (0x4BE-0x4C3, no caller anywhere in
    // the tree) and ZI_S_elecGate_a (0x9F2), which is a dungeon gate (d_a_obj_lv6egate.cpp:192)
    // and not a creature's spark. Widening to "elec" to save two keywords buys one live false
    // positive, so it was not done - counts measured, not estimated.
    //
    // THIS DOES NOT DECIDE WHETHER THEY LIGHT. All 8 were Class::Other before, and Other is
    // ungated exactly as Spark is - both are admitted on the additive-and-glow rule alone. What
    // the reclassification buys is that the report NAMES them (keyword `elecat`/`yb_elec`
    // instead of `-`), that the Spark gate and the Spark hold below reach them, and that the
    // class-split readout counts them. The one thing it does change in the picture is the merge
    // tie-break, 1.0 -> 2.0, and for these 8 that is inert: the four elecAt ids always
    // co-locate with each other on one body joint, and setDigEffect reuses the SAME two emitter
    // handles (field_0xad8/0xadc, d_a_e_ym.cpp:242-243 vs :257-260), so the dig markers and the
    // spark cannot be alive at once to compete for the site.
    if (nameHas(name, "kira") || nameHas(name, "pika") || nameHas(name, "spark") ||
        nameHas(name, "elecat") || nameHas(name, "yb_elec")) {
        return Class::Spark;
    }

    if (nameHas(name, "hikari") || nameHas(name, "light") || nameHas(name, "glow") ||
        nameHas(name, "aura") || nameHas(name, "shine")) {
        return Class::Glow;
    }

    return Class::Other;
}

// Which keyword actually claimed this name, for the report. Deliberately a SECOND pass in the
// same order rather than a refactor of classifyByName into something that returns both: the
// classifier is on the per-frame path and cached, this runs once per distinct effect at report
// time, and keeping them separate means the report cannot slow the classifier down. The order
// below must match classifyByName exactly - if it drifts, the report lies about the class it is
// printing beside.
const char* classKeyword(uint16_t id) {
    const char* name = effectName(id);
    if (name[0] == '\0') {
        return "(unnamed)";
    }
    static const char* const kLava[] = {"lava", "magma", "youdo", "yogan", "yougan", nullptr};
    static const char* const kExcluded[] = {"yoda",     "taieki", "sandworm", "sand",
                                            "suna",     "dust",   nullptr};
    static const char* const kBurst[] = {"bakuha", "explo", "bomb", "baku", nullptr};
    static const char* const kLantern[] = {"kantera", nullptr};
    static const char* const kFire[] = {"fire",   "honoo", "hono",  "kaen",   "flame", "taimatsu",
                                        "maki",   "torch", "ablaze", "kagarib", nullptr};
    static const char* const kSpark[] = {"kira", "pika", "spark", "elecat", "yb_elec", nullptr};
    static const char* const kGlow[] = {"hikari", "light", "glow", "aura", "shine", nullptr};
    static const char* const* const kLists[] = {kLava,    kExcluded, kBurst, kLantern,
                                                kFire,    kSpark,    kGlow};

    for (const char* const* list : kLists) {
        for (const char* const* w = list; *w != nullptr; w++) {
            if (nameHas(name, *w)) {
                return *w;
            }
        }
    }
    return "-";
}

Class cachedClass(uint16_t id) {
    const uint16_t key = id & kIdMask;
    if (s_classCache[key] == 0) {
        s_classCache[key] = static_cast<uint8_t>(classifyByName(id)) + 1;
    }
    return static_cast<Class>(s_classCache[key] - 1);
}

// "Adds light to the frame rather than covering what is behind it."
//
// This is the renderer's own statement that the effect is emissive, and it is the same
// distinction the material rule draws from the other end (aurora docs, remix-material-interface
// section 9: no TEV colour stage reads the rasterized channel). Fire, glow, sparks and lava
// blend additively; smoke, dust, water and splashes do not.
//
// INFERENCE, not measurement: the .jpa assets are not in this repo, so this reads the format's
// semantics rather than this game's authoring. requestReport() exists to settle it from one
// play session.
bool isAdditive(const JPABaseShape* shape) {
    if (shape == nullptr) {
        return false;
    }

    if (shape->getBlendMode() != GX_BM_BLEND) {
        return false;
    }

    // ONE only. A destination factor of ONE is the one configuration that literally adds the
    // draw to what is behind it; SRC_ALPHA or INV_SRC_ALPHA there scale the background down,
    // which is how smoke and spray cover things up. Being permissive here would buy a few more
    // fires at the price of lighting every puff of dust, and the price is worse - a wrong light
    // is visible, a missing one is only dim, and the report says which effects were refused.
    return shape->getBlendDst() == GX_BL_ONE;
}

// Deliberately the same two functions the fork's material self-illumination rule uses
// (rtx_dusklight_emissive.h): chroma is the unnormalized spread between the brightest and
// dimmest channel, luma is Rec.601. The two rules are meant to be the same judgement made in
// two places, so an earlier revision of this file that used normalized chroma and Rec.709 was
// quietly asking a different question with the same words.
float chromaOf(const float c[3]) {
    return std::max(c[0], std::max(c[1], c[2])) - std::min(c[0], std::min(c[1], c[2]));
}

float lumaOf(const float c[3]) {
    return 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
}

// Saturated, or near white hot. Same shape as the material self illumination rule's third
// clause, and thresholds rather than constants for the same reason: it is a judgement about
// this game's palette.
bool readsAsGlow(const float c[3], const Params& params) {
    return chromaOf(c) >= params.minChroma || lumaOf(c) >= params.minLuma;
}

void byteColor(const GXColor& in, float out[3]) {
    out[0] = in.r * (1.0f / 255.0f);
    out[1] = in.g * (1.0f / 255.0f);
    out[2] = in.b * (1.0f / 255.0f);
}

void multiplyColor(const float a[3], const float b[3], float out[3]) {
    out[0] = a[0] * b[0];
    out[1] = a[1] * b[1];
    out[2] = a[2] * b[2];
}

// The effect's colour reaches the TEV in two registers, and which one carries the hue depends
// on how the effect was authored: a fire whose texture is white gets its orange from the
// primary register, one whose texture is already orange often leaves the primary white and
// tints through the environment register. Take whichever has more chroma, and ignore a near
// black environment colour (which means "no tint" rather than "black light").
void pickColor(const float prm[3], const float env[3], float out[3]) {
    const float envLuma = lumaOf(env);
    if (envLuma > 0.05f && chromaOf(env) > chromaOf(prm)) {
        out[0] = env[0];
        out[1] = env[1];
        out[2] = env[2];
        return;
    }

    out[0] = prm[0];
    out[1] = prm[1];
    out[2] = prm[2];
}

// --- what the artists authored ------------------------------------------------------------
//
// TWO DIFFERENT THINGS get called "the emitter's values", and which one is read decides whether
// a derivation is stable enough to put into a light:
//
//   the AUTHORED blocks - JPAResource::getBsp() / getDyn(), parsed once from the .jpa at load
//     (JPABaseShape.cpp:1666-1703, JPADynamicsBlock::init). Nothing in the game tree writes
//     through them. IMMUTABLE for the session.
//   the LIVE emitter fields - seeded from those blocks in JPABaseEmitter::init and then
//     overwritten every frame by JPAResource::calcKey (JPAResource.cpp:1304-1339) and by
//     several hundred actor setters: setRate 119 call sites, setGlobalAlpha 58,
//     setGlobalParticleScale 50, setLifeTime 18, setVolumeSize 12.
//
// Everything below reads the first, and that is the point. A value that animates re-creates the
// Remix light every frame it moves more than 2% and costs that light its temporal history - and
// one class of that animation is not even the effect's own: the shared "simple" emitter is made
// continuous when it is created (d_particle.cpp:807, becomeContinuousParticle) so its colour
// cycle free-runs from level load, and every torch in the world reads the same unrelated phase
// of it.
//
// The live values are still what the ACCEPT TEST reads - see collectEmitters. That is
// deliberate and it is the one place these must not be swapped: the game hides an effect by
// fading its global alpha and its global colour, so judging "is this drawing light right now"
// from the authored colour would light effects that are invisible.

// JPADynamicsBlock.cpp:143-150 - the volume type enum is anonymous and local to that file, so
// the value is repeated here with its citation rather than included. VOL_Point is the one type
// whose volume size means nothing: JPAVolumePoint zeroes the offset outright (:7-13).
constexpr u32 kVolumePoint = 4;

// Direct-mapped, one slot per low 9 bits of the effect id, validated on BOTH the id and the
// resource pointer. A room reload rebinds an id to a different JPAResource, and a collision
// between two ids sharing those bits costs a recompute rather than a wrong answer - which is
// the property worth having, because the wrong answer here is a light in the wrong colour with
// nothing in any log to say so.
constexpr int kAuthoredSlots = 512;

struct Authored {
    const JPAResource* res;
    uint16_t id;
    bool valid;
    bool hasColor;
    bool hasExtent;
    bool persistent;   // authored maxFrame == 0: emits forever rather than for a window
    float color[3];    // hue, already through pickColor
    float extent;      // emitter-local units, 0 when unknown
};

Authored s_authored[kAuthoredSlots];

// Walk one authored colour ramp and return its most saturated entry.
//
// The table is exactly getClrAnmMaxFrm() + 1 entries, allocated at load and pre-interpolated
// per frame by makeColorTable (JPABaseShape.cpp:1541-1583, allocation at :1543), and it is NULL
// unless the matching flag is set (:1686-1702). Taking the most saturated entry rather than the
// first or the mean is what makes a fire that ramps yellow -> orange -> black report as orange:
// the black tail is the particle dying, not the colour of the light.
bool rampColor(const JPABaseShape* bsp, bool anim, const GXColor* table, bool primary,
               float out[3]) {
    if (bsp == nullptr) {
        return false;
    }

    if (!anim || table == nullptr) {
        GXColor c;
        if (primary) {
            bsp->getPrmClr(&c);
        } else {
            bsp->getEnvClr(&c);
        }
        byteColor(c, out);
        return true;
    }

    // Bounded twice: by the authored frame count, and by a cap of its own. maxFrm is an s16 out
    // of a file, so a corrupt or unexpected value must not turn one cache miss into a walk of
    // thirty thousand entries on the frame path.
    const int frames = std::min<int>(std::max<int>(bsp->getClrAnmMaxFrm(), 0), 1024);
    float best[3] = {0.0f, 0.0f, 0.0f};
    float bestChroma = -1.0f;
    float bestLuma = -1.0f;
    for (int i = 0; i <= frames; i++) {
        float c[3];
        byteColor(table[i], c);
        const float chroma = chromaOf(c);
        const float luma = lumaOf(c);
        if (chroma > bestChroma || (chroma == bestChroma && luma > bestLuma)) {
            bestChroma = chroma;
            bestLuma = luma;
            best[0] = c[0];
            best[1] = c[1];
            best[2] = c[2];
        }
    }

    out[0] = best[0];
    out[1] = best[1];
    out[2] = best[2];
    return true;
}

const Authored& authoredFor(uint16_t effectId, const JPABaseEmitter* emitter) {
    const uint16_t key = effectId & kIdMask;
    Authored& slot = s_authored[key % kAuthoredSlots];

    const JPAResource* res = (emitter != nullptr) ? emitter->pRes : nullptr;
    if (slot.valid && slot.id == key && slot.res == res) {
        return slot;
    }

    slot = Authored {};
    slot.res = res;
    slot.id = key;
    slot.valid = true;
    if (res == nullptr) {
        return slot;
    }

    const JPABaseShape* bsp = res->getBsp();
    if (bsp != nullptr) {
        float prm[3] = {1.0f, 1.0f, 1.0f};
        float env[3] = {0.0f, 0.0f, 0.0f};
        const bool haveP =
            rampColor(bsp, bsp->isPrmAnm() != 0, bsp->mpPrmClrAnmTbl, true, prm);
        const bool haveE =
            rampColor(bsp, bsp->isEnvAnm() != 0, bsp->mpEnvClrAnmTbl, false, env);
        if (haveP || haveE) {
            pickColor(prm, env, slot.color);
            slot.hasColor = true;
        }

        // Authored particle size. NOT multiplied by the emitter's live global particle scale,
        // even though the drawn quad is: that scale is actor-driven at 50 call sites and one of
        // them ramps it to zero every frame a fire dies (d_a_e_db.cpp:1871-1877). Folding it in
        // would put an animating term into the radius, which is the exact mistake this whole
        // section exists to avoid.
        const float size = std::max(bsp->getBaseSizeX(), bsp->getBaseSizeY());
        if (std::isfinite(size) && size > 0.0f) {
            slot.extent = size;
            slot.hasExtent = true;
        }
    }

    const JPADynamicsBlock* dyn = res->getDyn();
    if (dyn != nullptr) {
        // maxFrame 0 is the format's "emit forever". This is the AUTHORED window, which is why
        // it is read here and not off the emitter: becomeContinuousParticle forces the live
        // mMaxFrame to 0 on every simple emitter and becomeImmortalEmitter does the same at 42
        // more call sites, so the live field says "persistent" for effects that are not.
        slot.persistent = dyn->getMaxFrame() == 0;

        // The spawn volume, in emitter-local units - it is transformed by the emitter's local
        // and global scale matrices before use (JPAResource.cpp:1342-1355), so this is an
        // extent signal rather than an exact world measurement, and it is only ever allowed to
        // GROW a radius that already has a working default.
        if (dyn->getVolumeType() != kVolumePoint) {
            const float vol = static_cast<float>(dyn->getVolumeSize());
            if (std::isfinite(vol) && vol > slot.extent) {
                slot.extent = vol;
                slot.hasExtent = true;
            }
        }
    }

    return slot;
}


void noteForReport(uint16_t effectId, const JPABaseShape* shape, const float prm[3],
                   const float env[3], Class cls, bool additive, bool glow, bool simple,
                   const JPABaseEmitter* emitter, const float decided[3]) {
    for (int i = 0; i < s_reportCount; i++) {
        if (s_report[i].effectId == effectId) {
            return;
        }
    }

    if (s_reportCount >= kMaxReportEntries) {
        s_reportOverflowed = true;
        return;
    }

    ReportEntry& entry = s_report[s_reportCount++];

    // The measured inputs to the glow test, on the colour pickColor actually chose. Printing
    // these rather than a pass/fail is what makes a refusal actionable: the threshold that
    // would have accepted it is then arithmetic instead of another test session.
    entry.chroma = chromaOf(decided);
    entry.luma = lumaOf(decided);
    entry.keyword = classKeyword(effectId);

    // Animation configuration. Everything here is a public runtime accessor over the loaded
    // resource - no .jpa parsing - and it is what decides whether an effect's colour is capable
    // of animating at all. Guarded hard: mpPrmClrAnmTbl is NULL when the flag is clear, and the
    // whole resource may be absent on a record whose emitter has already gone.
    const JPAResource* res = (emitter != nullptr) ? emitter->pRes : nullptr;
    const JPABaseShape* bsp = (res != nullptr) ? res->getBsp() : nullptr;
    entry.hasRes = (bsp != nullptr);
    entry.glblClrAnm = bsp != nullptr && bsp->isGlblClrAnm() != 0;
    entry.prmAnm = bsp != nullptr && bsp->isPrmAnm() != 0;
    entry.envAnm = bsp != nullptr && bsp->isEnvAnm() != 0;
    entry.anmType = bsp != nullptr ? static_cast<uint8_t>(bsp->getClrAnmType()) : 0xFF;
    entry.anmMaxFrm = bsp != nullptr ? bsp->getClrAnmMaxFrm() : 0;
    entry.baseSizeX = bsp != nullptr ? bsp->getBaseSizeX() : 0.0f;
    entry.baseSizeY = bsp != nullptr ? bsp->getBaseSizeY() : 0.0f;

    // Guarded the way JPAResourceManager::getResUserWork does it (JPAResourceManager.cpp:66-73),
    // plus a getDyn() null check it does not do - the manager can assume a resource it just
    // looked up has a dynamics block; a resource reached through a live emitter may not.
    const JPADynamicsBlock* dyn = (res != nullptr) ? res->getDyn() : nullptr;
    entry.userWork = (dyn != nullptr) ? dyn->getResUserWork() : 0u;

    entry.maxFrame = emitter != nullptr ? emitter->mMaxFrame : 0;
    entry.lifeTime = emitter != nullptr ? emitter->mLifeTime : 0;
    entry.age = emitter != nullptr ? emitter->getAge() : 0;
    entry.particles = emitter != nullptr ? static_cast<int>(emitter->getParticleNumber()) : 0;

    entry.globalScale = 1.0f;
    if (emitter != nullptr) {
        JGeometry::TVec3<f32> pscl;
        emitter->getGlobalParticleScale(&pscl);
        entry.globalScale = pscl.x;
    }

    // Which authored curves land on the emitter. calcKey dispatches on the block's ID and
    // writes emitter fields directly, so this says what CAN animate beyond colour.
    entry.keyIds = 0;
    if (res != nullptr) {
        const int n = res->keyNum;
        for (int k = 0; k < n && k < 16; k++) {
            const JPAKeyBlock* kb = res->ppKey[k];
            if (kb != nullptr) {
                const u8 id = static_cast<u8>(kb->getID());
                // 11, not 8. calcKey writes emitter fields for IDs 0,1,3,4,6,7,8,9,10
                // (JPAResource.cpp:1304-1339) and this mask stopped at 8 until 2026-08-13, so
                // three of them were dropped silently - including ID 10, mScaleOut, an
                // emitter-level scale curve that seeds every new particle (JPAParticle.cpp:96-101).
                // An effect that grows or shrinks over its life showed nothing in this column.
                if (id < 11) {
                    entry.keyIds |= static_cast<uint16_t>(1u << id);
                }
            }
        }
    }

    // The authored side, cached per effect id. Printed beside the live columns rather than
    // instead of them: the pair is what says whether an effect's live colour has drifted from
    // what its artists wrote, which is the whole reason the derivation moved.
    const Authored& authored = authoredFor(effectId, emitter);
    entry.hasAuthoredColor = authored.hasColor;
    for (int i = 0; i < 3; i++) {
        entry.authoredColor[i] = static_cast<uint8_t>(
            std::min(255.0f, std::max(0.0f, authored.color[i] * 255.0f)));
    }
    entry.hasExtent = authored.hasExtent;
    entry.extent = authored.extent;
    entry.persistent = authored.persistent;

    entry.effectId = effectId;
    entry.blendMode = shape != nullptr ? static_cast<uint8_t>(shape->getBlendMode()) : 0xFF;
    entry.blendSrc = shape != nullptr ? static_cast<uint8_t>(shape->getBlendSrc()) : 0xFF;
    entry.blendDst = shape != nullptr ? static_cast<uint8_t>(shape->getBlendDst()) : 0xFF;
    for (int i = 0; i < 3; i++) {
        entry.prm[i] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, prm[i] * 255.0f)));
        entry.env[i] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, env[i] * 255.0f)));
    }
    entry.cls = cls;
    entry.additive = additive;
    entry.glow = glow;
    entry.simple = simple;
}

const char* blendModeName(uint8_t v) {
    switch (v) {
    case GX_BM_NONE: return "NONE";
    case GX_BM_BLEND: return "BLEND";
    case GX_BM_LOGIC: return "LOGIC";
    case GX_BM_SUBTRACT: return "SUBTRACT";
    default: return "?";
    }
}

const char* blendFactorName(uint8_t v) {
    switch (v) {
    case GX_BL_ZERO: return "ZERO";
    case GX_BL_ONE: return "ONE";
    case GX_BL_SRCCLR: return "SRCCLR";
    case GX_BL_INVSRCCLR: return "INVSRCCLR";
    case GX_BL_SRCALPHA: return "SRCALPHA";
    case GX_BL_INVSRCALPHA: return "INVSRCALPHA";
    case GX_BL_DSTALPHA: return "DSTALPHA";
    case GX_BL_INVDSTALPHA: return "INVDSTALPHA";
    default: return "?";
    }
}

// Record one trace line. Called only from the snapshot below, which is the only place that
// knows whether anything changed.
void pushTrace(const TrackedSite& t, uint8_t event, float radiance) {
    TraceEntry& e = s_trace[s_traceHead];
    s_traceHead = (s_traceHead + 1) % kMaxTraceEntries;
    s_traceWritten++;

    e.frame = s_frameCounter;
    e.siteId = t.site.id;
    e.cls = t.site.cls;
    e.effectId = t.site.effectId;
    e.radiance = radiance;
    e.reach = t.reach;
    e.radius = t.site.radius;
    const float brightest =
        std::max(t.site.radiance[0], std::max(t.site.radiance[1], t.site.radiance[2]));
    for (int c = 0; c < 3; c++) {
        const float n = brightest > 0.0f ? t.site.radiance[c] / brightest : 0.0f;
        e.color[c] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, n * 255.0f)));
    }
    e.members = t.site.members;
    e.derived = t.site.derived;
    e.event = event;
}

float siteRadiance(const Site& s) {
    return std::max(s.radiance[0], std::max(s.radiance[1], s.radiance[2]));
}

// Once per frame, at the end of collect(). Two jobs: keep the trace ring fed, and hold a
// snapshot of the live sites so a report pressed at any moment describes the frame it was
// pressed on rather than a half-built intermediate.
void snapshotSites(const std::vector<TrackedSite>& tracked) {
    s_frameCounter++;

    s_siteReportCount = 0;
    for (const TrackedSite& t : tracked) {
        const float rad = siteRadiance(t.site);

        // Find this site's last recorded radiance. Linear over at most kMaxSites, once per
        // site per frame - the same order the clustering already pays.
        int mem = -1;
        for (int i = 0; i < s_traceMemoryCount; i++) {
            if (s_traceMemory[i].siteId == t.site.id) {
                mem = i;
                break;
            }
        }

        if (mem < 0) {
            if (s_traceMemoryCount < kMaxSites) {
                mem = s_traceMemoryCount++;
                s_traceMemory[mem].siteId = t.site.id;
                s_traceMemory[mem].radiance = rad;
            }
            pushTrace(t, 1 /* appeared */, rad);
        } else {
            const float before = s_traceMemory[mem].radiance;
            const float scale = std::max(std::fabs(rad), std::fabs(before));
            if (std::fabs(rad - before) > std::max(0.01f, scale * kTraceRelative)) {
                s_traceMemory[mem].radiance = rad;
                pushTrace(t, 0 /* changed */, rad);
            }
        }

        if (s_siteReportCount < kMaxSiteReport) {
            SiteReportEntry& s = s_siteReport[s_siteReportCount++];
            s.id = t.site.id;
            s.cls = t.site.cls;
            s.effectId = t.site.effectId;
            for (int c = 0; c < 3; c++) {
                s.pos[c] = t.site.position[c];
            }
            s.members = t.site.members;
            s.derived = t.site.derived;
            s.colorFromGame = t.site.colorFromGame;
            s.reach = t.reach;
            s.radius = t.site.radius;
            s.radiance = rad;
            s.vanillaDistance = t.vanillaDistance;
            s.maxJump = t.maxJump;
            s.jumps = t.jumps;
            s.mass = t.mass;
            s.massBoost = t.massBoost;
            s.colorSource = t.colorSource;
            s.radiusAuthored = t.radiusAuthored;
            s.lantern = t.lantern;
        }
    }

    // Forget sites that no longer exist, and say so in the trace. Walking backwards so the
    // swap-erase does not skip an entry.
    for (int i = s_traceMemoryCount; i-- > 0;) {
        bool alive = false;
        for (const TrackedSite& t : tracked) {
            if (t.site.id == s_traceMemory[i].siteId) {
                alive = true;
                break;
            }
        }
        if (!alive) {
            TraceEntry& e = s_trace[s_traceHead];
            s_traceHead = (s_traceHead + 1) % kMaxTraceEntries;
            s_traceWritten++;
            std::memset(&e, 0, sizeof(e));
            e.frame = s_frameCounter;
            e.siteId = s_traceMemory[i].siteId;
            e.event = 2;  // gone

            s_traceMemory[i] = s_traceMemory[--s_traceMemoryCount];
        }
    }
}

const char* anmTypeName(uint8_t v) {
    // JPABaseShape.cpp's five JPACalcClrIdx* variants, in flag order. Merge and Random pin the
    // key frame at 0 on the EMITTER path, so an effect using either cannot animate its colour
    // where we can see it however rich its authored table is.
    switch (v) {
    case 0: return "normal";   // clamps at anmMaxFrm - a one-shot ramp
    case 1: return "repeat";
    case 2: return "reverse";
    case 3: return "merge*";   // * pinned to frame 0 at emitter level
    case 4: return "random*";  // * pinned to frame 0 at emitter level
    default: return "?";
    }
}

// Which authored curves land on the EMITTER (JPAResource::calcKey dispatches on block ID and
// writes emitter fields directly). Anything not listed here animates per particle only, where
// this system cannot see it.
void formatKeyIds(uint16_t mask, char* out, size_t cap) {
    // Eleven, not eight. IDs 8, 9 and 10 were dropped by both this table and the mask that
    // feeds it until 2026-08-13; 10 is the one that mattered, an emitter-level scale curve.
    static const char* const kNames[11] = {"rate", "volsz", "?2",     "volrad", "life",  "?5",
                                           "away", "axis",  "dirspd", "spread", "scale"};
    size_t n = 0;
    out[0] = '\0';
    if (mask == 0) {
        std::snprintf(out, cap, "-");
        return;
    }
    for (int i = 0; i < 11; i++) {
        if ((mask & (1u << i)) == 0) {
            continue;
        }
        const int w = std::snprintf(out + n, cap - n, "%s%s", n ? "," : "", kNames[i]);
        if (w <= 0 || static_cast<size_t>(w) >= cap - n) {
            break;
        }
        n += static_cast<size_t>(w);
    }
}

const char* verdictOf(const ReportEntry& e) {
    // The refusal reason, not just a refusal. Ordered exactly as the accept path tests, so the
    // reason named is the clause that actually stopped it.
    if (!e.additive) {
        return "no(opaque)";
    }
    if (!e.glow) {
        return "no(colour)";
    }
    if (e.cls == Class::Excluded) {
        return "no(name)";
    }
    if (e.cls == Class::Burst) {
        return "LIT-if-bursts";
    }
    return "LIT";
}

// Folds the frame just measured into the since-last-report high-water record. Called once per
// collect, after the counters are final and before any report is emitted, so a report always
// includes the frame it was requested on.
void accumulatePeak() {
    s_peak.frames++;
    auto hi = [](int& dst, int v) { if (v > dst) { dst = v; } };
    hi(s_peak.emitters, s_stats.emitters);
    hi(s_peak.considered, s_stats.considered);
    hi(s_peak.candidates, s_stats.candidates);
    hi(s_peak.sites, s_stats.sites);
    hi(s_peak.culled, s_stats.culled);
    hi(s_peak.excluded, s_stats.excluded);
    hi(s_peak.orphans, s_stats.orphans);
    hi(s_peak.vanillaPoint, s_stats.vanillaPoint);
    hi(s_peak.vanillaSpot, s_stats.vanillaSpot);
    hi(s_peak.droppedCandidates, s_stats.droppedCandidates);
    hi(s_peak.droppedSites, s_stats.droppedSites);
    hi(s_peak.droppedSimple, s_stats.droppedSimple);
    hi(s_peak.sparkSeen, s_stats.sparkSeen);
    hi(s_peak.sparkLit, s_stats.sparkLit);
}

void emitReport(const Params& params) {
    char buf[64];

    Log.info("=== dusklight effect lights: full report ===");
    // Build stamp, so two logs from two builds can be told apart. The first real report had no
    // identifier at all: the only thing distinguishing it from the next build's was the echoed
    // settings line, which happens to carry values this round changed - luck, not design.
    // The protocol number is deliberately NOT duplicated here; the Dusklight tab reports it,
    // and a second copy is a second thing to drift.
    Log.info("build {} {}", __DATE__, __TIME__);
    Log.info("One press answers every open question. Five sections: counters, effects, sites, "
             "game lights, trace. Sections are capped and say so when they truncate.");

    // ---- 1. counters -------------------------------------------------------------------
    Log.info("[counters] the chain, in the order a light can be lost.");
    Log.info("  THREE TIME BASES, and mixing them up has already cost a reading. Each line says "
             "which it is.");

    Log.info("  THIS FRAME - the single frame the button was pressed on, nothing more:");
    Log.info("    emitters {} -> considered {} -> candidates {} -> sites {} -> drawn {}",
             s_stats.emitters, s_stats.considered, s_stats.candidates, s_stats.sites,
             s_bridgeDrawn);
    Log.info("    derived {}  colourFromGame {}  orphans {}  culled {}  excluded {}",
             s_stats.derived, s_stats.colorFromGame, s_stats.orphans, s_stats.culled,
             s_stats.excluded);
    Log.info("    game lights available (point/spot) {}/{}", s_stats.vanillaPoint,
             s_stats.vanillaSpot);
    // Where this frame's sites got their values. This is the "authored versus defaulted"
    // question asked of the whole frame at once, so a look change can be attributed before
    // anybody reads a single site line.
    Log.info("    values taken from: colour authored {} of {} sites   radius authored {}   "
             "lantern solved separately {}",
             s_stats.authoredColor, s_stats.sites, s_stats.authoredRadius, s_stats.lantern);
    {
        char classes[192];
        size_t n = 0;
        classes[0] = '\0';
        for (int c = 0; c < static_cast<int>(Class::Count); c++) {
            const int w = std::snprintf(classes + n, sizeof(classes) - n, "%s%s %d",
                                        n ? "  " : "", className(static_cast<Class>(c)),
                                        s_stats.byClass[c]);
            if (w <= 0 || static_cast<size_t>(w) >= sizeof(classes) - n) {
                break;
            }
            n += static_cast<size_t>(w);
        }
        Log.info("    sites by class: {}", classes);
        Log.info("    (excl never appears here - an excluded effect never becomes a site. A "
                 "lantrn count of 0 while Link's lamp is lit means the lantern's emitter failed "
                 "the rule, not that the class is wrong.)");
    }
    Log.info("    drawn is the PREVIOUS frame's count. This report is emitted before the bridge "
             "draws, so this frame's is still zero at this point - it used to print 0 always.");

    Log.info("  PEAK SINCE THE LAST PRESS - the highest each reached over {} frames. Press at a "
             "calm moment and the line above reads zero while these do not:", s_peak.frames);
    Log.info("    emitters {}  considered {}  candidates {}  sites {}  culled {}  excluded {}  "
             "orphans {}",
             s_peak.emitters, s_peak.considered, s_peak.candidates, s_peak.sites, s_peak.culled,
             s_peak.excluded, s_peak.orphans);
    Log.info("    game lights available (point/spot) {}/{}", s_peak.vanillaPoint,
             s_peak.vanillaSpot);
    Log.info("    dropped: candidates {}  sites {}  simple {}   (hard array limits, not the "
             "budget - a non-zero here means lights went missing with every other counter "
             "healthy. simple counts records recordSimple refused because its {}-slot table was "
             "already full that frame, so a non-zero one means torches on the simple-effect path "
             "went unlit.)",
             s_peak.droppedCandidates, s_peak.droppedSites, s_peak.droppedSimple,
             kMaxSimpleRecords);
    // Class::Spark reached the rule / earned a light, peak and this frame. This is the line that
    // settles the shadow insect on its own, so it says how to read itself: the bug's spark is
    // ZI_S_ym_elecAt_a..d (0x393-0x396) and the big one's is ZI_S_yb_elec_a..d (0x630-0x633),
    // and every one of those ids has its own row with a verdict in the [effects] section below.
    Log.info("    spark: seen {} lit {} (this frame {} / {})   gate {}  hold +{} frames on top "
             "of the {}-frame base grace",
             s_peak.sparkSeen, s_peak.sparkLit, s_stats.sparkSeen, s_stats.sparkLit,
             params.sparks ? "on" : "OFF", params.sparkHold, kSiteGraceFrames);
    Log.info("      seen counts Spark emitters the game was DRAWING, lit counts those the "
             "additive-and-glow rule then accepted. seen>0 with lit==0 is the negative result: "
             "the shadow insect sparked in view and the rule refused it, which is a property of "
             "the .jpa's blend/colour and NOT something a classification change can fix - read "
             "the 0x393-0x396 rows' verdict for which clause said no. seen==0 means none was "
             "ever in view (or its draw group is filtered), a different question entirely.");

    Log.info("  SINCE LAUNCH - never reset, so compare two presses by subtracting:");
    Log.info("    bridge: creates {}  destroys {}   (creates is per light UPDATE, not per light "
             "- an animating light re-creates every frame it changes by more than 2%)",
             s_bridgeCreates, s_bridgeDestroys);
    Log.info("  settings: maxLights {}  maxDistance {:.0f}  mergeRadius {:.0f}  adoptRadius "
             "{:.0f}  bursts {}  minChroma {:.2f}  minLuma {:.2f}",
             params.maxLights, params.maxDistance, params.mergeRadius, params.adoptRadius,
             params.bursts ? "on" : "off", params.minChroma, params.minLuma);
    // The whole chain from an authored value to a radiance, in the order it is applied, with
    // this session's numbers substituted in. Printing it here rather than only in the document
    // is what lets a log answer "is a multiplier being applied twice" on its own.
    Log.info("  the chain, authored value -> radiance, in the order it is applied:");
    Log.info("    reach    = (game mPow | undetermined {:.0f}) x mass^{:.2f} x reachScale {:.2f}",
             params.undeterminedReach, params.massExponent, params.reachScale);
    Log.info("    radius   = (derived {:.1f} | undetermined {:.1f}, grown to the authored extent "
             "when authoredRadius is on: {}) x radiusScale {:.2f}",
             params.derivedRadius, params.undeterminedRadius,
             params.authoredRadius ? "on" : "off", params.radiusScale);
    Log.info("    radiance = reach^2 x {:.2f} / (pi x radius^2) x (derived {:.2f} | undetermined "
             "{:.2f}) x intensity {:.2f}",
             kNewLightEndValue, params.derivedIntensity, params.undeterminedIntensity,
             params.intensity);
    Log.info("    hue      = adopted game light, else the authored ramp when authoredColor is "
             "on ({}), else the live registers. Radiance is normalised by its brightest channel, "
             "so the hue never changes the brightness.",
             params.authoredColor ? "on" : "off");
    Log.info("    lantern  = separate {}: when on, a lantrn site takes reach {:.0f} radius {:.1f} "
             "intensity {:.2f} RAW - no mass boost and none of the three multipliers above. When "
             "off it goes through the same chain as every other fire.",
             params.lanternSeparate ? "ON" : "off", params.lanternReach, params.lanternRadius,
             params.lanternIntensity);
    if (s_stats.culled > 0) {
        Log.info("  NOTE culled > 0: the per-frame budget IS binding, so sites are competing. A "
                 "site the budget drops is destroyed and comes back with a new id and no "
                 "temporal history.");
    }

    // ---- 2. effects --------------------------------------------------------------------
    Log.info("[effects] {} distinct effects seen since the last report{}", s_reportCount,
             s_reportOverflowed ? "  (CAPPED)" : "");
    Log.info("  id name | blend mode/src/dst | prm rgb | env rgb | chroma luma | class(keyword) "
             "| verdict | AUTHORED rgb extent persist | anim: glbl/prm/env type maxfrm | "
             "maxFrame life age ptcls | size gscale | uw | keys");
    Log.info("  AUTHORED is what the artists wrote into the .jpa and it CANNOT change while you "
             "play: the rgb is the most saturated entry of the effect's own colour ramp, extent "
             "is its particle size or spawn volume in emitter-local units, and persist=Y means "
             "the authored emission window is 0 - it emits forever. Compare the authored rgb "
             "against the prm/env columns beside it: those are the LIVE registers, which carry "
             "the time-of-day tint and any colour animation, and a large difference between the "
             "two is exactly what the authored-colour setting is choosing between.");
    Log.info("  persist is read from the AUTHORED dynamics block, never from the live maxFrame "
             "column further right - becomeContinuousParticle forces that one to 0 on every "
             "simple effect, so the live column says 'forever' for things that are not.");
    Log.info("  verdict: LIT drawn. no(opaque) failed the additive test. no(colour) was additive "
             "but chroma < {:.2f} AND luma < {:.2f}. no(name) refused as a substance. "
             "LIT-if-bursts is one-shot, off by default.",
             params.minChroma, params.minLuma);
    Log.info("  anim: colour animates ONLY when glbl=1 AND prm=1 AND type is normal/repeat/"
             "reverse. A starred type is pinned to frame 0 at emitter level and never moves.");
    Log.info("  uw is the effect's authored resUserWork word, RAW AND UNINTERPRETED. Bits the "
             "game itself acts on: 0x80 -> draw group 13 (drawFogScreen), 0x1000 -> group 12 "
             "(drawDarkworld), 0x2000 -> group 14 (draw2Dgame); 0x400/0x800 attach the gen_b/"
             "gen_d Light8 draw callbacks; 0x20/0x40 pick a kankyo tint source but ONLY when "
             "neither 0x400 nor 0x800 is set. Nothing here decodes it - read the bits, do not "
             "trust a summary of them.");
    Log.info("  NOTE an effect in group 13 or 14 is never censused here at all (this walk skips "
             "groups >= 13), so a 0x80 or 0x2000 word can only ever appear on an effect that "
             "reached us some other way.");

    for (int i = 0; i < s_reportCount; i++) {
        const ReportEntry& e = s_report[i];
        formatKeyIds(e.keyIds, buf, sizeof(buf));
        Log.info("  {:#06x} {:<32} | {}/{}/{} | {:3},{:3},{:3} | {:3},{:3},{:3} | {:.2f} {:.2f} | "
                 "{:<6}({:<8}) | {:<13} | {:3},{:3},{:3} {:6} {} | {}/{}/{} {:<7} {:<4} | "
                 "{:<5} {:<5} {:<5} {:<4} | {:.0f}x{:.0f} {:.2f} | uw={:#010x} | {}{}",
                 e.effectId, effectName(e.effectId), blendModeName(e.blendMode),
                 blendFactorName(e.blendSrc), blendFactorName(e.blendDst), e.prm[0], e.prm[1],
                 e.prm[2], e.env[0], e.env[1], e.env[2], e.chroma, e.luma, className(e.cls),
                 e.keyword, verdictOf(e),
                 e.hasAuthoredColor ? e.authoredColor[0] : 0,
                 e.hasAuthoredColor ? e.authoredColor[1] : 0,
                 e.hasAuthoredColor ? e.authoredColor[2] : 0,
                 e.hasExtent ? e.extent : 0.0f, e.persistent ? "Y" : "n",
                 e.glblClrAnm ? 1 : 0, e.prmAnm ? 1 : 0,
                 e.envAnm ? 1 : 0, e.hasRes ? anmTypeName(e.anmType) : "?", e.anmMaxFrm,
                 e.maxFrame, e.lifeTime, e.age, e.particles, e.baseSizeX, e.baseSizeY,
                 e.globalScale, e.userWork, buf, e.simple ? " (simple)" : "");
    }

    if (s_reportOverflowed) {
        Log.info("  effects capped at {}; later ones were dropped. Ask again - the memory is "
                 "cleared below, so the next report covers what is seen from now on.",
                 kMaxReportEntries);
    }

    // ---- 3. sites ----------------------------------------------------------------------
    Log.info("[sites] {} live this frame{}", s_siteReportCount,
             s_siteReportCount >= kMaxSiteReport ? "  (CAPPED)" : "");
    Log.info("  id class effect | position | members | derived colour=SOURCE radius=SOURCE | "
             "reach radius radiance | adoptDist | maxJump jumps | mass boost");
    Log.info("  members > 1 means several emitters merged into one light. adoptDist -1 means it "
             "adopted nothing and is on the configured fallback.");
    Log.info("  colour= says which of THREE sources this site's hue came from. game: a light the "
             "game itself registered nearby, which always wins. authored: the effect's own "
             "authored colour ramp, fixed for the session. live: the emitter's current "
             "registers, which is what every site used before 2026-08-13 and what they all fall "
             "back to when the authored setting is off or the resource carried no colour.");
    Log.info("  radius= says whether the sphere grew to the effect's authored extent (authored) "
             "or stayed on the configured value for its branch (default). LANTERN on a line "
             "means that site was solved from the lantern's own reach, radius and intensity, "
             "with no global multiplier and no mass boost applied to it.");
    Log.info("  maxJump is the largest single-frame move this site has made, in world units, and "
             "jumps counts the frames it moved more than {:.0f}. A light that follows an actor "
             "shows a small maxJump and jumps 0; a light snapping between emitters shows a "
             "maxJump near mergeRadius and a jumps count that keeps climbing. That is what a "
             "stuttering shadow looks like from here.", kJumpNotable);
    for (int i = 0; i < s_siteReportCount; i++) {
        const SiteReportEntry& s = s_siteReport[i];
        Log.info("  {:<4} {:<6} {:#06x} | {:8.0f} {:8.0f} {:8.0f} | {:2} | {} colour={:<8} "
                 "radius={:<8}{} | {:7.1f} {:5.1f} {:9.2f} | {:8.1f} | {:7.1f} {:<6} | {:5.2f} "
                 "{:5.2f}",
                 s.id, className(s.cls), s.effectId, s.pos[0], s.pos[1], s.pos[2], s.members,
                 s.derived ? "derived  " : "undetermd", colorSourceName(s.colorSource),
                 s.radiusAuthored ? "authored" : "default", s.lantern ? " LANTERN" : "",
                 s.reach, s.radius, s.radiance, s.vanillaDistance, s.maxJump, s.jumps, s.mass,
                 s.massBoost);
    }

    // ---- 4. the game's own lights ------------------------------------------------------
    Log.info("[game lights] {} registered this frame{}", s_vanillaReportCount,
             s_vanillaReportCount >= kMaxVanillaReport ? "  (CAPPED)" : "");
    Log.info("  position | colour | reach | list | adopted by effect (adoption is EXCLUSIVE - "
             "one light, one site - so a short-lived effect can take a torch's light and leave "
             "the torch on the fallback)");
    for (int i = 0; i < s_vanillaReportCount; i++) {
        const VanillaReportEntry& v = s_vanillaReport[i];
        if (v.adoptedByEffect != 0) {
            Log.info("  {:8.0f} {:8.0f} {:8.0f} | {:3},{:3},{:3} | {:7.1f} | {:<5} | {:#06x} {} "
                     "at {:.1f}",
                     v.pos[0], v.pos[1], v.pos[2], v.color[0], v.color[1], v.color[2], v.reach,
                     v.spot ? "spot" : "point", v.adoptedByEffect,
                     effectName(v.adoptedByEffect), v.adoptDistance);
        } else {
            Log.info("  {:8.0f} {:8.0f} {:8.0f} | {:3},{:3},{:3} | {:7.1f} | {:<5} | ORPHAN "
                     "(no effect near it - this light is being dropped)",
                     v.pos[0], v.pos[1], v.pos[2], v.color[0], v.color[1], v.color[2], v.reach,
                     v.spot ? "spot" : "point");
        }
    }

    // ---- 5. the trace ------------------------------------------------------------------
    const int traced = static_cast<int>(
        s_traceWritten < static_cast<uint32_t>(kMaxTraceEntries)
            ? s_traceWritten
            : static_cast<uint32_t>(kMaxTraceEntries));
    Log.info("[trace] last {} changes, oldest first (of {} ever recorded, ring holds {})", traced,
             s_traceWritten, kMaxTraceEntries);
    Log.info("  This is RETROSPECTIVE - do the thing first, then press the button. A line is "
             "written only when a site appears, goes, or its radiance moves more than {:.0f}%, "
             "so a steady torch is one line and an explosion is many.",
             kTraceRelative * 100.0f);
    Log.info("  frame | event | site class effect | radiance | reach radius | colour | members");
    const int first = (s_traceWritten < static_cast<uint32_t>(kMaxTraceEntries))
                          ? 0
                          : s_traceHead;
    for (int k = 0; k < traced; k++) {
        const TraceEntry& e = s_trace[(first + k) % kMaxTraceEntries];
        const char* ev = e.event == 1 ? "appear" : (e.event == 2 ? "gone  " : "change");
        if (e.event == 2) {
            Log.info("  {:6} | {} | {:<4}", e.frame, ev, e.siteId);
        } else {
            Log.info("  {:6} | {} | {:<4} {:<5} {:#06x} | {:9.2f} | {:7.1f} {:5.1f} | "
                     "{:3},{:3},{:3} | {:2}{}",
                     e.frame, ev, e.siteId, className(e.cls), e.effectId, e.radiance, e.reach,
                     e.radius, e.color[0], e.color[1], e.color[2], e.members,
                     e.derived ? " derived" : "");
        }
    }

    Log.info("=== end of report ===");

    // Start the effect memory again. Without this the table fills once and then caps for the
    // rest of the session, so the effect you walked up to specifically to ask about is the one
    // missing from the answer. Each request covers everything seen since the last one.
    //
    // The TRACE is deliberately NOT cleared: it is a rolling window, and clearing it would make
    // two presses in a row lose the very thing the second press was asking about.
    s_reportCount = 0;
    // The peak record covers "since the last press", so it resets with the effects table. The
    // frame counter goes with it, since it is that window's denominator.
    s_peak = StatsPeak();
    s_reportOverflowed = false;
}

// --- the class table -------------------------------------------------------------------
//
// One row per Class, and the ONLY place any of the three things a class decides is written
// down. It replaced three separate switch statements that had to be kept in step by hand and
// had already drifted once (the enum's own comment described a fallback-radius rule that no
// branch implemented).
//
// There is deliberately no per-class brightness column. Nothing the artists authored is a
// brightness, so a per-class radiance number would be a number this project invented, and
// inventing one and then citing the class table for it is precisely how a plausible mechanism
// gets recorded as a finding. Brightness comes from the game's own LIGHT_INFLUENCE::mPow where
// there is one, from the undetermined settings where there is not, and - for exactly one
// class - from the lantern's own settings.
enum OffsetSource : uint8_t { kOffsetNone = 0, kOffsetFire, kOffsetGlow };

struct ClassProfile {
    const char* name;      // what the report and the readouts print
    float weight;          // merge tie-break; ties fall through to the lower effect id
    uint8_t offset;        // which of the two configured vertical offsets it takes
};

// Indexed by Class. Order must match the enum in effect_lights.hpp.
const ClassProfile kClassTable[static_cast<int>(Class::Count)] = {
    /* Other    */ {"other",  1.0f, kOffsetNone},
    /* Fire     */ {"fire",   4.0f, kOffsetFire},
    // Same weight as Fire while the lantern is not separated, so that a lantern standing in a
    // bonfire merges exactly as it did when it was Class::Fire. classWeight adds the bump that
    // makes it lead only when the toggle is on and the site is therefore meant to be the
    // lantern's.
    /* Lantern  */ {"lantrn", 4.0f, kOffsetFire},
    /* Glow     */ {"glow",   2.0f, kOffsetGlow},
    // Deliberately identical to Glow: the split exists to be seen and to be tunable later, not
    // to reorder merges today. Giving it a lower weight would change which member donates the
    // colour in a mixed cluster, which is a look change bought for nothing.
    /* Spark    */ {"spark",  2.0f, kOffsetGlow},
    /* Lava     */ {"lava",   3.0f, kOffsetNone},
    /* Burst    */ {"burst",  1.5f, kOffsetFire},
    /* Excluded */ {"excl",   1.0f, kOffsetNone},
};

const ClassProfile& profileOf(Class cls) {
    const int i = static_cast<int>(cls);
    return kClassTable[(i >= 0 && i < static_cast<int>(Class::Count)) ? i : 0];
}

float classOffset(Class cls, const Params& params) {
    switch (profileOf(cls).offset) {
    case kOffsetFire: return params.fireOffset;
    case kOffsetGlow: return params.glowOffset;
    default: return 0.0f;
    }
}

// Weight decides which member of a merged site donates its position and colour. A core fire
// beats a glow beats anything else; equal weights fall through to the lower effect id, which is
// what keeps the choice stable frame to frame.
float classWeight(Class cls, const Params& params) {
    const float w = profileOf(cls).weight;
    // The lantern outranks every other flame only while it is being solved separately. If it
    // did not, Link standing at a bonfire would hand the site to the bonfire and his own
    // settings would do nothing at the one moment he can see them.
    if (cls == Class::Lantern && params.lanternSeparate) {
        return w + 1.0f;
    }
    return w;
}

// Radiance for a sphere light standing in for a light that was meant to reach `reach` units.
//
// Deliberately the same solve the retired local light mirror used (Remix's own
// LightUtils::calculateIntensity, inverted): work out the radiance a sphere of this radius
// needs to still be perceptible at that distance. Keeping it identical is what let the
// numbers tuned for that path carry over unchanged when it was removed at protocol 17.
float solveIntensity(float reach, float radius, float scale) {
    if (reach <= 0.0f || radius <= 0.0001f) {
        return 0.0f;
    }
    return (reach * reach) * kNewLightEndValue / (kPi * radius * radius) * scale;
}

// Returns false when the fixed array is full, so the caller can stop counting a candidate that
// never reached clustering. It used to return void and the counter was incremented by the caller
// regardless, which made `candidates` an overcount in exactly the crowded scenes where the drop
// mattered.
bool addCandidate(Candidate* candidates, int& count, const Candidate& c) {
    if (count >= kMaxCandidates) {
        s_stats.droppedCandidates++;
        return false;
    }
    candidates[count++] = c;
    return true;
}

// True when this emitter is the one shared instance the game reuses for a "simple" effect.
// dPa_control_c::getSimple looks the effect id up in its own table of shared callbacks
// (src/d/d_particle.cpp:1680) and each of those owns exactly one emitter, so comparing the two
// identifies the shared one without reaching into the class's private members.
bool isSharedSimpleEmitter(uint16_t effectId, const JPABaseEmitter* emitter) {
    dPa_control_c* particle = g_dComIfG_gameInfo.play.getParticle();
    if (particle == nullptr) {
        return false;
    }

    const dPa_simpleEcallBack* simple = particle->getSimple(effectId);
    return simple != nullptr && simple->mEmitter == emitter;
}

// Is this emitter actually putting light into the world right now?
//
// ONE implementation, called from both collectors. It used to be four inline tests in
// collectEmitters and NOTHING at all in collectSimple, and that asymmetry was a real defect
// rather than an oversight in style: the game hides an effect by setting the shared emitter's
// alpha to zero and calling stopDrawParticle (dPa_fsenthPcallBack, src/d/d_particle.cpp:1955),
// and collectSimple looked at neither. Wolf-only dig markers - invisible in human form - were
// lighting the ground in Hyrule Field because of it (reported in game 2026-08-07).
//
// The scale test is the newest and has the same shape. d_a_e_db hides the Deku Baba's drool by
// ramping the emitter's global particle scale to zero (d_a_e_db.cpp:1871-1877, fed to
// setGlobalSRTMatrix at :1894) while leaving alpha at 0xFF for the emitter's whole life. A
// zero-size particle draws nothing; treating it as a light source is the same mistake as
// ignoring alpha, one field along.
bool emitterIsLive(const JPABaseEmitter* emitter, const Params& params) {
    if (emitter == nullptr) {
        return false;
    }

    // The game's own on/off. This one test is why the lantern needs no special case:
    // daAlink_c::setLight gates the flame on the oil meter and calls stopDrawParticle when it
    // runs out (src/d/actor/d_a_alink.cpp:14874).
    if (emitter->checkStatus(JPAEmtrStts_StopDraw) || emitter->checkStatus(JPAEmtrStts_Delete)) {
        return false;
    }

    if (emitter->getParticleNumber() == 0) {
        return false;
    }

    if (emitter->getGlobalAlpha() * (1.0f / 255.0f) < params.minAlpha) {
        return false;
    }

    JGeometry::TVec3<f32> pscl;
    emitter->getGlobalParticleScale(&pscl);
    if (std::fabs(pscl.x) < kMinParticleScale && std::fabs(pscl.y) < kMinParticleScale) {
        return false;
    }

    return true;
}

// --- source 1: the emitter table -------------------------------------------------------
//
// Every level and one-shot emitter in the world is <= 250 pointers away, hanging off the
// manager's per group in-use lists. No actor iteration, no hooks in 800 files, no chance of
// missing one.
void collectEmitters(const Params& params, Candidate* candidates, int& count) {
    JPAEmitterManager* mgr = dPa_control_c::getEmitterManager();
    if (mgr == nullptr || mgr->pEmtrUseList == nullptr) {
        return;
    }

    for (u8 group = 0; group < mgr->gidMax; group++) {
        if (group >= kFirstScreenGroup) {
            continue;  // 2D, menu and full screen passes have no world position
        }

        for (JSULink<JPABaseEmitter>* link = mgr->pEmtrUseList[group].getFirst();
             link != mgr->pEmtrUseList[group].getEnd(); link = link->getNext()) {
            JPABaseEmitter* emitter = link->getObject();
            if (emitter == nullptr || emitter->pRes == nullptr) {
                continue;
            }

            s_stats.emitters++;

            const uint16_t sweptId = emitter->pRes->getUsrIdx();

            // A shared "simple" emitter is teleported around the world once per frame, so its
            // position here is whichever instance happened to be last. Those instances come in
            // through recordSimple() instead; skip the shared emitter so it is not counted
            // twice and so a torch is not lit at another torch's position.
            if (isSharedSimpleEmitter(sweptId, emitter)) {
                continue;
            }

            if (!emitterIsLive(emitter, params)) {
                continue;
            }

            // Re-read after the gate: alpha is a liveness test above and a merge WEIGHT here,
            // so a fainter layer of the same fire contributes less to where the site lands.
            // (collectSimple does not do this - it has no per-instance alpha to use, only the
            // shared emitter's. Noted rather than "fixed": making them match would change which
            // member wins a merged site, which is a behaviour change and not this one.)
            const float alpha = emitter->getGlobalAlpha() * (1.0f / 255.0f);

            s_stats.considered++;

            const uint16_t effectId = sweptId;
            const Class cls = cachedClass(effectId);

            GXColor prmByte;
            GXColor envByte;
            GXColor globalPrm;
            GXColor globalEnv;
            prmByte = emitter->mPrmClr;
            envByte = emitter->mEnvClr;
            globalPrm = emitter->mGlobalPrmClr;
            globalEnv = emitter->mGlobalEnvClr;

            float prm[3];
            float env[3];
            float gp[3];
            float ge[3];
            byteColor(prmByte, prm);
            byteColor(envByte, env);
            byteColor(globalPrm, gp);
            byteColor(globalEnv, ge);
            multiplyColor(prm, gp, prm);
            multiplyColor(env, ge, env);

            const JPABaseShape* shape = emitter->pRes->getBsp();
            const bool additive = isAdditive(shape);

            float color[3];
            pickColor(prm, env, color);
            const bool glow = readsAsGlow(color, params);

            noteForReport(effectId, shape, prm, env, cls, additive, glow, false, emitter,
                          color);

            // Counted BEFORE the rule, so that "the shadow insect was sparking in front of the
            // camera and the rule refused it" is a readable outcome rather than an absence.
            if (cls == Class::Spark) {
                s_stats.sparkSeen++;
            }

            if (!additive || !glow) {
                continue;
            }

            // AFTER the report, deliberately. This exclusion is the one the report exists to
            // settle - "is a two frame flash right for this game's bombs" - and excluding
            // bursts before recording them meant the default hid every explosion from the log
            // that was supposed to decide it. The same reasoning applies to Excluded.
            if (cls == Class::Excluded) {
                s_stats.excluded++;
                continue;
            }

            if (cls == Class::Burst && !params.bursts) {
                continue;
            }

            if (cls == Class::Spark) {
                if (!params.sparks) {
                    continue;
                }
                s_stats.sparkLit++;
            }

            Candidate c;
            JGeometry::TVec3<f32> origin;
            emitter->calcEmitterGlobalPosition(&origin);
            c.pos[0] = origin.x;
            c.pos[1] = origin.y;
            c.pos[2] = origin.z;
            c.color[0] = color[0];
            c.color[1] = color[1];
            c.color[2] = color[2];
            c.weight = alpha;
            c.cls = cls;
            c.effectId = effectId;

            // Authored hue and extent, from the loaded resource rather than from the emitter's
            // live registers. Read AFTER the accept test above on purpose: the test asks "is
            // this drawing light right now", which only the live colour can answer.
            {
                const Authored& a = authoredFor(effectId, emitter);
                c.hasAuthoredColor = a.hasColor;
                c.authoredColor[0] = a.color[0];
                c.authoredColor[1] = a.color[1];
                c.authoredColor[2] = a.color[2];
                c.hasExtent = a.hasExtent;
                c.extent = a.extent;
            }

            if (!isFinite3(c.pos)) {
                continue;
            }

            if (addCandidate(candidates, count, c)) {
                s_stats.candidates++;
            }
        }
    }
}

// --- source 2: the simple effect records -----------------------------------------------
void collectSimple(const Params& params, Candidate* candidates, int& count) {
    for (int i = 0; i < s_simpleCount; i++) {
        const SimpleRecord& rec = s_simple[i];

        s_stats.emitters++;

        // The same liveness gates the sweep applies. rec.emitter is the SHARED emitter for this
        // effect id, and that is exactly the right object to ask: the game turns a simple effect
        // off by acting on the shared emitter, so its StopDraw / alpha / particle count / scale
        // are the game's statement about every instance of it this frame.
        if (!emitterIsLive(rec.emitter, params)) {
            continue;
        }

        s_stats.considered++;

        const Class cls = cachedClass(rec.effectId);

        const JPABaseShape* shape =
            (rec.emitter != nullptr && rec.emitter->pRes != nullptr) ? rec.emitter->pRes->getBsp()
                                                                     : nullptr;
        const bool additive = isAdditive(shape);

        // Multiply the RESOURCE colour in, exactly as the sweep does. Without this the colour
        // tested here is the caller's global alone - and all 27 dComIfGp_particle_setSimple call
        // sites in the game pass g_whiteColor, so it was literally (1,1,1) every time: chroma
        // 0.00, luma 1.00, readsAsGlow unconditionally true. The colour half of the rule was a
        // no-op on this entire path and isAdditive was deciding alone, which is not what any of
        // the design says. The two paths now ask the same question of the same colour.
        float prm[3];
        float env[3];
        if (rec.emitter != nullptr) {
            float rp[3];
            float re[3];
            byteColor(rec.emitter->mPrmClr, rp);
            byteColor(rec.emitter->mEnvClr, re);
            multiplyColor(rp, rec.prm, prm);
            multiplyColor(re, rec.env, env);
        } else {
            for (int k = 0; k < 3; k++) {
                prm[k] = rec.prm[k];
                env[k] = rec.env[k];
            }
        }

        float color[3];
        pickColor(prm, env, color);
        const bool glow = readsAsGlow(color, params);

        noteForReport(rec.effectId, shape, prm, env, cls, additive, glow, true, rec.emitter,
                      color);

        // Same two counters as the sweep, and they must exist on BOTH paths: collectSimple
        // applying none of the sweep's gates is exactly the defect that had wolf-only dig
        // markers lighting Hyrule Field on 2026-08-07.
        if (cls == Class::Spark) {
            s_stats.sparkSeen++;
        }

        if (!additive || !glow) {
            continue;
        }

        // AFTER the report, like the Burst test below it, so an exclusion that turns out to be
        // wrong is visible in the log rather than silent.
        if (cls == Class::Excluded) {
            s_stats.excluded++;
            continue;
        }

        if (cls == Class::Burst && !params.bursts) {
            continue;
        }

        if (cls == Class::Spark) {
            if (!params.sparks) {
                continue;
            }
            s_stats.sparkLit++;
        }

        Candidate c;
        c.pos[0] = rec.pos[0];
        c.pos[1] = rec.pos[1];
        c.pos[2] = rec.pos[2];
        c.color[0] = color[0];
        c.color[1] = color[1];
        c.color[2] = color[2];
        c.weight = 1.0f;
        c.cls = cls;
        c.effectId = rec.effectId;

        // The same authored read as the sweep. rec.emitter is the SHARED emitter for this
        // effect id, and its RESOURCE is the right thing to ask - every instance of a simple
        // effect is drawn from that one resource, so the authored colour is the same for all of
        // them where the shared emitter's live colour is a free-running cycle none of them
        // started (d_particle.cpp:807).
        {
            const Authored& a = authoredFor(rec.effectId, rec.emitter);
            c.hasAuthoredColor = a.hasColor;
            c.authoredColor[0] = a.color[0];
            c.authoredColor[1] = a.color[1];
            c.authoredColor[2] = a.color[2];
            c.hasExtent = a.hasExtent;
            c.extent = a.extent;
        }

        if (!isFinite3(c.pos)) {
            continue;
        }

        if (addCandidate(candidates, count, c)) {
            s_stats.candidates++;
        }
    }
}

// --- the vanilla lights, as a parameter source ------------------------------------------
//
// We keep every one of the game's authored NUMBERS and throw away its POSITION. That is the
// whole trade: the artists were right about how orange a torch is and how far it reaches, and
// wrong - for a path tracer, which casts a real shadow from the exact point the light occupies
// - about where to put it.
struct VanillaLight {
    float pos[3];
    float color[3];
    float reach;      // world units; only meaningful when reachKnown
    bool reachKnown;
    bool adopted;
    bool spot;                 // came from the BOSS_LIGHT list rather than pointlight/efplight
    uint16_t adoptedByEffect;  // effect id of the cluster that took it, 0 until then
    float adoptDistance;       // -1 until then
};

int gatherVanillaLights(VanillaLight* out, int cap) {
    const dScnKy_env_light_c* env = dKy_getEnvlight();
    if (env == nullptr) {
        return 0;
    }

    int n = 0;

    const auto push = [&](const cXyz& pos, float r, float g, float b, float reach,
                          bool reachKnown, bool spot) {
        if (n >= cap) {
            return;
        }
        const float brightest = std::max(r, std::max(g, b));
        if (brightest <= 0.0f) {
            return;
        }
        VanillaLight& v = out[n];
        v.pos[0] = pos.x;
        v.pos[1] = pos.y;
        v.pos[2] = pos.z;
        v.color[0] = r / 255.0f;
        v.color[1] = g / 255.0f;
        v.color[2] = b / 255.0f;
        v.reach = reach;
        v.reachKnown = reachKnown;
        v.adopted = false;
        v.spot = spot;
        v.adoptedByEffect = 0;
        v.adoptDistance = -1.0f;
        if (isFinite3(v.pos)) {
            n++;
        }
    };

    // The point list. mPow is a radius in world units - dKy_light_influence_id treats "closer
    // than mPow" as "inside this light" (src/d/d_kankyo.cpp:924), and the actor shading fades
    // linearly to nothing at exactly mPow (src/d/d_kankyo.cpp:3536). So this one is a reach.
    for (int i = 0; i < 100; i++) {
        const LIGHT_INFLUENCE* l = env->pointlight[i];
        if (l != nullptr && l->mPow > 0.01f) {
            push(l->mPosition, l->mColor.r, l->mColor.g, l->mColor.b, l->mPow, true, false);
        }
    }

    for (int i = 0; i < 5; i++) {
        const LIGHT_INFLUENCE* l = env->efplight[i];
        if (l != nullptr && l->mPow > 0.01f) {
            push(l->mPosition, l->mColor.r, l->mColor.g, l->mColor.b, l->mPow, true, false);
        }
    }

    s_stats.vanillaPoint = n;

    // The spot list. field_0x26 is a per frame liveness flag: dScnKy_env_light_c::exeKankyo
    // clears all six at the top of the frame (src/d/d_kankyo.cpp:4769) and whoever registers a
    // light sets it again. That ordering works out for us: processes execute in ascending
    // list-ID order (cTrIt_Method, c_tree_iter.cpp:12-21), kankyo is list 1
    // (d_kankyo.cpp:8420), the torch actors are list 3 and Link is list 5 - and the bridge runs
    // after all of them, so the flags we read are this frame's. Slot 0 is Link's lantern and the wolf senses (dKy_WolfEyeLight_set,
    // src/d/d_kankyo.cpp:10314); slots 1..5 are the torches, candles and carried lights that
    // went through dKy_BossLight_set rather than dKy_plight_set - a large share of the game's
    // torches, so skipping this list would leave most of them without the game's own colour.
    //
    // COLOUR ONLY, deliberately. mRefDistance is loaded into the same GX distance attenuation
    // mPow is (src/d/d_kankyo.cpp:9077), which reads as "it is a reach" - but the callers
    // disagree with that reading. The lantern passes a 0..1 ramp (d_a_alink.cpp:14964, from
    // daAlinkHIO_huLight_c1::mPower = 1.0f) and the BossLight torches pass their own 0..1
    // intensity ramp (d_a_obj_fireWood2.cpp:144). Treating that as metres would make every
    // torch and the lantern black. So the field is used as what the callers clearly mean by it,
    // an on/strength signal, and the reach comes from the settings.
    // SIX, not eight, even though field_0x0c18 is BOSS_LIGHT[8]. Do not "fix" this to 8:
    // field_0x26 is only a per-frame liveness flag for the slots exeKankyo clears, and its loop
    // runs 0..5 (src/d/d_kankyo.cpp:4768). Slots 6 and 7 are written by neither setter -
    // dKy_WolfEyeLight_set takes slot 0 (:10223-10225) and dKy_BossLight_set allocates in
    // [1, 6 - stage_light_info_num) (:10064) - so they hold whatever they were left with and
    // their flag never expires. Reading them would resurrect a light from an old room.
    for (int i = 0; i < 6; i++) {
        const BOSS_LIGHT& b = env->field_0x0c18[i];
        if (b.field_0x26 == 1 && b.mRefDistance > 0.0f) {
            push(b.mPos, b.mColor.r, b.mColor.g, b.mColor.b, 0.0f, false, true);
        }
    }

    s_stats.vanillaSpot = n - s_stats.vanillaPoint;

    return n;
}

}  // namespace

const char* className(Class cls) {
    return profileOf(cls).name;
}

void setRecording(bool enabled) {
    s_recordingEnabled = enabled;
    if (!enabled) {
        s_simpleCount = 0;
        s_simpleOverflowed = false;
        s_simpleDroppedPending = 0;
        // Re-arm the warning: the system being switched off and back on is a discontinuity, and
        // an overflow after it is a new fact rather than a repeat of the old one.
        s_simpleOverflowWarned = false;
    }
}

void recordSimple(uint16_t effectId, const void* emitter, float x, float y, float z,
                  const float prm[3], const float env[3]) {
    if (!s_recordingEnabled) {
        return;
    }

    if (s_simpleCount >= kMaxSimpleRecords) {
        s_simpleOverflowed = true;
        s_simpleDroppedPending++;
        return;
    }

    SimpleRecord& rec = s_simple[s_simpleCount++];
    rec.effectId = effectId;
    rec.emitter = static_cast<const JPABaseEmitter*>(emitter);
    rec.pos[0] = x;
    rec.pos[1] = y;
    rec.pos[2] = z;
    for (int i = 0; i < 3; i++) {
        rec.prm[i] = prm[i];
        rec.env[i] = env[i];
    }
}

void requestReport() {
    s_reportRequested = true;
}

void setBridgeCounters(int creates, int destroys, int drawn) {
    // The bridge owns these and has counted them since the system landed, but nothing has ever
    // printed them - a rule 4 gap in shipped code. `creates` is the one that matters for any
    // question about cost: it counts light UPDATES, not lights, because the bridge re-creates a
    // light whenever its radiance moves more than 2%.
    s_bridgeCreates = creates;
    s_bridgeDestroys = destroys;
    s_bridgeDrawn = drawn;
}

const Stats& stats() {
    return s_stats;
}

void reset() {
    s_tracked.clear();
    s_sites.clear();
    s_simpleCount = 0;
    s_simpleOverflowed = false;
    s_simpleDroppedPending = 0;
    s_simpleOverflowWarned = false;
}

const std::vector<Site>& collect(const Params& params) {
    s_stats = Stats();
    s_sites.clear();

    if (!params.enable) {
        setRecording(false);
        for (TrackedSite& t : s_tracked) {
            t.seen = false;
        }
        s_tracked.clear();
        return s_sites;
    }

    // Arm the simple recorder for the NEXT frame's actor pass. The records read below were
    // written during the frame that has just executed.
    setRecording(true);
    s_stats.ran = true;

    Candidate candidates[kMaxCandidates];
    int candidateCount = 0;

    collectEmitters(params, candidates, candidateCount);
    collectSimple(params, candidates, candidateCount);

    // The simple records are consumed here; the next actor pass refills them.
    //
    // The refusals are counted rather than warned about every frame. The line this replaces
    // fired once per frame for as long as the scene stayed over the cap - a condition that
    // persists by nature - which is the unbounded output the logging rule in CLAUDE.md forbids.
    // droppedSimple now sits beside droppedCandidates and droppedSites in the on-demand report,
    // where one button press answers "did it happen, how badly, and over how many frames"; the
    // warn survives only as a 0->1 edge so the first occurrence is still visible in a log where
    // nobody pressed it.
    //
    // How reachable this is has not been measured, and the read below is the arithmetic, not an
    // observation. dPa_simpleEcallBack::create allocates 0x20 records per simple effect id and
    // refuses past field_0xe (src/d/d_particle.cpp:775, :825), and at most 19 simple ids are ever
    // registered (5 common at :1238, 14 scene at :1262), so the ceiling is ~608 against this
    // 192-slot table: possible, but it takes roughly seven distinct simple effects near their own
    // caps at once and nobody has recorded that happening. Silence here now means the counter is
    // the thing to read, not that the overflow was fixed.
    s_stats.droppedSimple = s_simpleDroppedPending;
    s_simpleDroppedPending = 0;
    if (s_simpleOverflowed) {
        if (!s_simpleOverflowWarned) {
            s_simpleOverflowWarned = true;
            Log.warn("simple effect records capped at {} ({} dropped this frame); some torches "
                     "will be unlit. Later frames are counted, not logged - read 'dropped: ... "
                     "simple' in the effect-light report for the high-water mark.",
                     kMaxSimpleRecords, s_stats.droppedSimple);
        }
        s_simpleOverflowed = false;
    }
    s_simpleCount = 0;


    // --- cluster ---------------------------------------------------------------------
    //
    // A bonfire is five emitters at one point - ZI_S_maki_fire_a.._ind, spawned together at
    // src/d/actor/d_a_obj_maki.cpp:56. Five lights there would cost five times as much for
    // none of the benefit, and their alphas animate independently so the sum flickers.
    struct Cluster {
        float pos[3];
        float color[3];
        float mass;   // summed candidate weights - "how much fire is standing here"
        Class cls;
        uint16_t effectId;
        int members;
        // Taken from the LEADING member, exactly like the position and the colour, and for the
        // same reason: the lead is chosen without any animating term, so anything carried with
        // it is stable for as long as the membership is. Extent is the one exception - it is
        // the largest of the members', because a site is as big as the biggest thing in it.
        float authoredColor[3];
        bool hasAuthoredColor;
        float extent;
        bool hasExtent;
    };

    Cluster clusters[kMaxSites];
    int clusterCount = 0;
    const float mergeR2 = params.mergeRadius * params.mergeRadius;

    for (int i = 0; i < candidateCount; i++) {
        const Candidate& c = candidates[i];

        int target = -1;
        float bestD2 = mergeR2;
        for (int j = 0; j < clusterCount; j++) {
            const float d2 = dist2(c.pos, clusters[j].pos);
            if (d2 <= bestD2) {
                bestD2 = d2;
                target = j;
            }
        }

        if (target < 0) {
            if (clusterCount >= kMaxSites) {
                s_stats.culled++;
                continue;
            }
            Cluster& n = clusters[clusterCount++];
            n.pos[0] = c.pos[0];
            n.pos[1] = c.pos[1];
            n.pos[2] = c.pos[2];
            n.color[0] = c.color[0];
            n.color[1] = c.color[1];
            n.color[2] = c.color[2];
            n.mass = c.weight;
            n.cls = c.cls;
            n.effectId = c.effectId;
            n.members = 1;
            n.authoredColor[0] = c.authoredColor[0];
            n.authoredColor[1] = c.authoredColor[1];
            n.authoredColor[2] = c.authoredColor[2];
            n.hasAuthoredColor = c.hasAuthoredColor;
            n.extent = c.hasExtent ? c.extent : 0.0f;
            n.hasExtent = c.hasExtent;
            continue;
        }

        Cluster& t = clusters[target];
        t.members++;
        t.mass += c.weight;
        if (c.hasExtent && c.extent > t.extent) {
            t.extent = c.extent;
            t.hasExtent = true;
        }

        // The leading member donates the position and colour rather than a centroid: a centroid
        // drifts as members come and go, and a light that drifts has visible shadow swim.
        //
        // The lead is decided WITHOUT any animating quantity, which is the whole point. It used
        // to be `weight`, and weight is classWeight * the emitter's global alpha - so two
        // same-class emitters in one cluster swapped the lead every time their alphas crossed,
        // and the site position SNAPPED between them, by up to mergeRadius. A flame's alpha
        // animates constantly, so that crossing is not rare; it is the steady state. Fixing
        // centroid drift by picking the heaviest member replaced a slow swim with a per-frame
        // jump, which is worse - a jump is what reads as a stuttering shadow.
        //
        // classWeight is a constant per class and effectId is fixed, so this is stable frame to
        // frame for as long as the membership is.
        //
        // Compared on the WEIGHT rather than on the class, which matters now that two classes
        // can share a weight: Spark carries Glow's, and Lantern carries Fire's until it is
        // being solved separately. Falling through to the lower effect id whenever the weights
        // tie keeps that case decided by a fixed number instead of by sweep order - and for
        // two members of the same class it is exactly the rule that was here before.
        const float cw = classWeight(c.cls, params);
        const float tw = classWeight(t.cls, params);
        const bool leads = cw != tw ? cw > tw : c.effectId < t.effectId;
        if (leads) {
            t.pos[0] = c.pos[0];
            t.pos[1] = c.pos[1];
            t.pos[2] = c.pos[2];
            t.color[0] = c.color[0];
            t.color[1] = c.color[1];
            t.color[2] = c.color[2];
            t.cls = c.cls;
            t.effectId = c.effectId;
            t.authoredColor[0] = c.authoredColor[0];
            t.authoredColor[1] = c.authoredColor[1];
            t.authoredColor[2] = c.authoredColor[2];
            t.hasAuthoredColor = c.hasAuthoredColor;
        }
    }

    // --- adopt the game's own parameters ------------------------------------------------
    VanillaLight vanilla[128];
    const int vanillaCount = gatherVanillaLights(vanilla, 128);
    const float adoptR2 = params.adoptRadius * params.adoptRadius;

    struct Resolved {
        int cluster;
        int vanillaIndex;
        float sortKey;
    };

    Resolved resolved[kMaxSites];

    for (int i = 0; i < clusterCount; i++) {
        resolved[i].cluster = i;
        resolved[i].vanillaIndex = -1;
        resolved[i].sortKey = 0.0f;

        int best = -1;
        float bestD2 = adoptR2;
        for (int v = 0; v < vanillaCount; v++) {
            if (vanilla[v].adopted) {
                continue;
            }
            const float d2 = dist2(clusters[i].pos, vanilla[v].pos);
            if (d2 <= bestD2) {
                bestD2 = d2;
                best = v;
            }
        }

        if (best >= 0) {
            vanilla[best].adopted = true;
            resolved[i].vanillaIndex = best;
            // For the report only. Adoption is EXCLUSIVE and resolved in cluster order rather
            // than priority order, so a short-lived effect inside adoptRadius of a torch takes
            // that torch's light for its whole life and leaves the torch on the undetermined
            // fallback - about 19x dimmer. Recording who took what is what makes that visible
            // in a log instead of being reported as "the room got darker near the explosion".
            vanilla[best].adoptedByEffect = clusters[i].effectId;
            vanilla[best].adoptDistance = std::sqrt(std::max(0.0f, bestD2));
        }
    }

    // Snapshot the game's own lights and who took each one, for the report.
    s_vanillaReportCount = 0;
    for (int v = 0; v < vanillaCount && s_vanillaReportCount < kMaxVanillaReport; v++) {
        VanillaReportEntry& e = s_vanillaReport[s_vanillaReportCount++];
        for (int c = 0; c < 3; c++) {
            e.pos[c] = vanilla[v].pos[c];
            e.color[c] = static_cast<uint8_t>(
                std::min(255.0f, std::max(0.0f, vanilla[v].color[c] * 255.0f)));
        }
        e.reach = vanilla[v].reachKnown ? vanilla[v].reach : -1.0f;
        e.spot = vanilla[v].spot;
        e.adoptedByEffect = vanilla[v].adoptedByEffect;
        e.adoptDistance = vanilla[v].adoptDistance;
    }

    for (int v = 0; v < vanillaCount; v++) {
        if (!vanilla[v].adopted) {
            s_stats.orphans++;
        }
    }

    // --- solve, offset, budget ----------------------------------------------------------
    struct Pending {
        float pos[3];
        float color[3];
        float reach;
        float radius;
        float scale;
        Class cls;
        uint16_t effectId;
        int members;
        bool derived;         // reach came from the game
        bool colorFromGame;   // colour came from the game (a wider set - see gatherVanillaLights)
        // Which of the three possible sources each value actually came from, so the site report
        // can say "authored" or "defaulted" per site rather than leaving it to be inferred from
        // the settings. This is the instrumentation half of the derivation change: without it a
        // hue that came out wrong gives no way to tell whether the authored ramp was even read.
        uint8_t colorSource;  // ColorSource below
        bool radiusAuthored;  // the sphere grew to the effect's authored extent
        bool lantern;         // solved from the lantern's own settings, not the shared ones
        float intensity;      // the master multiplier this site gets - 1.0 for the lantern
        float priority;
        // Summed emitter alpha at this site, and what it multiplied reach by. Report-only, so a
        // log can show whether a bonfire actually measured as one rather than leaving the
        // scaling to be judged by eye.
        float mass;
        float massBoost;
        // Distance to the vanilla light this site adopted, or -1. Recorded purely for the
        // report: it is the one number the whole burst design turns on and the only one that
        // has never been measured - the site is at calcEmitterGlobalPosition (which includes
        // the emitter's authored local translation, unreadable here), not at the actor origin.
        float vanillaDistance;
    };

    Pending pending[kMaxSites];
    int pendingCount = 0;

    for (int i = 0; i < clusterCount; i++) {
        Pending p;
        p.cls = clusters[i].cls;
        p.effectId = clusters[i].effectId;
        p.members = clusters[i].members;
        p.pos[0] = clusters[i].pos[0];
        p.pos[1] = clusters[i].pos[1] + classOffset(clusters[i].cls, params);
        p.pos[2] = clusters[i].pos[2];

        const int v = resolved[i].vanillaIndex;
        if (v >= 0) {
            // The game's colour is always better than the effect's: it is what the artists chose
            // for the light rather than for the sprite. The reach only comes across when the
            // registry it came from actually carries one.
            p.color[0] = vanilla[v].color[0];
            p.color[1] = vanilla[v].color[1];
            p.color[2] = vanilla[v].color[2];
            p.colorFromGame = true;
            p.colorSource = kColorGame;
        } else if (params.authoredColor && clusters[i].hasAuthoredColor) {
            // The effect's OWN authored colour, from the ramp its artists wrote, in preference
            // to the register the emitter is holding this frame. Three things move the live
            // register and none of them is the fire getting a different colour: a global colour
            // animation walking its key frame, the kankyo time-of-day tint the game multiplies
            // into effects carrying resUserWork bit 0x20 or 0x40 (d_particle.cpp:1568-1620), and
            // the free-running cycle on a shared simple emitter that every instance reads the
            // same unrelated phase of.
            //
            // Radiance is normalised by its brightest channel a few lines further down, so this
            // changes HUE only - never how bright a light is.
            p.color[0] = clusters[i].authoredColor[0];
            p.color[1] = clusters[i].authoredColor[1];
            p.color[2] = clusters[i].authoredColor[2];
            p.colorFromGame = false;
            p.colorSource = kColorAuthored;
        } else {
            p.color[0] = clusters[i].color[0];
            p.color[1] = clusters[i].color[1];
            p.color[2] = clusters[i].color[2];
            p.colorFromGame = false;
            p.colorSource = kColorLive;
        }

        // The adoption distance, recorded whether or not the reach came with it. This is the
        // number the burst design turns on and it has never been measured: the site is at
        // calcEmitterGlobalPosition, which folds in the emitter's authored local translation,
        // so "the light is 85 units from the site" was always inference.
        // Measured from the CLUSTER position, not from p.pos, because that is the distance the
        // adoption test itself used - p.pos has already had the per-class vertical offset added.
        // Reporting the post-offset distance here made the sites and game-lights sections
        // disagree about the same pair, which is worse than reporting neither.
        p.vanillaDistance =
            v >= 0 ? std::sqrt(std::max(0.0f, dist2(clusters[i].pos, vanilla[v].pos))) : -1.0f;

        // How much fire is standing here, and what that does to the light.
        //
        // Until this existed nothing scaled a light by the amount of fire present: a five
        // emitter bonfire and a single candle emitted identically, which is the whole reason
        // large fires read as underwhelming. `mass` is the summed alpha of the emitters merged
        // into this site - 5 for a full bonfire, 1 for a candle, less for anything fading.
        //
        // Applied to REACH rather than to the radiance scale on purpose. Radiance already goes
        // as reach squared (solveIntensity), so an exponent of 0.5 makes radiance exactly
        // proportional to mass - twice the fire, twice the light, which is what summing emitters
        // physically means. Reach also feeds the budget sort, so a genuinely big fire now
        // outranks a candle when maxLights binds, which it should.
        //
        // A single full alpha emitter has mass 1, and 1 to any power is 1 - so every candle and
        // torch in the game is untouched at every exponent, and existing tuning survives. An
        // exponent of 0 disables the whole thing exactly, which is what makes it a clean A/B.
        const float mass = std::max(clusters[i].mass, 0.0f);
        const float massBoost = (params.massExponent > 0.0f && mass > 0.0f)
                                    ? std::pow(mass, params.massExponent)
                                    : 1.0f;
        p.mass = mass;
        p.massBoost = massBoost;

        if (v >= 0 && vanilla[v].reachKnown) {
            p.derived = true;
            // The game's authored reach, scaled rather than replaced: LIGHT_INFLUENCE::mPow is
            // the only genuinely photometric number the artists left anywhere, and the only
            // thing that distinguishes a bonfire from a candle, so a fixed reach here would
            // flatten every derived light onto one size. Note the two places reach is read -
            // solveIntensity, where radiance goes as its square, and p.priority below, which is
            // why the reach multiplier is not simply an intensity by another name: it also
            // decides who survives maxLights.
            p.reach = vanilla[v].reach * massBoost;
            p.radius = params.derivedRadius;
            p.scale = params.derivedIntensity;
        } else {
            p.derived = false;
            p.reach = params.undeterminedReach * massBoost;
            p.radius = params.undeterminedRadius;
            p.scale = params.undeterminedIntensity;
        }

        // The authored extent, where the artists made the effect bigger than the configured
        // sphere. Only ever GROWS the radius, never shrinks it: the two configured radii are
        // what every existing tuning was done against, so the worst this can do when it is
        // switched on is soften a light that was already the right brightness. Radiance is
        // solved to carry to the same reach whatever the radius is, so this changes softness
        // and near-field falloff rather than how far the light travels.
        p.radiusAuthored = false;
        if (params.authoredRadius && clusters[i].hasExtent &&
            clusters[i].extent > p.radius && clusters[i].extent <= kMaxAuthoredRadius) {
            p.radius = clusters[i].extent;
            p.radiusAuthored = true;
        }

        // The lantern, when it is being solved separately: its three values replace the
        // branch's outright and the global multipliers do NOT apply, which is what makes them
        // independent rather than merely additional. The mass boost does not apply either - the
        // lantern is one emitter, so it would be 1 anyway, and leaving it out means the three
        // settings are the whole story.
        //
        // The colour is untouched. The game registers a real lamp light at the flame point and
        // that light's colour is the artists' own; nothing in the survey said it was wrong.
        p.lantern = false;
        p.intensity = params.intensity;
        if (params.lanternSeparate && p.cls == Class::Lantern) {
            p.lantern = true;
            p.derived = false;
            p.radiusAuthored = false;
            p.reach = params.lanternReach;
            p.radius = params.lanternRadius;
            p.scale = params.lanternIntensity;
            p.intensity = 1.0f;
        } else {
            // The two global multipliers, applied at one point to both branches. Defaulting to
            // 1.0, so a stock configuration solves exactly the numbers the branch produced.
            p.reach *= params.reachScale;
            p.radius *= params.radiusScale;
        }

        // Closer and brighter first, so the budget drops the lights nobody will notice.
        float d2 = 0.0f;
        if (params.cameraValid) {
            d2 = dist2(p.pos, params.cameraPos);
            if (params.maxDistance > 0.0f && d2 > params.maxDistance * params.maxDistance) {
                s_stats.culled++;
                continue;
            }
        }
        p.priority = p.reach * p.reach / (1.0f + d2);

        pending[pendingCount++] = p;
    }

    std::sort(pending, pending + pendingCount,
              [](const Pending& a, const Pending& b) { return a.priority > b.priority; });

    const int budget = params.maxLights > 0 ? std::min(params.maxLights, pendingCount)
                                            : pendingCount;
    s_stats.culled += pendingCount - budget;

    // --- track across frames -------------------------------------------------------------
    //
    // A Remix light is keyed by hash and its temporal history goes with it, so a site's
    // identity has to survive a frame in which one of its emitters came or went. Match to last
    // frame's sites by proximity and class; anything unmatched is new.
    for (TrackedSite& t : s_tracked) {
        t.seen = false;
    }

    for (int i = 0; i < budget; i++) {
        const Pending& p = pending[i];

        TrackedSite* match = nullptr;
        // Generous compared to the merge radius, because a site's position steps whenever a
        // different member becomes the heaviest. Floored so that a merge radius of zero, which
        // means "never merge", does not also mean "never recognise the same site twice".
        float bestD2 = std::max(mergeR2 * 4.0f, 100.0f);
        for (TrackedSite& t : s_tracked) {
            if (t.seen || t.site.cls != p.cls) {
                continue;
            }
            const float d2 = dist2(p.pos, t.site.position);
            if (d2 <= bestD2) {
                bestD2 = d2;
                match = &t;
            }
        }

        if (match == nullptr) {
            if (s_tracked.size() >= static_cast<size_t>(kMaxSites)) {
                // Counted rather than silently skipped. This can only fire in a crowded scene,
                // and grace-held sites accumulate here too, so it is reachable well before 128
                // real lights exist - a light that never appears with every other counter
                // healthy used to have no explanation at all.
                s_stats.droppedSites++;
                continue;
            }
            s_tracked.push_back(TrackedSite {});
            match = &s_tracked.back();
            match->site.id = s_nextSiteId++;
        }

        match->seen = true;
        match->missingFrames = 0;

        // Measure how far this site moved since last frame, before the new position lands.
        // A pure position jump at constant brightness wrote nothing to any section before this:
        // the sites list is a single frame and the trace only records radiance movement, so the
        // one signature of a stuttering shadow was the one thing the report could not show.
        if (match->hasPrevPos) {
            const float jump = std::sqrt(std::max(0.0f, dist2(p.pos, match->prevPos)));
            if (jump > match->maxJump) {
                match->maxJump = jump;
            }
            if (jump > kJumpNotable) {
                match->jumps++;
            }
        }
        match->prevPos[0] = p.pos[0];
        match->prevPos[1] = p.pos[1];
        match->prevPos[2] = p.pos[2];
        match->hasPrevPos = true;

        match->site.position[0] = p.pos[0];
        match->site.position[1] = p.pos[1];
        match->site.position[2] = p.pos[2];
        match->site.radius = std::max(p.radius, 0.01f);
        match->site.cls = p.cls;
        match->site.derived = p.derived;
        match->site.colorFromGame = p.colorFromGame;
        match->site.effectId = p.effectId;
        match->site.members = p.members;
        match->reach = p.reach;
        match->vanillaDistance = p.vanillaDistance;
        match->mass = p.mass;
        match->massBoost = p.massBoost;
        match->colorSource = p.colorSource;
        match->radiusAuthored = p.radiusAuthored;
        match->lantern = p.lantern;

        const float intensity =
            solveIntensity(p.reach, match->site.radius, p.scale * p.intensity);
        const float brightest = std::max(p.color[0], std::max(p.color[1], p.color[2]));
        for (int c = 0; c < 3; c++) {
            match->site.radiance[c] =
                brightest > 0.0f ? (p.color[c] / brightest) * intensity : 0.0f;
        }

        if (!isFinite3(match->site.radiance)) {
            match->site.radiance[0] = 0.0f;
            match->site.radiance[1] = 0.0f;
            match->site.radiance[2] = 0.0f;
        }

        if (p.derived) {
            s_stats.derived++;
        }
        if (p.colorFromGame) {
            s_stats.colorFromGame++;
        }
    }

    // Sites that produced no candidate this frame are held, unchanged, for a few frames before
    // they are let go - and they are still reported, so the caller keeps drawing them.
    //
    // Holding the site id alone would not be enough. Not reporting a site tells the bridge the
    // light is gone, and it destroys it; when the effect comes back a frame later the light is
    // created again from scratch and its temporal history starts over. Several effects in this
    // game are re-set every few frames rather than continuously, so without this they would
    // strobe. The cost is that a fire that genuinely goes out lingers for the grace period,
    // which at this length is under a fifth of a second.
    //
    // Class::Spark is held for kSiteGraceFrames + params.sparkHold, and that extra is the
    // periodicity answer for the shadow insect. Its spark is not a timer, it is a set of
    // state-machine windows, and the shortest is 5-15 frames (d_a_e_ym.cpp:2624, initFireFly on
    // a wall bounce) - shorter than the base grace itself. Without the extra hold a bug
    // bouncing around a room destroys and re-creates its site repeatedly, and because a site's
    // id is its Remix light hash, every re-creation throws away that light's RTXDI temporal
    // history. The hold keeps ONE id alive across the gaps inside a burst. It does not extend
    // the light indefinitely: when the sparking genuinely stops, the site still goes out.
    for (size_t i = s_tracked.size(); i-- > 0;) {
        if (s_tracked[i].seen) {
            continue;
        }
        int limit = kSiteGraceFrames;
        if (s_tracked[i].site.cls == Class::Spark && params.sparkHold > 0) {
            limit += params.sparkHold;
        }
        if (++s_tracked[i].missingFrames > limit) {
            s_tracked.erase(s_tracked.begin() +
                            static_cast<std::vector<TrackedSite>::difference_type>(i));
        }
    }

    // Sites this frame refreshed, then sites the grace period is holding - and the budget
    // applies to the total, not just to the refresh.
    //
    // Reporting every tracked site regardless was the bug: a site dropped by the budget or the
    // distance cull is also "not seen", so it went into the grace period and kept being
    // returned. The counters said culled while the caller was handed the light anyway, which is
    // the worst combination - a budget that does not bound anything and a readout that says it
    // does.
    const size_t cap = params.maxLights > 0 ? static_cast<size_t>(params.maxLights)
                                            : s_tracked.size();

    // Counted here rather than in the solve loop above, so that these agree with s_stats.sites
    // EXACTLY - the grace period returns sites that were not refreshed this frame, and those
    // are still being drawn with the values they were last solved from. `derived` and
    // `colorFromGame` are counted in the solve loop and therefore do not include grace-held
    // sites; that predates this and is deliberately left alone rather than changed underneath
    // a readout somebody may have been reading for a week.
    const auto countSite = [](const TrackedSite& t) {
        if (t.colorSource == kColorAuthored) {
            s_stats.authoredColor++;
        }
        if (t.radiusAuthored) {
            s_stats.authoredRadius++;
        }
        if (t.lantern) {
            s_stats.lantern++;
        }
        const int ci = static_cast<int>(t.site.cls);
        if (ci >= 0 && ci < static_cast<int>(Class::Count)) {
            s_stats.byClass[ci]++;
        }
    };

    for (const TrackedSite& t : s_tracked) {
        if (t.seen && s_sites.size() < cap) {
            s_sites.push_back(t.site);
            countSite(t);
        }
    }
    for (const TrackedSite& t : s_tracked) {
        if (!t.seen && s_sites.size() < cap) {
            s_sites.push_back(t.site);
            countSite(t);
        }
    }

    s_stats.sites = static_cast<int>(s_sites.size());

    // Everything the report needs, captured once per frame while it is still in scope. This is
    // what makes one button press enough: the log describes the frame it was pressed on, and
    // the trace ring already holds the seconds before it.
    snapshotSites(s_tracked);

    // Fold this frame into the high-water record before any report is emitted, so a report
    // always includes the frame it was requested on.
    accumulatePeak();

    // AFTER the snapshot, so the report describes the frame it was pressed on rather than the
    // one before it - including the effects classified during this very call.
    if (s_reportRequested) {
        emitReport(params);
        s_reportRequested = false;
    }

    return s_sites;
}

}  // namespace effect_lights
}  // namespace dusk
