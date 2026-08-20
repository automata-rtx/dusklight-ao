# Kankyo fog: what the game computes, and what the bridge pushes

Game-side companion to `dxvk-remix/documentation/DusklightAtmosphere.md`, which
owns the renderer design. This file covers the vanilla side. The
palette/pselect/EnvR data model is in [`kankyo-remix.md`](kankyo-remix.md) §I.1–I.3
and is not repeated.

**Vocabulary.** **kankyo** = 環境 environment, **kasumi** = 霞 the horizon haze
band, **kumo** = 雲 cloud, **moya** = 靄 mist. "vrbox" is the game's word for the
skybox dome and "wether" is its own spelling of *weather*.
[`japanese-naming-remix.md`](japanese-naming-remix.md) is the reference.

---

## 1. Fog is authored alongside the sky, not separately

`fog_col`, `fog_start_z`, `fog_end_z` and every `vrbox_*` colour come from the
**same** palette entry, selected by the **same** time-of-day and colpat indices,
and blended by the **same** four-way `dKy_calc_color_set` call.

**TP's fog colour therefore *is* approximately its sky colour at that moment.**
Distant geometry dissolves into the sky because it was authored to. Any
reproduction that lets fog colour and sky colour come from independent sources
will look wrong in a way no amount of tuning fixes — which is exactly the defect
the fork spent 2026-08-17 correcting on its own two fog paths.

Fog is `GX_FOG_PERSP_LIN` and nothing else — a **ramp**, not extinction:

```
f(z) = saturate((z - start) / (end - start))
```

`GXSetFogRangeAdj` is set but not forwarded to D3D9.

---

## 2. Four layers modify fog before it reaches the screen

**This is why the bridge reads the outputs (`fog_col`, `mFogNear`, `mFogFar`)
and never re-derives from the palette tables.** Reading the final values
inherits all four for free and keeps working if the game changes; re-deriving
would mean reimplementing all four and keeping them in sync forever.

| # | Layer | Effect |
| :-- | :-- | :-- |
| 1 | Palette 4-way blend | time-of-day × colpat transition, `wether_pat0` → `wether_pat1` on `pat_ratio` |
| 2 | `dKy_addcol_fog_set` | additive colour offset |
| 3 | `now_fogcol_ratio` | scales fog colour; lightning pulses it |
| 4 | `dKy_fog_startendz_set` | start/end override with a blend ratio |

("kytag" = *kankyo tag*: `d_a_kytag00`…`d_a_kytag17`, invisible actors that
override environment state for the area they sit in.)

> **There was a layer 5 here, and it was the same blend counted twice.**
> Corrected 2026-08-11. The table used to carry "Gather colpat blend — a
> **second**, independent palette blend". There is no second blend. The
> `*Gather` fields are a **staging area** — the write port that
> `dKy_change_colpat`, `dKy_custom_colset` and the kankyo tags use — and
> `exeKankyo` empties it into layer 1 once a frame, clearing each field back to
> its sentinel. There is exactly one `pat_ratio` and exactly one blend on it.
>
> Two things a future reader will want: `mColPatMode` selects whether staged
> values are applied every frame and *held* (which also stops
> `setLight_palno_get` advancing `pat_ratio` itself) or only once the previous
> fade has finished, so a tag cannot interrupt a transition in flight. And the
> staging area is **not the only route** — a handful of actors, including
> `d_a_kytag06` and `d_a_kytag01`, assign `wether_pat0`/`wether_pat1` directly
> and skip it. Anything built on "all palette changes go through the gather
> fields" is wrong.

---

## 3. The five regimes

The *mechanism* is verified in code. The **values live in stage `.dzs` data, not
in source**, so per-area numbers have to be read at runtime — see §5.

| Regime | Where | Mechanism |
| :-- | :-- | :-- |
| Field, clear weather | Hyrule Field | straight palette lookup, large ranges; fog as a draw-distance cue tied to the sky |
| Morning fog that fades | Faron Woods | pure palette scheduling — the morning slot carries a nearer range than the day slot and `dKy_calc_color_set` interpolates as `daytime` advances. **No actor involvement, nothing special needed to reproduce it** |
| Dramatic fog banks | Lost Woods / Sacred Grove, `kytag01` | layer 4 override to `-2000` / `200`, weighted by distance from the tag and by view angle — see below |
| Dense interior | Goron Mines, Forest Temple | palette again, near ranges and a strongly tinted `fog_col`. Interiors usually have `hide_vrbox` set, so there is no sky contribution to reconcile |
| Twilight Realm | Palace of Twilight | ordinary palette machinery with amber values, plus the full-screen mono overlay. **Not a special rendering mode** |

**One mapping covers all five with no per-area branching**, which is the design
result worth carrying: the same derivation handles "dense interior heat haze"
and "thin outdoor distance fog", and the dense end is where sizing the froxel
grid from the fog range pays off most.

### `kytag01` marks the CLEAR CENTRE, not the bank

Both of the tag's weights run the **opposite** way from the intuitive model, and
this has misled two rounds of work:

- **Distance** is `0` at or inside `mNamiInnerRange`, ramps between the two
  radii, and is `1` beyond `mNamiOuterRange`. That value is a **lerp weight
  toward the override**, not a density — `1` means *fully* `-2000/200` and `0`
  means the area's own palette fog, untouched. **So the whiteout is strongest
  far from the tag and absent at it.**
