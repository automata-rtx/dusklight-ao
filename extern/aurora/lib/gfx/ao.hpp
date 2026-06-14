#pragma once

#include "../webgpu/gpu.hpp"

#include <aurora/math.hpp>

namespace aurora::gfx::ao {

// User-facing options. Per the design, only the quality level (and a resolution
// scale, added later) are meant to be exposed; everything else uses XeGTAO
// defaults. debugMode is a development aid.
struct Options {
  bool enabled = true;
  int qualityLevel = 3; // 0=low, 1=medium, 2=high, 3=ultra
  int debugMode = 0;    // 0=modulate scene color, 1=occlusion, 2=normals, 3=depth
  // Runtime-tunable parameters (multipliers/values; defaults match XeGTAO).
  float radius = 1.0f;     // scales the effect radius (coverage extent)
  float intensity = 1.0f;  // 0=no darkening, 1=full, >1=stronger
  float power = 2.2f;      // contrast curve applied to the AO term
  float angleBias = 0.15f; // min sin(angle) above the surface for a sample to occlude
  int resolutionDivisor = 2; // AO internal-resolution divisor: 1=full, 2=half, 4=quarter
  bool denoise = true;       // spatial (bilateral) denoise + upsample
  // Fog-aware AO fade tuning. AO is faded out toward the game's distance fog:
  // fade = clamp((fogFactor - fogFadeStart) / (1 - fogFadeStart) * fogFadeStrength).
  float fogFadeStrength = 1.0f; // how aggressively AO fades with fog (1 = matches fog)
  float fogFadeStart = 0.0f;    // fog level [0,1] below which AO does not fade yet
};

// Per-frame camera data, snapshotted on the main thread and consumed on the
// render worker (which must not read live GX state).
struct FrameParams {
  bool valid = false;
  bool enabled = false;
  int qualityLevel = 3;
  int debugMode = 0;
  float radius = 1.0f;
  float intensity = 1.0f;
  float power = 2.2f;
  float angleBias = 0.15f;
  int resolutionDivisor = 2;
  bool denoise = true;
  // GX scene fog, snapshotted so AO fades out as the game's per-pixel distance fog
  // fades in (the fog is baked into opaque geometry, so AO must not darken what the
  // fog has already washed toward the fog color). fogCurve: 0=none, 1=linear,
  // 2=exp, 3=exp2, 4=revexp, 5=revexp2. fogA/B/C are the GX fog coefficients.
  int fogCurve = 0;
  float fogA = 0.f;
  float fogB = 0.5f;
  float fogC = 0.f;
  float fogFadeStrength = 1.0f;
  float fogFadeStart = 0.0f;
  // Maps homogeneous clip/NDC coordinates (ndc.xy in [-1,1], stored reversed-Z
  // depth in z, w=1) back to view space: viewPos = (ndc, depth, 1) * clipToView,
  // then divide by w. Already accounts for Aurora's reversed-Z convention.
  Mat4x4<float> clipToView{};
  Vec2<float> cameraTanHalfFOV{1.f, 1.f};
  // 1/farViewZ: normalizes view-space depth into ~[0,1] for the regression resolve.
  float depthNormScale = 1.f;
};

void set_options(const Options& opts) noexcept;
const Options& options() noexcept;

// Main thread: record the scene perspective projection seen this frame.
void set_scene_projection(const Mat4x4<float>& proj) noexcept;

// Main thread (GX command processing): record the scene's GX fog as it is set, so
// AO can fade out exactly where the game's per-pixel distance fog fades in. Called
// for every GXSetFog; ignores fog-disable (type NONE) so the dominant scene fog is
// retained even if a later draw turns fog off before the AO apply point. gxFogType
// is the raw GXFogType. reset_scene_fog clears it (call once per frame).
void note_scene_fog(int gxFogType, float a, float b, float c) noexcept;
void reset_scene_fog() noexcept;

// Main thread: snapshot the parameters for the frame about to be submitted.
FrameParams capture_frame_params() noexcept;

// Render worker: run the AO passes, modulating sceneColor in place using
// sceneDepth. No-op when disabled, when params are invalid, or when the depth
// buffer is multisampled.
void render(const wgpu::CommandEncoder& encoder, const webgpu::TextureWithSampler& sceneColor,
            const webgpu::TextureWithSampler& sceneDepth, const FrameParams& params) noexcept;

void shutdown() noexcept;

} // namespace aurora::gfx::ao
