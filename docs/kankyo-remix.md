# Kankyo → RTX Remix: environment colour driving

How Twilight Princess's environment system (d_kankyo, "kankyo" = environment)
feeds our dxvk-remix fork, so time of day, weather, twilight, wolf senses and
per-area palettes shape the path-traced image the way they shaped the original
TEV pipeline.

**This file is the stable design reference.** It changes when the design
changes, not when a test session happens. Everything volatile lives elsewhere.

---

## Start here

| I want to… | Read |
| :-- | :-- |
| Understand what is broken right now | [`remix-open-issues.md`](remix-open-issues.md) |
| Run a test session | [`remix-test-playbook.md`](remix-test-playbook.md) |
| Check whether something was already investigated (last resort — unmaintained archive) | [`remix-history.md`](remix-history.md) |
| Understand the fog specifically | [`kankyo-fog.md`](kankyo-fog.md) (game side) + `dxvk-remix/documentation/DusklightAtmosphere.md` (renderer) |
| Understand where fire and glow get their lights | [`effect-lights.md`](effect-lights.md) |
| Set the game up under Remix | [`dx9-fixed-function.md`](dx9-fixed-function.md) |
| Understand why a material's colour went wrong | `extern/aurora/docs/dx9/remix-material-interface.md` |
| Read a log | `extern/aurora/docs/dx9/material-report.md` |
| Change the overlay / the option wire | `dxvk-remix/documentation/DusklightOverlay.md` |

**The three repos.** `dusklight-ao` is the game; `aurora-ao` is the GX→D3D9
backend, vendored at `extern/aurora`; `dxvk-remix` is the Remix fork. All three
develop on `Fixed-Function-dev`. **`CLAUDE.md` at each repo root is the authority
on branch rules** — this file deliberately does not restate them.

**Two standing constraints that are easy to lose:**

1. **The game and the Remix DLL are one protocol.** Build both from the same
   commit point. Protocol is at **6**; skew in either direction has cost an
   evening twice. The Dusklight tab reports which side is old — read it before
   debugging anything else.
2. **Interactive approval prompts do not work in the owner's environment.**
   Never route anything through one. See `CLAUDE.md`.

**How this project diagnoses things.** All three codebases are ours, which most
Remix projects cannot say. The consequence is a working rule: prefer
*translating* game state into Remix over *tagging* assets in Remix, and prefer
*instrumentation* over asking the owner to describe what they saw. A question we
would have to ask is a defect in the logging.

