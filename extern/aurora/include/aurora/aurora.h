#ifndef AURORA_AURORA_H
#define AURORA_AURORA_H

#ifdef __cplusplus
#include <cstddef>
#include <cstdint>

extern "C" {
#else
#include "stdbool.h"
#include "stddef.h"
#include "stdint.h"
#endif

typedef enum {
  SAMPLER_BILINEAR,
  SAMPLER_AREA,
} AuroraSampler;

typedef enum {
  BACKEND_AUTO,
  BACKEND_D3D11,
  BACKEND_D3D12,
  BACKEND_METAL,
  BACKEND_VULKAN,
  BACKEND_OPENGL,
  BACKEND_OPENGLES,
  BACKEND_WEBGPU,
  BACKEND_NULL,
} AuroraBackend;

typedef enum {
  LOG_DEBUG,
  LOG_INFO,
  LOG_WARNING,
  LOG_ERROR,
  LOG_FATAL,
} AuroraLogLevel;

typedef struct {
  int32_t x;
  int32_t y;
} AuroraWindowPos;

typedef struct {
  uint32_t width;
  uint32_t height;

  /**
   * Width of the main GX framebuffer.
   */
  uint32_t fb_width;

  /**
   * Height of the main GX framebuffer.
   */
  uint32_t fb_height;

  /**
   * The size of the framebuffer used to present to the operating system.
   * May differ from fb_width if Aurora is instructed to force an aspect ratio or resolution configuration.
   */
  uint32_t native_fb_width;

  /**
   * The size of the framebuffer used to present to the operating system.
   * May differ from fb_height if Aurora is instructed to force an aspect ratio or resolution configuration.
   */
  uint32_t native_fb_height;
  float scale;
} AuroraWindowSize;

typedef struct SDL_Window SDL_Window;
typedef struct AuroraEvent AuroraEvent;

typedef void (*AuroraLogCallback)(AuroraLogLevel level, const char* module, const char* message, unsigned int len);
typedef void (*AuroraImGuiInitCallback)(const AuroraWindowSize* size);

#define MEM1_DEFAULT_SIZE = 24 * 1024 * 1024;
#define ARAM_DEFAULT_SIZE = 16 * 1024 * 1024;

typedef struct {
  const char* appName;
  const char* userPath;
  const char* cachePath;
  const char* resourcesPath;
  AuroraBackend desiredBackend;
  uint32_t msaa;
  uint16_t maxTextureAnisotropy;
  bool vsync;
  bool startFullscreen;
  bool allowJoystickBackgroundEvents;
  bool pauseOnFocusLost;
  bool allowTextureDumps;
  bool allowCpuAdapter;
  int32_t windowPosX;
  int32_t windowPosY;
  uint32_t windowWidth;
  uint32_t windowHeight;
  void* iconRGBA8;
  uint32_t iconWidth;
  uint32_t iconHeight;
  AuroraLogCallback logCallback;
  AuroraLogLevel logLevel;
  AuroraImGuiInitCallback imGuiInitCallback;

  /*
   * The size of the GameCube's main memory, or MEM1 on the Wii.
   * Note that it will not be allocated at the exact 0x80000000 address, as that cannot be guaranteed.
   * This can be set to 0 to disable allocating this region.
   */
  uint32_t mem1Size;

  /*
   * The size of the GameCube's ARAM, or MEM2 on the Wii.
   * This can be set to 0 to disable allocating this region.
   */
  uint32_t mem2Size;
} AuroraConfig;

typedef struct {
  AuroraBackend backend;
  const char* userPath;
  const char* cachePath;
  SDL_Window* window;
  AuroraWindowSize windowSize;
} AuroraInfo;

AuroraInfo aurora_initialize(int argc, char* argv[], const AuroraConfig* config);
void aurora_shutdown();
const AuroraEvent* aurora_update();
bool aurora_begin_frame();
void aurora_end_frame();

void aurora_set_log_level(AuroraLogLevel level);
void aurora_set_pause_on_focus_lost(bool value);
void aurora_set_background_input(bool value);
void aurora_set_resampler(AuroraSampler sampler);
void aurora_set_ao_enabled(bool enabled);
// AO debug mode: 0 = off (apply normally), 1 = occlusion grayscale,
// 2 = view-space normals, 3 = linearized depth bands.
void aurora_set_ao_debug(int mode);
// AO tuning: radius (coverage multiplier, 1=default), intensity (darkening
// strength, 1=full), power (contrast curve applied to the occlusion term).
void aurora_set_ao_tuning(float radius, float intensity, float power);
// AO quality preset: 0 = low, 1 = medium, 2 = high, 3 = ultra. Controls the
// per-pixel slice/step (sample) count of the occlusion search.
void aurora_set_ao_quality(int level);
// AO internal-resolution divisor: 1 = full, 2 = half, 4 = quarter. Lower
// resolution is much cheaper and looks nearly identical for this low-frequency effect.
void aurora_set_ao_resolution(int divisor);
// Toggle the AO spatial denoise (depth-guided regression resolve) + upsample.
void aurora_set_ao_denoise(bool enabled);
// Fog-aware AO fade tuning. strength: how aggressively AO fades with the game's
// distance fog (1 = matches the fog, >1 fades sooner/harder). start: fog level
// [0,1] that must build up before AO begins to fade (lower = fades closer in).
void aurora_set_ao_fog_fade(float strength, float start);
// Apply screen-space AO to the EFB right now, at the current point in the frame.
// Call this once per frame from the game's render orchestration, after the opaque
// scene geometry has been drawn but before translucent fog / particle effects, so
// that AO darkens the solid world without darkening those effects. Internally
// drains the GX FIFO first so all pending opaque draws are recorded before AO is
// inserted. Safe to call every frame; no-op if AO is disabled or already applied
// this frame.
void aurora_apply_ao_now(void);

AuroraBackend aurora_get_backend();
const AuroraBackend* aurora_get_available_backends(size_t* count);

#ifdef __cplusplus
}
#endif

#endif
