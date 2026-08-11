#pragma once
#include <types.h>
#define dPa_RM(id) (0x8000 | (id))
struct dPa_name { static const char* getName(u32); };
