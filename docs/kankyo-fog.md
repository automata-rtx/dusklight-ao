# Kankyo fog: what the game computes, and what the bridge pushes

Game-side companion to `dxvk-remix/documentation/DusklightAtmosphere.md`, which
owns the renderer design. This file covers the vanilla side: how TP's fog is
authored and modified, how it varies by area and time, and what the bridge has
to send so Remix can reproduce it.

The palette/pselect/EnvR data model is already documented in
`docs/kankyo-remix.md` §I.1–I.3; this file does not repeat it.

**Vocabulary.** The game's identifiers are romanized Japanese: **kankyo** = 環境
(environment), **kasumi** = 霞 (the horizon haze band), **kumo** = 雲 (cloud),
**moya** = 靄 (mist). "vrbox" is the game's word for the skybox dome, and
"wether" is its own spelling of *weather*. [`japanese-naming.md`](japanese-naming.md)
is the reference.

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

("kytag" = *kankyo tag*: `d_a_kytag00`…`d_a_kytag17`, invisible actors that
override environment state for the area they sit in.)

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

### 3.3 Dramatic fog banks — the Lost Woods / Sacred Grove (kytag01)

> **This was attributed to Lake Hylia until 2026-08-11 — in twelve passages
> across two repos**, four here and eight in `DusklightAtmosphere.md`. (The
> audit that caught it said five; the rest turned up by grepping `Hylia` across
> both repos instead of working from the list.) The game says otherwise, twice,
> and the two sources are independent:
>
> ```
> src/d/actor/d_a_kytag01.cpp:1-4    /** d_a_kytag01.cpp   Sacred Grove Mist Tag */
> src/d/actor/d_a_kytag01.cpp:202    OS_REPORT("\n迷いの森　霧タグのスケールでの…")
>                                      迷いの森 = the Lost Woods, 霧タグ = fog tag
> ```
>
> The `OS_REPORT` is the stronger of the two — it is an **authored** string, the
> original team labelling their own actor, where the header comment is a
> decompilation reconstruction (`japanese-naming.md` §8b). Treat the *other*
> reconstruction with more suspicion: `include/d/actor/d_a_kytag01.h:9` calls the
> class **"Twilight Tag 1"**, which matches nothing this actor does. Do not
> "correct" it — it is a decomp artefact we inherit — but do not cite it either.
> They agree anyway,
> and they are not in conflict: **"Lost Woods" and "Sacred Grove" are the same
> stage**, `F_SP117` (`src/dusk/map_loader_definitions.h:132,135` — Lost Woods
> is room 3, Sacred Grove is the `F_SP117_1` layer).
>
> **Where the mix-up probably came from**, because it has produced more than one
> wrong attribution: `d_kankyo.cpp:7457` is an HIO combo item
> 「２：ハイリア湖専用」 — *"2: Lake Hylia only"* — but it sits in the
> 「■ トワイライト　センスパターン」 panel (`:7451`) bound to
> `twilight_sense_pat` (`:7454`), the **wolf-sense** pattern index. That is a
> different index space from colpat, and one Japanese label read out of its
> panel appears to have seeded two separate wrong attributions (see
> `japanese-naming-audit.md` §4.1).
>
> **What is *not* established:** whether Lake Hylia also carries a kytag01.
> Actor placement lives in stage `.dzs` data, which this repo does not contain —
> `ky_tag1` (the `argument & 0xFF == 0` mist variant, `d_stage.cpp:941`) appears
> exactly once in the whole tree, in that name table. So this correction moves
> the *worked example* to where the game names it; it does not prove Lake Hylia
> has no tag of its own.

This one *is* scripted, and it stacks four things
(`src/d/actor/d_a_kytag01.cpp:94-102`):

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

**Only layer 1 varies with where you are.** Layers 2–4 are weighted by
`field_0x594` alone (`:95-102`), which is a per-actor fade with no spatial term
at all — so the colour shift, the moya count and the audio are **uniform across
the whole room** while the fog *range* moves with the camera. The document said
"the blend ratio" as though one number drove all four; it does not.

#### The tag marks the clear centre, not the bank

Read `:53-71` and `:81-94`. Both weights on layer 1 run the opposite way from
the mental model these documents were written with:

- **Distance** (`:53-69`) is `0` at or inside `mNamiInnerRange`, ramps
  `(d − inner) / (outer − inner)` between the two radii, and is `1` beyond
  `mNamiOuterRange`. It is the third argument to `dKy_fog_startendz_set`, and
  that argument is a **lerp weight toward the override**, not a density:
  `d_kankyo.cpp:747-748` (`float_kankyo_color_ratio_set`, called at `:2506-2513`
  and `:2969-2976`) computes `value += ratio * (override − value)`, so `1`
  means *fully* `-2000/200` and `0` means the area's own palette fog, untouched.
  **So the whiteout is strongest far from the tag and absent at it.**
