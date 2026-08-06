#include "dusk/effect_lights.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "dusk/logging.h"

#include "JSystem/JParticle/JPABaseShape.h"
#include "JSystem/JParticle/JPAEmitter.h"
#include "JSystem/JParticle/JPAEmitterManager.h"
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

// A site whose members all vanish is kept this many frames before its light is dropped. Some
// effects are re-set every few frames rather than continuously, and without the grace period
// those would create and destroy a Remix light in a loop.
constexpr int kSiteGraceFrames = 6;

struct Candidate {
    float pos[3];
    float color[3];   // 0..1, the effect's own colour
    float weight;     // higher wins the site's position and colour
    Class cls;
    uint16_t effectId;
};

struct SimpleRecord {
    uint16_t effectId;
    float pos[3];
    float prm[3];
    float env[3];
    const JPABaseEmitter* emitter;
};

struct TrackedSite {
    Site site;
    int missingFrames;
    bool seen;
};

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
};

bool s_recordingEnabled = false;

SimpleRecord s_simple[kMaxSimpleRecords];
int s_simpleCount = 0;
bool s_simpleOverflowed = false;

std::vector<Site> s_sites;
std::vector<TrackedSite> s_tracked;
uint32_t s_nextSiteId = 1;

Stats s_stats;

ReportEntry s_report[kMaxReportEntries];
int s_reportCount = 0;
bool s_reportOverflowed = false;
bool s_reportRequested = false;

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

// The name decides what KIND of thing this is - which sets the vertical offset and which
// fallback size applies. It never decides whether a light exists; that is the rule in
// classify() below. Reading the effect's own name is not "tagging" in the sense the project's
// rules forbid: the name is the game's own identity for the effect, shipped in the game's own
// table, at the granularity the game itself uses.
Class classifyByName(uint16_t id) {
    const char* name = effectName(id);
    if (name[0] == '\0') {
        return Class::Other;
    }

    if (nameHas(name, "lava") || nameHas(name, "magma") || nameHas(name, "youdo")) {
        return Class::Lava;
    }

    // One-shot violence. Kept apart from Fire because a light that lives for a handful of
    // frames is a flash, which is sometimes exactly right and sometimes a flicker artefact.
    if (nameHas(name, "bakuha") || nameHas(name, "explo") || nameHas(name, "bomb") ||
        nameHas(name, "baku")) {
        return Class::Burst;
    }

    if (nameHas(name, "fire") || nameHas(name, "honoo") || nameHas(name, "hono") ||
        nameHas(name, "kaen") || nameHas(name, "flame") || nameHas(name, "taimatsu") ||
        nameHas(name, "maki") || nameHas(name, "kantera") || nameHas(name, "torch") ||
        nameHas(name, "ablaze") || nameHas(name, "kagarib")) {
        return Class::Fire;
    }

    if (nameHas(name, "hikari") || nameHas(name, "light") || nameHas(name, "kira") ||
        nameHas(name, "pika") || nameHas(name, "glow") || nameHas(name, "aura") ||
        nameHas(name, "shine") || nameHas(name, "spark")) {
        return Class::Glow;
    }

    return Class::Other;
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

    const GXBlendFactor dst = shape->getBlendDst();
    return dst == GX_BL_ONE || dst == GX_BL_SRCALPHA || dst == GX_BL_DSTALPHA;
}

float chromaOf(const float c[3]) {
    const float hi = std::max(c[0], std::max(c[1], c[2]));
    const float lo = std::min(c[0], std::min(c[1], c[2]));
    if (hi <= 0.0001f) {
        return 0.0f;
    }
    return (hi - lo) / hi;
}

