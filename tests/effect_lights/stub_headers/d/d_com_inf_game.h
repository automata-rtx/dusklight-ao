#pragma once
#include <d/d_particle.h>
struct dComIfG_play_c { dPa_control_c* getParticle(); };
struct dComIfG_inf_c { dComIfG_play_c play; };
extern dComIfG_inf_c g_dComIfG_gameInfo;