- **View angle** (`:81-92`) does the same. `temp_f2_2` is
  `(1 − |Δyaw| / 32768)⁴`, which is `1` when the camera looks straight at the
  tag; the weight is `1 − temp_f2_2 + 0.2`, clamped to `1`. **Facing the tag
  gives the floor, 0.2. Looking away gives 1.0**, and because of the fourth
  power it saturates at 1.0 by roughly 60° off-axis — the "clear" window is
  narrow.

The old wording, *"it strengthens as you look into the fog bank"*, **was not a
sign error** and is not being flipped: if "the fog bank" means the away-from-tag
region, it is correct. The defect was that nothing said where the bank is, so it
read as though the bank were at the tag.

**Two narrowings, so this is not overstated:**

- **It is conditional on the tag being switched on.** `:71` multiplies the
  distance term by `field_0x594`, the switch-gated fade from `:124-144`:
  `mSwNo1` on drives it toward `1`, `mSwNo1` off — or `mSwNo2` *also* on —
  drives it to `0`, and the actor deletes itself on reaching `0`. If
  `mSwNo1 == 0xFF` it is never driven at all and stays at its `Create` value of
  `0` (`:189`) — the one other writer, the `field_0x59c == 2` escape at `:147`
  that would force it to `1.0`, is **unreachable in this tree**: within
  `kytag01_class` that member is only ever assigned `0` (`:211`) and `1`
  (`:64`, `:68`). With the gate shut, `Execute` never even calls `mist_tag_move`
  (`:151`), so **the fog is zero everywhere regardless of distance.**
  "Whiteout away from the tag" describes the tag when it is *on*.
- **Which switches, and where the tag sits, are not in source.** Both come from
  `.dzs` stage data. The mechanism below is verified; the placement is not
  knowable from this tree.

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

All under `rtx.dusklight.env.`, all `NoSave`, written by `src/dusk/remix_bridge.cpp` every frame. These keys arrived at
protocol **2**; the wire has since advanced to **11** (3 = overlay + warp, 4 = the clock, 5 = per-blade grass,
6 = the Controls tab, 7 = effect lights, 8 = the effect-light exclusion readout) and gained more keys. The authoritative list is
`dxvk-remix/src/dxvk/rtx_render/rtx_dusklight_env.h`.

| Key | Source |
| :-- | :-- |
| `fogActive` | `fogIsActive()` — see below |
| `fogColor` | `g_env_light.fog_col`, normalized 0..1 |
| `fogStartZ` | `g_env_light.mFogNear`, quantized to 1 unit |
| `fogEndZ` | `g_env_light.mFogFar`, quantized to 1 unit |
| `skyHidden` | `skyIsHidden()` — **recomputed, not `hide_vrbox`**, see below |
| `skyColor` | `vrbox_sky_col` |
| `kasumiInner`, `kasumiOuter` | `vrbox_kasumi_inner_col` / `vrbox_kasumi_outer_col`. **`outer` is the *near* band and `inner` the *far* one** — the reverse of the English reading; the game labels them 霞手前/霞奥 in its palette exporter and `kasumiF`/`kasumiB` in its debug view ([`japanese-naming.md`](japanese-naming.md) §6) |
| `kumoTop`, `kumoBottom`, `kumoShadow` | cloud colours; pushed but not consumed yet (Phase D) |
| `colpat` | `g_env_light.wether_pat1` |
| `moyaMode`, `moyaCount` | `g_env_light.mMoyaMode` / `mMoyaCount`, clamped at 0 |

Two of these are not the obvious field, and both matter:

**`skyHidden` recomputes the colour-sum test rather than reading `g_env_light.hide_vrbox`.** That flag is written by
`daVrbox_color_set` in the sky dome *actor* (`d_a_vrbox.cpp:69-79`), so in any stage with no such actor — which is every
interior, exactly where the question matters — it is never updated and still holds whatever the last outdoor area left
there. Recomputing the same test is correct everywhere and one frame fresher.

**`colpat` is `wether_pat1`.** There is no `mColpatPrev`/`mColpatCurr`; the only similarly named fields are the `*Gather`
pair, which hold a sentinel most of the time because they belong to the secondary blend the fog bank tags drive.

**`fogActive`** is computed, because the game has no fog-enable flag at all and leaves the distances entirely unclamped:
`isfinite(near) && isfinite(far) && far > near && far > 0`. Note a *negative* `mFogNear` is normal rather than broken —
scripted fog banks set it that way deliberately (§3.3), and the predicate accepts it.

**These are also displayed live in Remix's Dusklight tab.** That readout is the
only practical way to learn the real per-area values: the numbers live in stage
`.dzs` data, so the way to find out what Lake Hylia does at 6am is to stand
there and read them. Do that before tuning anything — §3's mechanisms are
verified, its *values* are not.

