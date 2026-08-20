#pragma once
#include <types.h>

// Stub of libs/JSystem/include/JSystem/JParticle/JPADynamicsBlock.h. Only the four accessors
// effect_lights reads off the AUTHORED dynamics block are declared - the real class carries
// thirty-odd, all of them one-line reads out of the parsed JPADynamicsBlockData.
//
// The return TYPES matter as much as the names. getMaxFrame is s16 and the module compares it
// against 0 ("emit forever"); getVolumeSize is u16 and is cast to float before use; getVolumeType
// is the u32 the real accessor builds out of bits 8-10 of mFlags, and the module compares it
// against the VOL_Point value it repeats with a citation rather than including.
// getResUserWork is the effect's authored user-work word (mResUserWork, verbatim); the module
// carries it into the report so an effect can be identified by what its artist wrote rather than
// only by emitter id.
class JPADynamicsBlock {
public:
    u32 getResUserWork() const;
    u32 getVolumeType() const;
    s16 getMaxFrame() const;
    u16 getVolumeSize() const;
};
