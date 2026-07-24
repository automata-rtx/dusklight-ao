# Thin G-Buffer for Authored Normals — Feasibility & Design

**Status:** implemented on branch `claude/thin-gbuffer-authored-normals-wgqupt` — **pending on-device validation** (builds against Dawn/WebGPU; not runnable in the investigation environment). See §12 for the change list and validation checklist.
**Scope:** Aurora (`aurora-ao`) renderer + Dusklight (`dusklight-ao`) mod SDK. The mods that *consume* the buffer live in the separate `automata-rtx/dusklight-mods` repo; the demo mods checked into `dusklight-ao/mods/` are upstream's and are deliberately **left untouched** on this branch.
**Goal:** Let screen-space modded effects (e.g. GTAO ambient occlusion) consume the game's **authored, interpolated vertex normals** instead of normals reconstructed from the depth buffer, eliminating faceting without an expensive normal-smoothing pass.

> This document lives in both `aurora-ao` and `dusklight-ao` on branch
> `claude/thin-gbuffer-authored-normals-wgqupt`; the feature spans both repos.

---

## 1. Verdict

| Question | Answer |
| --- | --- |
| Full deferred **g-buffer** (albedo/material/normal for deferred shading)? | **Not viable.** GX's per-object TEV combiner has no fixed notion of "material" to store and re-light. Confirmed below. |
| **Thin g-buffer** storing only screen-space **normals**? | **Viable, low-risk, and additive.** The value we need — the authored model-view normal `mv_nrm` — is *already computed in every GX vertex shader today*. We only need to route it to a second render target and expose a snapshot. |
| Does it fix the faceting? | **Yes, at the source.** Authored vertex normals are smooth by construction; depth-gradient normals are per-triangle-facet by construction. |
| Does it remove an expensive pass? | It removes the **5-tap depth→normal reconstruction** inside GTAO (8 depth taps + 2 unprojections per pixel) and any normal-smoothing built to hide faceting. (See §4 for a correction to the "blur the normals" premise.) |

This confirms and details the earlier intuition that "a g-buffer is unlikely but a thin g-buffer might be possible." The reason thin works is specific and strong: **the normal is the one geometric quantity GX already carries per-vertex and Aurora already transforms per-vertex.** Everything else in a g-buffer (albedo, roughness, etc.) is an emergent product of an arbitrary TEV stage graph and cannot be captured generically.

---

## 2. Why faceting happens (and why authored normals fix it)

The GTAO port reconstructs the surface normal from the depth buffer with atyuwen's 5-tap method (`mods/ao_mod/res/gtao.wgsl:113-147`): it differences neighbouring reconstructed view positions and takes a cross product:

```wgsl
var normal = normalize(cross(ddy, ddx));       // gtao.wgsl:141
```

A cross product of screen-space position deltas yields the **geometric (face) normal of the rasterized triangle** — it is piecewise-constant across each triangle. On this game's coarse depth (far plane ~200000 → depth ~5e-3, see the port note at `gtao.wgsl:176-179`) the reconstruction also degrades near depth steps. The result is the staircase/faceting the maintainers already have a debug view for (`composite.wgsl` "Normals" and "Staircase" modes).

Authored vertex normals are the opposite: the artist stores a smooth normal per vertex, and the rasterizer **interpolates** them across the triangle. That interpolation is exactly what produces smooth (Gouraud/Phong-style) shading. Writing that interpolated normal to a buffer gives pixel-smooth normals for free — no reconstruction, no blur.

```
Depth-reconstructed (today)          Authored + interpolated (proposed)
   ___                                   ___
  /   \  <- one flat normal             /   \  <- normal varies smoothly
 /_____\    per triangle facet         /_____\    across the surface
 \     /    => faceting                \     /    => smooth
  \___/                                  \___/
```

---

## 3. Why a *full* g-buffer is not viable

GameCube GX is a configurable **forward** pipeline. Each draw sets up:

- up to 16 **TEV stages** combining textures, per-vertex-lit raster colors, konst colors, and previous results with per-stage ops (`lib/gx/shader.cpp` builds this into the fragment shader);
- indirect-texture stages, emboss/bump, fog, alpha-compare, etc.

