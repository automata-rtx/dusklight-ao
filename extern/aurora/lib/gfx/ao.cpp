#include "ao.hpp"

#include "../gx/gx.hpp"
#include "../internal.hpp"
#include "../webgpu/gpu.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <mutex>
#include <vector>

// Screen-space ambient occlusion for Aurora.
//
// This is an original, independent implementation written from the published
// literature. The occlusion estimator combines two techniques:
//   - Ground Truth Ambient Occlusion (GTAO): Jimenez, Wu, Pesce, Jarzynski,
//     "Practical Real-Time Strategies for Accurate Indirect Occlusion" (2016).
//   - The visibility bitmask, introduced for indirect lighting by Therrien,
//     Levesque, Gilet, "Screen Space Indirect Lighting with Visibility Bitmask"
//     (2022). That paper applies the bitmask to indirect lighting; here it is
//     adapted to drive screen-space ambient occlusion instead.
// Intel's XeGTAO (GameTechDev, MIT-licensed) is a useful reference for the GTAO
// slice/horizon formulation.
//
// The visual direction is inspired by Marty's MXAO (iMMERSE) -- i.e. the choice
// to pair the bitmask estimator with interleaved sampling and a depth-guided
// reconstruction filter -- but the code here is our own WGSL, derived from the
// papers above. No MXAO source is used, copied, or required at build or run time.
//
// Pipeline (all full-screen fragment passes, matching Aurora's existing post
// effects): linearize depth -> occlusion main pass -> depth-guided regression
// resolve -> apply. There is intentionally no temporal pass; Dusklight relies on
// supersampling, so we use high per-pixel sample counts plus interleaved sampling
// and a spatial resolve, and let the existing downsample average out residual noise.

namespace aurora::gfx::ao {
namespace {
Module Log("aurora::gfx::ao");

Options g_options{};
std::mutex g_projMutex;
bool g_haveProj = false;
Mat4x4<float> g_pendingProj{};

// Scene fog, tracked across the frame on the main thread (see note_scene_fog).
struct SceneFog {
  int curve = 0; // 0=none, 1=linear, 2=exp, 3=exp2, 4=revexp, 5=revexp2
  float a = 0.f;
  float b = 0.5f;
  float c = 0.f;
  bool valid = false;
};
std::mutex g_fogMutex;
SceneFog g_sceneFog{};

// ---- Pass resources -------------------------------------------------------

struct PassPipeline {
  wgpu::BindGroupLayout bindGroupLayout;
  wgpu::RenderPipeline pipeline;
};

bool g_initialized = false;
wgpu::ShaderModule g_module;
wgpu::Buffer g_uniformBuffer;
wgpu::Sampler g_sampler;       // linear: depth upsample in the apply pass
wgpu::Sampler g_pointSampler;  // nearest: exact-texel depth reads in GTAO + resolve
PassPipeline g_linearize;
PassPipeline g_gtao;
PassPipeline g_denoise;
PassPipeline g_apply;       // multiplicative modulation of the scene color
PassPipeline g_applyDebug;  // overwrite the scene color with AO grayscale

// Intermediate targets, recreated on resolution change.
webgpu::TextureWithSampler g_linearDepth; // R32Float, signed view-space Z (full res)
webgpu::TextureWithSampler g_aoTerm;       // RGBA8Unorm (AO working res)
webgpu::TextureWithSampler g_aoDenoised;   // RGBA8Unorm (AO working res)
uint32_t g_texWidth = 0;
uint32_t g_texHeight = 0;
uint32_t g_texDivisor = 0;

// GPU-side uniform layout. Field order/offsets must match the WGSL AOUniform
// struct below. All members are 4-byte; vec2 fields land on 8-byte offsets and
// the whole struct is padded to a multiple of 16 bytes.
struct alignas(16) AOUniform {
  float clipToView[16];        // 0   (mat4x4f, column-major)
  float viewportSize[2];       // 64
  float invViewportSize[2];    // 72
  float cameraTanHalfFOV[2];   // 80
  float effectRadius;          // 88
  float effectFalloffRange;    // 92
  float radiusMultiplier;      // 96
  float finalValuePower;       // 100
  float sampleDistributionPower; // 104
  float thinOccluderCompensation; // 108
  float denoiseBlurBeta;       // 112
  float noiseIndex;            // 116
  float sliceCount;            // 120
  float stepsPerSlice;         // 124
  uint32_t debugMode;          // 128
  float angleBias;             // 132
  float intensity;             // 136
  uint32_t denoiseEnabled;     // 140
  uint32_t fogCurve;           // 144
  float fogA;                  // 148
  float fogB;                  // 152
  float fogC;                  // 156
  float fogFadeStrength;       // 160
  float fogFadeStart;          // 164
  float depthNormScale;        // 168  1/farViewZ: normalizes depth into [0,1] for the resolve fit
  uint32_t pad1;               // 172
};                             // size 176
static_assert(sizeof(AOUniform) == 176);

// GTAO-style tuning constants (reference values that approximate ray-traced
// ground truth; we expose only the quality level to the user).
constexpr float kRadius = 0.5f;
constexpr float kRadiusMultiplier = 1.457f;
constexpr float kFalloffRange = 0.615f;
constexpr float kSampleDistributionPower = 2.0f;
constexpr float kThinOccluderCompensation = 0.0f;
constexpr float kFinalValuePower = 2.2f;
constexpr float kDenoiseBlurBeta = 1.2f;

// Slices/steps per quality level. Each step samples both slice directions, so the
// per-slice sample count is 2*steps. These are conventional SSAO quality presets.
void quality_to_slices(int quality, float& slices, float& steps) {
  switch (quality) {
  case 0: // low      ~16 samples
    slices = 2.f;
    steps = 4.f;
    break;
  case 1: // medium   ~24 samples
    slices = 2.f;
    steps = 6.f;
    break;
  case 2: // high     ~40 samples
    slices = 2.f;
    steps = 10.f;
    break;
  default: // ultra   ~72 samples
    slices = 3.f;
    steps = 12.f;
    break;
  }
}

// ---- CPU matrix helpers ---------------------------------------------------

// Row-vector * matrix, matching WGSL's `v * M` (result[j] = dot(v, column_j)).
// Aurora stores Mat4x4 with m0..m3 as the WGSL columns.
Vec4<float> row_vec_mul(const Vec4<float>& v, const Mat4x4<float>& m) {
  Vec4<float> out{};
  const Vec4<float>* col[4] = {&m.m0, &m.m1, &m.m2, &m.m3};
  for (int j = 0; j < 4; ++j) {
    out[j] = v[0] * (*col[j])[0] + v[1] * (*col[j])[1] + v[2] * (*col[j])[2] + v[3] * (*col[j])[3];
  }
  return out;
}

// Invert a 4x4 matrix stored column-major (m0..m3 are columns). Returns the
// inverse in the same layout. Standard cofactor expansion.
Mat4x4<float> invert(const Mat4x4<float>& src) {
  float m[16];
  const Vec4<float>* col[4] = {&src.m0, &src.m1, &src.m2, &src.m3};
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 4; ++r) {
      m[c * 4 + r] = (*col[c])[r];
    }
  }

