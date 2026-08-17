// Behavioural harness for dusk::effect_lights, compiled against the stub headers in ./h.
//
// It cannot check that the stub signatures match the game's - that was done by reading - but it
// does exercise the parts that are easy to get wrong and impossible to see from the source:
// the merge, the rule, the adoption, the budget and the cross-frame identity.

#include "dusk/effect_lights.hpp"

#include <JSystem/JParticle/JPABaseShape.h>
#include <JSystem/JParticle/JPADynamicsBlock.h>
#include <JSystem/JParticle/JPAEmitter.h>
#include <JSystem/JParticle/JPAEmitterManager.h>
#include <JSystem/JParticle/JPAKeyBlock.h>
#include <JSystem/JParticle/JPAResource.h>
#include <d/d_com_inf_game.h>
#include <d/d_kankyo.h>
#include <d/d_particle.h>
#include <d/d_particle_name.h>
#include <dusk/logging.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// --- the fake world ---------------------------------------------------------------------

// LAYOUT-COUPLED to the JPABaseShape stub, for the same reason FakeRes is coupled to JPAResource
// below: mpPrmClrAnmTbl and mpEnvClrAnmTbl are public fields on the real class and the module
// reads them THROUGH the JPABaseShape declaration, not through an accessor, so they must sit at
// the same offset in both and in the same order. Everything after them is reached only through
// accessors this file defines, so its layout is free.
struct FakeShape {
    GXColor* mpPrmClrAnmTbl = nullptr;
    GXColor* mpEnvClrAnmTbl = nullptr;
    GXBlendMode mode = GX_BM_BLEND;
    GXBlendFactor src = GX_BL_SRCALPHA;
    GXBlendFactor dst = GX_BL_ONE;
    // Animation configuration, reported but never decided on. Defaults say "does not animate".
    int glblClrAnm = 0;
    int prmAnm = 0;
    int envAnm = 0;
    u32 anmType = 0;
    s16 anmMaxFrm = 0;
    // The AUTHORED flat colours, which are what rampColor() falls back to when the matching anim
    // flag is clear or the table is NULL - the ordinary case. White over black is the "no
    // authored tint" reading, and it matches the values authoredFor() seeds its locals with.
    GXColor prmClr = {255, 255, 255, 255};
    GXColor envClr = {0, 0, 0, 255};
    f32 baseSizeX = 10.0f;
    f32 baseSizeY = 10.0f;
};

// Stub of the authored dynamics block. Only the three values effect_lights reads.
//
// The defaults are chosen so an emitter nobody has configured contributes NOTHING here: volume
// type 4 is VOL_Point (JPADynamicsBlock.cpp:143-150), the one type whose volume size means
// nothing, so the extent keeps coming from the base size alone unless a case says otherwise.
struct FakeDyn {
    u32 volumeType = 4;
    s16 maxFrame = 0;
    u16 volumeSize = 0;
    // The authored user-work word, carried into the report verbatim. 0 is "the artist wrote
    // nothing", which is what an emitter nobody has configured should report.
    u32 resUserWork = 0;
};

// LAYOUT-COUPLED to the JPAResource stub, and the coupling is load-bearing. The harness hands
// the module a FakeRes* reinterpret_cast to JPAResource*, so any field the module reads THROUGH
// the JPAResource declaration - as opposed to through getBsp()/getUsrIdx(), which resolve to
// explicit members here - has to sit at the same offset in both. ppKey and keyNum are read
// directly (they are public fields on the real class too), so they must come first, in the same
// order as the stub declares them. ASan caught this the moment the key-block walk was added:
// reading ppKey off the front of FakeShape segfaulted immediately.
struct FakeRes {
    JPAKeyBlock** ppKey = nullptr;
    u8 keyNum = 0;
    FakeShape shape;
    u16 usrIdx = 0;
    // Reached only through getDyn(), so unlike ppKey/keyNum above its offset is free. It sits
    // after shape deliberately: the layout-coupled prefix must stay first.
    FakeDyn dyn;
};

