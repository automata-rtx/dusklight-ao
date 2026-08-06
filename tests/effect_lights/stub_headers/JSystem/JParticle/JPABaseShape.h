#pragma once
#include <gx.h>
struct JPABaseShape {
    GXBlendMode getBlendMode() const;
    GXBlendFactor getBlendSrc() const;
    GXBlendFactor getBlendDst() const;
};
