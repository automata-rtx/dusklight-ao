#pragma once
#include <cstdint>
typedef uint32_t SDL_MouseButtonFlags;
#define SDL_BUTTON_MASK(X) (1u << ((X) - 1))
#define SDL_BUTTON_LMASK SDL_BUTTON_MASK(1)
#define SDL_BUTTON_MMASK SDL_BUTTON_MASK(2)
#define SDL_BUTTON_RMASK SDL_BUTTON_MASK(3)
#define SDL_BUTTON_X1MASK SDL_BUTTON_MASK(4)
#define SDL_BUTTON_X2MASK SDL_BUTTON_MASK(5)
extern "C" SDL_MouseButtonFlags SDL_GetMouseState(float*, float*);