**What the D3D9 renderer is for.** The raw fixed-function D3D9 image is never
shown to a player. It exists so Remix's DX9→Vulkan translation picks the scene
up automatically — geometry, transforms, textures, most of a frame, for free.
**Remix's renderer is the product; D3D9 is the feed.** So fixed-function limits
are not the ceiling: where the D3D9 stream cannot carry something faithfully
enough to reach Remix, implement it *in Remix* — API or fork change — rather
than contorting D3D9 to approximate it. "Raw D3D9 stays correct" is a passing
safety property, never a design goal. The two exceptions that still have to
rasterize correctly are the **HUD** (Remix rasterizes UI draws) and **alpha**
(Remix reads the stage's alpha for opacity and the alpha test). Full statement:
`extern/aurora/docs/dx9/remix-material-interface.md` §0.

---

## Part I — How kankyo actually works

### I.1 The data model

Stage files (.dzs chunks, loaded through `dComIfGp_getStage*Info()`) provide
four tables per stage (`include/d/d_stage.h`):

- **PAL — `stage_palette_info_class`** (0x34 bytes): one *palette* =
  `actor_amb_col` (RGB), `bg_amb_col[4]` (RGB ×4 BG layers),
  `plight_col[6]` (dungeon light colours), `fog_col`, `fog_start_z`,
  `fog_end_z`, `vrboxcol_id` (skybox colour set), `bg_light_influence`,
  `cloud_shadow_density`, `bloom_tbl_id` (index into the bloom table!),
  `BG1..3_amb_alpha`.
- **Pselect — `stage_pselect_info_class`**: `palette_id[8]` — a palette for
  each of the up-to-8 canonical *time slots* — plus `change_rate` (how fast
  a weather transition into this selection blends).
- **EnvR — `stage_envr_info_class`**: `pselect_id[65]` — one pselect per
  *colour pattern* ("colpat"): index 0 = clear, 1/2/… = weather or story
  variants, 8/9 = underwater, 10 = special. EnvR entries are selected **per
  room** (the envr index is the room number the player/camera is in).
- **VrboxCol — `stage_vrboxcol_info_class`**: skybox colour set: `sky_col`,
  `kumo_top/bottom/shadow_col` (clouds), `kasumi_outer/inner_col` (horizon
  haze).

Global tables in `src/d/d_kankyo_data.cpp`:

- **`l_time_attribute[11]`** (`dKyd_lightSchejule`): maps time of day
  (0–360, 15°/hour) to a pair of the 6 canonical time lights and a blend
  window. The six slots are: 0 morning-0, 1 morning-1, 2 afternoon,
  3 evening-0, 4 evening-1, 5 night. Boss stages use a rotated variant.
- **`l_kydata_BloomInf_tbl[64]`** (`dkydata_bloomInfo_info_class`): the
  bloom mood table. Each entry: `mType` (CLEAR/SOFT — SOFT+id≠0 selects
  screen-blend compositing), `mThreshold`, `mBlurAmount`, `mDensity`,
  `mColorR/G/B` (bloom tint), `mOrigDensity` (base-image weight during
  composite — see I.5), `mSaturateSubtractR/G/B/A` (the **mono colour**:
  full-screen desaturate/tint overlay). Entry 0 = neutral; 1/2 = Twilight
  (golden CF,B1,38 tint, base dimmed to 0xD2/255, 0x60 desaturation);
  3 = wolf senses; 4–9 = field times of day; etc.
- Darkworld table (`l_darkworld_tbl`), light-size tables, maple colours.

### I.2 Selection: time × weather × room

Per frame (`drawKankyo`, `src/d/d_kankyo.cpp:8132` → `setSunpos`,
`SetBaseLight`, `setLight`):

`setLight_palno_get` (`d_kankyo.cpp:1793`) resolves **four palettes**:

```
prev_envr = stage_envr_info[PrevCol]         (PrevCol = previous room/envr id)
next_envr = stage_envr_info[UseCol]          (UseCol  = current room/envr id)
psel_prev = prev_envr.pselect_id[wether_pat0]   (previous colpat)
psel_next = next_envr.pselect_id[wether_pat1]   (current colpat)
schedule slot for daytime → (start_slot, end_slot, color_ratio)
→ palettes: prev[start], prev[end], next[start], next[end]
```

- `color_ratio` = position inside the schedule window (time-of-day blend).
- `pat_ratio` = 0→1 ramp between the *prev* and *next* selections (room
  change, weather change, event colpat change), advanced at the pselect's
  `change_rate` (scaled by our `timeScale` on PC).
- Weather → colpat via `dKy_change_colpat` (`d_kankyo.cpp:9468`) and
  `dKy_custom_colset` (events force both endpoints + blend directly).
- Underwater camera forces pselect 8/9.

### I.3 The blend: one formula for everything

`kankyo_color_ratio_set` (`d_kankyo.cpp:711`):

```
a     = lerp(prev_start, prev_end, color_ratio)     // time within prev pattern
b     = lerp(next_start, next_end, color_ratio)     // time within next pattern
c     = lerp(a, b, pat_ratio)                       // weather/room transition
c     = c + add_color                               // event add-colours
c     = c * now_allcol_ratio * scale                // global + per-channel dimmers
clamp 0..255
```

`scale` carries the per-category ratios (`now_actcol_ratio²`,
`now_bgcol_ratio`, `now_fogcol_ratio`, vrbox ratios…), which events fade via
`dKy_set_*col_ratio`. Add-colours come from `dKy_actor_addcol_amb_set` and
friends (`d_kankyo.cpp:9241-9301`). Everything below uses this one blend.

### I.4 Per-frame outputs (all in `g_env_light`)

`setLight` (`d_kankyo.cpp:2285`) computes, every frame:

| Output | Fields | Consumed by |
| :-- | :-- | :-- |
| Actor ambient | `actor_amb_col` | per-actor TEV via tevstr |
| BG ambients | `bg_amb_col[0..3]` + alphas | room geometry TEV |
| Dungeon light colours | `dungeonlight_col[6]` | `DUNGEON_LIGHT` point lights |
| Fog | `fog_col`, `mFogNear`, `mFogFar` | `GXSetFog(GX_FOG_PERSP_LIN, …)` |
| Fog range adjust | `mXFogTbl`, `mFogAdjCenter` | `GXSetFogRangeAdj` (radial correction) |
| Skybox | `vrbox_sky_col`, kumo ×3, kasumi ×2 | vrbox dome TEV tint |
| Bloom | threshold/size/ratio/blend col/mode/mono col | `mDoGph_gInf_c::getBloom()` setters |
| Misc | `bg_light_influence`, cloud shadow density, shadow alpha | shadows, cloud shadow |

Notable details:

- **The bloom is palette-driven**: `setLight` blends four
  `l_kydata_BloomInf_tbl` entries with the same time/pattern ratios and
  calls `setPoint/setBlureSize/setBlureRatio/setBlendColor/setMonoColor/
  setMode/setEnable` (`d_kankyo.cpp:2473-2604`). In twilight the blur size
  gets a random sinus wobble (`S_fuwan_sin`) — the twilight shimmer.
- **Sun/moon**: `setSunpos` orbits `sun_pos`/`moon_pos` with daytime;
  `SetBaseLight` (`d_kankyo.cpp:4684`) picks the sun as base light between
  daytime 67.5–292.5, else the moon. The base light *colour* is constant
  white; the sun's J3D diffuse for actors is a constant warm
  (126,110,89) — **all mood comes from the ambients**, not the sun colour.
- **Per-actor**: `settingTevStruct` (`d_kankyo.cpp:3678`) fills a
  `dKy_tevstr_c` (amb colour, fog colour/near/far, 6 J3D lights from nearby
  `LIGHT_INFLUENCE`s + base light); `setLightTevColorType_MAJI` writes it
  into J3D material TEV registers. Rooms other than the player's blend via
  `dKy_move_room_ratio`.
- **Wolf senses**: `daPy_py_c::checkNowWolfPowerUp()` short-circuits many
  outputs (black fog/vrbox, bloom table 3, negative cloud density) —
  i.e. senses is *also* mostly palette-driven.
- **Twilight** (`dKy_darkworld_check`, `d_kankyo.cpp:11061` →
  `dComIfGp_world_dark_get()`): twilight stages ship twilight palettes in
  their stage data; the *look* on top is bloom table 1/2: golden bloom
  tint, low threshold, high density, base image dimmed (OrigDensity 0xD2),
  37.5 % desaturation via mono colour, plus d_kyeff particle overlays.

### I.5 How the outputs hit the screen (GC path)

Three mechanisms, all TEV/GX state:

1. **Lighting**: ambients + diffuse lights per draw (J3D material regs +
   `GXLoadLightObjImm`). This is what a path tracer replaces wholesale.
2. **Fog**: `GXSetFog(GX_FOG_PERSP_LIN, near, far, …, fog_col)` per draw
   (`dKy_GxFog_set` / `dKy_GxFog_tevstr_set`, `d_kankyo.cpp:9387-9463`)
   plus `GXSetFogRangeAdj` radial correction.
3. **The EFB post chain** (`m_Do/m_Do_graphic.cpp` bloom_c): order matters —
   1. **Mono pass**: if `mMonoColor.a > 0`:
      `out = lerp(fb, replicate(fb.r) * monoRGB, monoA/255)` — greyscale
      (red channel as luma proxy) tinted by monoRGB, lerped by alpha. This
      is the twilight/senses desaturation.
   2. **Bloom gather** on the mono'd framebuffer (threshold subtract, blur
      pyramid — ported to Remix already as `rtx.bloom.dusklight*`).
   3. **Composite**: `GXSetBlendMode(BM_BLEND, mMode==1 ? INVDSTCLR : ONE,
      SRCALPHA, …)` with src = bloom × blendRGB, srcAlpha = OrigDensity ⇒
      `out = bloom*blendRGB*(screen|add) + fb*(OrigDensity/255)`.
      **OrigDensity scales the base image** — twilight dims the whole scene
      to 82 % here. Our Remix bloom port does not yet do the mono pass or
      the base-image weight.

