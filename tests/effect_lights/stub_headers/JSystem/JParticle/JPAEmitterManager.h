#pragma once
#include <types.h>
#include <JSystem/JParticle/JPAEmitter.h>
template <typename T> struct JSUList { void* storage[2]; JSULink<T>* getFirst(); JSULink<T>* getEnd(); };
class JPAEmitterManager {
public:
    JSUList<JPABaseEmitter>* pEmtrUseList;
    u8 gidMax;
};