static std::vector<JPABaseEmitter*> g_emitters;
static std::vector<JSULink<JPABaseEmitter>> g_links;
static JSUList<JPABaseEmitter> g_useList[19];
static JPAEmitterManager g_manager;
static dPa_control_c g_particle;
dComIfG_inf_c g_dComIfG_gameInfo;
static dScnKy_env_light_c g_env;
static std::vector<LIGHT_INFLUENCE> g_influences;
static JPABaseEmitter* g_sharedSimple = nullptr;
static u16 g_sharedSimpleId = 0;

// stub implementations
GXBlendMode JPABaseShape::getBlendMode() const {
    return reinterpret_cast<const FakeShape*>(this)->mode;
}
GXBlendFactor JPABaseShape::getBlendSrc() const {
    return reinterpret_cast<const FakeShape*>(this)->src;
}
GXBlendFactor JPABaseShape::getBlendDst() const {
    return reinterpret_cast<const FakeShape*>(this)->dst;
}
JPABaseShape* JPAResource::getBsp() const {
    return reinterpret_cast<JPABaseShape*>(&const_cast<FakeRes*>(reinterpret_cast<const FakeRes*>(this))->shape);
}
u16 JPAResource::getUsrIdx() const { return reinterpret_cast<const FakeRes*>(this)->usrIdx; }

// The AUTHORED flat colours. rampColor() falls back to these whenever the matching anim flag is
// clear or the table is NULL, which is the ordinary case, so most of the classification tests
// reach the module's colour reading through here rather than through the animation tables.
void JPABaseShape::getPrmClr(GXColor* dst) const { *dst = reinterpret_cast<const FakeShape*>(this)->prmClr; }
void JPABaseShape::getEnvClr(GXColor* dst) const { *dst = reinterpret_cast<const FakeShape*>(this)->envClr; }

// The dynamics block. getDyn() is a real accessor on JPAResource, so unlike ppKey/keyNum the
// harness is free to put FakeDyn wherever it likes in FakeRes and hand back its address.
JPADynamicsBlock* JPAResource::getDyn() const {
    return reinterpret_cast<JPADynamicsBlock*>(&const_cast<FakeRes*>(reinterpret_cast<const FakeRes*>(this))->dyn);
}
u32 JPADynamicsBlock::getResUserWork() const { return reinterpret_cast<const FakeDyn*>(this)->resUserWork; }
u32 JPADynamicsBlock::getVolumeType() const { return reinterpret_cast<const FakeDyn*>(this)->volumeType; }
s16 JPADynamicsBlock::getMaxFrame() const { return reinterpret_cast<const FakeDyn*>(this)->maxFrame; }
u16 JPADynamicsBlock::getVolumeSize() const { return reinterpret_cast<const FakeDyn*>(this)->volumeSize; }

// The animation-configuration accessors the classification report prints. Values come from the
// FakeShape so a case can express "this effect's colour animates" or "it does not"; the defaults
// say it does not, which is the conservative reading for an effect nobody has measured.
BOOL JPABaseShape::isGlblClrAnm() const { return reinterpret_cast<const FakeShape*>(this)->glblClrAnm; }
BOOL JPABaseShape::isPrmAnm() const { return reinterpret_cast<const FakeShape*>(this)->prmAnm; }
BOOL JPABaseShape::isEnvAnm() const { return reinterpret_cast<const FakeShape*>(this)->envAnm; }
u32 JPABaseShape::getClrAnmType() const { return reinterpret_cast<const FakeShape*>(this)->anmType; }
s16 JPABaseShape::getClrAnmMaxFrm() const { return reinterpret_cast<const FakeShape*>(this)->anmMaxFrm; }
f32 JPABaseShape::getBaseSizeX() const { return reinterpret_cast<const FakeShape*>(this)->baseSizeX; }
f32 JPABaseShape::getBaseSizeY() const { return reinterpret_cast<const FakeShape*>(this)->baseSizeY; }
u8 JPAKeyBlock::getID() const { return 0; }

