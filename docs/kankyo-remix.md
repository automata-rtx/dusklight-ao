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

Game-side gating: when the bridge is active, force `game.bloomMode = Off`
behaviour for the EFB bloom (skip `bloom_c::draw()` under Remix) so the
raster filter quads stop overdrawing the path-traced image — this replaces
the "please set Bloom to Off" advice in `docs/dx9-fixed-function.md`.

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
> 2. **Volumetrics is not a drop-in "user preference".** With stock options it
>    cannot express the game's fog at all: the froxel grid is 20 m, fog remap
>    is off by default, and the remap's endpoints are unclamped and calibrated
>    for a different game.
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
> "Bloom fidelity: four errors in the port" below. Kept for the history of
> how the port got here.

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
- **Sky/vrbox tint** (note: `env.skyColor`/`env.hazeColor` were designed
  here but Phase 1 shipped only the bloom set, so nothing pushes them
  today; adding them is a two-line change once there is a consumer):
  investigate whether the vrbox raster draws reach Remix's sky probe with
  TEV tint applied (TFACTOR path in `dx9_tev.cpp`); if yes, nothing to do;
  if no, drive a low-intensity dome light or sky brightness from the
  pushed colours.
- **Local point lights** (implemented — see the status log). The design
  originally scoped this to the dungeon lights; the right list turned out
  to be `g_env_light.pointlight[100]`, which the dungeon lights register
  into along with every torch, brazier, lantern, campfire, Midna glow and
  bomb flash in the game (`dKy_plight_set`).

---

## Part V — Implementation plan

### Verification state (read this first)

As of 2026-07-27. CI baselines: dusklight/aurora green on all 8 targets
(Windows MSVC x86_64 + arm64, macOS x3, Linux x2, Android); the Remix fork
green on its 3 Windows configs. Aurora is unchanged since `a7b47ac` and the
submodule pin still points there.

**One standing rule:** the game and the Remix DLL are a single protocol and
must be built from the same point. Both directions of skew have already cost
an evening — see "Two protocol bugs" below. The Dusklight tab reports which
is which.

#### Confirmed working in-game

- **The bridge connects.** Owner log: `RTX Remix detected; kankyo bridge
  active (remixapi 0.6.4)` and `registered D3D9 device with the Remix API`,
  with no missing-export warning, which also proves the `getRtxOptionValue`
  export mechanism works.
- **Dusklight bloom — "massively improved"** after the four fidelity fixes
  and the 100× composite fix. This is the one part of the look that is now
  confirmed close to the real thing.
- The kankyo feed is live and its colour tracks time of day.
- GX→D3D9 fog reaches Remix. Faithful mode consistent; volumetric mode
  over-reactive to kankyo's near/far.
- Vanilla Remix post FX work once their strengths are raised well above the
  near-invisible defaults. No Remix bug.
- The sun/moon distant light runs and produces "interesting results". No
  report of inverted shadows, so the handedness is *probably* right — Flip
  Direction remains in the tab if that turns out wrong.

#### Open issues

1. **Crash entering some levels.** `EXCEPTION_ACCESS_VIOLATION` reading
   `0x10`, entirely inside `d3d9.dll` on a Remix-owned worker thread (the
   outermost frames are `BaseThreadInitThunk` / `RtlUserThreadStart`), during
   a cutscene transition right after a camera cut. Local lights were off in
   that run. **Bisect not yet run**: `rtx.dusklight.game.bridgeEnable = False`
   settles whether any of this is ours; if it still crashes, the
   NRC-on-camera-cut path is next (`rtx.neuralRadianceCache.enable = False`).
   NaN guards were added to both light paths regardless — plausible as the
   fix, not demonstrated.
2. **Shadow coverage wanders as the camera moves — at night only.**
   Eliminated: the light direction (locking it changes nothing), NRC
   (persists under ReSTIR), and brightness (persists with moon intensity
   raised). Current hypothesis, with code evidence and untested: the sun,
   moon and star billboards are drawn at a fixed offset from the camera eye
   (`dKyr_drawStar`: `moon_pos = camera->view.lookat.eye + envlight->moon_pos`),
   so anything Remix captures from them as world geometry is an occluder that
   travels with the player — and stars and the moon are the only sky
   billboards drawn at night, which is exactly the asymmetry.
   `rtx.dusklight.game.hideSkyBillboards` tests it in one click; tagging those
   textures as Sky is the real fix.

