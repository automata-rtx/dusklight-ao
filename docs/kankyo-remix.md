# Kankyo → RTX Remix: environment colour driving

Design doc for feeding Twilight Princess's environment/mood system (d_kankyo,
"kankyo" = environment) into our dxvk-remix fork, so time of day, weather,
twilight, wolf senses and per-area palettes shape the path-traced image the
way they shaped the original TEV pipeline.

Everything below is grounded in the three codebases as of this writing:
dusklight-ao (game), aurora-ao (GX backend, `extern/aurora`), and
automata-rtx/dxvk-remix (Remix fork). File references use repo-relative
paths.

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

## Part II — What survives under Remix today: almost none of it

Aurora's D3D9 backend (`extern/aurora/lib/dx9/`) deliberately ships "v1
unlit" (`docs/dx9/gx-to-d3d9-mapping.md` §8):

- **Lighting**: `D3DRS_LIGHTING = FALSE` permanently
  (`dx9_backend.cpp:83`); GX light state is fully decoded into
  `g_gxState.lights[8]` but never becomes D3D9 lights. Remix sees zero
  lights from the game. Kankyo ambients therefore never reach Remix.
- **Fog**: `D3DRS_FOGENABLE = FALSE` (`dx9_backend.cpp:93`,
  `dx9_draw.cpp:206`). GX fog regs *are* decoded into `g_gxState.fog`
  (type/a/b/c/colour) and then dropped. The D3D9→Remix fog capture path
  (below) never fires.
- **TEV tints**: the mono pass, bloom, vrbox TEV colours are EFB tricks —
  replaced by Remix's pipeline entirely (and our rtx.bloom.dusklight port).
- **Vertex colours**: only static CLR0 from map data reaches Remix (used as
  albedo tint). Since GX *lighting* isn't baked into vertices, there is
  **no double-counting risk** when we re-apply kankyo mood Remix-side —
  the dynamic component is currently 100 % absent.

Net: under Remix, Ordon at dusk and Ordon at noon differ only by what the
path tracer sees — geometry and textures. The entire mood engine idles.

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

Values pushed per frame (diffed), all from `g_env_light` after `setLight`:

| Key | Source |
| :-- | :-- |
| `rtx.dusklight.env.enable` | bridge heartbeat (true while game drives) |
| `rtx.dusklight.env.actorAmbient` | `actor_amb_col` /255 |
| `rtx.dusklight.env.bgAmbient` | `bg_amb_col[0]` /255 |
| `rtx.dusklight.env.fogColor` | `fog_col` /255 (backup to fog capture; see IV.4) |
| `rtx.dusklight.env.skyColor` | `vrbox_sky_col` /255 |
| `rtx.dusklight.env.hazeColor` | `vrbox_kasumi_outer_col` /255 |
| `rtx.dusklight.env.darkworld` | `dKy_darkworld_check()` |
| `rtx.dusklight.env.sensesStrength` | `senses_effect_strength` |
| `rtx.bloom.dusklightThreshold` | `getPoint()/255 × rtx.dusklight.bloomThresholdScale` (game reads the scale? no — see IV.5) |
| `rtx.bloom.dusklightBlurSize` | `getBlureSize()` (same 0–255 units by design) |
| `rtx.bloom.dusklightBlurRatio` | `getBlureRatio()` (same 0–255 units) |
| `rtx.bloom.dusklightTint` | `getBlendColor().rgb` /255 |
| `rtx.bloom.dusklightScreenBlend` | `mMode == 1` |
| `rtx.bloom.dusklightBaseWeight` | `getBlendColor().a` /255 (OrigDensity — new option, IV.5) |
| `rtx.bloom.dusklightMonoColor` | `getMonoColor().rgb` /255 (new option, IV.5) |
| `rtx.bloom.dusklightMonoAmount` | `getMonoColor().a` /255 (new option, IV.5) |

