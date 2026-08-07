#pragma once
#include <types.h>
class JPABaseShape;
class JPAKeyBlock;

// Stub of libs/JSystem/include/JSystem/JParticle/JPAResource.h. keyNum and ppKey are PUBLIC
// FIELDS on the real class (offsets 0x3F and 0x34), not accessors - effect_lights walks them to
// report which authored curves reach the emitter. Declared in the same shape so the module's
// use of them compiles the same way here as it does against the game's header.
class JPAResource {
public:
    JPABaseShape* getBsp() const;
    u16 getUsrIdx() const;

    JPAKeyBlock** ppKey;
    u8 keyNum;
};
