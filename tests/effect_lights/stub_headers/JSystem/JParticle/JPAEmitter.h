#pragma once
#include <types.h>
#include <gx.h>
namespace JGeometry { template <typename T> struct TVec3 { T x, y, z; }; }
enum { JPAEmtrStts_StopEmit = 0x01, JPAEmtrStts_StopCalc = 0x02, JPAEmtrStts_StopDraw = 0x04,
       JPAEmtrStts_EnableDeleteEmitter = 0x08, JPAEmtrStts_Immortal = 0x40, JPAEmtrStts_Delete = 0x100 };
class JPAEmitterCallBack;
class JPAResource;
template <typename T> struct JSULink { void* storage[2]; T* getObject(); JSULink<T>* getNext(); };
class JPABaseEmitter {
public:
    u32 checkStatus(u32) const;
    u8 getGlobalAlpha() const;
    u32 getParticleNumber() const;
    // Signature copied verbatim from libs/JSystem/include/JSystem/JParticle/JPAEmitter.h:172.
    // The real one returns (mGlobalPScl.x, mGlobalPScl.y, 1.0f); the test double lets a case
    // set it, so a hidden-by-zero-scale emitter can be expressed.
    void getGlobalParticleScale(JGeometry::TVec3<f32>*) const;
    // Lifetime terms, reported and never decided on. Names and types copied from the real
    // header: mMaxFrame is the EMISSION window (0 = continuous), mLifeTime is PARTICLE life and
    // is rewritten every frame by calcKey, and getAge() returns mTick, the emitter's own age in
    // completed calc frames.
    u32 getAge() const;
    void calcEmitterGlobalPosition(JGeometry::TVec3<f32>*) const;
    JPAEmitterCallBack* getEmitterCallBackPtr() const;
    JGeometry::TVec3<f32> mGlobalTrs;
    GXColor mPrmClr, mEnvClr, mGlobalPrmClr, mGlobalEnvClr;
    s32 mMaxFrame;
    s16 mLifeTime;
    JPAResource* pRes;
    JSULink<JPABaseEmitter> mLink;
};