That's ≤ 17 strings, and outside palette transitions almost all are stable
frame-to-frame, so the steady-state push count is ~0–3.

Game-side gating: when the bridge is active, force `game.bloomMode = Off`
behaviour for the EFB bloom (skip `bloom_c::draw()` under Remix) so the
raster filter quads stop overdrawing the path-traced image — this replaces
the "please set Bloom to Off" advice in `docs/dx9-fixed-function.md`.

### IV.3 Remix fork: the grade stage (mono + ambient tint + base weight)

One new compute stage, running **inside the Dusklight bloom pass, before
the pyramid** (faithful to the GC order mono → gather → composite), on
`m_finalOutput` in linear HDR:

```
c   = color[ipos]
// 1. mono (twilight/senses desaturation): red-replicate luma, like the TEV
mono = lerp(c, lumaProxy(c) * monoColor, monoAmount)
// 2. ambient mood tint (new; emulates the lost kankyo relight)
tint = normalize–or–raw(actorAmbient, bgAmbient)   // see IV.6
g    = mono * lerp(1, tint, gradeStrength)
// 3. base weight (OrigDensity — twilight dims base image to 0.82)
out  = g * baseWeight
```

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
  `rtx.dusklight.grade.maxDarkening` (floor on the tint, default 0.35).
