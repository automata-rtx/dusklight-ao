# Kankyo → RTX Remix: environment colour driving

How Twilight Princess's environment system (`d_kankyo`; **kankyo** = 環境,
*environment*) feeds our dxvk-remix fork, so time of day, weather, twilight,
wolf senses and per-area palettes shape the path-traced image the way they
shaped the original TEV pipeline.

> **The game's identifiers are romanized Japanese.** `kankyo`, `kumo` (雲,
> cloud), `kasumi` (霞, horizon haze), `moya` (靄, mist), `sibuki` (飛沫, spray) —
> and `wether` is the game's own spelling of *weather*, not a typo. This file
> glosses each term on first use and then uses it bare.
> [`japanese-naming-remix.md`](japanese-naming-remix.md) is the reference, and explains why
> a grep for one of these can come back empty for a symbol that exists.

**This file is the stable design reference.** It changes when the design
changes, not when a test session happens. Everything volatile lives elsewhere.

---

## Start here

| I want to… | Read |
| :-- | :-- |
| Understand what is broken or untested right now | [`remix-open-issues.md`](remix-open-issues.md) — the project's **single** verification ledger |
| Run a test session | [`remix-test-playbook.md`](remix-test-playbook.md) — a prioritised sheet, not a catalogue |
| Understand the fog | [`kankyo-fog.md`](kankyo-fog.md) (game side) + `dxvk-remix/documentation/DusklightAtmosphere.md` (renderer) |
| Understand where fire and glow get their lights | [`effect-lights.md`](effect-lights.md) |
| Set the game up under Remix | [`dx9-fixed-function.md`](dx9-fixed-function.md) — setup and the annotated `rtx.conf` |
| Understand why a material's colour went wrong | `extern/aurora/docs/dx9/remix-material-interface.md` |
| Read a log | `extern/aurora/docs/dx9/material-report.md` |
| Work out what a game symbol's name *means* | [`japanese-naming-remix.md`](japanese-naming-remix.md) |
| Work out why something is **slow** | Read `dx9.draws` in the log first. Remix charges **per draw, not per pixel**, so a problem that does not respond to texture categorisation is usually draw count — `remix-open-issues.md` closed issue 13 is the worked example |
| Change the overlay or the option wire | `dxvk-remix/documentation/DusklightOverlay.md` |
| Rebase the fork onto upstream | `dxvk-remix/documentation/DusklightRebase.md` |

**`CLAUDE.md` at each repo root is the authority** on branch rules, the
translate-don't-tag principle, what the D3D9 renderer is for, and the owner's
broken-approval-prompt constraint. This file deliberately does not restate them.

**The game and the Remix DLL are one protocol** — currently **17**. Build both
from the same commit point; skew in either direction has cost an evening twice,
and the overlay's status strip reports which side is old.

---

## Part I — How kankyo actually works

### I.1 The data model

Stage files (.dzs chunks, via `dComIfGp_getStage*Info()`) give four tables per
stage (`include/d/d_stage.h`):

- **PAL — `stage_palette_info_class`**: one *palette*. Every ambient, the six
  dungeon light colours, the fog colour and range, the skybox colour set id,
  the cloud shadow density and — note this one — an index into the bloom table.
- **Pselect**: `palette_id[8]`, one palette per canonical *time slot*, plus
  `change_rate`, how fast a weather transition into this selection blends.
- **EnvR**: `pselect_id[65]`, one pselect per *colour pattern* ("colpat"):
  0 = clear, 1/2/… = weather or story variants, 8/9 = underwater, 10 = special.
  **Selected per room** — the envr index is the room the camera is in.
- **VrboxCol**: the skybox colour set — sky, `kumo` ×3 (雲, clouds) and
  `kasumi` inner/outer (霞, horizon haze). "vrbox" is the game's own word for
  the skybox dome, `d_a_vrbox.cpp`.

The field lists are in `include/d/d_stage.h` and are not restated here. Two
global tables in `src/d/d_kankyo_data.cpp` do need reading:

- **`l_time_attribute[11]`** (`dKyd_lightSchejule` — the game's spelling of
  *schedule*) maps time of day (0–360, 15°/hour) to a pair of the six canonical
  time lights and a blend window: 0 morning-0 (朝 *asa*), 1 morning-1,
  2 **midday** (昼 *hiru*, pure window 135–240), 3 evening-0 (夕 *yuu*),
  4 evening-1, 5 night (夜 *yoru*). Boss stages use a rotated variant. The game
  names all six itself in its debug time-fix menu and pins each to one exact
  time — 90 = 06:00, 165 = 11:00, 285 = 19:00, and so on.