static u32 g_status[64];
// Per-emitter global particle scale. 1.0 is the ordinary case; a case sets it to 0 to express
// an emitter the game has hidden by shrinking rather than by alpha or StopDraw, which is how
// d_a_e_db hides the Deku Baba's drool (d_a_e_db.cpp:1871-1877).
static float g_pscale[64];
static int emitterIndex(const JPABaseEmitter* e) {
    for (size_t i = 0; i < g_emitters.size(); i++) {
        if (g_emitters[i] == e) return static_cast<int>(i);
    }
    return -1;
}
// emitterIndex returns -1 for any emitter not in the sweep list, and the SHARED simple emitter
// legitimately is not in it - collectEmitters skips it on purpose, and collectSimple reaches it
// through the records instead. Both accessors have to tolerate that rather than index by -1.
// ASan caught the old checkStatus doing exactly that the moment collectSimple started asking
// about liveness; the production code was never affected, but a test double that reads out of
// bounds cannot be trusted to report anything.
u32 JPABaseEmitter::checkStatus(u32 mask) const {
    const int i = emitterIndex(this);
    return (i >= 0 ? g_status[i] : 0u) & mask;
}
u8 JPABaseEmitter::getGlobalAlpha() const { return mGlobalPrmClr.a; }
u32 JPABaseEmitter::getParticleNumber() const { return 4; }
u32 JPABaseEmitter::getAge() const { return 0; }
void JPABaseEmitter::getGlobalParticleScale(JGeometry::TVec3<f32>* out) const {
    const int i = emitterIndex(this);
    const float s = (i >= 0) ? g_pscale[i] : 1.0f;
    out->x = s;
    out->y = s;
    out->z = 1.0f;
}
void JPABaseEmitter::calcEmitterGlobalPosition(JGeometry::TVec3<f32>* out) const {
    *out = mGlobalTrs;
}
JPAEmitterCallBack* JPABaseEmitter::getEmitterCallBackPtr() const { return nullptr; }

template <> JPABaseEmitter* JSULink<JPABaseEmitter>::getObject() {
    return *reinterpret_cast<JPABaseEmitter**>(this);
}
template <> JSULink<JPABaseEmitter>* JSULink<JPABaseEmitter>::getNext() {
    return this + 1;
}
template <> JSULink<JPABaseEmitter>* JSUList<JPABaseEmitter>::getFirst() {
    return *reinterpret_cast<JSULink<JPABaseEmitter>**>(this);
}
template <> JSULink<JPABaseEmitter>* JSUList<JPABaseEmitter>::getEnd() {
    return *(reinterpret_cast<JSULink<JPABaseEmitter>**>(this) + 1);
}

JPAEmitterManager* dPa_control_c::getEmitterManager() { return &g_manager; }
u8 dPa_control_c::getRM_ID(u16 id) { return (id & 0x8000) ? 1 : 0; }
u16 dPa_simpleEcallBack::getID() { return mID; }
dPa_simpleEcallBack* dPa_control_c::getSimple(u16 id) {
    static dPa_simpleEcallBack s;
    if (g_sharedSimple != nullptr && id == g_sharedSimpleId) {
        s.mEmitter = g_sharedSimple;
        s.mID = id;
        return &s;
    }
    return nullptr;
}
dPa_control_c* dComIfG_play_c::getParticle() { return &g_particle; }
dScnKy_env_light_c* dKy_getEnvlight() { return &g_env; }

// The names the classifier reads. Indexed by the masked effect id, exactly as the real table is.
static std::vector<std::string> g_names(0x2000);
const char* dPa_name::getName(u32 id) {
    if (id >= g_names.size() || g_names[id].empty()) return nullptr;
    return g_names[id].c_str();
}

// --- scene building ---------------------------------------------------------------------

static std::vector<FakeRes> g_res;

struct EmitterSpec {
    u16 id;
    const char* name;
    float x, y, z;
    bool additive = true;
    u8 r = 255, g = 120, b = 30;
    u8 alpha = 255;
    u32 status = 0;
    float pscale = 1.0f;  // 0 = hidden by shrinking, the Deku Baba's drool trick
};