---

## Part II — What the D3D9 stream carries on its own: almost none of it

This part is about the D3D9 feed by itself. It is the *reason* for Parts III
and IV, not a statement of what the build does today — most of what is listed
here as lost now reaches Remix over the option wire and the Remix API instead.

Aurora's D3D9 backend (`extern/aurora/lib/dx9/`) deliberately ships "v1
unlit" (`docs/dx9/gx-to-d3d9-mapping.md` §8):

- **Lighting**: `D3DRS_LIGHTING = FALSE` permanently
  (`dx9_backend.cpp:106`); GX light state is fully decoded into
  `g_gxState.lights[8]` but never becomes D3D9 lights, so the D3D9 stream
  carries no light at all. **The conclusion that used to follow — "Remix
  sees zero lights from the game" — is superseded.** Light reaches Remix,
  just not through D3D9: the bridge creates the sun/moon distant light and
  the game's live point lights through the Remix API (IV.7), and the kankyo
  ambients drive the grade pass (IV.3).
- **Fog**: **superseded 2026-07-27 — this bullet predates the implementation.**
  `apply_fog_state()` (`extern/aurora/lib/dx9/dx9_draw.cpp:181`) translates
  `g_gxState.fog` into `D3DRS_FOG*` per draw, so the D3D9→Remix capture path
  does fire. What it does not buy is a stable frame fog: Remix keeps the first
  non-`NONE` state of the frame and TP sets fog per tevstr, so the bridge
  pushes the global `g_env_light` fog instead — `kankyo-fog.md` §4, and IV.4
  below.