- **View angle** does the same: **facing the tag gives the floor (0.2), looking
  away gives 1.0**, and the fourth power saturates it by roughly 60° off-axis,
  so the clear window is narrow.

Two narrowings so this is not overstated. **It is conditional on the tag being
switched on** — with the gate shut, `Execute` never calls `mist_tag_move` at all
and the fog is the palette's everywhere regardless of distance. And **which
switches, and where the tag sits, are not in source**; both come from `.dzs`
data, so the mechanism is verified and the placement is not knowable from this
tree.

*(This regime was attributed to Lake Hylia until 2026-08-11, in twelve passages
across two repos. The game says otherwise twice and independently:
`d_a_kytag01.cpp:1-4` calls itself the Sacred Grove Mist Tag, and its own
`OS_REPORT` at `:202` says 迷いの森 — the Lost Woods — 霧タグ, fog tag.)*

---

## 4. Fog is per-object, and Remix picks one at random

`setLight_bg` / `setLight_actor` write fog **per tevstr** and `GXSetFog` is
issued per draw with those values, so a frame contains many different D3D9 fog
states. The fork's scene manager keeps only the **first non-`NONE` state it
sees** and discards the rest — and which draw is first depends on submission
order, so the fog Remix would take from the capture is unstable frame to frame
and blind to per-room variation.

**That is why the bridge pushes the global `g_env_light` fog rather than relying
on the D3D9 capture.** The global values are the room's authoritative
environment fog, and taking them removes the lottery. **The cost is that genuine
per-object variation is flattened** — a live limitation, recorded as compromise
C7 in the Remix doc, not a solved problem.

---

## 5. What the bridge pushes, and how to read it

All under `rtx.dusklight.env.`, all `NoSave`, written by
`src/dusk/remix_bridge.cpp` every frame. **The authoritative key list is the
fork's `rtx_dusklight_env.h`**, where each readout's own description says what
it is; only the fog and sky group is summarised here, and only for the three
entries that are not the obvious field.

Pushed: `fogActive`, `fogColor`, `fogStartZ`, `fogEndZ`, `skyHidden`,
`skyColor`, `kasumiInner`/`kasumiOuter`, `kumoTop`/`kumoBottom`/`kumoShadow`,
`colpat`, `colpatPrev`, `colpatBlend`, `moyaMode`, `moyaCount`.

**`kasumiOuter` is the *near* band and `kasumiInner` the *far* one** — the
reverse of the English reading. The game labels them 霞手前 / 霞奥 in its palette
exporter and `kasumiF` / `kasumiB` in its debug view. This is the worked example
behind `japanese-naming-remix.md` §4's rule that a struct member name is a hypothesis.

**`skyHidden` recomputes the colour-sum test rather than reading
`g_env_light.hide_vrbox`.** That flag is written by the sky dome *actor*, so in
any stage with no such actor — which is every interior, exactly where the
question matters — it is never updated and still holds whatever the last outdoor
area left there.

**`colpat` is `wether_pat1`, and it is only one third of the answer.** The live
blend is the trio `wether_pat0` → `wether_pat1` on `pat_ratio`, and until
protocol 13 only `wether_pat1` crossed. Outside a transition `colpatPrev ==
colpat` and `colpatBlend == 1.0`, which is exactly why the old single push
looked complete: **the missing two thirds are constant except in the moments
they decide something.**

**`fogActive` is computed**, because the game has no fog-enable flag at all and
leaves the distances unclamped:
`isfinite(near) && isfinite(far) && far > near && far > 0`. A *negative*
`mFogNear` is normal rather than broken — scripted fog banks set it that way
deliberately — and the predicate accepts it.

### The measurement pass, and how to take the kytag01 reading

These are displayed live in the overlay's Readouts tab, and **that readout is
the only practical way to learn the real per-area values**, because the numbers
live in `.dzs` data. §3's mechanisms are verified; its *values* are not. Record
`fogStartZ` / `fogEndZ` / `fogColor` at Hyrule Field (dawn, noon, dusk, night),
Faron Woods, the Lost Woods / Sacred Grove (`F_SP117`), Lake Hylia, the Goron
Mines, the Forest Temple and the Palace of Twilight.

**The kytag01 reading is the opposite of the obvious one.** The tag sits in a
clear centre and the fog is strongest *away* from it, so **stand where the fog
is thinnest and walk outward**, watching the range slide toward `-2000` / `200`.
Walking *into* the thick part to find the tag will not find it. Two things worth
capturing on the way: the two radii are visible in the numbers (the values stop
changing past `mNamiOuterRange` and are pinned to the palette inside
`mNamiInnerRange`), and **turning on the spot at a fixed position** isolates the
view term from the distance term.

If the numbers never leave the palette values anywhere in the area, the tag's
gating switches are off rather than the readout being broken.

**Take it in the default `fogRampMode` 1, or record the mode** — mode 2 floors
`froxelRangeScale` at 1 and does not use `zHalfMin` on the view ray at all, so a
walk-out taken there measures neither setting. Full recipe:
[`remix-test-playbook.md`](remix-test-playbook.md).

---

## 6. Moya — the haze particles are a separate system

**Moya** is 靄, mist. It is **not** GX fog: billboard particles driven by
`g_env_light.mMoyaMode` / `mMoyaCount`, pushed as readouts and not consumed yet.
Layering a billboard haze on top of a dense medium double-counts, so what to do
with it is an explicit decision rather than something that arrives for free.

What is currently built, and what is verified in game, is in
[`remix-open-issues.md`](remix-open-issues.md) — not here.