Suggested measurement pass, recording `fogStartZ` / `fogEndZ` / `fogColor` at
each: Hyrule Field (dawn / noon / dusk / night), Faron Woods (morning and
midday), the **Lost Woods / Sacred Grove** (`F_SP117`) for the kytag01 bank,
Lake Hylia (morning) for a dense *palette* regime, Goron Mines entrance and
interior, Forest Temple, Palace of Twilight.

**How to take the kytag01 reading, because it is the opposite of the obvious
one.** The tag sits in a **clear centre** and the fog is strongest *away* from
it (§3.3). So: **stand where the fog is thinnest, then walk outward** and watch
`fogStartZ` / `fogEndZ` slide toward `-2000` / `200`. Walking *into* the thick
part to find the tag will not find it. Two things worth capturing on the way:

- the two radii are visible in the numbers — the values stop changing once you
  are past `mNamiOuterRange`, and are pinned to the palette inside
  `mNamiInnerRange`;
- **turn on the spot at a fixed position.** The range moves with view angle
  alone, weakest facing the tag; that isolates the layer-1 view term from the
  distance term.

If the numbers never leave the palette values anywhere in the area, the tag's
gating switches are off (§3.3) rather than the readout being broken.

---

## 6. Moya (haze particles)

**Moya** is 靄 — mist, or low-lying haze. Separate from GX fog — billboard
particles driven by `g_env_light.mMoyaMode` / `mMoyaCount`:

| Mode | Set by | Case |
| :-- | :-- | :-- |
| 1 | `d_a_demo00.cpp:1825` | cutscene |
| 3 | `d_a_kytag01.cpp` | Lost Woods / Sacred Grove mist tag (§3.3) |
| 4 | `d_a_kytag02.cpp` | area haze |
| 10 / 11 | `d_a_kytag06.cpp` | weather |

**They are already not drawn on the D3D9 backend.** `mMoyaCount` feeds
`mpCloudPacket->mCount` in `cloud_shadow_move` (`d_kankyo_rain.cpp:1616`), and
that packet's `draw()` returns early under D3D9 (`d_kankyo_wether.cpp`,
`dKankyo_cloud_Packet::draw`, disabled because its projected fake shadows fought
Remix's path-traced ones). There is a second gate at the same rank inside
`drawCloudShadow` itself (`d_kankyo_rain.cpp:4510`) — belt-and-braces, so
neither one is load-bearing alone.

*Naming, so the next reader is not misled:* "haze particles" is the label this
document and the fork's `rtx.dusklight.env.moyaMode` description both use, but
the only draw path `mMoyaCount` actually reaches is `drawCloudShadow` — a
projected cloud-shadow overlay. Whether moya also has a separate billboard
particle path is **unverified**; nothing in `src/` was found that reads
`mMoyaCount` for one.

So the double-count this section was written to warn about does not arise here,
and no suppression switch was needed — one was written and then removed rather
than ship a control that does nothing. `moyaMode`/`moyaCount` are still pushed:
they say how much haze an area wants, which is the right input for folding that
density into the medium instead.

---

## 7. Status

| Item | State |
| :-- | :-- |
| Mechanisms in §1–§4, §6 | verified in code, 2026-07-27 |
| Per-area values in §3 | **not measured** — needs the §5 readout pass |
| Bridge keys in §5 | implemented 2026-07-27 at protocol 2, **tested good 2026-07-28** as part of atmosphere phase A |
| Sky dome suppression (`game.remixHideVrbox`) | implemented, **tested good 2026-07-28** with the generated sky — covers both `d_a_vrbox` and `d_a_vrbox2` |
| Renderer side | `dxvk-remix/documentation/DusklightAtmosphere.md` |

**The calibration pass was skipped, then run.** Phase 0 was finally run on 2026-07-28 and phases A/B were confirmed good
in game (owner: *"a massive, frankly monumental success"*). One constant was wrong — `skyIntensity` needed 1.0 → 6.0,
because the palette is sRGB-decoded before it is scaled. `zHalfMin` and `froxelRangeScale` were not reported wrong, but
the dense-fog regime that would actually challenge them — the **kytag01 whiteout in the Lost Woods / Sacred Grove**
(§3.3; this said "Lake Hylia" until 2026-08-11 and was wrong), and the Goron Mines — was never visited, so
they remain unchallenged rather than confirmed. Lake Hylia in the morning *was* visited on 2026-07-29 and read
"suitably intense", but that is palette fog: only the tag's `-2000` start puts the half-density point behind the camera,
which is the one case `zHalfMin` exists for. Read `DusklightAtmosphere.md` §13 before concluding a result is wrong, and run the §5 measurement
pass here — the Dusklight tab in Remix now shows the live fog range and colour, which is the only way to see values that
live in stage data rather than in source.