The final pixel color is the output of *that draw's specific stage graph*. There is no stable "albedo" or "material" channel to write into a g-buffer and shade later — the "material" **is** the program. Deferring it would mean storing every TEV input plus the recipe per pixel: unbounded and impractical. Aurora's pipeline reflects this: a single color target in the surface format (`lib/gx/gx.cpp:651-655`), one `@location(0)` output (`lib/gx/shader.cpp:1919`), and **no multi-render-target rendering anywhere in the codebase** (verified). So classic deferred shading is out.

The normal is the exception, which is why "thin" works.

---

## 4. Current state — how `ao_mod` works today (and a premise correction)

Frame flow (game thread hook `GFX_STAGE_SCENE_AFTER_OPAQUE`, then render worker):

```
resolve_pass(depth=true)  -> pooled R32Float depth snapshot     (mod.cpp:565; aurora snapshot_depth)
  + camera proj / inverse-proj (reversed-Z, WebGPU)             (mod.cpp:561; camera.cpp)
push_uniform + push_compute + push_draw                         (mod.cpp:609/626/634)
  (1) preprocess_depth.wgsl  -> 5-level depth MIP chain
  (2) gtao.wgsl:  reconstruct view pos -> reconstruct_normal (5-tap) -> horizon AO
                  + pack depth-difference edges
  (3) denoise.wgsl  -> 3x3 bilateral blur of the *AO term*, guided by depth edges
  (4) composite.wgsl -> fullscreen multiply of AO over the scene
```

**Note on the two AO mod variants.** In the *public* `mods/ao_mod` demo checked into the repo, the reconstructed normals are **never stored or blurred** — they are produced inside `gtao.wgsl` and consumed in place, and the expensive bilateral blur (`denoise.wgsl`) smooths the **AO visibility output**, not normals. The maintainer's *actual* (private) AO mod is a more advanced fork that **does** reconstruct normals from depth and then run a dedicated **normal blur pass** before the AO — so "blur the reconstructed normals" is accurate for that variant. Either way, the authored-normal buffer removes the reconstruction (and any normal-smoothing built to hide its faceting):

- What the thin g-buffer **removes**: the per-pixel 5-tap `reconstruct_normal` (8 `load_depth` taps + 2 extra unprojections) and the faceting it causes — plus any separate normal-smoothing you run to hide that faceting.
- What **stays**: the AO spatial denoise (`denoise.wgsl`) is inherent to GTAO's sample noise and is unrelated to normals; it is unaffected. (It may be tuned down once normals are cleaner, but it is not the pass being replaced.)

Key insight from the port itself (`gtao.wgsl:108-112`): the reconstruction *"replaces Bevy's `load_normal_view_space` (which reads a prepass normal texture we do not have)."* **Upstream GTAO is designed to read a prepass normal texture.** The thin g-buffer gives Aurora exactly that texture back, restoring the algorithm's intended, higher-quality input.

Space alignment is favourable: Aurora's `mv_nrm` is the **model-view (view-space)** normal (`nrm_mtx` is the model→view normal matrix, `lib/gx/shader.cpp:1022-1025`), and GTAO already works in the same view space (it unprojects depth with `inverse_projection`). So the g-buffer normal is a drop-in for `reconstruct_normal`'s output, modulo renormalization and a camera-facing sign guard (§7).

---

## 5. The enabler: `mv_nrm` already reaches the fragment stage

Aurora already computes the authored view-space normal in every GX vertex shader (`lib/gx/shader.cpp:1022-1025`):

```wgsl
let nrm_tmp = vec4f(in_nrm, 0.0) * ubuf.nrm_mtx[in_pnmtxidx];
let mv_nrm  = select(nrm_tmp, normalize(nrm_tmp), dot(nrm_tmp, nrm_tmp) > 1e-10);
```

Two existing (compile-time) paths already **interpolate `mv_nrm` into the fragment shader**, proving the plumbing works:

