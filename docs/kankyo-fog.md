# Kankyo fog: what the game computes, and what the bridge pushes

Game-side companion to `dxvk-remix/documentation/DusklightAtmosphere.md`, which
owns the renderer design. This file covers the vanilla side: how TP's fog is
authored and modified, how it varies by area and time, and what the bridge has
to send so Remix can reproduce it.

The palette/pselect/EnvR data model is already documented in
`docs/kankyo-remix.md` §I.1–I.3; this file does not repeat it.

---

## 1. Fog is authored alongside the sky, not separately

From `include/d/d_kankyo.h`, the per-frame environment state:

```
/* 0x10C0 */ GXColorS10 vrbox_sky_col;       // sky
/* 0x10E0 */ GXColorS10 vrbox_kasumi_outer_col;
/* 0x10E8 */ GXColorS10 vrbox_kasumi_inner_col;
...
/* 0x1158 */ GXColorS10 fog_col;             // fog
/* 0x11E4 */ f32        mFogNear;
/* 0x11E8 */ f32        mFogFar;
```

`fog_col`, `fog_start_z`, `fog_end_z` and every `vrbox_*` colour come from the
**same** palette entry, selected by the **same** time-of-day and colpat
indices, and blended by the **same** four-way `dKy_calc_color_set` call in
`dScnKy_env_light_c::setLight_*` (`src/d/d_kankyo.cpp:2469`, `:2931`, `:3095`).

TP's fog colour therefore *is* approximately its sky colour at that moment.
Distant geometry dissolves into the sky because it was authored to. Any
reproduction that lets fog colour and sky colour come from independent sources
will look wrong in a way no amount of tuning fixes.

Fog is `GX_FOG_PERSP_LIN` and nothing else — a **ramp**, not extinction:

```
f(z) = saturate((z - start) / (end - start))
```

Applied at `src/d/d_kankyo.cpp:9423` (global) and `:9458` (per-tevstr).
`GXSetFogRangeAdj` is set (`:9486`) but is not forwarded to D3D9 — see
`aurora-ao/lib/dx9/dx9_draw.cpp` `apply_fog_state()` for why.

---

## 2. Six layers modify fog before it reaches the screen

This is why the bridge must read the **outputs** (`fog_col`, `mFogNear`,
`mFogFar`) and never re-derive from the palette tables.

| # | Layer | Where | Effect |
| :-- | :-- | :-- | :-- |
| 1 | Palette 4-way blend | `d_kankyo.cpp:2469` | time-of-day × colpat transition |
| 2 | `addcol_fog` | `dKy_addcol_fog_set` | additive colour offset |
| 3 | `now_fogcol_ratio` | `d_kankyo.cpp:4802`, `:9606` | scales fog colour; lightning pulses it (`d_kankyo_rain.cpp:362`) |
| 4 | `dKy_fog_startendz_set` | `d_kankyo.cpp:9324` → `field_0x11ec/f0/f4` | start/end override with a blend ratio |
| 5 | Gather colpat blend | `mColpatPrevGather` / `mColpatCurrGather` / `mColPatBlendGather` / `mColPatModeGather` | a **second**, independent palette blend layered on layer 1 |
| 6 | `fog_avoid_tag` (kytag08) | `env_light.fog_avoid_tag` | a moving position that pushes fog away from the player |

Reading the final values inherits layers 1–5 for free and keeps working if the
game changes. Re-deriving would mean reimplementing all five and keeping them
in sync forever. Layer 6 is positional and is not represented in the outputs —
see the compromise ledger (C6) in the Remix doc.

---

## 3. How fog actually varies — mechanism by observed case

The *mechanism* below is verified in code. The *values* live in stage `.dzs`
data, not in source, so specific numbers per area have to be read at runtime —
see §5.

### 3.1 Field areas, clear weather

Straight palette lookup: `fog_start_z`/`fog_end_z` per time slot, blended
across the day. Large values; the fog is a draw-distance cue that ties distant
terrain to the sky palette.

### 3.2 Morning fog that fades — Faron Woods

Pure palette scheduling. The morning time slot carries a nearer
`fog_start_z`/`fog_end_z` than the day slot, and `dKy_calc_color_set`
interpolates between them as `daytime` advances. No actor involvement, no
special case. The "surreal" quality comes from the fog range being close enough
to read as atmosphere rather than as distance.

**Implication:** nothing special is needed to reproduce this. The §5.1 mapping
in the Remix doc turns a near `fog_end_z` into a dense medium automatically, and
the fade is the palette interpolation the game already does.

### 3.3 Dramatic fog banks — Lake Hylia (kytag01)

This one *is* scripted, and it stacks four things
(`src/d/actor/d_a_kytag01.cpp:94`):

```cpp
dKy_fog_startendz_set(-2000.0f, 200.0f, var_f31 * var_f3);
g_env_light.mColpatPrevGather = 0;
g_env_light.mColpatCurrGather = 1;
g_env_light.mColPatBlendGather = temp_f1;
g_env_light.mColPatModeGather  = 1;
g_env_light.mMoyaMode  = 3;
g_env_light.mMoyaCount = g_env_light.mColPatBlendGather * 50.0f;
if (g_env_light.mColPatBlendGather > 0.5f) mDoAud_startFogSe();
```

