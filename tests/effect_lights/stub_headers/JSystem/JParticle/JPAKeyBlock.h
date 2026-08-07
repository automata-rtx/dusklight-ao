#pragma once
#include "JSystem/JParticle/JPABaseShape.h"

// Stub of libs/JSystem/include/JSystem/JParticle/JPAKeyBlock.h. Only getID() is used by
// effect_lights, to report WHICH authored curves land on the emitter (JPAResource::calcKey
// dispatches on this id). The real one reads mDataStart[8]; the double lets a case set it.
class JPAKeyBlock {
public:
    u8 getID() const;
};