  float inv[16];
  inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
           m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
  inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
           m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
  inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
           m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
  inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
            m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
  inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
           m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
  inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
           m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
  inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
           m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
  inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
            m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
  inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
           m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
  inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
           m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
  inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
            m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
  inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
            m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
  inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
           m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
  inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
           m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
  inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
            m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
  inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] +
            m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

  float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
  if (std::abs(det) < 1e-20f) {
    return Mat4x4_Identity;
  }
  const float invDet = 1.0f / det;

  Mat4x4<float> out{};
  Vec4<float>* ocol[4] = {&out.m0, &out.m1, &out.m2, &out.m3};
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 4; ++r) {
      (*ocol[c])[r] = inv[c * 4 + r] * invDet;
    }
  }
  return out;
}

// ---- WGSL -----------------------------------------------------------------

constexpr const char* kShaderSource = R"WGSL(
const PI: f32 = 3.14159265359;
const HALF_PI: f32 = 1.57079632679;

struct AOUniform {
  clip_to_view: mat4x4<f32>,
  viewport_size: vec2<f32>,
  inv_viewport_size: vec2<f32>,
  camera_tan_half_fov: vec2<f32>,
  effect_radius: f32,
  effect_falloff_range: f32,
  radius_multiplier: f32,
  final_value_power: f32,
  sample_distribution_power: f32,
  thin_occluder_compensation: f32,
  denoise_blur_beta: f32,
  noise_index: f32,
  slice_count: f32,
  steps_per_slice: f32,
  debug_mode: u32,
  angle_bias: f32,
  intensity: f32,
  denoise_enabled: u32,
  fog_curve: u32,
  fog_a: f32,
  fog_b: f32,
  fog_c: f32,
  fog_fade_strength: f32,
  fog_fade_start: f32,
  depth_norm_scale: f32,
  pad1: u32,
};
@group(0) @binding(0) var<uniform> U: AOUniform;

struct VSOut {
  @builtin(position) pos: vec4<f32>,
  @location(0) uv: vec2<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) vidx: u32) -> VSOut {
  var p = array<vec2<f32>, 3>(vec2<f32>(-1.0, 1.0), vec2<f32>(-1.0, -3.0), vec2<f32>(3.0, 1.0));
  var t = array<vec2<f32>, 3>(vec2<f32>(0.0, 0.0), vec2<f32>(0.0, 2.0), vec2<f32>(2.0, 0.0));
  var o: VSOut;
  o.pos = vec4<f32>(p[vidx], 0.0, 1.0);
  o.uv = t[vidx];
  return o;
}

const SKY_DEPTH: f32 = 1.0e9;