- `EnableNormalVisualization` (`lib/gx/gx.hpp:48`) adds `@location(N) nrm` to the vertex output and writes `out.nrm = mv_nrm` (`shader.cpp:1026-1029`), then `prev = vec4f(in.nrm, prev.a)` in the fragment shader (`shader.cpp:1558-1560`).
- `UsePerPixelLighting` (`gx.hpp:50`) passes `mv_pos`/`mv_nrm` to the fragment stage for per-pixel lighting (`shader.cpp:1100-1106`).

So the thin g-buffer is not a new capability so much as **routing an already-available value to a second output**.

Whether a draw *has* an authored normal is known at shader-build time: `config.attrs[GX_VA_NRM].attrType != GX_NONE`. When absent, the shader substitutes a default `vec3f(1,0,0)` (`shader.cpp:536`). We use this to emit a **validity mask** so consumers can tell real normals from filler (UI, some particles, sky).

---

## 6. Design — Aurora side (the enabling change)

MRT is used nowhere today, so this is net-new but unobstructed. All insertion points below are verified against the current tree.

### 6.1 Feature gate
Add an opt-in so there is zero cost when unused. Two workable options:
- **Runtime:** add `bool normalBuffer` (and optionally `wgpu::TextureFormat normalFormat`) to `GraphicsConfig` (`lib/webgpu/gpu.hpp:14`), set from `AuroraConfig` at init (`gpu.cpp:1028-1040`). Preferred — lets Dusklight enable it and other Aurora games ignore it.
- Compile-time `constexpr bool` beside the existing flags in `gx.hpp:48-51` (simplest, but global).

### 6.2 Normal target texture(s) — `lib/webgpu/gpu.cpp`
Alongside `g_frameBuffer` / `g_frameBufferResolved` / `g_depthBuffer` (created at `gpu.cpp:1103-1105`, torn down at `:1061-1063`), create:
```
g_normalBuffer          = create_render_texture(w, h, /*multisampled*/ true,  normalFormat);
g_normalBufferResolved  = create_render_texture(w, h, /*multisampled*/ false, normalFormat);   // only if MSAA
```
Generalize `create_render_texture` (`gpu.cpp:254-308`) to take a format (it currently hardcodes the surface format at `:260`). Recommended `normalFormat`: **`RGBA8Unorm`** (see §8).

### 6.3 `RenderPass` struct + target wiring — `lib/gfx/common.cpp`
- Add fields to `struct RenderPass` (`common.cpp:220`): `normalView`, `normalResolveView`, `copySourceNormalTexture`, `copySourceNormalView`, `snapshotNormalDst`.
- Assign them in **all three** EFB-target construction sites:
  - `set_efb_targets` (`common.cpp:459-472`)
  - `resume_efb_pass_loading` (`common.cpp:1201-1216`)
  - offscreen setup does **not** get a normal target (`begin_offscreen`, `common.cpp:1046`): offscreen passes stay single-target, so their pipelines must be built with the feature off (see 6.5).

### 6.4 Render-pass encoding (the MRT point) — `lib/gfx/common.cpp`
In the static `render()` replay (`common.cpp:1838-1965`), grow the color-attachment array (`:1853-1869`) from one to two elements when the pass has a normal target:
```cpp
attachments[1] = wgpu::RenderPassColorAttachment{
    .view          = passInfo.normalView,
    .resolveTarget = passInfo.normalResolveView,          // null if no MSAA
    .loadOp        = wgpu::LoadOp::Clear,                 // clear to (0,0,0,0) => validity 0
    .storeOp       = wgpu::StoreOp::Store,
    .clearValue    = {0, 0, 0, 0},
};
```
`colorAttachmentCount` follows `attachments.size()` automatically (`:1891`).

