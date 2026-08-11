#pragma once
#include <types.h>
#include <SSystem/SComponent/c_xyz.h>
class JPABaseEmitter;
class JPAEmitterManager;
class dPa_simpleEcallBack {
public:
    u16 getID();
    JPABaseEmitter* mEmitter;
    u16 mID;
};
class dPa_control_c {
public:
    static u8 getRM_ID(u16);
    dPa_simpleEcallBack* getSimple(u16);
    static JPAEmitterManager* getEmitterManager();
};