#### Built and CI-green but NEVER RUN

- **`game.celestialNoonElevation`** (2026-07-27) — the sun/moon elevation
  cap lift. Defaults to vanilla (59.036 reproduces 48000/80000 to six
  decimals), so it is inert until moved. Verified numerically only: the
  default ratio, the peak elevation at several settings, and that sunrise
  and sunset move ≤0.2°. Unproven in game: whether an overhead noon actually
  reads better, and whether anything downstream dislikes a near-vertical
  light. Standalone write-up in `docs/sun-elevation.md`. **Start at 80–85,
  not 90** — at exactly 90 the azimuth flips instantaneously at noon.
- **`game.disableFrustumCulling`** — untested, off by default. Also unknown
  what it costs in frame time.
- **`game.remixHideSkyBillboards`** — untested, off by default (see open
  issue 2).
- **The Dusklight tab driving the game.** The export resolves, but no value
  set in the tab has been confirmed to change game behaviour yet. First
  thing to check: toggle something obvious like the sun light off.
- **Local point lights** — off by default. Unproven: whether the intensity
  from Remix's own conversion reads right at TP's scale, whether the 4-unit
  radius puts emitters inside wall sconces, and the churn cost in a busy
  room. The create/destroy lifecycle is exercised by a stub harness, so the
  bookkeeping is not the risk; the look is. If torches read weak, the
  derived alternative is intensity ≈ 19 (see the reach note below).
- **The ambient grade** — off by default. Unproven that the ambients arrive
  sane (watch the tab's readout and the grade's "Resolved tint" line) and
  whether 0.65 strength reads as mood or as a cast.
- **Mono overlay and composite base weight** — only engage in
  twilight/wolf-senses palettes, never reached.
- **Sky tagging — still not done.** This remains the single highest-value
  outstanding item: it is the missing fill light, and it is also the proper
  fix for open issue 2.

**"The sun seems tied to Link" — investigated 2026-07-26, no tie found, and
since narrowed.** Four things were checked and none can carry a dependency on
the player:

1. `setSunpos` (`d_kankyo.cpp:1666`) has **no rotation term at all** — the
   orbit is a function of `daytime` and an eye translation that cancels in
   the direction. (An earlier numerical check varied camera *position* over
   720 times and would not have caught an orientation dependency, so this was
   re-read rather than re-run.)
2. `dKy_SunMoon_Light_Check()` (`d_kankyo.cpp:10974`) keys on stage name and
   darkworld state only.
3. Remix's `direction` convention is the one we push:
   `distant_light.slangh:78` samples at `position - direction·100000`, so it
   is the direction light *travels*, and our `-toBody` is correct.
4. Aurora hands Remix true world space (`world = modelView · viewInv`,
   `dx9_draw.cpp:354/361/505`).

Also ruled out: the clock (~0.6°/s of sun motion — visible over a minute, not
over a lap) and baked lighting (`D3DRS_LIGHTING = FALSE`; aurora never
evaluates the GX light model, so vanilla's Link-following light reaches
neither the vertex colours nor the albedo). The day case is now believed
correct; what remains is night-only and is open issue 2.

**First-run checklist for the sun/moon light:** Remix's Dusklight tab should
report the device registered and `Drawing: SUN`. Walk past a lantern — the sun direction
must not move (that is the whole point of deriving it from the orbit
rather than the game's shadow-light selection). Watch a dawn (daytime
~67.5–75) for the moon→sun crossfade. If shadows fall from the wrong side,
tick Flip Direction; if that fixes it, the sign belongs in the code.

### Status log

- **2026-07-27 — bloom fidelity pass (confirmed good in-game).** Four errors
  in the port plus a fifth in the composite; owner reports the result
  "massively improved". Details in "Bloom fidelity" below. The composite one
  is worth repeating here because it explains everything that came before it:
  the Dusklight path inherited Remix's fixed `0.01` attenuation, which is
  calibrated for Remix's own broadly-gathering pyramid. Ours was 100× too
  faint, so every brightness knob had to be pinned to compensate and it still
  read as a weak wash — which is why turning it *off* looked closer to the
  original.
- **2026-07-27 — sun/moon elevation cap lifted** (`game.celestialNoonElevation`,
  default = vanilla). **Untested.** Write-up in `docs/sun-elevation.md`.
