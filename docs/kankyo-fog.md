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

## 2. Four layers modify fog before it reaches the screen

This is why the bridge must read the **outputs** (`fog_col`, `mFogNear`,
`mFogFar`) and never re-derive from the palette tables.

| # | Layer | Where | Effect |
| :-- | :-- | :-- | :-- |
| 1 | Palette 4-way blend | `d_kankyo.cpp:2469` | time-of-day × colpat transition. The colpat half is a **crossfade**: `wether_pat0` (outgoing) → `wether_pat1` (incoming) on `pat_ratio`. The `*Gather` fields are how tags and events *inject into* it — see below |
| 2 | `addcol_fog` | `dKy_addcol_fog_set` | additive colour offset |
| 3 | `now_fogcol_ratio` | `d_kankyo.cpp:4802`, `:9606` | scales fog colour; lightning pulses it (`d_kankyo_rain.cpp:362`) |
| 4 | `dKy_fog_startendz_set` | `d_kankyo.cpp:9324` → `field_0x11ec/f0/f4` | start/end override with a blend ratio |

("kytag" = *kankyo tag*: `d_a_kytag00`…`d_a_kytag17`, invisible actors that
override environment state for the area they sit in.)

Reading the final values inherits all four for free and keeps working if the
game changes. Re-deriving would mean reimplementing all four and keeping them
in sync forever.

> ### There was a layer 5 here, and it was the same blend counted twice
>
> **Corrected 2026-08-11.** This table used to carry a fifth row, "Gather colpat
> blend — a **second**, independent palette blend layered on layer 1". There is
> no second blend. `mColpatPrevGather` / `mColpatCurrGather` /
> `mColPatBlendGather` are a **staging area**, and `exeKankyo` empties it into
> layer 1 once per frame (`d_kankyo.cpp:4788-4828`):
>
> ```cpp
> if (mColpatPrevGather != 0xFF) { wether_pat0 = mColpatPrevGather; ... }
> if (mColpatCurrGather != 0xFF) { wether_pat1 = mColpatCurrGather; ... }
> if (mColPatBlendGather >= 0.0f) { pat_ratio  = mColPatBlendGather; ... }
> ```
>
> — clearing each back to its sentinel (`0xFF`, `0xFF`, `-1.0f`) as it goes, the
> same sentinels `envcolor_init` (`:1243`) writes at `:1373-1375`. `mColPatMode`
> selects which of the two arms runs: with it set the staged values are applied
> every frame and *held*, which is also what stops `setLight_palno_get`
> advancing `pat_ratio` itself (`:2192`); without it they are applied only once
> the previous fade has finished (`wether_pat0 == wether_pat1`, `:4812`), so a
> tag cannot interrupt a transition already in flight.
>
> There is exactly one `pat_ratio` and exactly one blend on it — the four
> palettes and that single ratio go into `setLight_palno_get` together at
> `:2406-2409`, and every colour, fog distance and bloom parameter below it
> lerps on the same number (`:2435`-`:2639`).
>
> **What the gather fields really are: the write port.** The API for changing
> the palette writes *there* rather than to the live fields —
> `dKy_change_colpat` (`:9528-9533`), `dKy_custom_colset` for events
> (`:9535-9553`), and the kankyo tags (`d_a_kytag00.cpp:93-123`,
> `d_a_kytag01.cpp:96-99`, `d_a_kytag06.cpp:244-252`).
>
> **It is not the only route, and that is worth knowing before anyone builds on
> it.** A handful of actors assign `wether_pat0`/`wether_pat1` directly and skip
> the staging area entirely — `d_a_kytag06.cpp:1106-1107` and `:1172-1173`,
> `d_a_kytag01.cpp:182-183`, and several bosses and enemies (`d_a_e_vt.cpp`,
> `d_a_b_mgn.cpp`, `d_a_b_bq.cpp`, `d_a_b_yo.cpp`, `d_a_e_fm.cpp`). Those
> writes set both endpoints at once, so they are a hard cut rather than a fade.
> Either way the *outputs* carry the result, which is the whole reason the
> bridge reads outputs.
>
> **The consequence that the wrong wording hid.** Because it read as a separate
> system, the Remix bridge pushed `wether_pat1` alone and nothing pushed the
> other two thirds — so Remix knew *which* pattern but never *how far through*.
> `dKy_change_colpat` sets the ratio to `0.0f` without touching `wether_pat0`,
> so on the frame the index changes the palette is still **100% the old
> pattern**, and a consumer cutting on the index alone is at its most wrong
> exactly then. Fixed at protocol 13; `dxvk-remix/documentation/DusklightAtmosphere.md`
> §4 carries the renderer half.

