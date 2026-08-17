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
//
// getPrmClr/getEnvClr are the ONE-argument overloads (JPABaseShape.h:116,118), which read the
// authored flat colour out of the block. The real header also carries (s16 idx, GXColor*)
// overloads that index the tables below; the module does not call them - it walks the tables
// itself, looking for the most saturated entry rather than a frame - so they are not stubbed.
//
// mpPrmClrAnmTbl and mpEnvClrAnmTbl are PUBLIC FIELDS on the real class (offsets 0x0C and 0x10),
// not accessors, and effect_lights reads them directly. Same rule as JPAResource::ppKey: they
// are declared here in the order the test's FakeShape declares them, because the harness reaches
// them through THIS declaration rather than through a reinterpret_cast'd accessor. They are NULL
// unless the matching anim flag is set, which is the case rampColor() branches on first.
struct JPABaseShape {
    GXColor* mpPrmClrAnmTbl;
    GXColor* mpEnvClrAnmTbl;

    GXBlendMode getBlendMode() const;
    GXBlendFactor getBlendSrc() const;
    GXBlendFactor getBlendDst() const;

    BOOL isGlblClrAnm() const;
    BOOL isPrmAnm() const;
    BOOL isEnvAnm() const;
    u32 getClrAnmType() const;
    s16 getClrAnmMaxFrm() const;
    void getPrmClr(GXColor* dst) const;
    void getEnvClr(GXColor* dst) const;
    f32 getBaseSizeX() const;
    f32 getBaseSizeY() const;
};