// Full unprojection from screen uv + stored (reversed-Z) NDC depth.
fn unproject(uv: vec2<f32>, ndc_z: f32) -> vec3<f32> {
  let ndc = vec2<f32>(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
  let p = vec4<f32>(ndc, ndc_z, 1.0) * U.clip_to_view;
  return p.xyz / p.w;
}

// View-space ray direction through a pixel (camera at the origin).
fn view_ray(uv: vec2<f32>) -> vec3<f32> {
  let ndc = vec2<f32>(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
  let p = vec4<f32>(ndc, 1.0, 1.0) * U.clip_to_view;
  return p.xyz / p.w;
}

// Reconstruct a view-space position from uv and a stored signed view-space Z.
fn view_pos_from_linz(uv: vec2<f32>, linz: f32) -> vec3<f32> {
  let dir = view_ray(uv);
  return dir * (linz / dir.z);
}

// --- Pass 1: linearize depth ---
@group(0) @binding(1) var depth_tex: texture_depth_2d;

@fragment
fn fs_linearize(in: VSOut) -> @location(0) vec4<f32> {
  let coord = vec2<i32>(floor(in.pos.xy));
  let d = textureLoad(depth_tex, coord, 0);
  if (d <= 0.0) {
    // Cleared/background (reversed-Z far plane); mark as sky.
    return vec4<f32>(SKY_DEPTH, 0.0, 0.0, 0.0);
  }
  let vp = unproject(in.uv, d);
  return vec4<f32>(vp.z, 0.0, 0.0, 0.0);
}

)WGSL"
R"WGSL(
// --- Pass 2: occlusion main pass (visibility-bitmask horizon search) ---
//
// The visibility bitmask (Therrien et al. 2022, originally for indirect lighting)
// adapted here to ambient occlusion. Unlike classic horizon-based GTAO (which
// tracks only the single furthest horizon per side and so assumes every sample
// below it is a continuous solid wall), this divides each slice into 32 angular
// sectors held in a u32 and marks only the sectors an occluder actually spans
// (front..back, using a thickness term). Separated occluders, gaps and thin
// geometry are all handled correctly, which is why it reads far cleaner.
@group(0) @binding(1) var lin_tex: texture_2d<f32>;
@group(0) @binding(2) var lin_samp: sampler;

fn load_linz(uv: vec2<f32>) -> f32 {
  return textureSampleLevel(lin_tex, lin_samp, clamp(uv, vec2<f32>(0.0), vec2<f32>(1.0)), 0.0).r;
}

// Reconstruct a view position from uv, clamping sky neighbours to a reference
// depth so silhouettes don't corrupt derived data (normals).
fn view_pos_at(uv: vec2<f32>, ref_linz: f32) -> vec3<f32> {
  let z = load_linz(uv);
  let zc = select(ref_linz, z, z < SKY_DEPTH * 0.5);
  return view_pos_from_linz(uv, zc);
}

// Edge-weighted normal reconstruction from depth: builds four candidate normals
// from neighbour cross products, weights each by an inverse depth-delta confidence,
// and returns the blended normal plus an edge_weight in [0,1] (1 = strong depth
// edge / low normal confidence) used to drive the resolve.
fn reconstruct_normals(uv: vec2<f32>, centerZ: f32) -> vec4<f32> {
  let px = U.inv_viewport_size;
  let center = view_pos_from_linz(uv, centerZ);
  let dL = view_pos_at(uv - vec2<f32>(px.x, 0.0), centerZ) - center;
  let dR = view_pos_at(uv + vec2<f32>(px.x, 0.0), centerZ) - center;
  let dT = view_pos_at(uv - vec2<f32>(0.0, px.y), centerZ) - center;
  let dB = view_pos_at(uv + vec2<f32>(0.0, px.y), centerZ) - center;

  let zd = abs(vec4<f32>(dL.z, dR.z, dT.z, dB.z)); // (L, R, T, B)
  var w = vec4<f32>(zd.x + zd.z, zd.z + zd.y, zd.y + zd.w, zd.w + zd.x);
  w = 1.0 / (0.001 + w * w); // inverse weighting: larger depth delta -> lower weight
  let edge_weight = clamp(1.0 - dot(w, vec4<f32>(1.0)), 0.0, 1.0);

  let n0 = cross(dT, dL);
  let n1 = cross(dR, dT);
  let n2 = cross(dB, dR);
  let n3 = cross(dL, dB);
  let fw = w * inverseSqrt(max(vec4<f32>(dot(n0, n0), dot(n1, n1), dot(n2, n2), dot(n3, n3)), vec4<f32>(1.0e-12)));
  var normal = n0 * fw.x + n1 * fw.y + n2 * fw.z + n3 * fw.w;
  normal = normal * inverseSqrt(dot(normal, normal) + 1.0e-8);
  return vec4<f32>(normal, edge_weight);
}

// Clear the angular sectors [h.x, h.y) (normalized to [0,1] across the slice)
// from the visibility bitfield. occ starts all-ones (fully visible); occluders
// AND away the sectors they cover. Shift amounts are kept < 32 (WGSL UB).
fn carve_occluded_sectors(occ: u32, h: vec2<f32>) -> u32 {
  let a = min(u32(clamp(h.x, 0.0, 1.0) * 32.0), 31u);
  let e = u32(clamp(h.y, 0.0, 1.0) * 32.0);
  let b = select(0u, e - a, e > a);
  let bs = min(b, 31u);
  let ones = select((1u << bs) - 1u, 0xFFFFFFFFu, b >= 32u);
  return occ & ~(ones << a);
}

// Structured interleaved jitter: a low-discrepancy value per pixel based on its
// position within a 4x4 tile, scrambled by *11. Adjacent pixels get complementary
// sample patterns so the 4x4 regression resolve can reconstruct a near-noise-free
// result -- interleaved sampling plus a reconstruction filter resolves far cleaner
// than white-noise jitter.
fn interleaved_jitter(p: vec2<i32>) -> f32 {
  let pm = vec2<u32>(u32(p.x & 3), u32(p.y & 3));
  let idx = (pm.x + pm.y * 4u) * 11u;
  return (f32(idx % 16u) + 0.5) / 16.0;
}

@fragment
fn fs_gtao(in: VSOut) -> @location(0) vec4<f32> {
  let uv = in.uv;
  let linz = load_linz(uv);
  if (linz >= SKY_DEPTH * 0.5 || abs(linz) > 1.0e8) {
    return vec4<f32>(1.0, 1.0, 1.0, 1.0); // sky / background: no occlusion, far depth
  }

  let nrm = reconstruct_normals(uv, linz);
  var N = nrm.xyz;
  let edge_weight = nrm.w;

  var P = view_pos_from_linz(uv, linz);
  if (dot(N, P) > 0.0) {
    N = -N;
  }
  let V = normalize(-P);
  P = P * 0.996; // small bias toward the camera to suppress self-occlusion

  // Depth-proportional radius: the AO reach is a fraction of the view-space depth,
  // so screen-space coverage stays roughly uniform with distance and the effect is
  // independent of the game's (large, unknown) world-unit scale. radiusPix is the
  // constant screen-space search radius; T is the occluder thickness.
  let projScaleY = 0.5 * U.viewport_size.y / max(U.camera_tan_half_fov.y, 1.0e-4);
  let absZ = max(abs(linz), 1.0e-4);
  let viewRadius = absZ * U.effect_radius;
  let radiusPix = clamp(U.effect_radius * projScaleY, 4.0, 0.4 * U.viewport_size.y);
  let T = log(1.0 + viewRadius) * 0.3333;

  let pixCoord = vec2<i32>(floor(in.pos.xy));
  let jitter = interleaved_jitter(pixCoord);

  let slices = max(U.slice_count, 1.0);
  let steps = max(U.steps_per_slice, 1.0);

  var visibility = 0.0;
  var norm = 0.0;

  for (var s = 0.0; s < slices; s = s + 1.0) {
    let phi = PI * (s + jitter) / slices;
    let dir = vec2<f32>(cos(phi), sin(phi)); // screen-space slice direction
    // View-space slice direction (screen y points down in framebuffer space).
    let dir3 = normalize(vec3<f32>(dir.x, -dir.y, 0.0));
    let slicePlaneNormal = normalize(cross(dir3, V));
    let projN = N - slicePlaneNormal * dot(N, slicePlaneNormal);
    let projNLen = length(projN);
    if (projNLen < 1.0e-4) {
      continue;
    }
    let projNn = projN / projNLen;
    let Tang = cross(slicePlaneNormal, V); // tangent in slice plane, perpendicular to V
    let n = atan2(dot(projNn, Tang), dot(projNn, V)); // projected-normal angle from V

    var occ: u32 = 0xFFFFFFFFu;
    for (var step = 1.0; step <= steps; step = step + 1.0) {
      let s01 = (step - jitter) / steps;
      let dist = pow(clamp(s01, 0.0, 1.0), U.sample_distribution_power) * radiusPix;
      let offset = dir * dist * U.inv_viewport_size;

      // +direction: front/back horizon angles of a thick occluder, mapped into
      // [0,1] across the slice (centred on the projected-normal angle) and run
      // through the cosine-lobe smoothstep (solid-angle weighting).
      let uvP = uv + offset;
      let lzP = load_linz(uvP);
      if (lzP < SKY_DEPTH * 0.5) {
        let dvec = view_pos_from_linz(uvP, lzP) - P;
        let ddv = dot(dvec, V);
        let ddd = dot(dvec, dvec);
        var fb = vec2<f32>(ddv, ddv - T) * inverseSqrt(max(vec2<f32>(ddd, ddd - 2.0 * T * ddv + T * T), vec2<f32>(1.0e-12)));
        fb = acos(clamp(fb, vec2<f32>(-1.0), vec2<f32>(1.0)));
        var hh = clamp((fb + n) / PI + 0.5, vec2<f32>(0.0), vec2<f32>(1.0));
        hh = hh * hh * (3.0 - 2.0 * hh);
        occ = carve_occluded_sectors(occ, hh);
      }

      // -direction: same, with the front/back pair negated and swapped so it maps
      // onto the opposite half of the slice axis.
      let uvN = uv - offset;
      let lzN = load_linz(uvN);
      if (lzN < SKY_DEPTH * 0.5) {
        let dvec = view_pos_from_linz(uvN, lzN) - P;
        let ddv = dot(dvec, V);
        let ddd = dot(dvec, dvec);
        var fb = vec2<f32>(ddv, ddv - T) * inverseSqrt(max(vec2<f32>(ddd, ddd - 2.0 * T * ddv + T * T), vec2<f32>(1.0e-12)));
        fb = acos(clamp(fb, vec2<f32>(-1.0), vec2<f32>(1.0)));
        let hs = vec2<f32>(-fb.y, -fb.x);
        var hh = clamp((hs + n) / PI + 0.5, vec2<f32>(0.0), vec2<f32>(1.0));
        hh = hh * hh * (3.0 - 2.0 * hh);
        occ = carve_occluded_sectors(occ, hh);
      }
    }

    // Visibility for this slice = fraction of sectors still unoccluded, weighted
    // by the projected-normal length (the slice's contribution to the hemisphere).
    let vis = f32(countOneBits(occ)) / 32.0;
    visibility = visibility + vis * projNLen;
    norm = norm + projNLen;
  }

  var ao = 1.0;
  if (norm > 1.0e-4) {
    ao = clamp(visibility / norm, 0.0, 1.0);
  }

  // Signed, normalized depth for the regression resolve (pass 3): the magnitude
  // is the depth guide for the linear fit; the sign carries the normal confidence
  // (negative = strong edge -> the resolve widens its variance floor there).
  let d = clamp(absZ * U.depth_norm_scale, 0.0, 1.0);
  let signedD = select(d, -d, edge_weight > 0.5);

  // Debug input-isolation visualizations (written into the term texture).
  if (U.debug_mode == 2u) {
    // View-space normals as RGB. A flat surface should be a solid, smooth colour.
    return vec4<f32>(N * 0.5 + vec3<f32>(0.5), 1.0);
  }
  if (U.debug_mode == 3u) {
    // Linearized depth as repeating bands; smooth near->far bands mean depth
    // reconstruction is correct.
    let band = fract(absZ * 0.1);
    return vec4<f32>(band, band, band, 1.0);
  }
  return vec4<f32>(ao, signedD, ao, 1.0);
}

// --- Pass 3: depth-guided linear-regression resolve ---
//
// Rather than a bilateral/Gaussian blur (which averages and smears across edges),
// this fits a line  ao = b*depth + a  by least squares over a 4x4 neighbourhood
// and evaluates it at the centre pixel's own depth. On flat regions it collapses
// to the local mean (denoise); across depth gradients it follows the AO-vs-depth
// trend instead of blurring across the silhouette. Combined with the structured
// jitter above, this reconstructs the interleaved samples into a clean result.
@group(0) @binding(1) var ao_in: texture_2d<f32>;
@group(0) @binding(2) var lin_in: texture_2d<f32>; // unused; retained for layout compatibility
@group(0) @binding(3) var blur_samp: sampler;

fn fetch_ao(uv: vec2<f32>) -> vec2<f32> {
  let t = textureSampleLevel(ao_in, blur_samp, clamp(uv, vec2<f32>(0.0), vec2<f32>(1.0)), 0.0);
  return vec2<f32>(t.r, t.g); // (ao, signed depth)
}

@fragment
fn fs_denoise(in: VSOut) -> @location(0) vec4<f32> {
  // Debug visualizations (normals/depth) are passed through unfiltered.
  if (U.debug_mode >= 2u) {
    return textureSampleLevel(ao_in, blur_samp, in.uv, 0.0);
  }
  let px = U.inv_viewport_size;
  let g = fetch_ao(in.uv).g;        // centre signed depth
  let blurry = g < 0.0;             // low normal confidence -> looser fit
  let eps = select(exp2(-30.0), exp2(-12.0), blurry);

  // First/second moments of (depth, ao) over a 4x4 window:
  // m = (sum depth, sum depth^2, sum ao, sum ao*depth).
  var m = vec4<f32>(0.0);
  for (var y = -1; y <= 2; y = y + 1) {
    for (var x = -1; x <= 2; x = x + 1) {
      let t = fetch_ao(in.uv + vec2<f32>(f32(x), f32(y)) * px);
      let aoT = t.r;
      let dT = abs(t.g);
      m = m + vec4<f32>(dT, dT * dT, aoT, aoT * dT);
    }
  }
  m = m / 16.0;

  let b = (m.w - m.x * m.z) / max(m.y - m.x * m.x, eps); // cov(d,ao)/var(d)
  let a = m.z - b * m.x;
  let ao = clamp(b * abs(g) + a, 0.0, 1.0);
  return vec4<f32>(ao, g, ao, 1.0);
}

)WGSL"
R"WGSL(
// --- Pass 4: apply ---
@group(0) @binding(1) var ao_final: texture_2d<f32>;
@group(0) @binding(2) var apply_samp: sampler;
@group(0) @binding(3) var lin_full: texture_2d<f32>;
@group(0) @binding(4) var ao_data: texture_2d<f32>; // rgb=normal, a=ambient-lit mask
@group(0) @binding(5) var depth_full: texture_depth_2d; // raw reversed-Z window depth (for fog)

