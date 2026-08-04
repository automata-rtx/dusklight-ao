# Dusklight platform direction: Deferred renderer vs RTX Remix — notes & GPU-skinning findings

Status notes originally captured on branch `claude/gpu-skinning-72pstj`
(dusklight-ao + aurora-ao). This records the strategic discussion about
evolving Dusklight's renderer and the concrete findings from the GPU-skinning
investigation, so the context is not lost.

> **That branch name is historical.** `claude/*` branches are disposable and
> get deleted. The GPU-skinning work itself is **not** lost — the
> `Fixed-Function` lineage is based on it, which is why Remix gets rest-pose
> vertices plus GPU-side skinning and therefore stable mesh hashing (§4). All
> current development is on `Fixed-Function-dev` in all three repos; see
> `CLAUDE.md`.

> **Superseded 2026-08-04 — the direction below was decided the other way.**
> §3 rejects the DXVK route ("you'd write a lossy GX→D3D9FF translator"). That
> translator was written, it works, and it is the active line: aurora carries a
> GX→D3D9 fixed-function backend (`extern/aurora/lib/dx9/`) that feeds Remix's
> DX9→Vulkan translation, with the Remix API and the `dxvk-remix` fork used for
> what the D3D9 stream cannot carry. The premise that lossiness disqualifies it
> was wrong: **the raw D3D9 image is never shown to a player — it is the feed,
> and Remix's renderer is the product** — so fixed-function limits are not the
> ceiling. Only the HUD and alpha still have to rasterize correctly. Full
> statement: `extern/aurora/docs/dx9/remix-material-interface.md` §0. The
> GPU-skinning findings (§4–§7) are unaffected by the reversal, which is the
> main reason this file is still worth reading.

---

## 1. Why this exists

Dusklight (Twilight Princess PC port) renders through a **forward** GX→WebGPU
path (aurora) — still true of the default build, but aurora now also carries a
GX→D3D9 fixed-function backend (`extern/aurora/lib/dx9/`), which is what the
Remix line uses. Graphics features are added as mods that reconstruct, at a
cost, information the renderer already had and discarded (normals from depth,
fog re-application, etc.). Two long-horizon directions were weighed:

1. Build a **deferred(-style) renderer** for Dusklight/aurora that preserves the
   authentic look but exposes the buffers modern effects need.
