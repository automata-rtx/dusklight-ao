#pragma once
enum GXBlendMode { GX_BM_NONE, GX_BM_BLEND, GX_BM_LOGIC, GX_BM_SUBTRACT, GX_MAX_BLENDMODE };
enum GXBlendFactor { GX_BL_ZERO, GX_BL_ONE, GX_BL_SRCCLR, GX_BL_INVSRCCLR,
                     GX_BL_DSTCLR = GX_BL_SRCCLR, GX_BL_INVDSTCLR = GX_BL_INVSRCCLR,
                     GX_BL_SRCALPHA = 4, GX_BL_INVSRCALPHA, GX_BL_DSTALPHA, GX_BL_INVDSTALPHA };
struct GXColor { u8 r, g, b, a; };
struct GXColorS10 { s16 r, g, b, a; };