// Occlusion below this fraction is treated as none: it removes the small uniform
// floor the discretized 32-sector horizon search leaves on flat, fully-open
// surfaces (which otherwise dims the whole lit scene a percent or two).
const AO_FLOOR: f32 = 0.04;

// Replicates the GX hardware fog factor (see build_shader_source) so AO can be
// faded out exactly as the game's per-pixel distance fog fades in. screenZ is the
// reversed-Z window depth, matching the GX fog input (1 - in.pos.z).
fn gx_fog_factor(screenZ: f32) -> f32 {
  let fogF = clamp(U.fog_a / (U.fog_b - screenZ) - U.fog_c, 0.0, 1.0);
  switch (U.fog_curve) {
    case 2u: { return 1.0 - exp2(-8.0 * fogF); }        // exp
    case 3u: { return 1.0 - exp2(-8.0 * fogF * fogF); } // exp2
    case 4u: { return exp2(-8.0 * (1.0 - fogF)); }      // revexp
    case 5u: { let g = 1.0 - fogF; return exp2(-8.0 * g * g); } // revexp2
    default: { return fogF; }                           // linear
  }
}

@fragment
fn fs_apply(in: VSOut) -> @location(0) vec4<f32> {
  if (U.debug_mode == 2u || U.debug_mode == 3u) {
    // Normals / depth visualizations: show raw and unblurred.
    return vec4<f32>(textureSampleLevel(ao_final, apply_samp, in.uv, 0.0).rgb, 1.0);
  }

  var ao = textureSampleLevel(ao_final, apply_samp, in.uv, 0.0).r;
  if (U.denoise_enabled != 0u) {
    // Depth-aware (joint bilateral) upsample of the AO term against the full-res
    // depth: smooths the low-resolution blockiness while keeping edges crisp.
    let centerZ = textureSampleLevel(lin_full, apply_samp, in.uv, 0.0).r;
    let texel = 1.0 / vec2<f32>(textureDimensions(ao_final, 0));
    var sum = 0.0;
    var wsum = 0.0;
    for (var j = -1; j <= 1; j = j + 1) {
      for (var i = -1; i <= 1; i = i + 1) {
        let suv = in.uv + vec2<f32>(f32(i), f32(j)) * texel;
        let a = textureSampleLevel(ao_final, apply_samp, suv, 0.0).r;
        let z = textureSampleLevel(lin_full, apply_samp, suv, 0.0).r;
        let rel = (z - centerZ) / max(abs(centerZ) * 0.05, 1.0e-3);
        let w = exp2(-rel * rel);
        sum = sum + a * w;
        wsum = wsum + w;
      }
    }
    ao = select(ao, sum / wsum, wsum > 1.0e-4);
  }

  // Black-point removal: drop the small uniform occlusion floor so flat, open
  // surfaces read as exactly 1 (no darkening) while real crevices are preserved.
  let occ = clamp((1.0 - ao - AO_FLOOR) / max(1.0 - AO_FLOOR, 1.0e-3), 0.0, 1.0);
  ao = 1.0 - occ;

  // Contrast (final value power) and strength (intensity). Applied for both the
  // normal apply and the occlusion debug view so tuning is visible in both.
  ao = pow(clamp(ao, 0.0, 1.0), U.final_value_power);
  ao = clamp(mix(1.0, ao, U.intensity), 0.0, 1.0);

  // The occlusion debug view (mode 1) stops here: a clean, full-frame grayscale of
  // the filtered AO term (a raw occlusion preview). The mask and fog-fade below
  // belong to the real apply only -- in the debug view they would whiten large
  // regions (UI/emissive) and the distant AO we want to inspect.
  if (U.debug_mode == 0u) {
    // Faithful application: only darken surfaces lit by the game's ambient/diffuse
    // lighting. Unlit/emissive/additive materials and UI carry mask 0, so they are
    // left fully bright.
    let lit = textureSampleLevel(ao_data, apply_samp, in.uv, 0.0).a;
    ao = mix(1.0, ao, lit);
    // Fade AO out as the game's hardware distance fog fades in: the distant geometry
    // is already washed toward the fog color in the EFB, so darkening it with AO at
    // full strength reads as harsh shading floating over the haze. Attenuating by the
    // same fog factor tracks the effect smoothly with no mask seam.
    if (U.fog_curve != 0u) {
      let d = textureLoad(depth_full, vec2<i32>(floor(in.pos.xy)), 0);
      let fogZ = clamp(gx_fog_factor(1.0 - d), 0.0, 1.0);
      // Remap the fog factor into the AO fade: fogFadeStart sets how much fog must
      // build up before AO starts fading (lower = fades closer); fogFadeStrength
      // scales how aggressively it fades once it starts.
      let denom = max(1.0 - U.fog_fade_start, 0.01);
      let fade = clamp((fogZ - U.fog_fade_start) / denom * U.fog_fade_strength, 0.0, 1.0);
      ao = mix(ao, 1.0, fade);
    }
  }
  return vec4<f32>(ao, ao, ao, 1.0);
}
)WGSL";