- **`l_kydata_BloomInf_tbl[64]`** — the bloom mood table, one row selected by
  the palette. Entry 0 neutral; 1/2 Twilight; 3 wolf senses; 4–9 field times of
  day. Two members are worth knowing by name: `mOrigDensity`, the base-image
  weight during composite (I.5), and `mSaturateSubtractR/G/B/A`, the mono colour.

  **Every field meaning here is the authors' own, not a reconstruction** — the
  HIO panel puts one labelled slider on each member in declaration order, which
  is what closes the `// ?` the decomp left beside `mOrigDensity`: its slider is
  元濃さ, "the *original's* density". **One naming trap English hides:**
  `m_saturationPattern` is a **row id** (飽和パターン, which of the 64 entries is
  in force) and `mSaturateSubtract*` is a **desaturation amount inside a row**.
  飽和 (clipping) and 彩度 (colourfulness) are different Japanese words that both
  come out as "saturation".

### I.2 Selection: time × weather × room

`setLight_palno_get` resolves **four palettes** per frame:

```
psel_prev = envr[PrevCol].pselect_id[wether_pat0]   ("wether" = weather)
psel_next = envr[UseCol ].pselect_id[wether_pat1]
schedule slot for daytime → (start_slot, end_slot, color_ratio)
→ palettes: prev[start], prev[end], next[start], next[end]
```

`color_ratio` is the position inside the schedule window; `pat_ratio` is a 0→1
ramp between the *prev* and *next* selections, advanced at `change_rate`. On
reaching 1.0 the pair collapses, so **the steady state is
`wether_pat0 == wether_pat1` with `pat_ratio == 1.0`** and those two fields say
nothing new outside a transition.

Three things that have each caused a wrong reading:

- `dKy_change_colpat` and `dKy_custom_colset` write **`*Gather` staging fields,
  never the live ones**; `exeKankyo` copies staging onto the live fields once a
  frame. There is **one** blend, not two — an earlier wording called the gather
  fields a second independent blend, and `kankyo-fog.md` §2 records the cost.
- `dKy_change_colpat` sets the ratio to `0.0f` and leaves `wether_pat0` alone,
  so on the frame a weather change lands the palette is **entirely the outgoing
  pattern**. Anything reading the incoming index alone is at its most wrong
  precisely then; the bridge did exactly that until protocol 13.
- Underwater camera forces pselect 8/9.

### I.3 The blend: one formula for everything

`kankyo_color_ratio_set`:

```
a = lerp(prev_start, prev_end, color_ratio)   // time within prev pattern
b = lerp(next_start, next_end, color_ratio)   // time within next pattern
c = lerp(a, b, pat_ratio)                     // weather/room transition
c = (c + add_color) * now_allcol_ratio * scale
clamp 0..255
```

`scale` carries the per-category ratios (`now_actcol_ratio²`, `now_bgcol_ratio`,
`now_fogcol_ratio`, vrbox ratios) that events fade. **Everything below uses this
one blend.**

### I.4 Per-frame outputs (all in `g_env_light`)

| Output | Fields | Consumed by |
| :-- | :-- | :-- |
| Actor ambient | `actor_amb_col` | per-actor TEV via tevstr |
| BG ambients | `bg_amb_col[0..3]` RGB | room geometry ambient — **one layer per room model file** |
| BG alphas | `bg_amb_col[1..3].a` | **not ambient**: per-material TEV constants on the water, murk and faked-fog materials |
| Dungeon light colours | `dungeonlight_col[6]` | `DUNGEON_LIGHT` point lights |
| Fog | `fog_col`, `mFogNear`, `mFogFar` | `GXSetFog(GX_FOG_PERSP_LIN, …)` |
| Skybox | `vrbox_sky_col`, kumo ×3, kasumi ×2 | vrbox dome TEV tint |
| Bloom | threshold/size/ratio/blend col/mode/mono col | `mDoGph_gInf_c::getBloom()` |
| Misc | `bg_light_influence`, cloud shadow density, shadow alpha | shadows, cloud shadow |

Four details that matter downstream:

- **The bloom is palette-driven**: `setLight` blends four `l_kydata_BloomInf_tbl`
  entries with the same ratios. In twilight the blur size gets a random sinus
  wobble (`S_fuwan_sin`) — the twilight shimmer.
- **All mood comes from the ambients, not the sun colour.** The base light
  colour is constant white and the sun's J3D diffuse for actors is a constant
  warm (126,110,89). `SetBaseLight` picks the sun between daytime 67.5 and
  292.5, else the moon.
- **Wolf senses** short-circuits many outputs (black fog and vrbox, bloom table
  3, negative cloud density) — so senses is *also* mostly palette-driven.