> ### There was a sixth row here, and it was wrong
>
> **Removed 2026-08-11.** This table used to carry a layer 6, "`fog_avoid_tag`
> (kytag08) — a moving position that pushes fog away from the player", with a
> note that it was positional and could not be read out of the outputs. That
> claim was the sole basis for compromise C6 and for a planned
> heterogeneous-fog feature in the Remix doc. **kytag08 writes no fog state.**
>
> The field has four references in the whole tree: its declaration
> (`include/d/d_kankyo.h:332`), one write (`d_a_kytag08.cpp:264`, on actor
> create) and one read, tested then dereferenced (`d_kankyo.cpp:11608`,
> `:11610`). That read sits in
> `dKy_bg_MAxx_proc`'s branch for background materials named **`MA11`**
> (`:11539`), on the non-twilight side of `dKy_darkworld_check()`. All it does
> is aim a `C_MTXLightPerspective` projection at the tag's `mAvoidPos` and set
> it as that material's texture matrix 0 effect matrix (`:11608-11637`) — a
> **projected texture on drawn geometry**.
>
> The game names that geometry itself. The same branch sets the material's TEV
> colours 1 and 2 (`:11584`, `:11590`), and the HIO panel that overrides exactly
> those two in a debug build — `mist_twilight_c1_col`/`c2_col`, `:11594-11604` —
> is headed 「霧沼　トワイライト時　色設定」 (`d_kankyo.cpp:7594`, sliders
> `:7596-7603`): 霧沼 *kirinuma*, **fog swamp**, "colour settings while in
> twilight". The "fog" being cleared is a **painted ground surface**, and the
> hole in it is a texture projection.
>
> The rest of the actor is two JPA emitters (`0x84A0`, plus `0x84A1`/`0x84A2`
> by world, `d_a_kytag08.cpp:252-257`), audio (`mDoAud_setFogWipeWidth` `:80`,
> `mDoAud_startFogWipeTrigger` `:97`) and one player flag (`onFogFade()`
> `:157`). Grepping the file for `fog_col`, `mFogNear`, `mFogFar`,
> `dKy_fog_startendz_set`, `GXSetFog`, `J3DFogInfo`, `addcol_fog` and
> `now_fogcol_ratio` returns nothing.
>
> **Consequence for the Remix side: nothing is owed.** The bubble is geometry
> and particles, so it reaches Remix through the ordinary draw stream. C6 is
> withdrawn; `DusklightAtmosphere.md` §8.2 carries the same finding.

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
> wrong attribution: `d_kankyo.cpp:7462` is an HIO combo item
> 「２：ハイリア湖専用」 — *"2: Lake Hylia only"* — but it sits in the
> 「■ トワイライト　センスパターン」 panel (`:7456`) bound to
> `twilight_sense_pat` (`:7459`), the **wolf-sense** pattern index. That is a
> different index space from colpat, and one Japanese label read out of its
> panel appears to have seeded two separate wrong attributions (see
> `japanese-naming-audit.md` §4.1).
>
> **The same panel struck a third time**, and that one is traced rather than
> suspected: its entry 9, 「９：Ｌｖ８専用　D_MN08」 (`:7469`), became the fork's
> `kPalaceOfTwilightColpat = 9`. §3.5 below.
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
2. **A colpat crossfade driven directly**, staged through the "gather" fields —
   so the sky and fog *colour* change too, not just the range. This is not a
   second blend (§2): the tag is writing the *one* palette crossfade's two
   endpoints and its ratio, pattern 0 → pattern 1, and `exeKankyo` copies all
   three onto `wether_pat0` / `wether_pat1` / `pat_ratio` next frame. Because
   `mColPatModeGather = 1`, the game stops advancing the ratio itself
   (`d_kankyo.cpp:2192`) and the tag owns it outright — so `field_0x594`, a
   `cLib_addCalc` ramp taking ~50 frames to travel 0 → 1 (`:129-148`), **is**
   the mist's strength. A consumer that reads only "the pattern is 1" sees full
   mist from the first frame the tag is in range.
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

Ordinary palette machinery with amber values, plus the full-screen mono overlay
the bridge already reports. Not a special rendering mode.