// ---- Pipeline construction ------------------------------------------------

wgpu::BindGroupLayoutEntry uniform_entry() {
  return wgpu::BindGroupLayoutEntry{
      .binding = 0,
      .visibility = wgpu::ShaderStage::Fragment,
      .buffer = wgpu::BufferBindingLayout{.type = wgpu::BufferBindingType::Uniform},
  };
}
wgpu::BindGroupLayoutEntry tex_entry(uint32_t binding) {
  return wgpu::BindGroupLayoutEntry{
      .binding = binding,
      .visibility = wgpu::ShaderStage::Fragment,
      .texture = wgpu::TextureBindingLayout{.sampleType = wgpu::TextureSampleType::Float,
                                            .viewDimension = wgpu::TextureViewDimension::e2D},
  };
}
wgpu::BindGroupLayoutEntry depth_tex_entry(uint32_t binding) {
  return wgpu::BindGroupLayoutEntry{
      .binding = binding,
      .visibility = wgpu::ShaderStage::Fragment,
      .texture = wgpu::TextureBindingLayout{.sampleType = wgpu::TextureSampleType::Depth,
                                            .viewDimension = wgpu::TextureViewDimension::e2D},
  };
}
wgpu::BindGroupLayoutEntry sampler_entry(uint32_t binding) {
  return wgpu::BindGroupLayoutEntry{
      .binding = binding,
      .visibility = wgpu::ShaderStage::Fragment,
      .sampler = wgpu::SamplerBindingLayout{.type = wgpu::SamplerBindingType::Filtering},
  };
}