- **Twilight** stages ship twilight palettes in their stage data; the look on
  top is bloom table 1/2 — golden tint, low threshold, high density, base image
  dimmed to 0xD2/255, 37.5 % desaturation via the mono colour.

### I.5 How the outputs hit the screen on the GameCube

Three mechanisms, all TEV/GX state: **lighting** (ambients plus diffuse lights
per draw, which a path tracer replaces wholesale); **fog** (`GXSetFog` per draw
plus `GXSetFogRangeAdj` radial correction); and **the EFB post chain**
(`m_Do_graphic.cpp` `bloom_c`), whose order matters:

1. **Mono pass**, if `mMonoColor.a > 0`:
   `out = lerp(fb, replicate(fb.r) * monoRGB, monoA/255)` — greyscale, red
   channel as luma proxy, tinted and lerped. This is the twilight/senses
   desaturation.
2. **Bloom gather** on the mono'd framebuffer — already ported to the fork as
   `rtx.bloom.dusklight*`.
3. **Composite**: `out = bloom*blendRGB*(screen|add) + fb*(OrigDensity/255)`.
   **`OrigDensity` scales the base image** — twilight dims the whole scene to
   82 % here. Our Remix bloom port does not yet do the mono pass or the
   base-image weight.

---

## Part II — What the D3D9 stream carries on its own

Aurora's backend ships "v1 unlit": `D3DRS_LIGHTING` is permanently `FALSE`, GX
light state is decoded but never becomes D3D9 lights, and the TEV tints, mono
pass and vrbox colours are EFB tricks the path tracer replaces. **Taken alone,
Ordon at dusk and Ordon at noon would differ only by geometry and textures, with
the whole mood engine idling.**

That is the *reason* for Parts III and IV, not a description of the build. The
mood engine reaches Remix over the option wire and the Remix API instead: the
bridge creates the sun/moon distant light and the effect lights through the API,
the ambients drive the grade pass, and the atmosphere builds fog and sky from
the pushed palette. **Material colour is the one thing that does travel through
D3D9**, and the stream was extended rather than accepted as it stood — aurora
encodes the colour a surface presents into `D3DMATERIAL9` and the fork reads it
back, evaluating two-colour ramps exactly rather than squeezing them into one
D3D9 op. `extern/aurora/docs/dx9/remix-material-interface.md` §9–§10.

**Vertex colours carry baked lighting on *some* draws, and GX says which per
draw.** Feeding a pre-lit colour to a renderer that then lights the scene itself
double-counts, so the material case is forwarded and the baked case withheld.
Two earlier revisions of this paragraph were wrong in opposite directions —
first claiming no baking risk existed, then withholding vertex colour outright,
which also discarded genuine material colour.

---

## Part III — The Remix-side surfaces we drive

Frame order in the fork's colour pipeline:

```
pathtrace → denoise → COMPOSITE (volumetrics, legacy fog, sky)  [linear HDR, render res]
  → upscale → BLOOM (incl. our dusklight pyramid)               [linear HDR, target res]
  → motion blur → auto exposure → TONEMAP                       [HDR → LDR]
  → PostFX lens → sRGB + dither                                 [display]
```

Three facts that shaped the design:

- **Legacy D3D9 fog capture exists and matches GX fog exactly.** Composite's
  `calculateFog` implements `f = (end−d)·1/(end−start)` for `D3DFOG_LINEAR`,
  identical to `GX_FOG_PERSP_LIN`, and world units pass through unscaled. But
  Remix keeps the *first* non-`NONE` fog state of the frame, and TP sets fog per
  tevstr, so the bridge pushes the global `g_env_light` fog instead.
  **`rtx.fogColorScale` no longer applies to us as of 2026-08-17** — while the
  atmosphere is on the fork writes its resolved fog radiance straight into the
  fog state and the composite skips the scale, because that path aiming at
  `palette × 0.25` while the volumetric path aimed at a decoded,
  exposure-referenced, dome-steered colour is exactly how one fog ended up with
  two colours. The level knob for **both** paths is now `fogRadianceScale`.
- **The grading surface is thin.** `rtx.tonemap.colorGradingEnabled` runs
  post-tonemap and only in Global mode, while the **default tonemapper is
  Local**, which has no grading at all, and there is no LUT anywhere. So we add
  our own small pass rather than hijacking the tonemapper.
- **Auto exposure runs *after* bloom**, so any global darkening we add is
  partially compensated. Desirable as eye adaptation, but it must be bounded —
  and it is why every brightness judgement in the playbook disables it.