- **2026-07-27 — geometry switches added** (`game.disableFrustumCulling`,
  `game.remixHideSkyBillboards`). Both **untested**, both off by default. The
  first is for occlusion the path tracer needs and the game throws away; the
  second is the one-click test for open issue 2.
- **2026-07-26/27 — controls moved into Remix's Dusklight tab.** The game's
  ImGui is never drawn in D3D9 mode, so every setting built for this work was
  behind a window that cannot appear. See "Where the controls live" below.

- **Phase 0 + Phase 1: implemented** (game: `src/dusk/remix_bridge.{cpp,hpp}`,
  vendored `include/remix/remix_c.h` @ 0.6.4, `game.remixKankyoBridge`
  config var, Remix Bridge debug window, `bloom_c::draw()` skipped while the
  bridge is active; fork: `rtx.dusklight.env.*` group (NoSave — verified the
  save path filters NoSave at `RtxOptionImpl::writeOption`, so game-fed
  values never reach user.conf), mono prepass shader, composite base
  weight, `rtx.bloom.dusklightFollowGame` + `dusklightThresholdScale` +
  manual mono/base-weight knobs).
- **Phase 2: implemented** (aurora checkpoint 3.17: `apply_fog_state()` in
  `lib/dx9/dx9_draw.cpp` forwards GX fog to `D3DRS_FOG*` per draw; ortho/UI
  draws stay fog-off). **Fog fidelity evaluation — read before testing:**
  - Remix has *two* consumers for captured D3D9 fog, and with Remix's
    **default settings neither fires**: composite's depth fog early-outs
    whenever volumetrics are enabled (`rtx.volumetrics.enable` defaults to
    True), and the volumetric fog remap defaults to off. Fog silently does
    nothing until a mode is chosen:
  - **Faithful mode** — `rtx.volumetrics.enable = False` (composite depth
    fog, on via `rtx.enableFog` by default). Reproduces the exact
    `D3DFOG_LINEAR` ramp `(end−d)/(end−start)` — identical maths to
    `GX_FOG_PERSP_LIN` — and it fogs by **radial distance**, which matches
    vanilla better than plain view-Z because TP keeps `GXSetFogRangeAdj`
    (the radial correction) enabled. Two knobs: `rtx.fogColorScale`
    (default 0.25; the captured gamma colour is used as linear pre-tonemap
    radiance, so with auto exposure off start near 1.0 and calibrate once)
    and `rtx.maxFogDistance` (default 65504 — raise it; TP fog ends exceed
    it and geometry past the cutoff gets no fog at all).
  - **Volumetric mode** — keep volumetrics on and set
    `rtx.volumetrics.enableFogRemap = True` +
    `rtx.volumetrics.enableFogColorRemap = True`. Kankyo's fog colour
    becomes the participating medium's transmittance colour (light shafts,
    real scattering); the distance mapping is *not* the linear ramp
    (fog end remapped through `rtx.volumetrics.fogRemap*Meters`, which
    interact with `rtx.sceneScale`). Prettier, physically consistent,
    less literal.
  - Expected weak points to watch on first test: fog colour shifting with
    exposure/tonemap (calibrate `fogColorScale`, or disable auto
    exposure), the first-fog-wins capture picking a stray draw (watch the
    Remix dev menu fog panel), and underwater palettes (very dense fog)
    tripping `rtx.volumetrics.waterFogDensityThreshold` and flipping modes.
- **Phase 3: implemented, untested** (fork: `DxvkDusklightGrade`, its own
  `RtxPass` dispatched immediately before the bloom rather than folded into
  it, so `rtx.bloom.enable = False` does not silently take the grade with
  it; `rtx.dusklight.grade.*` response options; a CPU-resolved constant
  tint, so the shader is one multiply and the pass skips itself when the
  tint is neutral. Bridge: `actorAmbient`/`bgAmbient` pushed from
  `g_env_light`). Two design points that changed from the draft during
  implementation are written up in IV.3: the grade runs *before* the mono
  overlay (that is the order the GC had — ambient at shading time, mono in
  the post pass), and the response rails act on the tint's level before
  they act per channel (a per-channel-only floor flattens a night ambient
  to grey). Defaults ship with `enable = False`.