2. Fork Dusklight/aurora to be **RTX Remix friendly** (mesh/texture/HUD/particle
   passthrough to Remix's path tracer), explicitly *not* preserving GameCube
   lighting/bloom/authentic look.

## 2. What a deferred / "thin G-buffer" renderer buys us

Every current mod reverse-engineers what a G-buffer would hand over directly:
- `depth_to_normal` reconstructs normals from depth (faceted, edge error).
- `deferred_fog` re-applies fog math and, for mixed scenes, burns an extra
  draw-list replay to build a per-pixel ID buffer.
- `realtime_sun_shadows` reconstructs receiver normals from depth.

With true buffers (world normal, albedo, material/fog ID, motion vectors,
optionally emissive / pre-compose lighting), the whole `depth_to_normal_consumers.md`
menu (SSR, SSGI/VBGI, SSDO, outlines, curvature) becomes robust, TAA/motion blur
and better temporal accumulation become possible, and several mods stop paying
for an extra replay.

**Recommended shape of Option 1: an MRT-augmented *forward* renderer ("thin
G-buffer"), NOT literal screen-space deferred shading.** Keep aurora's forward
TEV pass byte-identical and have the generated fragment shader *additionally*
write world normal (nearly free — the vertex shader already has it), an albedo
approximation (dominant TEV texture tap), a material/fog ID (trivial), and later
motion vectors. Authenticity is preserved by construction (attachment 0
unchanged). Literal deferred shading is the wrong shape for TEV (arbitrary
per-material programs don't cleanly separate albedo from lighting), and buys
nothing extra since GX lighting is cheap per-vertex.

### Thin G-buffer vs a "proper" G-buffer — what's still missing
- Missing from thin but present in proper deferred: **separable lighting**
  (can't cleanly relight — SSGI on top double-counts baked light), authoritative
  albedo, emissive tagging.
- Missing from **both** because GX/TEV isn't PBR: real roughness/metalness/F0
  (must be *authored*), and multi-layer transparency (single opaque layer only).

**Superseded 2026-08-04 on the Remix path — emitters are not a tagging
problem.** Aurora scores GX evidence per draw (colour-channel lighting disabled
0.50, register-sourced colour 0.25, a TEV stage scaled past displayable 0.25)
and the fork cuts at `rtx.dusklight.emissive.threshold`, live in the F1 overlay.
The measured counter-example is why it is a score rather than a rule: the Goron
Mines lava has GX lighting **enabled**, so "lighting is off" alone never
identified an emitter. `extern/aurora/docs/dx9/remix-material-interface.md` §9.
Rev 2 of that scoring is CI-green and **untested in game**.

## 3. Option 2: RTX Remix (SDK path)

- **DXVK path is wrong** for us — Dusklight isn't D3D9 and TEV exceeds
  fixed-function; you'd write a lossy GX→D3D9FF translator.
  **Superseded 2026-08-04:** the translator exists (`extern/aurora/lib/dx9/`)
  and is the path in use. TEV does exceed fixed-function, and that is not
  fatal, because the D3D9 stream only has to be a feed Remix can pick the scene
  up from; what it cannot carry is implemented in the fork instead. Two
  examples that were once written down as hard limits: two-colour TEV ramps are
  now evaluated exactly in the fork's shader, and self-illumination is carried
  as a GX evidence score (both CI-green, **untested in game**). The HUD and
  alpha are the two things that still have to rasterize correctly.
- **remixapi SDK path is a surprisingly good architectural match:** aurora's
  `push_gx_draw` (`aurora-ao/lib/gx/command_processor.cpp`) already centralizes
  per-draw {vertex/index ranges, world transforms in the uniform (proj + pnMtx),
  texture bind groups (already format-decoded)}. Since we waive TEV/lighting
  fidelity, the hard part (shader translation) is skipped. `GFX_STAGE_SCENE_AFTER_OPAQUE`
  gives a clean scene boundary. HUD is separable (game distinguishes 2D/HUD
  draws; mod stages bracket them). Particles → emissive/translucent billboards.
- **Cost/obstacles:** it's a months-scale new aurora backend plus a J3D-level
  skinning intercept, then an open-ended *content* effort (authoring lights/PBR
  materials/replacements in the toolkit for a 40-hour game). Windows + RTX only;
  does not feed the 7-platform CI or mobile. Long-tail scene coverage (sky,
  twilight realm, wolf senses, heat-haze) needs re-authoring or acceptance that
  it's gone initially. **2026-08-04:** the sky half of that is done — the fork
  generates a Hillaire physical sky, and API-submitted assets are capturable and
  replaceable in this fork (mesh hashes are derived from the submitted geometry
  rather than a creation-order counter, and `submitExternalDraw` consults the
  replacer), which upstream they are not. That second half is **CI-green and has
  not yet been exercised by an actual capture in game** — it is what this route
  would rest on, so it is worth testing before anyone plans around it.

## 4. CRITICAL: hardware/GPU skinning is a prerequisite for Remix skinned-mesh replacement

Remix identifies and replaces meshes by a **stable rest-pose hash** and does its
**own** GPU skinning from per-vertex **bone indices + weights** (remixapi supports
skinned meshes; `rtx.limitedBonesPerVertex` default **4**; the toolkit auto-remaps
USD skeletons). Requirements for Remix to swap a skinned character:
- **Stable rest-pose mesh** submitted (not per-frame-mutated geometry).
- **Per-vertex bone indices + weights** (≤4).
- **Per-instance bone transforms** supplied separately each frame.

If the game hands Remix **CPU-deformed** (already-posed) vertices, the mesh hash
is unstable every frame → per-asset replacement breaks for exactly the characters
you'd most want to remaster, and motion vectors are missing (ghosting). So Remix
compatibility for characters **requires** that skinning stays GPU-side (rest mesh
+ weights + bone matrices), never baked on the CPU. This is also why GPU skinning
+ motion vectors is the single highest-leverage shared prerequisite: it de-risks
**both** the deferred path (motion vectors) and the Remix path (stable hashes).

## 5. Renderer architecture facts (aurora)

- aurora reimplements Dolphin GX/OS/VI; the game issues real GX calls and is
  unaware of WebGPU. `lib/gx/gx.hpp` holds `g_gxState`.
- **TEV → WGSL is runtime string codegen** in `lib/gx/shader.cpp`
  (`build_shader_source`), keyed/deduped by `xxh3(ShaderConfig)`. No fixed shader
  set; ~hundreds–low-thousands of permutations/session, async-built.
- Draws flow game → GX (mostly display lists) → aurora FIFO
  (`command_processor.cpp`) → `push_gx_draw` builds one `DrawData` per draw;
  vertex/index/uniform/storage streamed per-frame into shared ring buffers.
- **Frame = one forward EFB pass** (single MSAA color+depth); EFB copies split
  offscreen passes. Not a G-buffer. Reversed-Z (1=near, sky depth 0).
- Uniform layout is maintained **by hand**, packed (not std140): the
  `uniBufAttrs` (shader.cpp) ↔ `build_uniform` (shader_info.cpp) ↔ `uniformSize`
  (build_shader_info) triad must stay in lockstep. `MaxUniformSize`=3840.
- Aurora already grows via public GX extensions (`GXSetProjectionFull`, custom
  draw types); that's the idiom for adding new backend features.

## 6. GPU-skinning finding — characters are ALREADY GPU-skinned

This corrected a wrong premise from the initial exploration.

- **Two J3D skinning forms exist:**
  1. `J3DSkinDeform` (**CPU per-vertex** linear-blend, `libs/JSystem/.../J3DSkinDeform.cpp`).
     Used **only** for models that (a) carry INF1 flag `J3DMLF_NoMatrixTransform`
     (0x100) **and** (b) had `setSkinDeform` called. In the entire game that is
     **two actors**: `d_a_door_boss` (boss-door lock) and `d_a_demo00` (cutscene
     dummies). `mSkinDeform` is NULL for Link and all normal characters.
  2. `calcWeightEnvelopeMtx` (**GX matrix-palette**, the normal path). Envelope
     matrices are blended into a small per-influence-set matrix palette on the
     CPU (cheap, per-draw-matrix — NOT per-vertex), and **the GPU applies them
     per-vertex via `PNMTXIDX` → `pnMtx`** in aurora's vertex shader.
- **Therefore Link's expensive per-vertex transform was already on the GPU.**
  aurora already receives, for characters: **rest-pose vertices + per-vertex
  PNMTXIDX + the matrix palette** — i.e. the skinning data Remix wants (the
  pre-blended draw matrix is effectively "1 bone per vertex").
- `J3DModel::calc` (`libs/JSystem/.../J3DModel.cpp:~426`) calls
  `mSkinDeform->deform(this)` only `if (mSkinDeform != NULL)`; `calcWeightEnvelopeMtx`
  runs when `getWEvlpMtxNum()!=0 && !checkFlag(0x100)`.

**Implication for Remix:** characters are ~90% of the way there already — rest
mesh + PNMTXIDX + palette are exposed at `push_gx_draw`. To feed Remix you'd
surface {rest verts, PNMTXIDX (or expanded weights), per-frame draw-matrix
palette}. Only the two niche `J3DSkinDeform` actors are genuinely CPU-deformed;
those DO need GPU-skinning offload (which this branch provides) to be Remix-safe.

**Status 2026-08-04:** this landed on the D3D9 feed rather than the SDK
backend — skinned characters render under Remix on both skinning forms.
Aurora's `docs/dx9/progress.md` is authoritative for what the backend does and
does not carry.

## 7. What was built on `claude/gpu-skinning-72pstj`

- **aurora-ao:** a public `GXSetSkinning` / `GXClearSkinning` extension +
  `GXSetSkinningDebugView`, WGSL linear-blend skinning codegen (palette +
  influence table in the shared storage buffer, model→view base matrix), and a
  **bone-index debug view**: any draw with a per-vertex PNMTXIDX attribute is
  coloured by matrix index when the view is on (global `skinDebugView`, set in
  `push_gx_draw`). Confirmed in-game: Link renders as a per-bone colour patchwork
  that deforms with animation = matrix-palette GPU skinning, visible.
- **dusklight-ao:** GPU-skins the rare `J3DSkinDeform` models on PC
  (`src/dusk/gpu_skinning.cpp`; `#if TARGET_PC` hooks in `J3DSkinDeform::deform`
  and `J3DShapePacket::drawFast`), by mirroring the CPU fast-path exactly (same
  `mSkinNList` influences, same `mPosMtx` palette) so output is identical; CPU
  fallback for >4 influences / non-fast-skin. UI toggle "Visualize GPU Skinning"
  at the bottom of Interface → Dusklight (`game.skinDebugView`), driven per-frame
  from `m_Do_graphic.cpp` scene-begin.
- **Build/branch reality:** this branch is based on upstream `ao` (a6f0598), NOT
  the platform base (76b56cd8 / `DUSKLIGHT_VERSION` 9361fbd9). `extern/aurora`
  was repointed to the aurora fork on the same branch. Game-linked mods built
  against the platform base may be **ABI-mismatched** with this game build.

## 8. Open items / recommended next steps

**Read these as of the date they were written.** The direction was settled in
favour of the D3D9 feed into the `dxvk-remix` fork (see the note at the top), so
items 2 and 3 are kept as the reasoning of the time, not as current plans.

1. **True per-character motion vectors on the matrix-palette path** — the genuinely
   new capability for characters, and the shared prerequisite for both deferred
   and Remix. Needs game-side previous-frame draw-matrix tracking (snapshot
   `mMtxBuffer` draw matrices per model) + a shader that computes screen motion
   from prev vs current matrices. Larger, matched-decomp-adjacent, needs in-game
   verification. The debug view proves the path it builds on.
2. **Thin G-buffer (normal + material/fog ID first, albedo, then motion)** as the
   deferred foundation — MRT-augment the forward TEV pass in aurora.
3. **Remix SDK backend** off `push_gx_draw` — only as its own standalone project
   with the full content cost accepted; reuse the per-draw transform/identity
   understanding from the skinning work.
   **Superseded 2026-08-04:** the route taken is the DXVK one — the GX→D3D9
   fixed-function backend in `extern/aurora/lib/dx9/` feeding the `dxvk-remix`
   fork, with the Remix API used for what the D3D9 stream cannot carry (the
   generated sky, lights, the atmosphere medium). The per-draw transform work
   was reused as predicted. Entry point: `docs/kankyo-remix.md`.
4. **Shadow interaction (root-caused and fixed).** With the debug view on, Link's
   cast shadow became a mess: other GPU-skinned casters (a bird, the Lake Hylia
   cannons) bled into it, it had gaps, and it was pose/camera dependent. Root cause
   was the debug view itself, *not* the `realtime_sun_shadows` mod. The game's own
   **real-shadow** system (`dDlst_shadowReal_c` / `dDlst_shadowControl_c` in
   `src/d/d_drawlist.cpp`) re-draws each character's skinned shapes — through the
   matrix-palette `drawFast` path, so those draws carry `PNMTXIDX` — into an
   **offscreen** silhouette texture, packing **four casters into the R/G/B/A
   channels** of one RGB5A3 image (`l_imageDrawColor[4]` = pure R/G/B/A), then
   swizzles each channel back out on readback. The bone-index debug override
   (`skinDebug`) had **no pass scoping**, so it replaced those silhouette masks with
   per-bone RGB + `alpha=1.0`, contaminating every channel → cross-caster bleed,
   zeroed-colour gaps, pose/camera dependence. A depth-only pass (the shadow *mod*)
   is immune, which is why the corruption is the game's own colour-packed shadow,
   independent of whether the mod is loaded (if the mod's game-shadow suppression
   hook misbinds under the `a6f0598` vs platform-`76b56cd8` ABI split, the game
   shadow renders and the bug shows).
   **Fix:** gate the debug override on `!gfx::is_offscreen()` in aurora
   `push_gx_draw` (aurora commit `7b7306e`), so it only colours the main EFB pass
   and never touches offscreen silhouette/reflection redraws. General lesson: any
   fragment-output override in aurora must be scoped to the main pass, because the
   game re-draws the same geometry into offscreen buffers with channel-specific
   meaning.