**The Remix API is in-process — no bridge.** The game is x86_64 MSVC and so is
dxvk-remix; aurora links `d3d9.lib` statically, so Remix's `d3d9.dll` is mapped
before anything asks for it. `dxvk_RegisterD3D9Device(device)` must be called
once after device creation for light submission; `SetConfigVariable` works
without it.

---

## Part IV — Design

### IV.1 The principle: the game reports state, Remix owns the response

The bridge pushes **raw kankyo values** into a dedicated namespace
(`rtx.dusklight.env.*` — written by the game every frame, never hand-edited).
How strongly each value shapes the image is controlled by **response options**
(`rtx.dusklight.grade.*`, `rtx.bloom.*`, `fogRadianceScale`, …) that the game
never touches, so they stay hand-tunable live and per user. The split also
sidesteps the "User layer beats `rtx.conf`" property: the game only occupies
keys nobody should be setting by hand.

### IV.2 What the bridge pushes

`dusk::remix::tick()` runs once per frame after `drawKankyo()` has updated
`g_env_light`, on the same thread that drives D3D9. Values are diffed, so
outside palette transitions the steady-state push count is ~0–3 strings. The
bridge **never writes user-facing knobs**, so a hand-tuned `rtx.bloom.dusklight*`
value survives it and `rtx.bloom.dusklightFollowGame` decides which set the
bloom pass consumes.

**What is shipped is not listed here.** `pushKankyoState`
(`src/dusk/remix_bridge.cpp`) is the live list — every bloom and mono field,
every ambient and BG alpha, the fog and sky colours, and everything added since —
and each readout's own description in the fork's `rtx_dusklight_env.h` says what
it means. A hand-written copy of that list is a copy that goes stale.

**What this table is for is the opposite: three keys that are designed and NOT
built.** Naming them is deliberate — `tools/check-remix-protocol.py` allowlists
exactly these three so it can keep failing on any *other* undeclared name a
document invents. **A tick here would mean shipped; there are none.**

| Key | Source | Status |
| :-- | :-- | :-- |
| `rtx.dusklight.env.hazeColor` | `vrbox_kasumi_outer_col` /255 — the **near** haze band, despite "outer" | phase 4, unbuilt |
| `rtx.dusklight.env.darkworld` | `dKy_darkworld_check()` | phase 4, unbuilt |
| `rtx.dusklight.env.sensesStrength` | `senses_effect_strength` | phase 4, unbuilt |

Two conventions worth carrying: `bgAmbient` is **layer 0 only** (see I.4), and
the three BG alphas are the game's own 水面α water surface, 補佐α auxiliary and
ウソFog "fake fog" — not ambients at all.

Game-side gating, implemented: with the bridge active `bloom_c::draw()` returns
immediately, so the EFB filter quads stop overdrawing the path-traced image
whatever `game.bloomMode` says. Nobody has to set anything by hand.

### IV.3 What was built on top

- **Sun/moon distant light** (`game.remixSunMoonLight`, default on). Direction
  from `setSunpos`/`SetBaseLight`; the colour is constant warm white. Skipped in
  twilight and interiors, which `dKy_SunMoon_Light_Check()` detects.
- **The generated sky** — and the answer was neither branch the draft offered.
  The sky colours are pushed every frame and the fork builds its own lat-long
  dome from them, registering it as a **dome light**, with the game's vrbox
  hidden so there is only one sky. Tested good 2026-07-28. Probing the vrbox
  raster draws was dropped rather than investigated to a conclusion: a generated
  dome is exact and is a light source, which a captured LDR probe is not.
- **Effect lights** — a sphere light at the **origin of the effect that draws
  the fire**, rather than at the position of any light the game registered. This
  is what lights interiors and night, and it is the only fine-grained light
  source indoors: the sun/moon is gated off there, and RTXDI's light list has no
  dome type, so a sky is never NEE-sampled at all. Hue, extent and persistence
  come from what the effect's artists authored into the `.jpa`; brightness does
  **not**, because nothing they authored is photometric and inventing one would
  be inference recorded as finding. [`effect-lights.md`](effect-lights.md).
- **The local point-light mirror** — the *previous* system, **removed at
  protocol 17 (2026-08-16)** once the A/B it was kept for had been decided. It
  mirrored `g_env_light.pointlight[100]` at the position the game put each
  light, which works under a rasterizer, where a point light casts no shadow and
  can sit wherever the shading looks best, and reads as wrong under a path
  tracer, which casts a real shadow from the exact point.
- **The ambient grade** — one multiply over the frame just before bloom, putting
  back the tint path tracing removed. Off by default and **never run**; it
  double-counts against the dome light's fill, so it has to be tested alone.

What is built versus what is actually tested:
[`remix-open-issues.md`](remix-open-issues.md).