### 6.5 Pipeline — `lib/gx/pipeline.hpp` + `lib/gx/gx.cpp`
- Add a discriminator to `PipelineConfig` (`pipeline.hpp:20-35`): `bool normalTarget = false;` (set from the current pass, like `msaaSamples`). **Bump `GXPipelineConfigVersion` 13 → 14** and keep `has_unique_object_representations` intact. This is required because the color format is *not* otherwise in the cache key (it is read globally at build time, `gx.cpp:651-655`) — without a discriminator, one-target and two-target pipelines would collide.
- In `build_pipeline` (`gx.cpp:633-681`), when `config.normalTarget`, append a second `wgpu::ColorTargetState` for `normalFormat` with **blend disabled** and a **write mask gated on depth-write**:
  ```cpp
  // target 1 writes for any depth-writing draw; frontmost depth-writer wins via the depth test
  const bool writeNormal = config.depthCompare && config.depthUpdate;
  colorTargets[1] = { .format = normalFormat, .blend = nullptr,
                      .writeMask = writeNormal ? wgpu::ColorWriteMask::All : wgpu::ColorWriteMask::None };
  ```
  All those inputs are already in the cache key, so the derived write mask is deterministic per key. Coverage therefore matches the depth buffer: opaque **and** depth-writing transparencies get an authored normal; blend-only non-depth-writing draws (which aren't in depth) write nothing.
- `config.normalTarget` is populated in `populate_pipeline_config` (`gx.cpp:683`) from the current pass (mirroring how `msaaSamples` comes from `gfx::get_sample_count()`), so EFB pipelines get 2 targets and offscreen pipelines get 1.

### 6.6 Shader generation — `lib/gx/shader.cpp`
When `config.normalTarget`:
- Ensure `mv_nrm` is a vertex output (reuse the `EnableNormalVisualization` plumbing at `:1026-1029`; if per-pixel lighting already emits `mv_nrm`, reuse that).
- Change the fragment entry from `-> @location(0) vec4f` (`:1918-1921`) to a struct output:
  ```wgsl
  struct FragmentOutput { @location(0) color: vec4f, @location(1) normal: vec4f, };
  // ...
  out.color  = prev;
  out.normal = vec4f(normalize(in.mv_nrm) * 0.5 + 0.5, VALIDITY);   // VALIDITY is a build-time 1.0 or 0.0
  ```
  `VALIDITY` = `1.0` when `config.attrs[GX_VA_NRM].attrType != GX_NONE`, else `0.0`.

### 6.7 Snapshot / resolve API — `include/aurora/gfx.hpp` + `lib/gfx/common.cpp`
The normal target is an ordinary color texture, so its snapshot reuses the **color** path (a plain `CopyTextureToTexture`), unlike depth (which needs a shader conversion).
- `ResolveDesc` (`gfx.hpp:111`): add `bool normal = false;`
- `ResolvedTargets` (`gfx.hpp:116`): add `wgpu::TextureView normal;` and `wgpu::TextureFormat normalFormat;`
- `acquire_pass_snapshot` (`common.cpp:504`): add a pooled normal texture (surface-like usage, `CopyDst | TextureBinding`).
- `resolve_pass` (`common.cpp:1152`): when `desc.normal`, stash `prevPass.snapshotNormalDst` and return the view.
- `render()` snapshot block (`common.cpp:1947-1964`): add a `CopyTextureToTexture` from `copySourceNormalTexture` (resolved under MSAA) to `snapshotNormalDst`, mirroring the existing color copy.

---

## 7. Design — Dusklight side (consumer)

### 7.1 Mod SDK C ABI — `sdk/include/mods/svc/gfx.h`
The structs use `struct_size` versioning, so append fields (ABI-safe):
```c
typedef struct GfxResolveDesc {
    uint32_t struct_size;
    bool color;
    bool depth;
    bool normal;                 /* NEW */
} GfxResolveDesc;
#define GFX_RESOLVE_DESC_INIT {sizeof(GfxResolveDesc), true, false, false}

typedef struct GfxResolvedTargets {
    uint32_t struct_size;
    WGPUTextureView color;
    WGPUTextureView depth;
    WGPUTextureView normal;             /* NEW: view-space normal, RGBA8 (xyz*0.5+0.5, w=validity) */
    WGPUTextureFormat color_format;
    WGPUTextureFormat normal_format;    /* NEW */
    uint32_t width;
    uint32_t height;
} GfxResolvedTargets;
```
The bridge in `src/dusk/mods/svc/gfx.cpp:353` maps this to `aurora::gfx::ResolveDesc/ResolvedTargets` (guard the new fields on `struct_size` for old mods).

### 7.2 `ao_mod` — `mods/ao_mod`
1. Request the normal snapshot: set `resolveDesc.normal = true` next to `resolveDesc.depth = true` (`mod.cpp:565-569`); pass `resolved.normal` into the compute payload beside `resolved.depth`.
2. Bind the normal texture in the GTAO bind group; in `gtao.wgsl`, **replace** the `reconstruct_normal(...)` call (`:180-181`) with a sample:
   ```wgsl
   let raw = textureLoad(scene_normal, pixel_coordinates, 0);
   var pixel_normal = normalize(raw.xyz * 2.0 - 1.0);
   if raw.w < 0.5 { pixel_normal = reconstruct_normal(...); }   // fallback where no authored normal
   if dot(pixel_normal, pixel_position) > 0.0 { pixel_normal = -pixel_normal; }  // keep GTAO's camera-facing guard
   ```
   Delete the now-unused 5-tap `reconstruct_normal` (and the duplicate in `composite.wgsl:77-110`) once the fallback is settled, or keep it solely for the validity fallback.
3. Enable Aurora's `normalBuffer` at startup (via the graphics config / `AuroraConfig`).
4. Leave `denoise.wgsl` as-is (it denoises AO, not normals).

Correctness notes: renormalize after sampling (interpolation, 8-bit quantization, and any MSAA resolve denormalize the vector); keep the camera-facing guard; treat `w < 0.5` as "no authored normal" and fall back or skip. `shadow_mod`'s contact-shadow raymarch is a potential second beneficiary later.

---

## 8. Normal encoding

| Option | Size | Pros | Cons |
| --- | --- | --- | --- |
| **RGBA8Unorm** — `xyz*0.5+0.5`, `w`=validity *(recommended v1)* | 4 B | Reuses the color snapshot copy path verbatim; free validity channel; simplest | ~8-bit/axis; slight banding possible on very smooth, low-curvature surfaces under strong AO |
| RG16Float, octahedral-encoded | 4 B | Better precision; robust over the full sphere | No free validity channel (needs a separate signal); extra encode/decode |
| RGB10A2Unorm — `xyz` in 10-bit, `w`=2-bit validity | 4 B | More normal precision than RGBA8, keeps a validity channel | 2-bit validity only |

Recommend **RGBA8Unorm + alpha validity** for v1 (cheapest to land, integrates with the existing snapshot copy). If banding shows in the "Normals" debug view, move to RGB10A2 or RG16F octahedral.

---

## 9. Costs, caveats, correctness

- **Extra bandwidth:** one 4-byte write per opaque fragment in the main pass, plus the target's memory (w·h·4, doubled under MSAA for MSAA+resolve). Small next to what it removes (8 depth taps + 2 unprojections/pixel in GTAO, over the whole screen).
- **MSAA:** if enabled (`g_graphicsConfig.msaaSamples > 1`), the normal target needs its own MSAA texture + single-sample resolve. Hardware MSAA resolve **averages** samples; averaged normals are only approximately unit and slightly wrong at silhouettes — renormalize on read; acceptable for AO. (Depth snapshots already forbid MSAA in `depth_peek`; the color/normal resolve path does not.)
- **Coverage = the depth buffer.** The normal write is gated on **depth-write** (`depthCompare && depthUpdate`), not on "opaque (no blend)". So the buffer holds an authored normal for *whatever surface established the depth* at each pixel (frontmost depth-writer wins via the depth test), which matches a depth-reconstructed normal's coverage exactly — including depth-writing transparencies such as water. Blend-only draws that do not write depth (additive particles, etc.) are absent from the depth buffer and are left out here too, so nothing that a depth reconstruction could reach is missed.
- **Geometry without authored normals** (UI, some billboards/particles, sky): validity `w = 0`; consumers must handle (fallback or skip). These are normally excluded from AO anyway.
- **Pipeline permutations:** the `normalTarget` bool adds at most a ×2 to GX pipeline variants in principle, but in practice a game either uses the feature (EFB pipelines all carry target 1) or not, so the real increase is ~1×. Requires the `GXPipelineConfigVersion` bump (cache invalidation on first run after upgrade).
- **First-run shader compile:** every EFB pipeline recompiles to add the second output; cached thereafter.
- **Space/sign:** `mv_nrm` is view-space and matches GTAO's space; keep the camera-facing guard for double-sided/flipped normals.

---

## 10. Effort & risk

| Area | Files | Rough size | Risk |
| --- | --- | --- | --- |
| Aurora textures + config | `gpu.hpp`, `gpu.cpp` | ~40 LOC | low |
| Aurora RenderPass + encoding + snapshot | `common.cpp` | ~80 LOC | medium (hot path) |
| Aurora pipeline + cache key | `pipeline.hpp`, `gx.cpp` | ~30 LOC | medium (version bump, permutations) |
| Aurora shader-gen | `shader.cpp` | ~30 LOC | medium (fragment struct output) |
| Aurora public API | `gfx.hpp` | ~6 LOC | low |
| Dusklight SDK ABI + bridge | `sdk/.../gfx.h`, `src/dusk/mods/svc/gfx.cpp` | ~30 LOC | low (struct_size-guarded) |
| `ao_mod` consume normals | `mod.cpp`, `gtao.wgsl` | ~30 LOC | low |

Overall: **medium**, mechanically straightforward, additive (color pipeline unchanged), gated off by default. The real risk is that it touches the render hot path and the pipeline cache, so it must be **compiled and validated on-device** (D3D12 / Vulkan / Metal, MSAA on and off) — which cannot be done in this investigation environment (Dawn build; the `extern/aurora` submodule is unpopulated here; no GPU).

---

## 11. Recommendation

Proceed with the thin normals g-buffer. Suggested phasing:

1. **Aurora, no-MSAA path first:** config flag → normal target → pipeline/shader MRT → `resolve_pass` normal snapshot. Validate with the `EnableNormalVisualization`-style output and the `ao_mod` "Normals" debug view.
2. **Dusklight:** SDK ABI field → bridge → `ao_mod` samples the normal, drops `reconstruct_normal`. Compare AO before/after with the existing debug views.
3. **MSAA + encoding hardening:** add the MSAA resolve texture; if precision shows, switch encoding (RGB10A2 / RG16F octahedral).
4. **Optional:** offer the normal buffer to `shadow_mod` (contact shadows) and any future SSR/rim effects.

The GTAO port was written *against* a prepass normal texture and only reconstructs because Aurora didn't expose one. This change closes that gap directly.

---

## 12. Implementation status (what was built) & validation checklist

This design has been **implemented** on `claude/thin-gbuffer-authored-normals-wgqupt` in both repos. It is **not yet compiled or run** — Dawn/WebGPU is not buildable in the investigation environment and there is no GPU — so treat everything below as needing on-device validation.

### Aurora (`aurora-ao`) — commit "gfx: optional thin g-buffer normal target"
- `AuroraConfig::enableNormalBuffer` (off by default) → `GraphicsConfig::normalBuffer`; `NormalBufferFormat = RGBA8Unorm` (`gpu.hpp`).
- `g_normalBuffer` / `g_normalBufferResolved` created/destroyed alongside the EFB textures; `create_render_texture` gained a format arg (`gpu.cpp`).
- `RenderPass` gained normal views + snapshot dst; wired in **all** EFB-pass construction sites (`set_efb_targets`, `resume_efb_pass_loading`, `resolve_pass_into`); `render()` adds a 2nd color attachment; `resolve_pass`/`acquire_pass_snapshot` snapshot the normal via `CopyTextureToTexture` (`common.cpp`).
- `ShaderConfig` gained a `normalTarget` bit (via a spare pad bit); `GXPipelineConfigVersion` 13→14; `build_pipeline` adds a 2nd `ColorTargetState` (blend off; write mask on for any depth-writing draw — `depthCompare && depthUpdate` — so coverage matches the depth buffer, transparencies included); the GX fragment shader emits `@location(1)` `normalize(mv_nrm)*0.5+0.5` + validity (`gx.cpp`, `shader.cpp`, `pipeline.hpp`, `gx.hpp`).
- Public API: `has_normal_buffer()`, `normal_format()`, `ResolveDesc::normal`, `ResolvedTargets::{normal,normalFormat}`, `DrawContext::normalFormat` (`gfx.hpp`).

**The one non-obvious constraint discovered during implementation:** a render pass with two color attachments requires **every** pipeline drawing into it to declare two targets. So the **clear** pipeline gained a write-masked second target (`clear.*`, `GXFrameBuffer.cpp`), and `DrawContext::normalFormat` is exposed so **custom mod draws** recorded into the EFB pass can add a matching (masked) target. RmlUi (its own layer passes), imgui (its own present pass), and the palette/copy conversions (outside the EFB pass) are unaffected.

### Dusklight (`dusklight-ao`) — commit "gfx service: expose the thin g-buffer normal target to mods"
- SDK ABI: `GfxResolveDesc::normal`, `GfxResolvedTargets::{normal,normal_format}`, `GfxDeviceInfo::normal_format`, `GfxDrawContext::normal_format` (all appended, `struct_size`-guarded); bridge maps them in `src/dusk/mods/svc/gfx.cpp`.
- `m_Do_main.cpp` sets `config.enableNormalBuffer = true` (could be gated on a video setting).
- `extern/aurora` is repointed to the matching aurora-ao branch (`claude/thin-gbuffer-authored-normals-wgqupt`).
- **`mods/` is untouched.** The demo `ao_mod`/`shadow_mod` in this repo are upstream's and must keep matching upstream; the real consumers live in `automata-rtx/dusklight-mods`. Any mod that records a **custom draw into the scene pass** (e.g. a `SCENE_AFTER_OPAQUE` composite) must, when `GfxDrawContext::normal_format != Undefined`, declare a second color target of that format with its write mask off — otherwise the pipeline's attachment count won't match the pass.

### Getting a build to test against
Pushes to this branch publish a **`platform-gbuffer-test`** prerelease (game zips + per-arch SDK link stubs), alongside the stable `platform-v2-test` published from `main`. To test in `dusklight-mods`, point `DUSKLIGHT_VERSION` at this branch's commit and `DUSKLIGHT_SDK_STUB_URL` at the `platform-gbuffer-test` release, and install the matching `win32-msvc-x86_64` game build. Revert both knobs to `platform-v2-test` when done.

### On-device validation checklist
1. **Build** Aurora + Dusklight (all three graphics backends: D3D12, Vulkan, Metal).
2. **Baseline (buffer off):** temporarily set `enableNormalBuffer = false`; confirm rendering is byte-for-byte unchanged (default-off path).
3. **Buffer on, no MSAA:** confirm the scene renders normally; then in `dusklight-mods` have **Graphics Hub → Depth to Normal** write the resolved authored normal into its `rgba32float` output instead of reconstructing, and use its debug view to confirm smooth (non-faceted) normals.
4. **MSAA on (2×/4×):** confirm no validation errors and that the normal snapshot resolves (normals are renormalized on read; slight silhouette error is expected/acceptable).
5. **Pass-break paths:** exercise `GXCopyTex`/`GXCopyDisp` clears and any mid-frame EFB copies (the clear pipeline's 2nd target); confirm no "attachment count" validation errors.
6. **Mods drawing into the scene pass:** enable VBAO/SSILVB, Realtime Sun Shadows and Graphics Hub's Deferred Fog together — each records a composite draw into the scene pass, so this is where a missing second color target shows up as a WebGPU validation error.
7. **Precision:** if RGBA8 banding shows on smooth surfaces under strong AO, switch `NormalBufferFormat` to `RGB10A2Unorm` (keeps the validity channel) or RG16F octahedral (needs a separate validity signal).
8. **Perf:** compare frame time with the authored-normal path vs. the old reconstruction/normal-blur path.

### Follow-ups
- Consider gating `enableNormalBuffer` on whether a normal-consuming effect is active, to avoid the extra target's cost when unused (or expose it as a Dusklight video setting).
- In `dusklight-mods`: switch **Graphics Hub / Depth to Normal** to the authored normal (rotate view→world with `world_from_view`, keep the 5-tap reconstruction as the per-pixel fallback where validity = 0), then delete `realtime_sun_shadows/res/normal_smooth.wgsl` — it exists only to hide reconstruction faceting.