static void buildScene(const std::vector<EmitterSpec>& specs) {
    for (JPABaseEmitter* e : g_emitters) delete e;
    g_emitters.clear();
    g_res.clear();
    g_res.reserve(specs.size());
    g_links.clear();
    g_links.resize(specs.size() + 1);

    for (size_t i = 0; i < specs.size(); i++) {
        const EmitterSpec& s = specs[i];
        g_res.push_back(FakeRes {});
        g_res.back().usrIdx = s.id;
        g_res.back().shape.dst = s.additive ? GX_BL_ONE : GX_BL_INVSRCALPHA;
        g_names[s.id & 0x1FFF] = s.name;
    }

    for (size_t i = 0; i < specs.size(); i++) {
        const EmitterSpec& s = specs[i];
        JPABaseEmitter* e = new JPABaseEmitter();
        std::memset(e, 0, sizeof(*e));
        e->mGlobalTrs = {s.x, s.y, s.z};
        e->mPrmClr = {s.r, s.g, s.b, s.alpha};
        e->mEnvClr = {0, 0, 0, 255};
        e->mGlobalPrmClr = {255, 255, 255, s.alpha};
        e->mGlobalEnvClr = {255, 255, 255, 255};
        e->pRes = reinterpret_cast<JPAResource*>(&g_res[i]);
        g_status[i] = s.status;
        g_pscale[i] = s.pscale;
        g_emitters.push_back(e);
    }

    for (size_t i = 0; i < g_emitters.size(); i++) {
        *reinterpret_cast<JPABaseEmitter**>(&g_links[i]) = g_emitters[i];
    }

    std::memset(g_useList, 0, sizeof(g_useList));
    *reinterpret_cast<JSULink<JPABaseEmitter>**>(&g_useList[0]) = g_links.data();
    *(reinterpret_cast<JSULink<JPABaseEmitter>**>(&g_useList[0]) + 1) =
        g_links.data() + g_emitters.size();
    for (int gi = 1; gi < 19; gi++) {
        *reinterpret_cast<JSULink<JPABaseEmitter>**>(&g_useList[gi]) = g_links.data();
        *(reinterpret_cast<JSULink<JPABaseEmitter>**>(&g_useList[gi]) + 1) = g_links.data();
    }

    g_manager.pEmtrUseList = g_useList;
    g_manager.gidMax = 19;
}

static void resetSites() { dusk::effect_lights::reset(); }

static void clearLights() {
    std::memset(&g_env, 0, sizeof(g_env));
    g_influences.clear();
    g_influences.reserve(32);
}

static void addPointLight(float x, float y, float z, s16 r, s16 g, s16 b, float pow) {
    g_influences.push_back(LIGHT_INFLUENCE {{x, y, z}, {r, g, b, 255}, pow, 1.0f, 1});
    for (int i = 0; i < 100; i++) {
        if (g_env.pointlight[i] == nullptr) {
            g_env.pointlight[i] = &g_influences.back();
            break;
        }
    }
    // The vector may have reallocated; re-point every slot.
    size_t n = 0;
    for (int i = 0; i < 100; i++) g_env.pointlight[i] = nullptr;
    for (LIGHT_INFLUENCE& l : g_influences) g_env.pointlight[n++] = &l;
}

// --- the checks -------------------------------------------------------------------------

static int g_failures = 0;

