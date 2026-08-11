#pragma once
#include <gx.h>
#include <types.h>

// Stub of libs/JSystem/include/JSystem/JParticle/JPABaseShape.h. Signatures copied from the
// real header so the module compiles identically here; the return TYPES matter as much as the
// names (isGlblClrAnm and friends return BOOL, not bool, and the module compares against 0).
//
// The animation accessors are what the classification report prints to say whether an effect's
// colour is capable of animating at all: mPrmClr is only re-sampled per frame when
// isGlblClrAnm() && isPrmAnm(), and two of the five getClrAnmType() values pin the key frame at
// 0 on the emitter path and never move.
struct JPABaseShape {
    GXBlendMode getBlendMode() const;
    GXBlendFactor getBlendSrc() const;
    GXBlendFactor getBlendDst() const;

    BOOL isGlblClrAnm() const;
    BOOL isPrmAnm() const;
    BOOL isEnvAnm() const;
    u32 getClrAnmType() const;
    s16 getClrAnmMaxFrm() const;
    f32 getBaseSizeX() const;
    f32 getBaseSizeY() const;
};
