#pragma once
#include <types.h>
class JPABaseShape;
class JPAResource {
public:
    JPABaseShape* getBsp() const;
    u16 getUsrIdx() const;
};