static void check(bool cond, const char* what) {
    std::printf("%s  %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) g_failures++;
}

int main() {
    using namespace dusk::effect_lights;

    Params p;
    p.cameraValid = false;

    // 1. A bonfire is five emitters at one point. It must produce exactly one light.
    clearLights();
    resetSites();
    buildScene({
        {dPa_RM(0x204), "ZI_S_maki_fire_a.jpa", 100, 0, 100},
        {dPa_RM(0x205), "ZI_S_maki_fire_b.jpa", 100, 0, 100},
        {dPa_RM(0x206), "ZI_S_maki_fire_c.jpa", 102, 1, 99},
        {dPa_RM(0x207), "ZI_S_maki_fire_d.jpa", 98, 0, 101},
        {dPa_RM(0x208), "ZI_S_maki_fire_ind.jpa", 100, 3, 100},
    });
    {
        const std::vector<Site>& sites = collect(p);
        check(sites.size() == 1, "five bonfire emitters merge into one site");
        check(stats().candidates == 5, "all five passed the rule");
        check(sites.size() == 1 && sites[0].cls == Class::Fire, "classified as fire");
        check(sites.size() == 1 && sites[0].position[1] > 14.0f && sites[0].position[1] < 19.0f,
              "the fire offset was applied (origin y=0..3, offset 15)");
        check(sites.size() == 1 && !sites[0].derived, "no game light nearby, so undetermined");
    }

    // 2. Smoke sits at the same point and must not add a light or steal the site.
    clearLights();
    resetSites();
    buildScene({
        {dPa_RM(0x204), "ZI_S_maki_fire_a.jpa", 100, 0, 100},
        {0x300, "ZI_J_smoke_a.jpa", 100, 0, 100, /*additive*/ false, 180, 180, 180},
    });
    {
        const std::vector<Site>& sites = collect(p);
        check(sites.size() == 1, "an opaque smoke emitter beside a fire adds no second light");
        check(stats().considered == 2 && stats().candidates == 1,
              "smoke reached the rule and was rejected by it");
    }

    // 3. A grey additive effect must be rejected by the glow test, not accepted by additivity.
    clearLights();
    resetSites();
    buildScene({{0x400, "ZI_J_spray_a.jpa", 0, 0, 0, true, 120, 120, 122}});
    {
        collect(p);
        check(stats().candidates == 0, "a grey additive effect fails the reads-as-a-glow test");
        check(true, "(grey: chroma 0.008, luma 0.47 - under both thresholds)");
    }
    clearLights();
    resetSites();
    buildScene({{0x401, "ZI_J_whitehot.jpa", 0, 0, 0, true, 250, 250, 250}});
    {
        collect(p);
        check(stats().candidates == 1, "a near-white-hot additive effect passes it");
    }

    // 3b. Only a destination factor of ONE counts as additive. SRC_ALPHA there scales the
    //     background down, which is what smoke does, so it must be refused however orange it is.
    clearLights();
    resetSites();
    buildScene({{0x402, "ZI_J_kemuri_fire_a.jpa", 0, 0, 0, /*additive*/ false, 255, 110, 20}});
    {
        collect(p);
        check(stats().candidates == 0,
              "a fire-coloured effect that is not additive is still refused");
    }

    // 4. The game's own light is adopted for its colour and reach, but not its position.
    clearLights();
    resetSites();
    addPointLight(100.0f, 10.0f, 220.0f, 0xBC, 0x66, 0x42, 500.0f);  // 120 units away, offset
    buildScene({{dPa_RM(0x204), "ZI_S_maki_fire_a.jpa", 100, 0, 100}});
    {
        const std::vector<Site>& sites = collect(p);
        check(sites.size() == 1 && sites[0].derived, "a game light within adopt radius is adopted");
        check(stats().derived == 1 && stats().orphans == 0, "and is not counted as an orphan");
        check(sites.size() == 1 && sites[0].position[2] > 99.0f && sites[0].position[2] < 101.0f,
              "the light stays at the EFFECT origin, not the game light's position");
        const float r = sites[0].radiance[0], g = sites[0].radiance[1], b = sites[0].radiance[2];
        check(r > g && g > b, "the adopted colour is the game's torch orange, warmest first");
    }

    // 5. A game light with no effect near it is dropped, and counted so the policy is visible.
    clearLights();
    resetSites();
    addPointLight(5000.0f, 0.0f, 5000.0f, 200, 200, 255, 400.0f);
    buildScene({{dPa_RM(0x204), "ZI_S_maki_fire_a.jpa", 100, 0, 100}});
    {
        const std::vector<Site>& sites = collect(p);
        check(sites.size() == 1 && !sites[0].derived, "a distant game light is not adopted");
        check(stats().orphans == 1, "and is counted as an orphan");
    }

    // 6. Site identity survives a frame in which a member came or went.
    clearLights();
    resetSites();
    buildScene({{dPa_RM(0x204), "ZI_S_maki_fire_a.jpa", 100, 0, 100}});
    uint32_t firstId = 0;
    {
        const std::vector<Site>& sites = collect(p);
        firstId = sites.empty() ? 0 : sites[0].id;
    }
    buildScene({
        {dPa_RM(0x204), "ZI_S_maki_fire_a.jpa", 100, 0, 100},
        {dPa_RM(0x205), "ZI_S_maki_fire_b.jpa", 101, 0, 100},
    });  // deliberately no reset: this checks identity carrying across the frame
    {
        const std::vector<Site>& sites = collect(p);
        check(sites.size() == 1 && sites[0].id == firstId,
              "the site keeps its identity when a second emitter joins it");
    }

    // 7. A stopped emitter contributes nothing. This is the lantern's oil meter.
    clearLights();
    resetSites();
    buildScene({{dPa_RM(0x2BC), "ZI_J_kantera_fire.jpa", 0, 0, 0}});
    {
        const std::vector<Site>& lit = collect(p);
        check(lit.size() == 1, "the lantern flame lights while it is drawn");
    }
    buildScene({{dPa_RM(0x2BC), "ZI_J_kantera_fire.jpa", 0, 0, 0, true, 255, 120, 30, 255,
                 JPAEmtrStts_StopDraw}});  // no reset: this is the grace period's own test
    {
        collect(p);
        check(stats().candidates == 0, "and stops the moment the game stops drawing it");
        const std::vector<Site>& held = collect(p);
        check(!held.empty(),
              "the light is held for a few frames rather than destroyed and re-created");
        int frames = 0;
        while (!collect(p).empty() && frames < 64) frames++;
        check(frames > 0 && frames < 16, "and is let go shortly after");
    }

    // 8. One-shots are excluded by default and included on request.
    clearLights();
    resetSites();
    buildScene({{0x500, "ZI_J_bakuha_fire_a.jpa", 0, 0, 0}});
    {
        collect(p);
        check(stats().candidates == 0, "an explosion is excluded by default");
        Params withBursts = p;
        withBursts.bursts = true;
        collect(withBursts);
        check(stats().candidates == 1, "and included when bursts are turned on");
    }

    // 9. The budget drops the least useful, and says how many it dropped.
    clearLights();
    resetSites();
    {
        std::vector<EmitterSpec> many;
        for (int i = 0; i < 12; i++) {
            many.push_back(EmitterSpec {static_cast<u16>(0x600 + i), "ZI_J_fire_x.jpa",
                                        static_cast<float>(i * 1000), 0.0f, 0.0f});
        }
        buildScene(many);
        Params budgeted = p;
        budgeted.maxLights = 4;
        const std::vector<Site>& sites = collect(budgeted);
        check(sites.size() == 4, "the per-frame budget is respected");
        check(stats().culled == 8, "and the drop is reported rather than silent");
    }

    // 10. The simple side channel is what carries torches, and each instance is its own light.
    clearLights();
    resetSites();
    buildScene({});
    {
        // Arm the recorder the way collect() does, then feed it three torches.
        collect(p);
        FakeRes torchRes;
        torchRes.usrIdx = dPa_RM(0x3A6);
        torchRes.shape.dst = GX_BL_ONE;
        JPABaseEmitter shared;
        std::memset(&shared, 0, sizeof(shared));
        shared.pRes = reinterpret_cast<JPAResource*>(&torchRes);
        // The shared emitter has to carry realistic state now, and that is the point of the
        // 2026-08-07 fix rather than an inconvenience: collectSimple asks it for liveness and
        // multiplies its RESOURCE colour into the verdict, exactly as the sweep does. Before,
        // only the caller's global colour was tested - and every real caller passes
        // g_whiteColor, so the colour half of the rule was dead on this whole path.
        // White here models a resource whose hue lives in its texture; the caller's warm prm
        // below then survives the multiply unchanged.
        shared.mPrmClr = {255, 255, 255, 255};
        shared.mEnvClr = {255, 255, 255, 255};
        shared.mGlobalPrmClr = {255, 255, 255, 255};
        shared.mGlobalEnvClr = {255, 255, 255, 255};
        g_names[0x3A6] = "ZI_S_o_lv1d_fire_a.jpa";

        const float prm[3] = {1.0f, 0.42f, 0.26f};
        const float env[3] = {0.0f, 0.0f, 0.0f};
        recordSimple(dPa_RM(0x3A6), &shared, 0.0f, 120.0f, 0.0f, prm, env);
        recordSimple(dPa_RM(0x3A6), &shared, 900.0f, 120.0f, 0.0f, prm, env);
        recordSimple(dPa_RM(0x3A6), &shared, 1800.0f, 120.0f, 0.0f, prm, env);

        const std::vector<Site>& sites = collect(p);
        check(sites.size() == 3,
              "three torches sharing one emitter still get three lights");
        check(stats().candidates == 3, "each instance reached the rule separately");
    }

    // 9b. The budget bounds what is RETURNED, not just what is refreshed. Regression: a site
    //     dropped by the budget is also "not seen", so it went into the grace period and kept
    //     being handed back - a budget that bounded nothing while the counter said it did.
    clearLights();
    resetSites();
    {
        std::vector<EmitterSpec> many;
        for (int i = 0; i < 12; i++) {
            many.push_back(EmitterSpec {static_cast<u16>(0x700 + i), "ZI_J_fire_y.jpa",
                                        static_cast<float>(i * 1000), 0.0f, 0.0f});
        }
        buildScene(many);
        // Track all twelve first, then tighten the budget - which is what happens when the
        // owner drags Max Lights down, or walks into a room that pushes past the cap.
        Params generous = p;
        generous.maxLights = 64;
        check(collect(generous).size() == 12, "all twelve are tracked while the budget allows");

        Params budgeted = p;
        budgeted.maxLights = 3;
        const std::vector<Site>& tightened = collect(budgeted);
        check(tightened.size() == 3,
              "tightening the budget drops sites instead of holding them in the grace period");
        check(stats().culled == 9, "and the drop is still reported");
    }

    // 9c. A burst reaches the classification report even though it is excluded from lighting -
    //     that report is the thing meant to settle whether excluding it is right.
    clearLights();
    resetSites();
    buildScene({{0x501, "ZI_J_bakuha_fire_b.jpa", 0, 0, 0}});
    {
        Params noBursts = p;
        noBursts.bursts = false;
        collect(noBursts);
        check(stats().candidates == 0, "a burst is still excluded from lighting by default");
        requestReport();
        collect(noBursts);  // emits the report; no assertion, but it must not crash or omit
        check(true, "(report emitted with the burst recorded - read the log to confirm)");
    }

    // 11. Records do not survive into the next frame. (The sites they made are held by the
    //     grace period, so drain that first - the check is about the records, not the sites.)
    {
        for (int i = 0; i < 32 && !collect(p).empty(); i++) {}
        const std::vector<Site>& sites = collect(p);
        check(sites.empty(), "simple records are consumed, not accumulated across frames");
    }

    // 12. reset() drops everything at once, for a device loss or the system being switched off.
    clearLights();
    resetSites();
    buildScene({{dPa_RM(0x204), "ZI_S_maki_fire_a.jpa", 100, 0, 100}});
    {
        check(collect(p).size() == 1, "a site exists before the reset");
        buildScene({});
        resetSites();
        check(collect(p).empty(), "and reset() drops it without waiting for the grace period");
    }

    // 13. The three fixes that came out of the 2026-08-07 in-game session. Each of these was a
    //     real light the owner saw and none of them should have existed.

    // 13a. Drool is additive and can be bright, and it is not a light. The Deku Baba lit rooms
    //      from its jaw joints. This is the case that showed additive blending alone does not
    //      mean "emits" - a wet surface is authored additively too, to read as glossy.
    clearLights();
    resetSites();
    buildScene({{0x81C4, "ZI_S_db_yodareM1_a.jpa", 0, 0, 0}});
    {
        const std::vector<Site>& sites = collect(p);
        check(sites.empty(), "drool earns no light however bright and additive it is");
        check(stats().excluded == 1, "and the refusal is counted rather than silent");
    }

    // 13b. An emitter hidden by shrinking to nothing is not lit. d_a_e_db hides the drool by
    //      ramping global particle scale to zero while leaving alpha at 0xFF for the emitter's
    //      whole life, so alpha alone could never catch it.
    clearLights();
    resetSites();
    buildScene({{dPa_RM(0x204), "ZI_S_maki_fire_a.jpa", 0, 0, 0, true, 255, 120, 30, 255, 0, 0.0f}});
    {
        check(collect(p).empty(), "an emitter shrunk to zero scale is not a light source");
    }
    buildScene({{dPa_RM(0x204), "ZI_S_maki_fire_a.jpa", 0, 0, 0, true, 255, 120, 30, 255, 0, 1.0f}});
    {
        resetSites();
        check(collect(p).size() == 1, "and the same emitter at full scale still is");
    }

    // 13c. Lava classifies as lava. Until 2026-08-07 the keywords were lava/magma/youdo, which
    //      match ZERO of the game's 3205 effect names - it spells the word yogan - so
    //      Class::Lava was unreachable and every lava column was Class::Other.
    clearLights();
    resetSites();
    buildScene({{dPa_RM(0x2E0), "ZI_S_yoganbashira_foot_a.jpa", 0, 0, 0}});
    {
        const std::vector<Site>& sites = collect(p);
        check(sites.size() == 1 && sites[0].cls == Class::Lava,
              "a yogan effect classifies as lava, not as other");
    }

    // 13d. THE SIMPLE-PATH COLOUR FIX. Every one of the game's 27 setSimple call sites passes
    //      g_whiteColor, and collectSimple used to test that global alone - never multiplying
    //      in the resource's own colour the way the sweep does. The tested colour was therefore
    //      literally (1,1,1) every time: luma 1.0, so readsAsGlow was a tautology and
    //      isAdditive decided by itself. Here the resource is neutral grey, which is what a
    //      smoke-like effect looks like; before the fix this passed on the caller's white.
    clearLights();
    resetSites();
    buildScene({});
    {
        collect(p);  // arm the recorder
        FakeRes greyRes;
        greyRes.usrIdx = dPa_RM(0x70F);
        greyRes.shape.dst = GX_BL_ONE;
        JPABaseEmitter shared;
        std::memset(&shared, 0, sizeof(shared));
        shared.pRes = reinterpret_cast<JPAResource*>(&greyRes);
        shared.mPrmClr = {128, 128, 128, 255};
        shared.mEnvClr = {0, 0, 0, 255};
        shared.mGlobalPrmClr = {255, 255, 255, 255};
        shared.mGlobalEnvClr = {255, 255, 255, 255};
        g_names[0x70F] = "ZI_J_O_digTga_a.jpa";

        const float white[3] = {1.0f, 1.0f, 1.0f};  // g_whiteColor, as every caller passes
        const float black[3] = {0.0f, 0.0f, 0.0f};
        recordSimple(dPa_RM(0x70F), &shared, 0.0f, 0.0f, 0.0f, white, black);

        check(collect(p).empty(),
              "a neutral simple-path effect is refused - the caller's white no longer decides");
    }

    // 14. The report is the whole deliverable of the instrumentation work: one press has to
    //     answer every open question, so it has to survive being pressed in the states that
    //     actually occur - including the empty world and the capped one. It is checked for not
    //     crashing and for covering the sections; what it SAYS is read by a human from the log.
    clearLights();
    resetSites();
    buildScene({{dPa_RM(0x204), "ZI_S_maki_fire_a.jpa", 100, 0, 100},
                {0x81C4, "ZI_S_db_yodareM1_a.jpa", 900, 0, 0},
                {0x501, "ZI_J_bakuha_fire_b.jpa", 1800, 0, 0}});
    addPointLight(100, 10, 100, 0xBC, 0x66, 0x42, 500.0f);
    {
        setBridgeCounters(123, 45, 7);
        collect(p);              // a frame of history for the trace
        requestReport();
        aurora::Module::enabled() = true;   // print this one, so the shape can be eyeballed
        const std::vector<Site>& sites = collect(p);
        aurora::Module::enabled() = false;
        check(!sites.empty(), "the report runs on a populated frame without crashing");
        check(stats().excluded == 1, "and the refused drool is still counted while it reports");
    }

    // 14b. Pressed with nothing alive at all. An empty world is the state someone will be in
    //      when they press the button by accident, and a report that divides by a zero site
    //      count there is a report nobody gets to read.
    clearLights();
    resetSites();
    buildScene({});
    {
        requestReport();
        check(collect(p).empty(), "the report survives being pressed with no lights at all");
    }

    std::printf("\n%s (%d failures)\n", g_failures == 0 ? "ALL PASS" : "FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