- **Phase 4 (partial): sun/moon distant light implemented.** The bridge now
  drives one Remix distant light through the light API
  (`CreateLight`/`DrawLightInstance` each frame; device registered via a new
  `aurora_dx9_get_device()` accessor, re-registered after resize-recreation).
  It is a true `remixapi_LightInfoDistantEXT` (Remix's dedicated sun/moon
  light type, mapping to `RtDistantLight`) — that struct carries only a
  direction, angular diameter and radiance, with no position, so the light
  is infinitely far by construction rather than "very far away".

  The direction is derived **analytically from time of day**, not from any
  world position. `setSunpos` places the body on an ellipse around the
  camera eye (`offset = (sin a · 80000, −cos a · 80000, −cos a · 48000)`,
  `sun_pos = eye + offset`); the eye cancels in the offset and the radii
  cancel under normalization, leaving `normalize(sin a, −cos a, −0.6 cos a)`
  — verified identical to differencing `sun_pos` against the camera to
  4.4e-16 across the full day at several camera positions. So no arc, no
  position and no camera enter the code path, and it keeps working in the
  stages where `setSunpos` declines to update `sun_pos`.

  Sun while 67.5 < daytime < 292.5, moon otherwise (same orbit, half a day
  out of phase), crossfaded over ±7.5 daytime units — the window edges
  coincide with the body dipping below the horizon, so the fade completes
  as it sets. Deliberately **not** driven by the game's shadow-light
  selection, which snaps to nearby lanterns. Gated on `dKy_SunMoon_Light_Check()`
  (outdoor stages only; false in twilight/interiors). Tuning:
  `game.remixSunMoonLight` (on), `game.remixSunIntensity` (5),
  `game.remixMoonIntensity` (0.3), `game.remixCelestialAngle` (2°), all
  live-editable in Remix's Dusklight tab, plus a debug direction-flip
  checkbox in case game→Remix handedness needs the sign. Sun tint is
  vanilla's constant actor sun diffuse (126,110,89 normalized); moon is a
  cool counterpart. With `rtx.fallbackLightMode = 1` (NoLightsPresent) the
  fallback light yields automatically once this light exists.