1. **Fog range override** with `start = -2000`, `end = 200` — note `start` is
   negative and below `end`, so the ramp is already ~90% opaque at `z = 0`. A
   near-whiteout, by design.
2. **A second colpat blend** ("gather") pulling in a different palette — so the
   sky and fog *colour* change too, not just the range.
3. **Moya particles** (`mMoyaMode = 3`) — billboard haze on top.
4. **Audio.**

The blend ratio is driven by distance to the tag's range *and* by the angle
between the camera's look direction and the tag
(`d_a_kytag01.cpp:73-92`) — so it strengthens as you look into the fog bank.

**Implication:** layers 1 and 2 arrive for free by reading outputs. Layer 3
needs an explicit decision (Remix doc §8.1) because a billboard haze plus a
dense medium double-counts.

### 3.4 Dense interior fog — Goron Mines, Forest Temple

Palette again, with near ranges and a strongly tinted `fog_col` — hot/orange
for the mines, cooler and thinner for the forest temple. Interiors also
typically have `hide_vrbox` set (`d_a_vrbox.cpp:69-77` sets it when the sky
colours sum to zero), so there is no sky contribution to reconcile.

**Implication:** the same mapping covers "dense interior heat haze" and "thin
outdoor distance fog" with no per-area branching. The dense end is where
driving `froxelMaxDistance` from the fog range pays off most — a tight grid
puts all 64 slices where the fog is.

### 3.5 Twilight Realm

Colpat pattern 9 (`d_kankyo.cpp:266-270`, stage prefix `D_MN08`). Ordinary
palette machinery with amber values, plus the full-screen mono overlay the
bridge already reports. Not a special rendering mode.

---

## 4. Fog is per-object, and Remix currently picks one at random

`setLight_bg` / `setLight_actor` write fog **per tevstr**
(`src/d/d_kankyo.cpp:4246-4247`):

```cpp
tevstr_p->FogCol     = fog_tev_col;
tevstr_p->mFogStartZ = fog_near;
tevstr_p->mFogEndZ   = fog_far;
```

and `GXSetFog` is issued per draw with those values (`:9458`). So a frame
contains many different D3D9 fog states.

`dxvk-remix/src/dxvk/rtx_render/rtx_scene_manager.cpp:609-611` keeps only the
**first non-`NONE` state it sees** and discards the rest. Which draw is first
depends on submission order, so the fog Remix uses is unstable frame to frame
and blind to per-room variation.

**This is why the bridge pushes the global `g_env_light` fog rather than
relying on the D3D9 capture.** The global values are the room's authoritative
environment fog; taking them removes the lottery. The cost is that genuine
per-object variation is flattened — recorded as compromise C7 in the Remix doc.

---

## 5. What the bridge pushes, and the readout

Added to the existing `rtx.dusklight.env.*` block (all `NoSave`, written by
`src/dusk/remix_bridge.cpp` every frame):

| Key | Source |
| :-- | :-- |
| `fogColor` | `g_env_light.fog_col`, normalized 0..1 |
| `fogStartZ` | `g_env_light.mFogNear` |
| `fogEndZ` | `g_env_light.mFogFar` |
| `skyHidden` | `g_env_light.hide_vrbox` |
| `colpat` | current colour pattern index |
| `moyaMode`, `moyaCount` | `g_env_light.mMoyaMode` / `mMoyaCount` |
| `vrboxSkyColor`, `vrboxKasumiInner`, `vrboxKasumiOuter` | sky colours for the dome |
| `vrboxKumoTop`, `vrboxKumoBottom`, `vrboxKumoShadow` | cloud colours (Phase D) |

**These are also displayed live in Remix's Dusklight tab.** That readout is the
only practical way to learn the real per-area values: the numbers live in stage
`.dzs` data, so the way to find out what Lake Hylia does at 6am is to stand
there and read them. Do that before tuning anything — §3's mechanisms are
verified, its *values* are not.

Suggested measurement pass, recording `fogStartZ` / `fogEndZ` / `fogColor` at
each: Hyrule Field (dawn / noon / dusk / night), Faron Woods (morning and
midday), Lake Hylia (morning, in and out of the kytag01 bank), Goron Mines
entrance and interior, Forest Temple, Palace of Twilight.

---

## 6. Moya (haze particles)

Separate from GX fog — billboard particles driven by
`g_env_light.mMoyaMode` / `mMoyaCount`:

| Mode | Set by | Case |
| :-- | :-- | :-- |
| 1 | `d_a_demo00.cpp:1825` | cutscene |
| 3 | `d_a_kytag01.cpp` | Lake Hylia fog bank |
| 4 | `d_a_kytag02.cpp` | area haze |
| 10 / 11 | `d_a_kytag06.cpp` | weather |

Under a path tracer these are camera-facing quads. Keeping them alongside a
dense medium double-counts the haze; the plan is to suppress them and fold the
density into the medium's heterogeneous noise (Remix doc §8.1, compromise C5).

---

## 7. Status

| Item | State |
| :-- | :-- |
| Mechanisms in §1–§4, §6 | verified in code, 2026-07-27 |
| Per-area values in §3 | **not measured** — needs the §5 readout pass |
| Bridge keys in §5 | designed, not implemented |
| Renderer design | `dxvk-remix/documentation/DusklightAtmosphere.md` |
