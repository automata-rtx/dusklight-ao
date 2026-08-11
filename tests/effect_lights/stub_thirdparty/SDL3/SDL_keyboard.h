#pragma once
#include <cstdint>
typedef int SDL_Scancode;
enum { SDL_SCANCODE_ESCAPE = 41, SDL_NUM_SCANCODES = 512 };
extern "C" const bool* SDL_GetKeyboardState(int*);
extern "C" const char* SDL_GetScancodeName(SDL_Scancode);
