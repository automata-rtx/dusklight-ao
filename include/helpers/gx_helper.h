#pragma once

#include <cstring>

#include <dolphin/gx/GXAurora.h>
#include <dolphin/gx/GXExtra.h>

#if defined(DUSK_BUILDING_GAME)
#include <tracy/Tracy.hpp>
#else
#ifndef ZoneScopedN
#define ZoneScopedN(name)
#endif
#endif

#if DUSK_GFX_DEBUG_GROUPS
#define GX_DEBUG_GROUP(name, ...) \
    do {                          \
        GXPushDebugGroup(#name);  \
        name(__VA_ARGS__);        \
        GXPopDebugGroup();        \
    } while (0)
#else
#define GX_DEBUG_GROUP(name, ...) name(__VA_ARGS__)
#endif

#ifdef TARGET_PC
class GXTexObjRAII : public GXTexObj {
public:
    GXTexObjRAII() : GXTexObj() {}
    ~GXTexObjRAII() { GXDestroyTexObj(this); }

    void reset() { GXDestroyTexObj(this); }

    GXTexObjRAII(const GXTexObjRAII&) = delete;
    GXTexObjRAII& operator=(const GXTexObjRAII&) = delete;
    GXTexObjRAII(GXTexObjRAII&& o) = delete;/*noexcept : GXTexObj(o) {
        std::memset(static_cast<GXTexObj*>(&o), 0, sizeof(GXTexObj));
    }*/
    GXTexObjRAII& operator=(GXTexObjRAII&& o) = delete;/*noexcept {
        if (this != &o) {
            GXDestroyTexObj(this);
            std::memcpy(static_cast<GXTexObj*>(this), &o, sizeof(GXTexObj));
            std::memset(static_cast<GXTexObj*>(&o), 0, sizeof(GXTexObj));
        }
        return *this;
    }*/
};
static_assert(sizeof(GXTexObjRAII) == sizeof(GXTexObj),
              "GXTexObjRAII should have the same size as GXTexObj");
typedef GXTexObjRAII TGXTexObj;

class GXTlutObjRAII : public GXTlutObj {
public:
    GXTlutObjRAII() : GXTlutObj() {}
    ~GXTlutObjRAII() { GXDestroyTlutObj(this); }

    void reset() { GXDestroyTlutObj(this); }

    GXTlutObjRAII(const GXTlutObjRAII&) = delete;
    GXTlutObjRAII& operator=(const GXTlutObjRAII&) = delete;
    GXTlutObjRAII(GXTlutObjRAII&&) = delete;
    GXTlutObjRAII& operator=(GXTlutObjRAII&&) = delete;
};
static_assert(sizeof(GXTlutObjRAII) == sizeof(GXTlutObj),
              "GXTlutObjRAII should have the same size as GXTlutObj");
typedef GXTlutObjRAII TGXTlutObj;
#else
typedef GXTexObj TGXTexObj;
typedef GXTlutObj TGXTlutObj;
#endif

// Declares what the draws inside this scope represent, for backends that have to resolve
// transparency differently depending on what it is (today: the D3D9/Remix path).
//
// Remix decides "is this alpha-blended draw a particle" from a texture tag, and answers it
// once per texture. This game reuses textures across contexts constantly, so that answer is
// wrong somewhere almost by construction - and it picks very different renderers: a tagged
// particle is accumulated layer by layer in the unordered TLAS, an untagged one gets a
// stochastic single-layer pick lit from a neighbouring opaque pixel, which is what makes
// dense smoke noisy. The game knows which is which, so it says so per draw.
//
// Unlike GXScopedDebugGroup this is NOT compiled out in release: it carries behaviour, not
// diagnostics. GXSetDrawClass itself is a cheap FIFO write that every backend but D3D9
// ignores, so an unclassified build and a classified one render identically off D3D9.
// extern/aurora/docs/dx9/remix-material-interface.md §11.
struct GXScopedDrawClass {
    explicit GXScopedDrawClass(u32 drawClass) { GXSetDrawClass(drawClass); }
    ~GXScopedDrawClass() { GXSetDrawClass(GX_AURORA_DRAW_CLASS_NONE); }

    GXScopedDrawClass(const GXScopedDrawClass&) = delete;
    GXScopedDrawClass& operator=(const GXScopedDrawClass&) = delete;
};

// Declares which of the game's draw lists is executing, so a backend can attribute a draw to
// the code that issued it. This is the working replacement for GXPushDebugGroup, which cannot
// do it: a group pushed around an actor's draw method labels where the draw is *scheduled* into
// a J3D buffer, not where GX commands are *issued* when that buffer is walked - which is why
// every material ever logged reported grp=-. It is also compiled out in release. This is a FIFO
// write at the right moment, and it is not compiled out.
//
// Diagnostic only: nothing renders differently because of it.
struct GXScopedDrawPhase {
    explicit GXScopedDrawPhase(u32 phase) { GXSetDrawPhase(phase); }
    ~GXScopedDrawPhase() { GXSetDrawPhase(GX_AURORA_DRAW_PHASE_NONE); }

    GXScopedDrawPhase(const GXScopedDrawPhase&) = delete;
    GXScopedDrawPhase& operator=(const GXScopedDrawPhase&) = delete;
};

struct GXScopedDebugGroup {
    explicit GXScopedDebugGroup(const char* text) {
#if DUSK_GFX_DEBUG_GROUPS
        GXPushDebugGroup(text);
#endif
    }
    ~GXScopedDebugGroup() {
#if DUSK_GFX_DEBUG_GROUPS
        GXPopDebugGroup();
#endif
    }
};

#define GX_AND_TRACY_SCOPED(name) GXScopedDebugGroup scope(name); ZoneScopedN(name);