> **Corrected 2026-08-11.** This paragraph used to open "Colpat pattern 9
> (`d_kankyo.cpp:266-270`, stage prefix `D_MN08`)". **That citation is a
> different index space** — the same trap as §3.3's Lake Hylia mix-up, from the
> same debug panel.
>
> `d_kankyo.cpp:266-270` is inside `dKy_sense_pat_get` (`:135-316`), the
> **wolf-sense vision** pattern. Its values pick what senses mode looks like, in
> `dKy_WolfPowerup_BgAmbCol` (`:318`) and `dKy_WolfPowerup_FogNearFar` (`:416`),
> and the panel that overrides it is 「■ トワイライト　センスパターン」 bound to
> `twilight_sense_pat` (`:7456`, combo at `:7459-7474`), whose entry 9 reads
> 「９：Ｌｖ８専用　D_MN08」 — *"9: for Lv8 only, D_MN08"* (`:7469`). True of the
> sense patterns; **it says nothing about colpat.**
>
> Colpat is `g_env_light.wether_pat1` (§5), which indexes
> `stage_envr_info_class::pselect_id[65]` (`d_stage.h:171`) through the switch at
> `d_kankyo.cpp:1896-1990`. **What pattern the Palace of Twilight actually runs
> is UNKNOWN** — no literal 9 is written to colpat anywhere in the tree
> (`dKy_change_colpat` takes 0–6 and 10–12; direct `wether_pat1` writes are 1,
> 2, 3, 4, 6), but three writers take it from actor parameters and path-point
> arguments in `.dzs`/`.dzr` stage data (`d_a_kytag06.cpp:854`, `:876`,
> `:1105-1107`; `d_a_kytag01.cpp:181-183`), which this repo does not contain. So
> 9 is *possible* and unestablished, not ruled out.
>
> This mattered outside this document: the fork bypasses its entire physical sky
> when the pushed colpat is 9, on this citation. It is now instrumented rather
> than removed — `dxvk-remix/documentation/DusklightAtmosphere.md` §8.6 has the
> log line and what each result decides.

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
protocol **2**; the wire has since advanced to **17** (3 = overlay + warp, 4 = the clock, 5 = per-blade grass,
6 = the Controls tab, 7 = effect lights, 8 = the effect-light exclusion readout, 13 = per-flower blossoms,
14 = the effect-light vocabulary, the three global multipliers and the lantern toggle,
17 = the local point-light mirror **removed**, which deleted the four `localLights*` readouts) and gained
more keys.
The full ladder, including why 12 is skipped, is `dxvk-remix/documentation/DusklightOverlay.md`; the authoritative key list is
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
| `colpat` | `g_env_light.wether_pat1` — the pattern being faded **in** to |
| `colpatPrev` | `g_env_light.wether_pat0` — the pattern being faded **out** of (protocol 13) |
| `colpatBlend` | `g_env_light.pat_ratio`, quantized to 0.01 — how far through, 0 = all `colpatPrev`, 1 = all `colpat` (protocol 13) |
| `moyaMode`, `moyaCount` | `g_env_light.mMoyaMode` / `mMoyaCount`, clamped at 0 |

Three of these are not the obvious field, and all three matter:

**`skyHidden` recomputes the colour-sum test rather than reading `g_env_light.hide_vrbox`.** That flag is written by
`daVrbox_color_set` in the sky dome *actor* (`d_a_vrbox.cpp:69-79`), so in any stage with no such actor — which is every
interior, exactly where the question matters — it is never updated and still holds whatever the last outdoor area left
there. Recomputing the same test is correct everywhere and one frame fresher.

**`colpat` is `wether_pat1`, and it is only one third of the answer.** There is no `mColpatPrev`/`mColpatCurr`; the
similarly named `*Gather` fields hold a sentinel most of the time because they are the *staging area* tags and events
write into, not a blend of their own (§2). The live blend is the trio above — `wether_pat0` → `wether_pat1` on
`pat_ratio` — and until protocol 13 only `wether_pat1` crossed. Outside a transition `colpatPrev == colpat` and
`colpatBlend == 1.0`, which is exactly why the old single push looked complete: **the missing two thirds are constant
except in the moments they decide something.**

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
"suitably intense", but that is palette fog, and a later run settles it: Lake Hylia's ramp was **measured** at
`[-3000, 70000]` on 2026-08-06 — a **midpoint** around 33500, nowhere near the `zHalfMin` clamp, and nothing like
the tag's `[-2000, 200]` (midpoint −900). Note it is the tag's near `end`, not its negative `start`, that collapses the
midpoint: negative starts turned out to be ordinary, present in every area measured in that run. Read `DusklightAtmosphere.md` §13 before concluding a result is wrong, and run the §5 measurement
pass here — the Dusklight tab in Remix now shows the live fog range and colour, which is the only way to see values that
live in stage data rather than in source.

> **Both quantities moved on 2026-08-17, so re-read them before taking the measurement.** This paragraph said
> "half-density point around 33500"; the anchor is the ramp's **midpoint**, and the game is half opaque there only
> while the `zHalfMin` clamp is idle. For Lake Hylia's `[-3000, 70000]` it is idle, so 33500 genuinely is a
> half-density point and nothing about that sentence's arithmetic changes. For the mist tag it is not, and the fork
> was asserting 0.5 opacity at a point where the game's ramp is **0.955** — a medium 4.46× thinner than it should
> have been, in every scripted fog bank in the game. That is now corrected (`σ = −ln(1 − f(anchor)) / anchor`), which
> means **a dense-fog reading taken before 2026-08-17 and one taken after are not comparable.**
> `froxelRangeScale` also changed: `fogRampMode` 2 floors it at 1, because its sub-1 default exists to leave an
> exponential's final closure to the composite and mode 2's field closes itself. **Measure in the default mode 1, or
> record which mode was used**, or the walk-out measures nothing.
> `dxvk-remix/documentation/DusklightAtmosphere.md` §5.1 and §5.2.
