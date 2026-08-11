#pragma once
#include <types.h>
#include <gx.h>
#include <SSystem/SComponent/c_xyz.h>
struct LIGHT_INFLUENCE { cXyz mPosition; GXColorS10 mColor; f32 mPow; f32 mFluctuation; int mIndex; };
struct BOSS_LIGHT { cXyz mPos; GXColor mColor; f32 mRefDistance; f32 field_0x14; f32 mCutoffAngle;
                    f32 mAngleX; f32 mAngleY; u8 mAngleAttenuation; u8 mDistAttenuation; u8 field_0x26; };
struct dScnKy_env_light_c {
    LIGHT_INFLUENCE* pointlight[100];
    LIGHT_INFLUENCE* efplight[5];
    BOSS_LIGHT field_0x0c18[8];
};
dScnKy_env_light_c* dKy_getEnvlight();