float lumaOf(const float c[3]) {
    return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
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

void noteForReport(uint16_t effectId, const JPABaseShape* shape, const float prm[3],
                   const float env[3], Class cls, bool additive, bool glow, bool simple) {
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

void emitReport() {
    Log.info("effect light classification report: {} distinct effects{}", s_reportCount,
             s_reportOverflowed ? " (CAPPED - more were seen than fit)" : "");
    Log.info("  columns: id name | blend mode/src/dst | prm rgb | env rgb | class | additive glow "
             "| verdict");

    for (int i = 0; i < s_reportCount; i++) {
        const ReportEntry& e = s_report[i];
        Log.info("  {:#06x} {:<34} | {}/{}/{} | {:3},{:3},{:3} | {:3},{:3},{:3} | {:<5} | {} {} | "
                 "{}{}",
                 e.effectId, effectName(e.effectId), blendModeName(e.blendMode),
                 blendFactorName(e.blendSrc), blendFactorName(e.blendDst), e.prm[0], e.prm[1],
                 e.prm[2], e.env[0], e.env[1], e.env[2], className(e.cls),
                 e.additive ? "additive" : "opaque  ", e.glow ? "glow" : "flat",
                 (e.additive && e.glow) ? "LIT" : "no", e.simple ? " (simple)" : "");
    }

    if (s_reportOverflowed) {
        Log.info("  report capped at {} entries; earlier effects are listed, later ones were "
                 "dropped", kMaxReportEntries);
    }
}

float classOffset(Class cls, const Params& params) {
    switch (cls) {
    case Class::Fire:
    case Class::Burst:
        return params.fireOffset;
    case Class::Glow:
        return params.glowOffset;
    default:
        return 0.0f;
    }
}

// Weight decides which member of a merged site donates its position and colour. A core fire
// beats a glow beats anything else; within a class, a bigger contribution wins.
float classWeight(Class cls) {
    switch (cls) {
    case Class::Fire: return 4.0f;
    case Class::Lava: return 3.0f;
    case Class::Glow: return 2.0f;
    case Class::Burst: return 1.5f;
    default: return 1.0f;
    }
}

// Radiance for a sphere light standing in for a light that was meant to reach `reach` units.
//
// Deliberately the same solve the local light mirror uses (Remix's own
// LightUtils::calculateIntensity, inverted): work out the radiance a sphere of this radius
// needs to still be perceptible at that distance. Keeping it identical is what lets the
// numbers tuned for that path carry over unchanged.
float solveIntensity(float reach, float radius, float scale) {
    if (reach <= 0.0f || radius <= 0.0001f) {
        return 0.0f;
    }
    return (reach * reach) * kNewLightEndValue / (kPi * radius * radius) * scale;
}

void addCandidate(Candidate* candidates, int& count, const Candidate& c) {
    if (count >= kMaxCandidates) {
        return;
    }
    candidates[count++] = c;
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

            // The game's own on/off. This one test is why the lantern needs no special case:
            // daAlink_c::setLight gates the flame on the oil meter and calls stopDrawParticle
            // when it runs out (src/d/actor/d_a_alink.cpp:14874).
            if (emitter->checkStatus(JPAEmtrStts_StopDraw) ||
                emitter->checkStatus(JPAEmtrStts_Delete)) {
                continue;
            }

            if (emitter->getParticleNumber() == 0) {
                continue;
            }

            const float alpha = emitter->getGlobalAlpha() * (1.0f / 255.0f);
            if (alpha < params.minAlpha) {
                continue;
            }

            s_stats.considered++;

            const uint16_t effectId = sweptId;
            const Class cls = cachedClass(effectId);

            if (cls == Class::Burst && !params.bursts) {
                continue;
            }

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

            noteForReport(effectId, shape, prm, env, cls, additive, glow, false);

            if (!additive || !glow) {
                continue;
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
            c.weight = classWeight(cls) * alpha;
            c.cls = cls;
            c.effectId = effectId;

            if (!isFinite3(c.pos)) {
                continue;
            }

            s_stats.candidates++;
            addCandidate(candidates, count, c);
        }
    }
}

// --- source 2: the simple effect records -----------------------------------------------
void collectSimple(const Params& params, Candidate* candidates, int& count) {
    for (int i = 0; i < s_simpleCount; i++) {
        const SimpleRecord& rec = s_simple[i];

        s_stats.emitters++;
        s_stats.considered++;

        const Class cls = cachedClass(rec.effectId);
        if (cls == Class::Burst && !params.bursts) {
            continue;
        }

        const JPABaseShape* shape =
            (rec.emitter != nullptr && rec.emitter->pRes != nullptr) ? rec.emitter->pRes->getBsp()
                                                                     : nullptr;
        const bool additive = isAdditive(shape);

        float color[3];
        pickColor(rec.prm, rec.env, color);
        const bool glow = readsAsGlow(color, params);

        noteForReport(rec.effectId, shape, rec.prm, rec.env, cls, additive, glow, true);

        if (!additive || !glow) {
            continue;
        }

        Candidate c;
        c.pos[0] = rec.pos[0];
        c.pos[1] = rec.pos[1];
        c.pos[2] = rec.pos[2];
        c.color[0] = color[0];
        c.color[1] = color[1];
        c.color[2] = color[2];
        c.weight = classWeight(cls);
        c.cls = cls;
        c.effectId = rec.effectId;

        if (!isFinite3(c.pos)) {
            continue;
        }

        s_stats.candidates++;
        addCandidate(candidates, count, c);
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
};

int gatherVanillaLights(VanillaLight* out, int cap) {
    const dScnKy_env_light_c* env = dKy_getEnvlight();
    if (env == nullptr) {
        return 0;
    }

    int n = 0;

    const auto push = [&](const cXyz& pos, float r, float g, float b, float reach,
                          bool reachKnown) {
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
            push(l->mPosition, l->mColor.r, l->mColor.g, l->mColor.b, l->mPow, true);
        }
    }

    for (int i = 0; i < 5; i++) {
        const LIGHT_INFLUENCE* l = env->efplight[i];
        if (l != nullptr && l->mPow > 0.01f) {
            push(l->mPosition, l->mColor.r, l->mColor.g, l->mColor.b, l->mPow, true);
        }
    }

    s_stats.vanillaPoint = n;

    // The spot list. field_0x26 is a per frame liveness flag: dScnKy_env_light_c::exeKankyo
    // clears all six at the top of the frame (src/d/d_kankyo.cpp:4769) and whoever registers a
    // light sets it again. Slot 0 is Link's lantern and the wolf senses (dKy_WolfEyeLight_set,
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
    for (int i = 0; i < 6; i++) {
        const BOSS_LIGHT& b = env->field_0x0c18[i];
        if (b.field_0x26 == 1 && b.mRefDistance > 0.0f) {
            push(b.mPos, b.mColor.r, b.mColor.g, b.mColor.b, 0.0f, false);
        }
    }

    s_stats.vanillaSpot = n - s_stats.vanillaPoint;

    return n;
}

}  // namespace