PassPipeline make_pipeline(const char* label, const char* entryPoint, wgpu::TextureFormat targetFormat,
                           std::initializer_list<wgpu::BindGroupLayoutEntry> entries,
                           const wgpu::BlendState* blend) {
  PassPipeline out;
  const std::vector<wgpu::BindGroupLayoutEntry> entryVec(entries);
  const wgpu::BindGroupLayoutDescriptor bglDesc{
      .entryCount = entryVec.size(),
      .entries = entryVec.data(),
  };
  out.bindGroupLayout = webgpu::g_device.CreateBindGroupLayout(&bglDesc);
  const wgpu::PipelineLayoutDescriptor plDesc{
      .bindGroupLayoutCount = 1,
      .bindGroupLayouts = &out.bindGroupLayout,
  };
  auto layout = webgpu::g_device.CreatePipelineLayout(&plDesc);
  const std::array colorTargets{wgpu::ColorTargetState{
      .format = targetFormat,
      .blend = blend,
      .writeMask = wgpu::ColorWriteMask::All,
  }};
  const wgpu::FragmentState fragmentState{
      .module = g_module,
      .entryPoint = entryPoint,
      .targetCount = colorTargets.size(),
      .targets = colorTargets.data(),
  };
  const wgpu::RenderPipelineDescriptor desc{
      .label = label,
      .layout = layout,
      .vertex = wgpu::VertexState{.module = g_module, .entryPoint = "vs_main"},
      .primitive = wgpu::PrimitiveState{.topology = wgpu::PrimitiveTopology::TriangleList},
      .multisample = wgpu::MultisampleState{.count = 1, .mask = UINT32_MAX},
      .fragment = &fragmentState,
  };
  out.pipeline = webgpu::g_device.CreateRenderPipeline(&desc);
  return out;
}

void ensure_initialized() {
  if (g_initialized) {
    return;
  }
  wgpu::ShaderSourceWGSL wgsl{};
  wgsl.code = kShaderSource;
  const wgpu::ShaderModuleDescriptor moduleDesc{.nextInChain = &wgsl, .label = "AO Module"};
  g_module = webgpu::g_device.CreateShaderModule(&moduleDesc);

  const wgpu::BufferDescriptor uniformDesc{
      .label = "AO Uniform Buffer",
      .usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Uniform,
      .size = AURORA_ALIGN(sizeof(AOUniform), 16),
  };
  g_uniformBuffer = webgpu::g_device.CreateBuffer(&uniformDesc);

  const wgpu::SamplerDescriptor samplerDesc{
      .label = "AO Sampler",
      .addressModeU = wgpu::AddressMode::ClampToEdge,
      .addressModeV = wgpu::AddressMode::ClampToEdge,
      .addressModeW = wgpu::AddressMode::ClampToEdge,
      .magFilter = wgpu::FilterMode::Linear,
      .minFilter = wgpu::FilterMode::Linear,
      .mipmapFilter = wgpu::MipmapFilterMode::Nearest,
      .lodMinClamp = 0.f,
      .lodMaxClamp = 0.f,
      .maxAnisotropy = 1,
  };
  g_sampler = webgpu::g_device.CreateSampler(&samplerDesc);

  const wgpu::SamplerDescriptor pointSamplerDesc{
      .label = "AO Point Sampler",
      .addressModeU = wgpu::AddressMode::ClampToEdge,
      .addressModeV = wgpu::AddressMode::ClampToEdge,
      .addressModeW = wgpu::AddressMode::ClampToEdge,
      .magFilter = wgpu::FilterMode::Nearest,
      .minFilter = wgpu::FilterMode::Nearest,
      .mipmapFilter = wgpu::MipmapFilterMode::Nearest,
      .lodMinClamp = 0.f,
      .lodMaxClamp = 0.f,
      .maxAnisotropy = 1,
  };
  g_pointSampler = webgpu::g_device.CreateSampler(&pointSamplerDesc);

  const auto surfaceFormat = webgpu::g_graphicsConfig.surfaceConfiguration.format;

  // The AO term carries (ao, signed depth) per texel for the regression resolve,
  // so it needs a float format (RGBA16F) rather than RGBA8Unorm.
  g_linearize = make_pipeline("AO Linearize", "fs_linearize", wgpu::TextureFormat::R32Float,
                              {uniform_entry(), depth_tex_entry(1)}, nullptr);
  g_gtao = make_pipeline("AO GTAO", "fs_gtao", wgpu::TextureFormat::RGBA16Float,
                         {uniform_entry(), tex_entry(1), sampler_entry(2)}, nullptr);
  g_denoise = make_pipeline("AO Denoise", "fs_denoise", wgpu::TextureFormat::RGBA16Float,
                            {uniform_entry(), tex_entry(1), tex_entry(2), sampler_entry(3)}, nullptr);

  // Multiplicative blend: out_color = dst * src.rgb, alpha unchanged.
  const wgpu::BlendState multiplyBlend{
      .color = wgpu::BlendComponent{.operation = wgpu::BlendOperation::Add,
                                    .srcFactor = wgpu::BlendFactor::Zero,
                                    .dstFactor = wgpu::BlendFactor::Src},
      .alpha = wgpu::BlendComponent{.operation = wgpu::BlendOperation::Add,
                                    .srcFactor = wgpu::BlendFactor::Zero,
                                    .dstFactor = wgpu::BlendFactor::One},
  };
  g_apply = make_pipeline(
      "AO Apply", "fs_apply", surfaceFormat,
      {uniform_entry(), tex_entry(1), sampler_entry(2), tex_entry(3), tex_entry(4), depth_tex_entry(5)},
      &multiplyBlend);
  g_applyDebug = make_pipeline(
      "AO Apply Debug", "fs_apply", surfaceFormat,
      {uniform_entry(), tex_entry(1), sampler_entry(2), tex_entry(3), tex_entry(4), depth_tex_entry(5)}, nullptr);

  g_initialized = true;
}

webgpu::TextureWithSampler make_target(uint32_t width, uint32_t height, wgpu::TextureFormat format, const char* label) {
  const wgpu::Extent3D size{.width = width, .height = height, .depthOrArrayLayers = 1};
  const wgpu::TextureDescriptor texDesc{
      .label = label,
      .usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding,
      .dimension = wgpu::TextureDimension::e2D,
      .size = size,
      .format = format,
      .mipLevelCount = 1,
      .sampleCount = 1,
  };
  auto texture = webgpu::g_device.CreateTexture(&texDesc);
  const wgpu::TextureViewDescriptor viewDesc{.label = label, .dimension = wgpu::TextureViewDimension::e2D};
  auto view = texture.CreateView(&viewDesc);
  return {.texture = std::move(texture), .view = std::move(view), .size = size, .format = format, .sampler = nullptr};
}

