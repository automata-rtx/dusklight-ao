#ifndef M_DO_M_DO_LIB_H
#define M_DO_M_DO_LIB_H

#include "JSystem/J3DU/J3DUClipper.h"
#include <dolphin/gx/GXStruct.h>

#if TARGET_PC
#include "JSystem/JGeometry.h"
#endif

#include "helpers/gx_helper.h"

typedef struct Vec Vec;
struct ResTIMG;

struct mDoLib_clipper {
    static void setup(f32, f32, f32, f32);

    static void changeFar(f32 far_) {
        mClipper.setFar(far_);
        mClipper.calcViewFrustum();
    }

    // Every frustum test in the game funnels through these two, and a non-zero result means
    // "outside, drop it": actors (fopAcM_cullingCheck), room geometry (d_a_bg hides the shape),
    // and grass all key off them. Reporting "inside" unconditionally is therefore a single
    // switch for the whole thing.
    //
    // That is worth having under a path tracer, where geometry outside the view still shapes the
    // image: a wall culled because the camera turned away stops occluding, and its light spills
    // into rooms it should never reach. The cost is that everything in the room is submitted
    // every frame, which is exactly what the game spends this machinery avoiding - so it stays
    // off by default. Set through mDoLib_clipper::setDisableCulling from the graphics settings.
    static int clip(const Mtx m, const Vec* param_1, const Vec* param_2) {
        if (mDisableCulling) {
            return 0;
        }
        return mClipper.clip(m, (Vec*)param_1, (Vec*)param_2);
    }

    static s32 clip(const Mtx m, Vec param_1, f32 param_2) {
        if (mDisableCulling) {
            return 0;
        }
        return mClipper.clip(m, param_1, param_2);
    }

    static void setDisableCulling(bool disable) { mDisableCulling = disable; }
    static bool getDisableCulling() { return mDisableCulling; }

    static f32 getFar() { return mSystemFar; }
    static f32 getFovyRate() { return mFovyRate; }
    
    static void resetFar() {
        mClipper.setFar(mSystemFar);
        mClipper.calcViewFrustum();
    }

    static DUSK_GAME_DATA J3DUClipper mClipper;
    static DUSK_GAME_DATA f32 mSystemFar;
    static DUSK_GAME_DATA f32 mFovyRate;
    static DUSK_GAME_DATA bool mDisableCulling;
};

void mDoLib_project(Vec* src, Vec* dst);
u32 mDoLib_setResTimgObj(ResTIMG const* res, TGXTexObj* o_texObj, u32 tlut_name,
                         TGXTlutObj* o_tlutObj);
void mDoLib_pos2camera(Vec* src, Vec* dst);

#if PLATFORM_WII
void mDoLib_2Dto3D(f32, f32, f32, Vec*);
#endif

#endif /* M_DO_M_DO_LIB_H */