- `rtx.dusklight.env.*` as plain options (Vector3/float/bool) with
  defaults = neutral, flagged **NoSave** so a UI "save settings" doesn't
  bake a random Tuesday's dusk into user.conf (verify `RtxOptionLayer::
  save()` honours NoSave; if not, fix that in the fork first — checklist
  item P1.4).

Implementation shape: mirror the existing pass structure —
`bloom_dusklight_grade.comp.slang` + push-constant struct in
`shaders/rtx/pass/bloom/bloom.h`, dispatched at the top of
`DxvkBloom::dispatch` when the Dusklight path or grade is active. It's a
single full-screen RW pass, same cost class as bloom composite.

### IV.4 Fog: implement the designed GX→D3D9 mapping in aurora

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

- **Sun/moon distant light.** `setSunpos`/`SetBaseLight` give direction;
  colour is constant warm white (I.4). Aurora calls
  `dxvk_RegisterD3D9Device` after device creation; the bridge then does
  `CreateLight`(same hash, DistantEXT, updated direction) when the sun
  moves + `DrawLightInstance` per frame. Gate behind
  `game.remixSunMoonLight` and let users pick it *or* hand-placed RTX
  lights. Twilight/interiors: skip drawing the light (interiors detect via
  `dKy_SunMoon_Light_Check()`.)
- **Sky/vrbox tint** (`env.skyColor`/`env.hazeColor` are already pushed):
  investigate whether the vrbox raster draws reach Remix's sky probe with
  TEV tint applied (TFACTOR path in `dx9_tev.cpp`); if yes, nothing to do;
  if no, drive a low-intensity dome light or sky brightness from the
  pushed colours.
- **Dungeon point lights** (`dungeonlight_col[6]` + `DUNGEON_LIGHT`
  positions) via the light API — replaces hand-placed approximations in
  interiors, colours tracking palettes automatically.

---

## Part V — Implementation plan

### Phase 0 — plumbing (dusklight)
1. Vendor `remix_c.h` from the fork into `include/remix/` (pin 0.6.4;
   comment the exact-minor rule).
2. `src/dusk/remix_bridge.{cpp,hpp}`: init/availability, diff-cached
   `setVar`, `tick()` wired into the main loop after kankyo draw; config
   var `game.remixKankyoBridge`; log lines on init/degrade.
3. ImGui "Remix Bridge" debug window (values, per-key override, push
   counter). Files added to `files.cmake`.
   - *Acceptance*: under Remix, heartbeat visible in Remix's dev menu
     (`rtx.dusklight.env.enable = True`); on stock D3D9/other backends the
     module logs "not under Remix" and goes dormant; zero calls when values
     are static.

### Phase 1 — bloom + mono (fork + bridge)
1. Fork: add `dusklightMonoColor/MonoAmount/MonoLumaMode/BaseWeight`
   options; new grade stage in `DxvkBloom::dispatch` (IV.3, mono +
   baseWeight only at this phase); RtxOptions.md rows; UI rows under
   Post-Processing → Bloom.
2. Fork: add `rtx.dusklight.env.*` option group (NoSave) + verify NoSave
   exclusion in `RtxOptionLayer::save()`; fix if needed.
3. Bridge: push the bloom block (IV.2 table); skip `bloom_c::draw()` when
   bridge active; update `docs/dx9-fixed-function.md` (drop the manual
   bloom table — it's now automatic).
   - *Acceptance*: walking Ordon dawn→noon→dusk visibly re-tunes Remix
     bloom continuously; entering twilight snaps the golden bloom + 37.5 %
     desat + base dim without touching the UI.

### Phase 2 — fog (aurora)
1. Implement GX→D3D9 fog in `dx9_draw.cpp` per the mapping doc (LIN first;
   EXP/EXP2 if any stage uses them — audit says PERSP_LIN only).
2. Verify capture in Remix dev menu (fog states panel); calibrate
   `rtx.fogColorScale` starting point; ship rtx.conf template values.
3. MinGW syntax harness both configs; aurora submodule bump dance per
   CLAUDE.md (aurora dev → dusklight dev, pin SHA).
   - *Acceptance*: Faron morning haze and Lanayru evening fog reappear with
     palette-correct colour, fading over distance pre-tonemap; toggling
     `rtx.enableFog` kills it.

### Phase 3 — ambient grade (fork + bridge)
1. Fork: `rtx.dusklight.grade.*` response options + tint math in the grade
   stage (chromaOnly normalization, strength, maxDarkening).
2. Bridge: push `actorAmbient`/`bgAmbient`.
3. Tune defaults on the four canonical test scenes: Ordon noon (should be
   ≈ neutral), Ordon dusk (warm shift), Faron rain (cool desat), any
   twilight zone (full look together with Phase 1).
   - *Acceptance*: time-of-day/weather grade the path-traced frame; bridge
     off ⇒ image identical to pre-phase baseline.

### Phase 4 — sun/moon light + sky (aurora + fork + bridge)
As designed in IV.7: `dxvk_RegisterD3D9Device` hook in aurora, distant
light lifecycle in the bridge, vrbox tint investigation, dungeon lights
stretch goal.

### Phase 5 — polish
rtx.conf template ships tuned defaults; documentation pass
(`docs/dx9-fixed-function.md` + this doc's status log); XFog evaluation;
HDR threshold calibration table per area if needed.

### Test/verification strategy
- Owner tests via the GitHub Actions "Build Windows (MSVC x86_64)"
  artifact (game/aurora) and the fork's Actions build (Remix DLL) — keep
  both CI green per phase; land in `Fixed-Function-dev` at checkpoints.
- Every phase has a hard off-switch (`game.remixKankyoBridge`,
  `rtx.dusklight.grade.enable`, `rtx.enableFog`) so regressions bisect in
  minutes.
- Debug affordances: game-side bridge window (Phase 0), Remix dev menu
  option inspection, and `rtx.dusklight.env.*` visible in RtxOptions UI.

### Risks / open questions
1. `remixapi_InitializeLibrary` export presence in *our* built DLL —
   sanity-check exports once (it rides `__declspec(dllexport)`, not the
   .def file).
2. NoSave behaviour of `RtxOptionLayer::save()` — verify before Phase 1.3.
3. First-fog-wins capture robustness — plan B documented (IV.4).
4. HDR calibration of threshold/tint responses is taste work — the knobs
   exist precisely so it can be done live in the Remix UI.
5. Quality-preset layer outranks the User layer for `UserSetting`-flagged
   options — none of our target options carry that flag today; keep it
   that way for `rtx.dusklight.*`.