- **TEV tints**: the mono pass, bloom, vrbox TEV colours are EFB tricks —
  replaced by Remix's pipeline entirely (and our rtx.bloom.dusklight port).
- **Material colour**: this one *does* travel, and it is where the stream was
  extended rather than accepted as it stood. Aurora encodes the colour a
  surface presents into `D3DMATERIAL9` and the TFACTOR/texture-op chain, and
  the fork reads it back: two-colour ramps — `lerp(colourA, colourB, texture)`,
  this game's dominant material shape — are evaluated exactly rather than
  squeezed into one D3D9 op, and the self-illumination facts ride the same transport
  to drive self-illumination (`rtx.dusklight.emissive.*`). Colour reaching
  Remix was tested in game 2026-08-04; the ramp and emissive revisions are
  CI-green and **untested in game**.
  `extern/aurora/docs/dx9/remix-material-interface.md` §9–§10.
- **Vertex colours**: static CLR0 from map data. **Corrected 2026-08-04 — an
  earlier revision of this line claimed GX lighting "isn't baked into vertices,
  so there is no double-counting risk". That is false.** Testing
  `rtx.vertexColorIsBakedLighting` settled it: turning that normalisation *off*
  makes shaded areas visibly darker, which means the vertex colours carry baked
  lighting and shadow. Feeding them to a renderer that then lights the scene
  itself double-counts.

  **Corrected again 2026-08-04:** aurora briefly withheld vertex colour from
  Remix outright, which was too blunt — it also discarded genuine material
  colour. GX distinguishes the two per draw (colour-channel lighting enabled =
  material colour, disabled = finished, possibly pre-lit output), so the
  material case is forwarded and the baked case withheld.
  See `extern/aurora/docs/dx9/remix-material-interface.md` §7c.

Net, **for the D3D9 stream alone**: Ordon at dusk and Ordon at noon would
differ only by what the path tracer sees — geometry and textures, with the
whole mood engine idling. **Superseded 2026-08-04 as a statement about the
build**: that is what Parts III and IV exist to fix, and the bridge, the
atmosphere, the generated sky and the local lights have since closed most of
it. The mood engine travels over the option wire and the Remix API rather than
over D3D9. What is built and what is actually tested:
[`remix-open-issues.md`](remix-open-issues.md).

---

## Part III — The Remix-side surfaces we can drive

### III.1 The colour pipeline (frame order, from `rtx_context.cpp:626-786`)

```
pathtrace → denoise → COMPOSITE (volumetrics, legacy D3D9 fog, sky)   [linear HDR, render res]
  → upscale (DLSS/…) → dust particles
  → BLOOM (incl. our dusklight pyramid)                               [linear HDR, target res]
  → motion blur → auto exposure → TONEMAP (global | local)            [HDR → LDR]
  → PostFX lens (chromatic aberration, vignette)                      [linear LDR]
  → sRGB + dither                                                     [display]
```

Key facts (verified against source):

- **Legacy D3D9 fog capture exists and matches GX fog.**
  `setFogState` (`src/d3d9/d3d9_rtx_utils.cpp:227`) captures
  `D3DRS_FOG*` per draw; the first enabled fog of the frame becomes the
  scene fog (`rtx_scene_manager.cpp:589-614`). Composite's `calculateFog`
  (`composite.slangh:31-72`) implements exactly `f = (end-d)·1/(end-start)`
  for `D3DFOG_LINEAR` — **identical to `GX_FOG_PERSP_LIN`** — applied
  pre-tonemap with `rtx.enableFog` / `rtx.fogColorScale` (default 0.25) /
  `rtx.maxFogDistance`. World units pass through unscaled, so the game's
  fog near/far values work as-is. There is also an optional physically-
  based remap into volumetrics (`rtx.volumetrics.enableFogRemap` +
  colour/distance remap options) for participating-media looks.
- **Grading surface is thin**: global tonemapper has
  `rtx.tonemap.colorGradingEnabled` + `colorBalance/contrast/saturation`,
  but they run **post-tonemap** and only in Global mode — while the
  **default tonemapper is Local**, which has no grading at all. There is no
  LUT anywhere in the pipeline. Conclusion: we add our own small pass, we
  don't hijack the tonemapper.
- **Auto exposure** (`rtx.autoExposure.*`, default on, ±EV clamp −2..+5)
  runs *after* bloom on the final buffer — any global darkening we add will
  be partially compensated. This is desirable (eye adaptation) but must be
  bounded — see IV.6.

### III.2 The Remix API (in-process, no bridge)

From `public/include/remix/remix_c.h` and `rtx_remix_api.cpp`:

- The game is x86_64 MSVC; dxvk-remix is x86_64 — **no bridge involved**.
  Aurora links `d3d9.lib` statically, so Remix's d3d9.dll is mapped before
  `main()`. `GetModuleHandleW(L"d3d9.dll")` +
  `GetProcAddress("remixapi_InitializeLibrary")` yields the interface — and
  doubles as a clean *"am I under Remix?"* detector (returns NULL on stock
  d3d9.dll). No LoadLibrary, no link dependency, no SDK build.
- `SetConfigVariable(key, value)` (`rtx_remix_api.cpp:1240`): sets **any
  RtxOption by dotted name**, string-valued, into the *User* layer
  (priority above rtx.conf). Applied at end of frame → visible next frame.
  Cost per call: mutex + hash + map lookup + string parse — fine for tens
  of calls, but **diff before pushing**. Unknown key → GENERAL_FAILURE
  (check returns; typos are silent otherwise). Malformed value → silently
  kept old value. Call it from the game/D3D9 thread only (the mutation path
  is not synchronized against the CS thread).
- Version gate: header 0.6.4; **minor must match exactly** while major is
  0. We vendor the header from *our fork* and fail soft on mismatch.
- Lights: `CreateLight` (re-create with same hash = update) +
  `DrawLightInstance` **every frame** (active list is cleared per frame);
  distant light via `remixapi_LightInfoDistantEXT` (direction, angular
  diameter, radiance). Requires `dxvk_RegisterD3D9Device(device)` once
  after device creation — aurora owns the device pointer, so this needs a
  small aurora hook. `SetConfigVariable` works without registration.

---

## Part IV — Design

### IV.1 Principle: the game reports state, Remix owns the response