void ensure_targets(uint32_t width, uint32_t height, uint32_t divisor) {
  if (g_texWidth == width && g_texHeight == height && g_texDivisor == divisor && g_linearDepth.view) {
    return;
  }
  // Linearized depth stays full resolution (cheap, one sample/pixel). The
  // expensive GTAO + denoise passes run at the reduced AO working resolution.
  const uint32_t aoW = std::max(1u, width / divisor);
  const uint32_t aoH = std::max(1u, height / divisor);
  g_linearDepth = make_target(width, height, wgpu::TextureFormat::R32Float, "AO Linear Depth");
  g_aoTerm = make_target(aoW, aoH, wgpu::TextureFormat::RGBA16Float, "AO Term");
  g_aoDenoised = make_target(aoW, aoH, wgpu::TextureFormat::RGBA16Float, "AO Denoised");
  g_texWidth = width;
  g_texHeight = height;
  g_texDivisor = divisor;
}

void run_pass(const wgpu::CommandEncoder& encoder, const char* label, const PassPipeline& pp,
              const wgpu::TextureView& target, wgpu::LoadOp loadOp,
              std::initializer_list<wgpu::BindGroupEntry> entries) {
  const std::vector<wgpu::BindGroupEntry> entryVec(entries);
  const wgpu::BindGroupDescriptor bgDesc{
      .layout = pp.bindGroupLayout,
      .entryCount = entryVec.size(),
      .entries = entryVec.data(),
  };
  auto bindGroup = webgpu::g_device.CreateBindGroup(&bgDesc);

  const std::array attachments{wgpu::RenderPassColorAttachment{
      .view = target,
      .loadOp = loadOp,
      .storeOp = wgpu::StoreOp::Store,
      .clearValue = wgpu::Color{0.0, 0.0, 0.0, 0.0},
  }};
  const wgpu::RenderPassDescriptor passDesc{
      .label = label,
      .colorAttachmentCount = attachments.size(),
      .colorAttachments = attachments.data(),
  };
  auto pass = encoder.BeginRenderPass(&passDesc);
  pass.SetPipeline(pp.pipeline);
  pass.SetBindGroup(0, bindGroup, 0, nullptr);
  pass.Draw(3);
  pass.End();
}

} // namespace

void set_options(const Options& opts) noexcept { g_options = opts; }
const Options& options() noexcept { return g_options; }

void set_scene_projection(const Mat4x4<float>& proj) noexcept {
  std::lock_guard lock{g_projMutex};
  g_pendingProj = proj;
  g_haveProj = true;
}

void note_scene_fog(int gxFogType, float a, float b, float c) noexcept {
  // Normalize PERSP/ORTHO fog types (which share the low 3 bits) to a curve id.
  int curve = 0;
  switch (gxFogType & 0x7) {
  case 2: curve = 1; break; // linear
  case 4: curve = 2; break; // exp
  case 5: curve = 3; break; // exp2
  case 6: curve = 4; break; // revexp
  case 7: curve = 5; break; // revexp2
  default: curve = 0; break; // none
  }
  if (curve == 0) {
    return; // fog disabled: keep the dominant scene fog already recorded this frame
  }
  std::lock_guard lock{g_fogMutex};
  g_sceneFog = SceneFog{.curve = curve, .a = a, .b = b, .c = c, .valid = true};
}

void reset_scene_fog() noexcept {
  std::lock_guard lock{g_fogMutex};
  g_sceneFog.valid = false;
}

FrameParams capture_frame_params() noexcept {
  FrameParams params;
  params.enabled = g_options.enabled;
  params.qualityLevel = g_options.qualityLevel;
  params.debugMode = g_options.debugMode;
  params.radius = g_options.radius;
  params.intensity = g_options.intensity;
  params.power = g_options.power;
  params.angleBias = g_options.angleBias;
  params.resolutionDivisor = g_options.resolutionDivisor;
  params.denoise = g_options.denoise;
  params.fogFadeStrength = g_options.fogFadeStrength;
  params.fogFadeStart = g_options.fogFadeStart;
  // Use the scene fog tracked across this frame (the dominant non-disabled fog,
  // i.e. the terrain's atmospheric fog) rather than whatever fog the very last
  // opaque draw left set, which may have fog turned off.
  {
    std::lock_guard lock{g_fogMutex};
    if (g_sceneFog.valid) {
      params.fogCurve = g_sceneFog.curve;
      params.fogA = g_sceneFog.a;
      params.fogB = g_sceneFog.b;
      params.fogC = g_sceneFog.c;
    }
  }
  Mat4x4<float> proj;
  {
    std::lock_guard lock{g_projMutex};
    if (!g_haveProj) {
      return params;
    }
    proj = g_pendingProj;
  }

  // Build clip->view: negate the column that produces clip.z (Aurora applies a
  // reversed-Z flip of out.pos.z), then invert.
  Mat4x4<float> projRevZ = proj;
  projRevZ.m2 = Vec4<float>{-proj.m2[0], -proj.m2[1], -proj.m2[2], -proj.m2[3]};
  params.clipToView = invert(projRevZ);

  // Derive tan(halfFOV) per axis from the unprojected ray directions.
  const auto rayX = row_vec_mul(Vec4<float>{1.f, 0.f, 1.f, 1.f}, params.clipToView);
  const auto rayY = row_vec_mul(Vec4<float>{0.f, 1.f, 1.f, 1.f}, params.clipToView);
  const float rxz = rayX[2] / (rayX[3] != 0.f ? rayX[3] : 1.f);
  const float rxx = rayX[0] / (rayX[3] != 0.f ? rayX[3] : 1.f);
  const float ryz = rayY[2] / (rayY[3] != 0.f ? rayY[3] : 1.f);
  const float ryy = rayY[1] / (rayY[3] != 0.f ? rayY[3] : 1.f);
  params.cameraTanHalfFOV.x = std::abs(rxz) > 1e-6f ? std::abs(rxx / rxz) : 1.f;
  params.cameraTanHalfFOV.y = std::abs(ryz) > 1e-6f ? std::abs(ryy / ryz) : 1.f;

  // Far view-space Z (reversed-Z far plane = NDC depth 0), used to normalize depth
  // into ~[0,1] for the regression resolve's least-squares fit. Falls back to a
  // sane constant for infinite-far / degenerate projections.
  const auto farView = row_vec_mul(Vec4<float>{0.f, 0.f, 0.f, 1.f}, params.clipToView);
  const float fw = farView[3];
  float farZ = (std::abs(fw) > 1e-9f) ? std::abs(farView[2] / fw) : 0.f;
  if (!(farZ > 1.f) || farZ > 1.e6f) {
    farZ = 1.e4f;
  }
  params.depthNormScale = 1.f / farZ;

  params.valid = true;
  return params;
}