const char* className(Class cls) {
    switch (cls) {
    case Class::Fire: return "fire";
    case Class::Glow: return "glow";
    case Class::Lava: return "lava";
    case Class::Burst: return "burst";
    default: return "other";
    }
}

void setRecording(bool enabled) {
    s_recordingEnabled = enabled;
    if (!enabled) {
        s_simpleCount = 0;
        s_simpleOverflowed = false;
    }
}

void recordSimple(uint16_t effectId, const void* emitter, float x, float y, float z,
                  const float prm[3], const float env[3]) {
    if (!s_recordingEnabled) {
        return;
    }

    if (s_simpleCount >= kMaxSimpleRecords) {
        s_simpleOverflowed = true;
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

const Stats& stats() {
    return s_stats;
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
    if (s_simpleOverflowed) {
        Log.warn("simple effect records capped at {} this frame; some torches will be unlit",
                 kMaxSimpleRecords);
        s_simpleOverflowed = false;
    }
    s_simpleCount = 0;

    if (s_reportRequested) {
        emitReport();
        s_reportRequested = false;
    }

    // --- cluster ---------------------------------------------------------------------
    //
    // A bonfire is five emitters at one point - ZI_S_maki_fire_a.._ind, spawned together at
    // src/d/actor/d_a_obj_maki.cpp:56. Five lights there would cost five times as much for
    // none of the benefit, and their alphas animate independently so the sum flickers.
    struct Cluster {
        float pos[3];
        float color[3];
        float bestWeight;
        float totalWeight;
        Class cls;
        uint16_t effectId;
        int members;
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
            n.bestWeight = c.weight;
            n.totalWeight = c.weight;
            n.cls = c.cls;
            n.effectId = c.effectId;
            n.members = 1;
            continue;
        }

        Cluster& t = clusters[target];
        t.members++;
        t.totalWeight += c.weight;

        // The heaviest member donates the position and colour rather than a centroid: a
        // centroid drifts as members come and go, and a light that drifts has visible shadow
        // swim.
        if (c.weight > t.bestWeight) {
            t.bestWeight = c.weight;
            t.pos[0] = c.pos[0];
            t.pos[1] = c.pos[1];
            t.pos[2] = c.pos[2];
            t.color[0] = c.color[0];
            t.color[1] = c.color[1];
            t.color[2] = c.color[2];
            t.cls = c.cls;
            t.effectId = c.effectId;
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
        }
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
        float priority;
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
        } else {
            p.color[0] = clusters[i].color[0];
            p.color[1] = clusters[i].color[1];
            p.color[2] = clusters[i].color[2];
            p.colorFromGame = false;
        }

        if (v >= 0 && vanilla[v].reachKnown) {
            p.derived = true;
            p.reach = vanilla[v].reach;
            p.radius = params.derivedRadius;
            p.scale = params.derivedIntensity;
        } else {
            p.derived = false;
            p.reach = params.undeterminedReach;
            p.radius = params.undeterminedRadius;
            p.scale = params.undeterminedIntensity;
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
        float bestD2 = mergeR2 * 4.0f;
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
                continue;
            }
            s_tracked.push_back(TrackedSite {});
            match = &s_tracked.back();
            match->site.id = s_nextSiteId++;
        }

        match->seen = true;
        match->missingFrames = 0;
        match->site.position[0] = p.pos[0];
        match->site.position[1] = p.pos[1];
        match->site.position[2] = p.pos[2];
        match->site.radius = std::max(p.radius, 0.01f);
        match->site.cls = p.cls;
        match->site.derived = p.derived;
        match->site.colorFromGame = p.colorFromGame;
        match->site.effectId = p.effectId;
        match->site.members = p.members;

        const float intensity =
            solveIntensity(p.reach, match->site.radius, p.scale * params.intensity);
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

    for (size_t i = s_tracked.size(); i-- > 0;) {
        if (s_tracked[i].seen) {
            continue;
        }
        // Some effects are re-set every few frames rather than continuously. Without the
        // grace period those would create and destroy a Remix light in a loop.
        if (++s_tracked[i].missingFrames > kSiteGraceFrames) {
            s_tracked.erase(s_tracked.begin() + static_cast<ptrdiff_t>(i));
        }
    }

    for (const TrackedSite& t : s_tracked) {
        if (t.seen) {
            s_sites.push_back(t.site);
        }
    }

    s_stats.sites = static_cast<int>(s_sites.size());
    return s_sites;
}

}  // namespace effect_lights
}  // namespace dusk