- **Phase 4 (local lights): implemented, untested.** The bridge mirrors
  `g_env_light.pointlight[0..99]` — everything registered through
  `dKy_plight_set`: torches, braziers, lanterns, campfires, Midna, bomb
  flashes, and the dungeon lights — into Remix sphere lights, created and
  destroyed as their actors come and go.

  **Why this matters more than it sounds.** Aurora deliberately does not
  forward GX lights to D3D9 (unsupported-effects #16: "Remix relights
  everything"), so Remix sees *no* game light at all. Outdoors the
  sun/moon distant light now covers that. Indoors and at night nothing
  did: the scene fell through to Remix's fallback light. These are the
  lights those scenes were lit by.

  **Intensity is not a tuning constant.** It reuses Remix's own
  legacy-light conversion (`LightUtils::calculateIntensity`): work out how
  far the original light was meant to reach, then solve for the radiance a
  sphere light of fixed radius needs to still be perceptible there —
  `radiance = reach² · 0.01 / (π · radius²)`. The game hands us the reach
  directly, because `LIGHT_INFLUENCE::mPow` *is* that distance
  (`dKy_light_influence_id` treats "closer than mPow" as "inside this
  light"). So these lights land in the same intensity range as the lights
  of any other Remix title rather than in a range we invented. A torch
  (`mPow` 500, colour AF5D00) resolves to radiance ≈ 49.7, 26.4, 0 at the
  default 4-unit radius — which is also Remix's own default radius for
  converted point lights.

  **The one real judgement call, and it is worth ~19×.** `mPow` is not
  where the light ends, it is where it reaches 1/11 of peak. The game loads
  these as `dKy_GXInitLightDistAttn(info, mPow·0.001, 0.99999, GX_DA_STEEP)`
  → `k0 = 1, k1 = 0, k2 = (1−b)/(d²b)` → `attenuation(D) = 1/(1 + 10D²/mPow²)`.
  Applying Remix's own end threshold (1/255 of the light's brightness) to
  that curve instead gives `reach = mPow·√((maxColorByte − 1)/10)`, which is
  4.3× further for a torch and therefore ~19× the radiance.

  That second reading is arguably *more* faithful, and it is the one Remix's
  philosophy points at: it deliberately ignores a legacy light's `Range` in
  favour of its attenuation curve, because `Range` was usually an
  optimization rather than the light's real extent — and `mPow` is exactly
  that kind of optimization. It is not the default for two reasons: the game
  never applied a point light beyond its influence radius anyway (each
  tevstr gets *one* light, chosen by proximity, so the long tail was rarely
  realized), and a scene that comes up too dim is far easier to diagnose
  than one that comes up blown out. **If the lights read as weak, set
  `game.remixLocalLightIntensity` to about 19** — that is a derived number,
  not a guess, and the slider reaches it.

  Identity is the `LIGHT_INFLUENCE`'s address, mixed into a 64-bit hash: it
  lives inside its actor, so it holds still exactly as long as the light
  does. Re-creates are epsilon-gated on position and radiance (0.5 world
  units, ~6mm at TP's scale) so a carried torch does not cross the API lock
  every frame. Lights whose actor is gone are destroyed, which is what
  keeps Remix's external-light map from growing all session as rooms load.
  On resize the handles are dropped without destroying — they belonged to
  the device that went away with them.

  Settings: `game.remixLocalLights` (**off** by default — third unverified
  system, same reasoning as the grade), `game.remixLocalLightIntensity`
  (1.0), `game.remixLocalLightRadius` (4.0), all live in Remix's Dusklight
  tab with drawn/tracked counters.

  Not done: `mFluctuation` (the flicker amount; every torch sets 1.0, bombs
  100) is ignored for now — applying it would mean a re-create every frame
  for every flickering light. Worth revisiting once the base look is
  calibrated.
- **Sky (Phase 4 remainder): manual tagging is the right mechanism, and it
  now fixes two things.** Besides being the missing fill light, it is the
  proper fix for the night-only wandering shadows: the sun, moon and star
  billboards sit at a fixed offset from the camera eye, so any of them Remix
  captures as world geometry is an occluder that travels with the player.
  Tagging them as Sky moves them into the sky probe, where they belong. The
  vrbox is drawn by the game with the *main* camera, so
  `rtx.skyAutoDetect` (which keys on a separate sky camera) is unlikely to
  catch it; Remix's texture tagging is. One-time setup in the Remix dev
  menu (texture categories → Sky): tag the vrbox sky dome, both cloud
  layers (kumo), the horizon haze (kasumi) and sun/moon billboard
  textures. Once tagged, the sky raster draws land in Remix's sky probe
  *with their TEV tints* — i.e. kankyo's per-palette sky colours reach
  reflections and GI automatically; scale with `rtx.skyBrightness`.
  Programmatic tagging was evaluated and rejected for now: it would
  require reproducing Remix's exact texture-content hash game-side.
- **Owner test feedback (first bloom/fog session), to address:**
  - Dusklight bloom renders and tracks time of day, but doesn't yet look
    like the game's — calibration pass pending (threshold scale vs. the
    scene's HDR range, gain distribution, and the burnIntensity=5 +
    blurRatio=255 test values need re-baselining once the sun light lands).
  - Volumetric fog mode reacts more strongly to kankyo's fog near/far than
    expected; faithful mode is consistent.
    `rtx.volumetrics.enableFogMaxDistanceRemap = False` (owner already set
    it) is the intended lever — it pins the medium's density and leaves
    only the colour game-driven. Revisit defaults after the light exists.
- **Owner tuning note:** auto exposure may simply be disabled for reference
  (`rtx.autoExposure.enabled = False`) instead of clamping it — with AE off
  the pre-tonemap range is fixed, which makes `dusklightThresholdScale`
  calibration straightforward and makes the base-weight dimming read
  exactly as authored. The chroma-only default for the Phase 3 grade
  matters less in that configuration but remains the right default for
  AE-on setups.

### Where the controls live (and why they are not in the game)

The game's ImGui is **never drawn in D3D9 mode** — its overlay renders
through WebGPU, which is not initialized here
(`docs/dx9-fixed-function.md`, limitations). So the "Remix Bridge" debug
window built in Phase 0 is invisible in the one mode the whole feature
exists for, and every instruction to open it was unfollowable. That was a
real design error, caught by the owner rather than by us.

The controls therefore live in **Remix's own ImGui overlay**, in a
`Dusklight` tab, as ordinary `rtx.dusklight.game.*` options. The game reads
them back every frame through a `getRtxOptionValue` export on the Remix DLL
— the Remix API only *writes* config variables, and extending
`remixapi_Interface` with a getter would break its ABI (its size is
asserted), so this rides the same plain `__declspec(dllexport)` mechanism
the fork already uses for `writeMarkdownDocumentation`.

Direction of travel:

- **Remix → game**: the settings (`rtx.dusklight.game.*`), polled each frame.
  The game's own `game.remix*` config values remain as the fallback for a
  Remix build without the export, and for backends where the bridge is inert.
- **Game → Remix**: state (`rtx.dusklight.env.*`), pushed as before. Light
  status — azimuth, elevation, day/night, fade, device registration, local
  light counts — is pushed too, purely so the tab can display it.

The game-side Remix Bridge window is kept: it still works on the WebGPU
backends, where it is the only way to edit the fallback values.

### Two protocol bugs found on first contact (2026-07-26)

Both surfaced the moment the owner ran the new Remix build against an older
game build, and both were mine.

1. **The diff cache assumed exclusive ownership.** `push()` only calls
   `SetConfigVariable` when a value changes, which is right for cost and
   wrong for correctness: `rtx.dusklight.env.*` are **NoSave**, so anything
   that rebuilds Remix's user layer — saving settings from its UI, a config
   reload — drops them back to their defaults. The cache then never pushes
   them again, and Remix reports the bridge as disconnected *forever* while
   the game is convinced it is connected. Fixed by verifying instead of
   assuming: the bridge reads its own heartbeat back each frame and clears
   the cache if it is missing (one getter call, recovers next frame), with a
   blind full re-push every 120 frames as the fallback for a Remix build
   without the getter.
2. **Build skew was indistinguishable from breakage.** The tab's only state
   was "is the game reporting anything", which is false in every failure
   mode. A game that connects but predates `rtx.dusklight.game.*` looks
   identical to one that never connected — except its controls silently do
   nothing. The game now stamps `rtx.dusklight.env.protocol`, and the tab
   distinguishes connected-and-current, connected-but-too-old, and absent.

Standing rule this leaves behind: **the game and the Remix DLL are one
protocol and have to be updated together.** The tab says so when they are
not.

### Crash on entering some levels (2026-07-26) — evidence, not yet a cause

Owner logs (`dusklight20260726214829`, `remixdxvk`). What the logs establish:

- **The bridge is connected**: `RTX Remix detected; kankyo bridge active
  (remixapi 0.6.4)` and `registered D3D9 device with the Remix API`. The
  `getRtxOptionValue` export resolved — there is no warning about it, which
  also proves the export mechanism works.
- **Build skew, reversed**: game `8b89e4f` against Remix
  `remix-main+4779899c`. `SetConfigVariable(rtx.dusklight.env.protocol)
  failed (1)` — error 1 is `GENERAL_FAILURE`, which
  `remixapi_SetConfigVariable` returns when the option does not exist, and
  `protocol` landed one Remix commit later. Harmless in itself.
- **Local lights were off** (`rtx.dusklight.game.localLights` defaults false
  and is not in the owner's rtx.conf), so that subsystem is not implicated.
  The sun/moon distant light *was* running.
- **The crash is entirely inside `d3d9.dll` on a Remix-owned worker thread**:
  all frames are in `d3d9.dll` and the outermost two are KERNEL32
  `BaseThreadInitThunk` / ntdll `RtlUserThreadStart`, i.e. a thread whose
  entry point is in Remix, not the game. `EXCEPTION_ACCESS_VIOLATION`
  reading address `0x10` — a null pointer plus a small member offset. No
  game frames at all.
- **Context**: a cutscene transition (`ZEV event [BSPTRANS]`,
  `entering_event=true`, Midna's `s_md` models loading), immediately after a
  Remix camera cut, which re-initializes the Neural Radiance Cache
  (`NRC SDK: Loading the default network config data`) — on a worker thread.

Ruled out along the way: aurora's view inverse is guarded by a determinant
check and would have logged `camera view matrix not invertible`, which it
did not. Remix's own `Attempted invert a non-invertible matrix` fired 19
seconds earlier and is not adjacent to the crash.

Hardened regardless, because both were real defects:

- `getRtxOptionValue` took no lock while Remix resolves options on its own
  thread at frame end. Now takes the same update mutex those writes do.
- The bridge fed positions and radiances to Remix without checking them for
  NaN. Remix validates radius and radiance for sign and range but **not**
  for NaN, and a NaN reaching its acceleration structures takes the renderer
  down on a worker thread with a backtrace that says nothing about where it
  came from — which is the shape of crash we are looking at. Both light
  paths now skip a light whose values are not finite.

*Next step is a bisect, not more analysis*: `rtx.dusklight.game.bridgeEnable
= False` turns off every push and both lights. If it still crashes, nothing
of ours is involved and the NRC-on-camera-cut path is the next suspect
(`rtx.neuralRadianceCache.enable = False`).

### Bloom fidelity: four errors in the port (2026-07-26)

The owner compared against vanilla Dusklight and reported the bloom simply
does not look like it. Re-derived the effect from the TEV setup in
`bloom_c::draw2()` (`m_Do_graphic.cpp:1456`) rather than from the earlier
reading, and found four things wrong, one of them fundamental.

**1. Wrong colour space — the fundamental one.** The effect was authored
against the EFB: an 8-bit framebuffer holding *finished display colours*.
Every part of it is defined against that. The threshold is a fraction of
display white. The intermediate buffers clip at white, and that clipping is
what gives bright cores their washed-out look. The screen blend asks "how
close to white is this pixel already", and the composite's base weight is a
blend alpha against a 0..1 image. We were running the whole thing on
open-ended **linear pre-tonemap radiance**, where none of those four mean
what they meant — and where blurring concentrates halos far more tightly,
because blurring linear radiance weights bright pixels enormously more than
blurring display values does. Fixed by moving the Dusklight pyramid to run
**after tone mapping**, in gamma space (`rtx.bloom.dusklightDisplaySpace`,
default on). This also makes the whole effect exposure-independent, which
is why `dusklightThresholdScale` existed at all.

**2. The threshold was the wrong operation entirely.** Decoding the three
TEV stages, with swap tables `R,R,R,G` and `B,B,B,A` mixed by `HALF`:

```
key    = 0.25*R + 0.25*G + 0.5*B
source = colour * saturate(key - mPoint)
```

It is a **luminance-keyed mask multiplied by the original colour**, not a
per-channel subtraction. The port did the latter, which is close to the
opposite in character: it shifts every bloomed highlight towards its
dominant channel, where the original preserves hue exactly. It also blooms
things the original refuses to — saturated red at full intensity has a key
of 0.25 and never clears the default 0.5 threshold, but the port bloomed it
at half strength. Note the weights: **blue counts double**, which is a real
and distinctive part of the look.

**3. `rtx.bloom.steps` should be 6, not 5.** The game runs five blur passes
over six levels (`divStart` 2 → `divNum` 6). Our default of 5 gives four,
which narrows the halo a level *and* changes the per-pass gain, since the
total is distributed as its N-th root.

**4. The upsample exponent was off by one.** The original is
`falloff^(1/(i - divStart + 1))` with `divStart = 2`, i.e. `1/(i-1)`; we
used `1/i`, leaving every level slightly too faint.

Deliberately kept: the 13-tap downsample on the threshold step, instead of
the original's point sample. A single bright pixel with a box filter makes
the bloom crawl frame to frame, and that trade is worth more than the
exactness.

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

### Phase 3 — ambient grade (fork + bridge)  — implemented, untested

Steps 1 and 2 are done; step 3 needs the game running.

1. ✅ Fork: `DxvkDusklightGrade` (`rtx_render/rtx_dusklight_grade.{h,cpp}`,
   `shaders/rtx/pass/dusklight/dusklight_grade.{h,comp.slang}`), dispatched
   from `RtxContext` immediately before the bloom. Response options
   `rtx.dusklight.grade.{enable,strength,chromaOnly,actorAmbientWeight,
   maxDarkening,maxBrightening}`, UI under Rendering → Post-Processing →
   Dusklight Ambient Grade (which also prints the resolved tint live).
2. ✅ Bridge: `rtx.dusklight.env.actorAmbient` / `bgAmbient` pushed from
   `g_env_light.actor_amb_col` / `bg_amb_col[0]` — the fully blended
   per-frame values, after the four-way palette blend, event add-colours
   and global ratios. BG layer 0 is the main room layer, the one the game
   itself reuses when it needs "the" background ambient (`d_a_mirror`).
3. ⬜ Tune defaults on the four canonical test scenes: Ordon noon (should
   be ≈ neutral), Ordon dusk (warm shift), Faron rain (cool desat), any
   twilight zone (full look together with Phase 1).
   - *Acceptance*: time-of-day/weather grade the path-traced frame; bridge
     off ⇒ image identical to pre-phase baseline.

**Shipped off by default.** `rtx.dusklight.grade.enable` defaults to
false. Phase 3 changes scene *tint* and the Phase 4 sun/moon light changes
scene *lighting*; both are unverified at runtime, and turning them on one
at a time is the difference between a five-minute bisect and an afternoon.

**What the defaults do**, from a simulation of `resolveGrade()` (the
ambients are illustrative, not measured from stage data — TP's palettes
live in `.dzs` files, not in the repo):

| ambient (actor / bg) | `chromaOnly` on (default) | `chromaOnly` off |
| :-- | :-- | :-- |
| neutral grey 180,180,180 | 1.000 1.000 1.000 *(pass skipped)* | 0.809 0.809 0.809 |
| noon, faint cool | 0.982 1.001 1.039 | 0.841 0.855 0.885 |
| dusk, warm | 1.228 0.954 0.779 | 0.841 0.688 0.590 |
| night, cool dark | 0.876 1.003 1.332 | 0.578 0.579 0.694 |
| rain, desaturated cool | 0.943 1.009 1.076 | 0.611 0.641 0.670 |
| twilight, gold | 1.127 1.007 0.578 | 0.845 0.769 0.578 |
| pure red (a real debug state) | 1.650 0.578 0.578 | 1.420 0.578 0.578 |
| black ambient | 1.000 1.000 1.000 *(pass skipped)* | 0.578 0.578 0.578 |

Two properties to hold on to: a neutral ambient resolves to exactly white
and skips the dispatch (so noon costs nothing and changes nothing), and the
rails contain the pathological red case that would otherwise resolve to a
4.7× red multiplier.

### Phase 4 — sun/moon light + sky (aurora + fork + bridge)
As designed in IV.7: `dxvk_RegisterD3D9Device` hook in aurora, distant
light lifecycle in the bridge, vrbox tint investigation, dungeon lights
stretch goal.

### Phase 5 — polish

- ✅ **XFog evaluation — nothing to do, and forwarding it would be wrong.**
  GX fog is computed from projected depth (planar); `GXSetFogRangeAdj`
  adds a per-column correction table whose whole purpose is to make that
  planar depth behave like *radial* distance, so fog does not thin out at
  the screen edges. Remix's composite fog already measures radial
  distance — `viewDistance = length(viewPosition)`
  (`composite.comp.slang:700`), fed straight into the `D3DFOG_LINEAR`
  ramp. So the correction is already applied by construction; forwarding
  the table would double-correct.

  TP does keep it on: `mFogAdjEnable = true` at kankyo init
  (`d_kankyo.cpp:1257`) and `GxXFog_set()` runs immediately after every
  scene `GFSetFog(GX_FOG_PERSP_LIN, …)` (`d_kankyo.cpp:9459`). Every
  `GXSetFogRangeAdj(GX_DISABLE, …)` in the game is on a 2D/UI/menu/movie
  path where fog is off anyway. Aurora records the same conclusion at
  `lib/dx9/dx9_draw.cpp:178`.
- ✅ **rtx.conf template + documentation pass** — `dx9-fixed-function.md`
  carries both fog modes, the bloom table, the ambient grade table and the
  local light notes; this doc's status log and verification section are
  current.
- ✅ **Re-baseline the owner's bloom values** — superseded by the fidelity
  pass. With the composite fix in, `burnIntensity` belongs at **1.0** (it is
  no longer attenuated 100×), `steps` at **6**, and threshold/blur/ratio come
  from the game feed. `dusklightThresholdScale` should stay at 1.0 now that
  the pass runs in display space.
- ⬜ **HDR threshold calibration table per area** — largely obviated by the
  move to display space, since the threshold now has a fixed meaning. Revisit
  only if areas still disagree.
- ⬜ **Sky tagging** — the outstanding item that matters most. It is the
  missing fill light and the proper fix for the night occluder issue.

### CI coverage note
The fork's workflow only built `main` and `release/**`, so a `claude/**`
branch got no build until its PR opened. `claude/**` is now in the push
triggers, which is what gives the Phase 3 grade a compile check without
opening a pull request for it.

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