void render(const wgpu::CommandEncoder& encoder, const webgpu::TextureWithSampler& sceneColor,
            const webgpu::TextureWithSampler& sceneDepth, const FrameParams& params) noexcept {
  // AO consumes the ao_data mask buffer (the EnableAOData MRT). Without it there is
  // no buffer to bind, so AO is fully disabled at compile time.
  if constexpr (!gx::EnableAOData) {
    return;
  }
  if (!params.enabled || !params.valid) {
    return;
  }
  if (webgpu::g_graphicsConfig.msaaSamples > 1) {
    return; // multisampled depth is unsupported by this path
  }
  const uint32_t width = sceneDepth.size.width;
  const uint32_t height = sceneDepth.size.height;
  if (width == 0 || height == 0) {
    return;
  }

  const uint32_t divisor = std::clamp(params.resolutionDivisor, 1, 4);
  const uint32_t aoW = std::max(1u, width / divisor);
  const uint32_t aoH = std::max(1u, height / divisor);

  ensure_initialized();
  ensure_targets(width, height, divisor);

  // Upload uniforms. viewport_size is the AO working (reduced) resolution; the
  // GTAO and denoise passes run there. The linearize and apply passes render at
  // full resolution and do not read viewport_size.
  AOUniform u{};
  std::memcpy(u.clipToView, &params.clipToView, sizeof(u.clipToView));
  u.viewportSize[0] = static_cast<float>(aoW);
  u.viewportSize[1] = static_cast<float>(aoH);
  u.invViewportSize[0] = 1.f / static_cast<float>(aoW);
  u.invViewportSize[1] = 1.f / static_cast<float>(aoH);
  u.cameraTanHalfFOV[0] = params.cameraTanHalfFOV.x;
  u.cameraTanHalfFOV[1] = params.cameraTanHalfFOV.y;
  u.effectRadius = 0.1f * params.radius; // depth fraction: 10% of view depth at 100%
  u.effectFalloffRange = kFalloffRange;
  u.radiusMultiplier = kRadiusMultiplier;
  u.finalValuePower = params.power;
  u.sampleDistributionPower = kSampleDistributionPower;
  u.thinOccluderCompensation = kThinOccluderCompensation;
  u.denoiseBlurBeta = kDenoiseBlurBeta;
  u.noiseIndex = 0.f;
  u.angleBias = params.angleBias;
  u.intensity = params.intensity;
  float slices = 9.f;
  float steps = 3.f;
  quality_to_slices(params.qualityLevel, slices, steps);
  u.sliceCount = slices;
  u.stepsPerSlice = steps;
  u.debugMode = static_cast<uint32_t>(params.debugMode);
  u.denoiseEnabled = params.denoise ? 1u : 0u;
  u.fogCurve = static_cast<uint32_t>(params.fogCurve);
  u.fogA = params.fogA;
  u.fogB = params.fogB;
  u.fogC = params.fogC;
  u.fogFadeStrength = params.fogFadeStrength;
  u.fogFadeStart = params.fogFadeStart;
  u.depthNormScale = params.depthNormScale;
  webgpu::g_queue.WriteBuffer(g_uniformBuffer, 0, &u, sizeof(u));

  const wgpu::BindGroupEntry uniformBinding{.binding = 0, .buffer = g_uniformBuffer, .size = sizeof(AOUniform)};

  // Pass 1: linearize depth.
  run_pass(encoder, "AO Linearize Pass", g_linearize, g_linearDepth.view, wgpu::LoadOp::Clear,
           {uniformBinding, wgpu::BindGroupEntry{.binding = 1, .textureView = sceneDepth.view}});

  // Pass 2: GTAO. Depth is read with the point sampler so horizon samples and the
  // normal reconstruction use exact texels (linear filtering across silhouettes
  // would invent intermediate depths and smear the AO).
  run_pass(encoder, "AO GTAO Pass", g_gtao, g_aoTerm.view, wgpu::LoadOp::Clear,
           {uniformBinding, wgpu::BindGroupEntry{.binding = 1, .textureView = g_linearDepth.view},
            wgpu::BindGroupEntry{.binding = 2, .sampler = g_pointSampler}});

  // Pass 3: regression resolve (optional). When disabled, the apply samples the
  // raw AO term. Point-sampled so the 4x4 fit reads exact neighbour texels.
  const wgpu::TextureView aoForApply = params.denoise ? g_aoDenoised.view : g_aoTerm.view;
  if (params.denoise) {
    run_pass(encoder, "AO Denoise Pass", g_denoise, g_aoDenoised.view, wgpu::LoadOp::Clear,
             {uniformBinding, wgpu::BindGroupEntry{.binding = 1, .textureView = g_aoTerm.view},
              wgpu::BindGroupEntry{.binding = 2, .textureView = g_linearDepth.view},
              wgpu::BindGroupEntry{.binding = 3, .sampler = g_pointSampler}});
  }

  // Pass 4: apply to the scene color (or visualize in debug mode).
  const PassPipeline& applyPipeline = params.debugMode != 0 ? g_applyDebug : g_apply;
  run_pass(encoder, "AO Apply Pass", applyPipeline, sceneColor.view, wgpu::LoadOp::Load,
           {uniformBinding, wgpu::BindGroupEntry{.binding = 1, .textureView = aoForApply},
            wgpu::BindGroupEntry{.binding = 2, .sampler = g_sampler},
            wgpu::BindGroupEntry{.binding = 3, .textureView = g_linearDepth.view},
            wgpu::BindGroupEntry{.binding = 4, .textureView = webgpu::g_aoDataBuffer.view},
            wgpu::BindGroupEntry{.binding = 5, .textureView = sceneDepth.view}});
}

void shutdown() noexcept {
  g_linearize = {};
  g_gtao = {};
  g_denoise = {};
  g_apply = {};
  g_applyDebug = {};
  g_module = {};
  g_uniformBuffer = {};
  g_sampler = {};
  g_pointSampler = {};
  g_linearDepth = {};
  g_aoTerm = {};
  g_aoDenoised = {};
  g_texWidth = 0;
  g_texHeight = 0;
  g_initialized = false;
}

} // namespace aurora::gfx::ao