The bridge pushes **raw kankyo values** into a dedicated, documented
namespace (`rtx.dusklight.env.*` — "written by the game every frame, do not
hand-edit"). How strongly each value shapes the image is controlled by
**response options** (`rtx.dusklight.grade.*`, existing `rtx.bloom.*`,
`rtx.fogColorScale`, …) that the game never touches — so they stay
hand-tunable live in the Remix UI and in rtx.conf, per user, per preset.
This split also sidesteps the "User layer beats rtx.conf" property: the
game only occupies keys nobody should be setting by hand.

### IV.2 Transport: `dusk::remix` module (game side)

New `src/dusk/remix_bridge.{cpp,hpp}` (+ vendored `include/remix/remix_c.h`
pinned to the fork's version):

- `dusk::remix::init()` — GetModuleHandle/GetProcAddress dance; logs and
  disables itself when not under Remix or on version mismatch.
- `dusk::remix::setVar(key, fmt, …)` — diff-cache (`unordered_map<string,
  string>`); only calls through on change; checks return code once and
  warns.
- `dusk::remix::tick()` — called once per frame from the game loop after
  `drawKankyo()` has updated `g_env_light` (single-threaded main loop,
  same thread that drives D3D9 — satisfies the API's threading model).
- Config: `game.remixKankyoBridge` (`ConfigVar<bool>`, default true; only
  active when `aurora_get_backend() == BACKEND_D3D9` and init succeeded).
- A small ImGui debug window (pattern: `ImGuiBloomWindow`) showing every
  value currently pushed, with override checkboxes.

Values pushed per frame (diffed), all from `g_env_light` after `setLight`.
Everything lands in the raw namespace — the bridge never writes user-facing
knobs, so a hand-tuned `rtx.bloom.dusklight*` value survives the bridge and
`rtx.bloom.dusklightFollowGame` decides which set the bloom pass consumes:

| Key | Source | Phase |
| :-- | :-- | :-- |
| `rtx.dusklight.env.enable` | bridge heartbeat (true while game drives) | 0 ✅ |
| `rtx.dusklight.env.bloomEnable` | `getEnable()` (palette can disable bloom) | 1 ✅ |
| `rtx.dusklight.env.bloomThreshold` | `getPoint()` /255 | 1 ✅ |
| `rtx.dusklight.env.bloomBlurSize` | `getBlureSize()` (native 0–255 units) | 1 ✅ |
| `rtx.dusklight.env.bloomBlurRatio` | `getBlureRatio()` (native 0–255 units) | 1 ✅ |
| `rtx.dusklight.env.bloomTint` | `getBlendColor().rgb` /255 | 1 ✅ |
| `rtx.dusklight.env.bloomBaseWeight` | `getBlendColor().a` /255 (OrigDensity) | 1 ✅ |
| `rtx.dusklight.env.bloomScreenBlend` | `mMode == 1` | 1 ✅ |
| `rtx.dusklight.env.monoColor` | `getMonoColor().rgb` /255 | 1 ✅ |
| `rtx.dusklight.env.monoAmount` | `getMonoColor().a` /255 | 1 ✅ |
| `rtx.dusklight.env.actorAmbient` | `actor_amb_col` /255 | 3 |
| `rtx.dusklight.env.bgAmbient` | `bg_amb_col[0]` /255 | 3 |
| `rtx.dusklight.env.fogColor` | `fog_col` /255 (backup to fog capture; see IV.4) | 2/3 |
| `rtx.dusklight.env.skyColor` | `vrbox_sky_col` /255 | 4 |
| `rtx.dusklight.env.hazeColor` | `vrbox_kasumi_outer_col` /255 | 4 |
| `rtx.dusklight.env.darkworld` | `dKy_darkworld_check()` | 4 |
| `rtx.dusklight.env.sensesStrength` | `senses_effect_strength` | 4 |

Outside palette transitions almost all values are stable frame-to-frame (and
quantized to the game's 8-bit parameters), so the steady-state push count is
~0–3 strings.

Game-side gating (implemented): when the bridge is active, `bloom_c::draw()`
returns immediately (`src/m_Do/m_Do_graphic.cpp`), so the EFB filter quads stop
overdrawing the path-traced image whatever `game.bloomMode` says. This replaced
the "please set Bloom to Off" advice in `docs/dx9-fixed-function.md`; nobody has
to set it by hand.

### IV.3 Remix fork: the grade stage (mono + ambient tint + base weight)

Faithful placement, refined during implementation to match the GC order
exactly (mono → gather → composite, with the base weight applied at
composite so the bloom gathers from the *undimmed* image):

```
// ambient mood tint (emulates the lost kankyo relight)             [Phase 3 ✅]
c    = c * lerp(1, tint(actorAmbient, bgAmbient), gradeStrength)    // see IV.6
// prepass (before the pyramid, in-place on the HDR final output)   [Phase 1 ✅]
c    = lerp(c, lumaProxy(c) * monoColor, monoAmount)
// composite (existing bloom composite shader)                      [Phase 1 ✅]
out  = c_orig_or_prepassed * baseWeight + bloom * tintCol * (screen ? 1-dst : 1)
```

*(The grade moved ahead of the mono overlay during implementation. The
draft had it between mono and composite, but that is not where the GC put
it: the ambient was applied per surface during **shading**, and the mono
overlay and bloom were the post pass in `draw2()` that ran afterwards. The
two do not commute — `lumaProxy` is linear, so `grey(c·t) ≠ grey(c)·t`
unless `t` is neutral — so the order is worth getting right. Grading first
also means the mono overlay desaturates an already-graded image, which is
what twilight did.)*

New options (all in the fork, all live-tunable):

- `rtx.bloom.dusklightMonoColor` (Vector3, default 1,1,1),
  `rtx.bloom.dusklightMonoAmount` (float 0–1, default 0),
  `rtx.bloom.dusklightMonoLumaMode` (0 = red-channel like the TEV,
  1 = BT.709 — default red for fidelity, luma for taste),
  `rtx.bloom.dusklightBaseWeight` (float 0–1, default 1).
- `rtx.dusklight.grade.enable` (default false),
  `rtx.dusklight.grade.strength` (0–1, default 0.65),
  `rtx.dusklight.grade.chromaOnly` (default true — normalize the tint to
  preserve luminance so auto exposure doesn't fight it; see IV.6),
  `rtx.dusklight.grade.actorAmbientWeight` (0–1, default 0.25 — the two
  ambients collapse into one multiply, weighted by screen share),
  `rtx.dusklight.grade.maxDarkening` (0.35) and
  `rtx.dusklight.grade.maxBrightening` (2.0) as the response rails.
- `rtx.dusklight.env.*` as plain options (Vector3/float/bool) with
  defaults = neutral, flagged **NoSave** so a UI "save settings" doesn't
  bake a random Tuesday's dusk into user.conf (verify `RtxOptionLayer::
  save()` honours NoSave; if not, fix that in the fork first — checklist
  item P1.4).

Implementation shape (as built): its own `RtxPass`,
`rtx_render/rtx_dusklight_grade.{h,cpp}` +
`shaders/rtx/pass/dusklight/dusklight_grade.{h,comp.slang}`, dispatched
from `RtxContext` immediately before `dispatchBloom`. The draft had it as
another step inside `DxvkBloom::dispatch`; a separate pass is better here
because the grade is not part of the bloom — folding it in would have made
`rtx.bloom.enable = False` silently take the ambient grade with it, and
the point of the phase gating is that the two fail independently.

The tint has no per-pixel variation, so all of the response shaping runs on
the CPU (`resolveGrade()`) and the shader is a single multiply. That also
lets `resolveGrade()` report the resolved tint in the UI, and lets the pass
skip its dispatch entirely when the tint comes out neutral — which is what
makes "bridge off ⇒ image identical to baseline" exact rather than
approximate.

**Rails, and why they are applied twice.** The floor/ceiling act on the
tint's overall *level* first (by scaling, so hue survives) and only then
per channel. A per-channel-only clamp destroys exactly what the grade is
for: a night ambient is dark in all three channels, so clamping each one
against a 0.35 floor flattens it to neutral grey. Scaling first lifts it to
the floor with its blue cast intact. Under `chromaOnly` the level is 1 by
construction so only the per-channel backstop can fire — and it must,
because normalizing a strongly saturated ambient leaves its dominant
channel several times above 1.

**Considered and rejected: weighting the grade by pixel darkness.** In TEV
the ambient was a *lift* (`matColor · (ambient + Σ lights)`), so it
dominated shadowed surfaces and was swamped on lit ones; weighting the tint
by `1/(1 + luma)` would emulate that far better than a flat multiply. It
was dropped because "darkness" has no stable meaning here: the grade runs
pre-tonemap, before auto exposure updates, so the luminance threshold
separating shadow from light would drift with scene exposure and the knob
would be untunable. Revisit if the grade ever moves after tone mapping, or
if exposure is pinned.

### IV.4 Fog: implement the designed GX→D3D9 mapping in aurora

> **Superseded (2026-07-27).** The aurora mapping described here *was*
> implemented and works — see `apply_fog_state()` in
> `extern/aurora/lib/dx9/dx9_draw.cpp`. What this section got wrong is what
> happens on the Remix side afterwards. Two findings:
>
> 1. **"First-fog-wins" is not a hypothetical.** `rtx_scene_manager.cpp:609`
>    keeps the first non-`NONE` fog state of the frame and discards the rest,
>    and TP sets fog *per tevstr*. Plan B (a pushed fog override) is now the
>    plan, not the fallback.
> 2. **Volumetrics is not a drop-in "user preference".** With *stock* options it
>    cannot express the game's fog at all: the froxel grid is 20 m, fog remap
>    is off by default, and the remap's endpoints are unclamped and calibrated
>    for a different game.
>
>    **Note the word "stock".** The fork has since removed that constraint —
>    `rtx.dusklight.atmosphere.*` derives one medium from the game's own palette
>    and sizes the froxel grid from the game's fog range, and Remix's own fog
>    remap is bypassed entirely while it is on. "Remix cannot express X" is a
>    claim about the runtime we were handed, and this one is ours.
>
> The fog design now lives in `docs/kankyo-fog.md` (game side) and
> `dxvk-remix/documentation/DusklightAtmosphere.md` (renderer side), where fog
> is treated as one system with the sky rather than as an independent effect.
> The rest of this section is kept for the aurora mapping rationale.

The best fog path needs **zero Remix changes**: aurora already decodes GX
fog (`g_gxState.fog`) and the mapping spec
(`extern/aurora/docs/dx9/gx-to-d3d9-mapping.md` §fog) already defines the
translation it never implemented. Implement it in `dx9_draw.cpp`:

- `GX_FOG_PERSP_LIN` → `D3DRS_FOGENABLE=TRUE`, `D3DFOG_LINEAR` (vertex
  mode), `FOGSTART/FOGEND` solved from the GX a/b/c curve (for LIN these
  are just the near/far the game passed), `FOGCOLOR` = kankyo `fog_col`.
- `GX_FOG_NONE` → `FOGENABLE=FALSE` (UI and most 2D already run fog-off).

Remix then captures it per draw (`setFogState`), and composite applies
exactly the right linear ramp pre-tonemap. Tuning lives in existing
options: `rtx.enableFog`, `rtx.fogColorScale` (raise from 0.25 toward ~1.0
because kankyo fog colours are authored as display colours; calibrate),
`rtx.maxFogDistance`. For heavy-atmosphere areas (twilight, Snowpeak) the
same captured state can optionally feed volumetrics via
`rtx.volumetrics.enableFogRemap` + `enableFogColorRemap` for real light
shafts — user preference, off by default.

`rtx.dusklight.env.fogColor` is still pushed as a debug/backup channel; if
per-draw capture proves noisy (first-fog-wins picking a stray draw), plan B
is a config-var-driven fog override in composite (small fork change), but
the capture path is strongly preferred.

XFog (`GXSetFogRangeAdj` radial correction) is deliberately out of scope —
its visual delta at PC aspect ratios is small; note it as a possible v2
refinement of the composite fog term.

### IV.5 Bloom: close the last gaps in the port

> **Superseded.** This section was written from the first reading of
> `bloom_c::draw2()` and is wrong in two places — the threshold and the
> colour space. The corrected account, derived from the TEV stages, is in
> [`dx9-fixed-function.md`](dx9-fixed-function.md) §"Dusklight bloom options"
> (its origin is "Bloom fidelity: four errors in the port" in
> [`remix-history.md`](remix-history.md), which is unmaintained). Kept for the
> history of how the port got here.

Already 1:1 (deliberately, same 0–255 units): threshold, blur size, blur
ratio, tint, screen-blend. Missing pieces this plan adds (IV.3): mono
colour/amount, base weight. One calibration knob is needed:

- `rtx.dusklight.bloomThresholdScale` (fork option, default 1.0): the
  game's threshold is an LDR 0–255 subtract *after* lighting; Remix's is a
  linear-HDR pre-tonemap value. The bridge pushes
  `mPoint/255 × 1.0` and the *user-tunable scale* is applied Remix-side
  when consuming it (keeps the response knob out of the game).

### IV.6 Interactions to design around (not away)

- **Auto exposure vs. mood darkening.** Chroma-only tint by default
  (`grade.chromaOnly`): normalize `tint` by its BT.709 luma so night/dusk
  changes hue balance, not net luminance — exposure stays stable and the
  path tracer's own darkness (sun angle etc., once phase 4 lands) carries
  the luminance story. `dusklightBaseWeight` *is* luminance-affecting by
  design (twilight dimming); recommend pairing twilight with a slightly
  narrower `rtx.autoExposure.evMaxValue` in the shipped rtx.conf template.
- **Frame latency.** SetConfigVariable lands next frame. Kankyo blends
  over seconds; one frame is invisible.
- **User overrides.** Game-fed keys occupy the User layer permanently once
  pushed. Response keys are never pushed, so UI/rtx.conf control them
  normally. Bridge off ⇒ everything reverts to hand-set values.
- **Persistence.** Verify/ensure NoSave options are excluded from
  `RtxOptionLayer::save()` so game-fed values don't leak into user.conf.
- **Units.** Composite fog consumes world units as submitted (scene-scale
  application is commented out in `rtx_composite.cpp:408-413`) — game fog
  near/far pass through unchanged. Volumetrics remap options are in
  meters — only relevant if fog-remap is enabled; document
  `rtx.sceneScale` interplay there.

### IV.7 Phase 4+ (out of first scope, designed for)

**All three of these have since been built.** The design below is kept because
it is still the shape of what shipped; where the built thing diverged, the
bullet says so.

- **Sun/moon distant light** (implemented, `game.remixSunMoonLight` default
  true). `setSunpos`/`SetBaseLight` give direction;
  colour is constant warm white (I.4). Aurora calls
  `dxvk_RegisterD3D9Device` after device creation; the bridge then does
  `CreateLight`(same hash, DistantEXT, updated direction) when the sun
  moves + `DrawLightInstance` per frame. Gate behind
  `game.remixSunMoonLight` and let users pick it *or* hand-placed RTX
  lights. Twilight/interiors: skip drawing the light (interiors detect via
  `dKy_SunMoon_Light_Check()`.)
- **Sky/vrbox tint** (implemented, and the answer was neither branch the draft
  offered). The sky colours are pushed every frame — `skyColor`,
  `kasumiInner`/`kasumiOuter` and the kumo set, `kankyo-fog.md` §5 — and Remix
  builds its own lat-long dome from them and registers it as a **dome light**,
  with the game's vrbox hidden (`rtx.dusklight.game.hideVrbox`) so there is
  only one sky. Tested good 2026-07-28. Probing the vrbox raster draws was
  dropped rather than investigated to a conclusion: a generated dome is exact
  and is a light source, which a captured LDR probe is not.
- **Effect lights** — a sphere light at the **origin of the effect that draws
  the fire**, rather than at the position of any light the game registered.
  This is what lights interiors and night, and it is the only fine-grained
  light source indoors: the sun/moon is gated off there, and Remix has no dome
  light type so a sky is never NEE-sampled at all. Design, and the reasons the
  anchor is the JPA emitter rather than the actor or the particle:
  [`effect-lights.md`](effect-lights.md). **Tested in game 2026-08-07.**
- **Local point lights** — the *previous* system, now off by default and kept
  only as the comparison path. It mirrored `g_env_light.pointlight[100]`
  (`dKy_plight_set`) — every torch, brazier, lantern, campfire, Midna glow and
  bomb flash — at the position the game put the light. That works under a
  rasterizer, where a point light casts no shadow and can sit anywhere the
  shading looks best, and reads as wrong under a path tracer, which casts a
  real shadow from the exact point the light occupies. Do not run both: every
  fire gets two lights, one of them in the old place.

---


## Where the implementation lives

Superseded phase plans and old session notes are archived in
[`remix-history.md`](remix-history.md) — unmaintained, and not a source of fact.
What is currently broken or untested: [`remix-open-issues.md`](remix-open-issues.md).
