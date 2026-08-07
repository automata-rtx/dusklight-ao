# Effect lights — Remix sphere lights at the origin of the game's own effects

How Dusklight decides that a fire, a flame or a glow deserves a real light in
the path tracer, where that light goes, and what colour and strength it gets.

**This file is the stable design reference for the system.** It changes when
the design changes. What is broken right now lives in
[`remix-open-issues.md`](remix-open-issues.md); how to run a test session lives
in [`remix-test-playbook.md`](remix-test-playbook.md).

Related reading, in the order it becomes useful:

| I want to… | Read |
| :-- | :-- |
| Understand the environment system this sits next to | [`kankyo-remix.md`](kankyo-remix.md) |
| Understand why a *surface* glows rather than a *light* existing | `extern/aurora/docs/dx9/remix-material-interface.md` §9 |
| Change a setting or add one | `dxvk-remix/documentation/DusklightOverlay.md` |
| Know what the sun/moon and sky already contribute | `dxvk-remix/documentation/DusklightAtmosphere.md` |

---

## 0. What this replaces, and why

Until this system existed, the bridge mirrored the game's **own registered
point lights** into Remix: everything an actor handed to `dKy_plight_set` or
`dKy_efplight_set` became a sphere light at that light's position
(`remix_bridge.cpp`, "Local point lights"). That works, in the sense that
interiors stop being black.

It also faithfully reproduces every **faked placement** the original lighting
model got away with. A GameCube point light is a per-vertex term with a soft
`1/(1 + 10·D²/r²)` falloff and no shadowing, so the artists could put it
wherever the *shading* looked best — offset from the flame, inside geometry,
floating above an object, one light standing in for three. None of that reads
as wrong under a rasterizer. All of it reads as wrong under a path tracer,
because a path-traced light casts real shadows from the exact point it
occupies, and the eye immediately sees that the shadow does not come from the
fire.

So the placement has to come from something that is *physically* where the
light is. In this game that thing exists and is exact: **the origin of the
effect that draws the fire.**

The old mirror is retained, off by default, as a comparison path. §8.

---

## 1. The one idea

> **The game already decides, every frame, where fire and glow exist and
> whether they are on. It expresses that decision as JPA emitters. Read the
> emitters.**

Not the actors — there are 800 of them and the fire is a detail in each.
Not the particles — a fire is drawn as dozens of short-lived quads spawned and
recycled continuously, and a light per particle would be a strobing mess. The
**emitter** is the thing in between: one long-lived object, sitting at the
world position the effect is generated from, which the game creates when the
fire starts and destroys when it stops.

That is exactly the anchor asked for. It is also the *only* one that is
correct for every case at once:

| Case | Where the emitter sits | How it turns off |
| :-- | :-- | :-- |
| Bonfire (`d_a_obj_maki`) | actor origin, five emitters `ZI_S_maki_fire_a…_ind` | actor sets a hit flag, stops re-setting them |
| Link's lantern | `mKandelaarFlamePos`, a member tracking the lamp's flame point | `daAlink_c::setLight` calls `stopDrawParticle` when the oil runs out |
| Fire arrow | the arrow's tip transform | emitter dies with the arrow |
| Torch being carried | moves with the carrier, because the actor re-sets the emitter's position every frame | dropped/extinguished → emitter released |

None of those needed a line of per-case code. They all fall out of one sweep.

---

## 2. Where the data is

Every fact below was read in source; citations are to this repo unless the
path says otherwise.

### 2.0 There are two spawn paths, and only one of them gives an emitter each

This is the single most important implementation fact, and it is not visible
from the API's shape.

- **Level and one-shot effects** — `dComIfGp_particle_set` →
  `dPa_control_c::set` (`src/d/d_particle.cpp:1471`, `:1736`) — create **one
  `JPABaseEmitter` per instance**, which the actor then keeps and re-positions
  every frame. Bonfires, Link's lantern, arrows, the wall torch `ktOnFire`.
- **"Simple" effects** — `dComIfGp_particle_setSimple` → `dPa_simpleEcallBack`
  (`src/d/d_particle.cpp:811`) — share **one emitter per effect ID across the
  whole world**. Each instance appends its position to a 32-slot array
  (`mData`), and once a frame `executeAfter` teleports the single emitter to
  each stored position in turn, spawning particles there
  (`src/d/d_particle.cpp:732-761`), then resets the count to zero.

The Forest Temple and Goron Mines torch stands are all the second kind
(`d_a_obj_lv1Candle00.cpp:171`, `d_a_obj_lv1Candle01.cpp:136`, `d_a_obj_lv2Candle.cpp:261`,
`d_a_obj_lv3Candle.cpp:153`, `d_a_obj_TvCdlst.cpp:157`). **A sweep of the emitter table sees
exactly one of them — whichever was drawn last — and its position is
meaningless.** Ten torches in a room would produce one light in a random one
of them.

So the system reads both: the emitter table for the first kind, and a copy
taken inside `dPa_simpleEcallBack::set` for the second, where each instance is
still its own thing. The recorder is a fixed-size array that saturates rather
than growing, and is inert unless the system asked for it.

**The ordinary wall torch is the worked example, and it is worth having in
full** — it is the most common fire in the game, and it exercises three
separate parts of this design at once. `d_a_ep::ep_move` emits **three** effects
at one position, all through `dComIfGp_particle_setSimple`
(`d_a_ep.cpp:495`, `:498`, `:512`):

| Variant 0 | Variant 1 | Resource | Role |
| :-- | :-- | :-- | :-- |
| `0x0100` | `0x8110` | `ZI_J_O_fire_a.jpa` | fire A |
| `0x0101` | `0x8111` | `ZI_J_O_fire_b.jpa` | fire B |
| `0x0103` | `0x8112` | `ZI_J_O_kagerou.jpa` | 陽炎, heat haze |

(Resource names from `d_particle_name.cpp`, via
`extern/aurora/docs/dx9/unsupported-effects.md`, which catalogues these three
for a different reason — the white-circle investigation.) The variant is
per-instance and comes from the actor's spawn parameter: **only** when bit 3 is
set does the actor compute `field_0x60c = ((param & 7) + 1) & 1`, giving 0 or 1
(`d_a_ep.cpp:888-891`); otherwise that branch is not taken and it stays 0
(`:893-897` sets a different field). So variant 1 is the opt-in case, and both
variants can be alight in one room.

Three things follow, and all three are checkable in one classification report:

1. **The hook is load-bearing, not defensive.** Every torch in the game takes
   the simple path. Without the `dPa_simpleEcallBack::set` recorder, a room of
   ten torches would light exactly one of them, in a position that changes
   frame to frame.
2. **Merging is the normal case, not an edge case.** Fire A and fire B sit at
   the same point, so §6 has to collapse them or every torch gets two lights.
   The 60-unit merge radius has no work to do here — the positions are equal.
3. **The heat haze is the rule's nearest miss.** `kagerou` is a distortion
   effect at the same point as real fire. If it passes §3 it costs nothing
   (it merges into the same site); if it fails, that is the rule working. Which
   it does is **not known** — it is the first line to look for in the report.

### 2.1 The emitter table is central and complete

`dPa_control_c` owns one `JPAEmitterManager` for the whole game
(`src/d/d_particle.cpp:1215`):

```cpp
mEmitterMng = JKR_NEW_ARGS (mHeap, 0) JPAEmitterManager(3000, 250, mHeap, 0x13, 2);
```

— 3000 particles, **250 emitters**, 19 groups, 2 resource banks. Reachable
from anywhere as `dComIfGp_getParticle()` / `dPa_control_c::getEmitterManager()`
(`include/d/d_com_inf_game.h:3086`, `include/d/d_particle.h`).

Live emitters hang off `pEmtrUseList[groupID]`, a `JSUList<JPABaseEmitter>`
per group (`libs/JSystem/include/JSystem/JParticle/JPAEmitterManager.h:45`).
The iteration idiom is the manager's own
(`libs/JSystem/src/JParticle/JPAEmitterManager.cpp:116`):

```cpp
for (JSULink<JPABaseEmitter>* pLink = pEmtrUseList[group_id].getFirst();
     pLink != pEmtrUseList[group_id].getEnd(); pLink = pLink->getNext()) { ... }
```

So the whole world's effects are **250 pointers away**, with no actor
iteration, no hooks in 800 files, and no chance of missing one.

### 2.2 What an emitter tells us

`JPABaseEmitter` (`libs/JSystem/include/JSystem/JParticle/JPAEmitter.h:227`):

| Member | Meaning here |
| :-- | :-- |
| `mGlobalTrs` | **the effect origin, in world space.** This is the light's position. |
| `mPrmClr`, `mEnvClr` | the effect's authored colour, as animated this frame |
| `mGlobalPrmClr`, `mGlobalEnvClr` | the per-emitter colour/alpha multiplier the actor set |
| `mStatus` | `StopDraw`, `StopEmit`, `StopCalc`, `Delete`, `Immortal` |
| `pRes` | the `JPAResource`; `pRes->getUsrIdx()` is the **effect ID** |
| `pRes->getBsp()` | the `JPABaseShape`: blend mode, size, colour flags |
| `getParticleNumber()` | live particles right now |
| `mMaxFrame` | `0` = continuous emitter, `>0` = finite |

The effective drawn colour is `mPrmClr ⊗ mGlobalPrmClr` for the TEV primary
register and `mEnvClr ⊗ mGlobalEnvClr` for the environment register — read
straight out of `JPABaseShape.cpp:23-36`, which is what the draw actually
loads. So "the colour of the fire" is available as data, exactly.

### 2.3 The effect ID space is global and named

`pRes->getUsrIdx()` returns the same `u16` the game passes to
`dComIfGp_particle_set` (`JPAResourceManager.cpp:24` matches on it). The room
bank is selected by bit `0x8000` (`dPa_RM`, `dPa_control_c::getRM_ID`,
`src/d/d_particle.cpp:1199`) — **the low 13 bits are a single global
namespace** of 3201 effects, enumerated in `include/d/d_particle_name.h` and
named in `src/d/d_particle_name.cpp`:

```
ID_ZI_S_MAKI_FIRE_A      0x204   "ZI_S_maki_fire_a.jpa"
ID_ZI_J_KANTERA_FIRE     0x2BC   "ZI_J_kantera_fire.jpa"
ID_IT_JN_ARWFIR_FIRE00   0x058   "IT_JN_arwFir_fire00.jpa"
```

`dPa_name::getName()` is compiled into every configuration, not just DEBUG.
That matters for the logging (§7) more than for the decision.

### 2.4 The vanilla lights are still there, and are still the best source of
### colour — and sometimes of reach

There are **two** registries, and reading only the first misses most of the
game's torches.

**Point lights.** `LIGHT_INFLUENCE` (`include/d/d_kankyo.h:17`) is
`{ cXyz mPosition; GXColorS10 mColor; f32 mPow; f32 mFluctuation; int mIndex; }`,
registered into `g_env_light.pointlight[100]` and `efplight[5]`
(`d_kankyo.h:245`) through `dKy_plight_set`. A bonfire's is authored right next
to its fire (`d_a_obj_maki.cpp:226`) — which is what makes it a good *example*
of the authored values, and a bad example of a light to adopt: it is cut again
one frame later, and in Bulblin Camp never registered at all. Hole 3 in §10
has the reading:

```cpp
a_this->mLightObj.mPosition = a_this->current.pos;
a_this->mLightObj.mColor.r = 0xaf;  // AF5D00 — the game's own bonfire colour
a_this->mLightObj.mColor.g = 0x5d;
a_this->mLightObj.mColor.b = 0;
a_this->mLightObj.mPow = 500.0f;
dKy_plight_set(&a_this->mLightObj);
```

`mPow` **is** a reach in world units: `dKy_light_influence_id` treats "closer
than `mPow`" as "inside this light" (`d_kankyo.cpp:924`) and the actor shading
fades linearly to nothing at exactly `mPow` (`d_kankyo.cpp:3536`). The torch
family's authored values are consistent — colour `BC6642`, `mPow` 500,
`mFluctuation` 1.0, at the flame point **+10 in Y** — repeated verbatim across
`fireWood`, `fireWood2`, `ktOnFire`, `lv1Candle00/01`, `lv3Candle`, `poCandle`,
`TvCdlst`, `timeFire`.

**Spot lights.** `BOSS_LIGHT field_0x0c18[8]` (`d_kankyo.h:260`), written by
`dKy_BossLight_set` (`d_kankyo.cpp:10028`) and, for slot 0,
`dKy_WolfEyeLight_set` (`d_kankyo.cpp:10213`). `field_0x26` is a **per-frame
liveness flag** — whoever registers a light sets it, and `exeKankyo` clears it
at the top of the next frame.

**We read six of the eight slots, and that is not an off-by-one.** Three loops
have to agree and all three stop at 6:

| Slot | Written by | Cleared by `exeKankyo`? |
| :-- | :-- | :-- |
| 0 | `dKy_WolfEyeLight_set` — Link's lantern, wolf eyes. It writes `field_0x0c18[var_r29]` with `var_r29` fixed at 0 (`d_kankyo.cpp:10223-10225`) | yes |
| 1 … 5 | `dKy_BossLight_set`, which allocates in `[1, 6 - stage_light_info_num)` (`d_kankyo.cpp:10064`) | yes |
| 6, 7 | **nothing** | **no** — its loop runs `i < 6` (`d_kankyo.cpp:4768`) |

Slots 6 and 7 are written by neither setter and cleared by nobody, so their
liveness flag never expires. Reading them would resurrect a light from a room
you left. `gatherVanillaLights` stops at 6 for that reason, and the loop
carries a comment saying so.

The `6 - stage_light_info_num` bound is worth its own line, because it is a
**real capacity limit rather than a formality**: `stage_light_info_num` is the
current room's own authored light count, clamped to 0..6
(`d_kankyo.cpp:10044`). A room with six authored lights leaves the range empty,
and **no BossLight torch in it can register at all**. So "this torch has no
vanilla light" is not always an authoring choice — sometimes the registry was
simply full. One more reason the position had to stop coming from the registry,
and one more thing `effLightsOrphans` will be counting.

This list is not optional. Every candle actor has a stage parameter that
chooses between the two registries, and when it picks this one
`dKy_plight_set` is never called at all (`d_a_obj_fireWood2.cpp:144`,
`d_a_obj_lv1Candle00.cpp:144`, `d_a_obj_lv1Candle01.cpp:121`, `d_a_obj_lv2Candle.cpp:229`,
`d_a_obj_poCandle.cpp:124`, `d_a_obj_TvCdlst.cpp:137`, `d_a_obj_lv3Candle.cpp:149`, and the carried
torch at `d_a_obj_carry.cpp:1687`). **Link's lantern is here too**, at slot 0, with
its position already at the flame — `dKy_WolfEyeLight_set(&spB8, …)` where
`spB8 = mKandelaarFlamePos` (`d_a_alink.cpp:14957`) — and colour
`(181, 112, 40)` from `daAlinkHIO_huLight_c1`
(`d_a_alink_HIO_data.inc:1627`).

**Colour is taken from both lists; reach only from the first.**
`BOSS_LIGHT::mRefDistance` *is* loaded into the same GX distance attenuation
`mPow` is (`d_kankyo.cpp:9077`), which reads as "it is a reach" — but the
callers disagree. The lantern passes a 0..1 ramp chasing
`daAlinkHIO_huLight_c1::mPower = 1.0f`, and the BossLight torches pass their
own 0..1 intensity ramp (`d_a_obj_fireWood2.cpp:144`). Reading that as world
units would make every one of them black. So the field is used as what its
callers plainly mean by it — an on/strength signal — and those sites take
their reach from the settings while still taking the game's colour.

We keep every one of those *colours* and throw away every *position*. That is
the whole trade: the game's artists were right about how orange a torch is,
and wrong — for a path tracer, which casts a real shadow from the exact point
the light occupies — about where to put it.

---

## 3. The rule: which emitters earn a light

The temptation is a list of effect IDs. This project's standing rule is
[translate, don't tag](../CLAUDE.md#1-translate-dont-tag), and a list of 3201
IDs is a tag table with extra steps — it would be wrong the first time an
effect is reused, and nobody would ever finish auditing it.

So the decision is a **rule over data the game already carries**, in the same
shape as the material self-illumination rule that this project already
settled on (`aurora-ao/docs/dx9/remix-material-interface.md` §9):

> An emitter earns a light when it is
> **drawn** (the game is showing it right now)
> **AND additive** (its blend adds light to the frame rather than covering
> what is behind it)
> **AND its colour reads as a glow** (saturated, or near-white-hot)
> **AND it is not excluded** (§4).

Each clause, and what it is doing:

**Drawn.** `!(mStatus & JPAEmtrStts_StopDraw)`, `!(mStatus & Delete)`,
`getParticleNumber() > 0`, and effective alpha above a floor. This one clause
is why the lantern needs no special case: `daAlink_c::setLight`
(`src/d/actor/d_a_alink.cpp:14848`) gates the flame on
`dComIfGs_getOil() != 0 && !checkNoResetFlg2(FLG2_KANDELAAR_LIGHT_OFF) && …`
and calls `stopDrawParticle(field_0x31c4)` when that fails. The oil meter is
already wired to `StopDraw`; we just read it.

**Additive.** `JPABaseShape::getBlendMode() == GX_BM_BLEND &&
getBlendDst() == GX_BL_ONE` (`JPABaseShape.h:74-76`). This is the renderer's
own statement that the effect *emits* rather than *occludes*: fire, glow,
sparks and lava blend additively; smoke, dust, water and splashes do not.
It is the same distinction the material rule draws with "no TEV colour stage
reads the rasterized channel", arrived at from the other end.

`ONE` and nothing else. A destination factor of `SRC_ALPHA` or
`INV_SRC_ALPHA` scales the background *down*, which is how smoke covers things
up, so accepting those would buy a few more fires at the price of lighting
every puff of dust — and a wrong light is visible where a missing one is only
dim. §7's report names the effects this refuses, so the trade is measurable
rather than argued.

**Reads as a glow.** Chroma or luminance of `prm ⊗ globalPrm` /
`env ⊗ globalEnv` above a threshold. **The same two functions and the same
default thresholds as the fork's material rule** — unnormalized chroma
(brightest channel minus dimmest) and Rec.601 luma, 0.50 and 0.70
(`rtx_dusklight_emissive.h`). That is the point: it is one judgement asked in
two places, and an earlier revision of this file used normalized chroma and
Rec.709 and so was quietly asking a different question with the same words.
Thresholds rather than constants, for the same reason §9's are: they are a
judgement about this game's palette.

**⚠ The additive clause is inference, not measurement.** It is read from the
JPA format's semantics and from what `setGX` does with it, *not* from having
inspected this game's `.jpa` assets — those are not in this repo. §7 exists
precisely so the first test session settles it: the classifier logs its own
inputs and verdict for every distinct effect it sees, so one play session
produces the ground truth for the whole game and the rule can be corrected
from data rather than from argument.

### 3.1 Class, offset and defaults come from the name

The rule above decides **whether**. The effect's own name decides **what kind**,
which drives the vertical offset and the fallback radius/reach — and, for one
value only, whether a light exists at all:

| Class | Name evidence | Why it is a separate class |
| :-- | :-- | :-- |
| `Lava` | `lava`, `magma`, `youdo`, **`yogan`, `yougan`** | large, dim, wide |
| `Excluded` | `yoda`, `taieki` | **refused outright.** The game names it as a substance that is never a light source |
| `Burst` | `bakuha`, `explo`, `bomb`, `baku` | one-shot; **off by default**, because a two-frame light reads as a flicker |
| `Fire` | `fire`, `honoo`, `kaen`, `taimatsu`, `maki`, `kantera`, `torch`, `flame`, `ablaze`, `kagarib` | wants a small upward offset — the emitter sits at the fuel, the light belongs in the flame |
| `Glow` | `hikari`, `light`, `kira`, `pika`, `aura`, `shine`, `glow`, `spark` | no offset; usually smaller and cooler |
| `Other` | anything else that passed §3 | global defaults |

**Two corrections to what this table said before 2026-08-07**, both of which
mattered:

- It said class "never decides whether a light exists". That is no longer true —
  `Excluded` decides. Nothing else does.
- `lava`/`magma`/`youdo` match **zero** of the game's 3,205 effect names. This
  game spells it **yogan/yougan**, so `Class::Lava` was unreachable dead code
  and every lava column was classified `Other`. It also listed `hit` as Burst
  evidence, which the code never had.

**The table is in precedence order, and that order is load-bearing**, because a
great many names carry two of these words. Each step was derived by replaying
the lists over all 3,205 names rather than chosen by ear:

- **`Lava` outranks everything.** `yoganshibuki` is lava *splash* — splash being
  exactly what a negative list wants, and this one molten. Nothing may override
  a name that says the substance is hot. No name collides today; the invariant
  is what stops a future addition to `Excluded` putting out the lava.
- **`Excluded` outranks `Burst`.** Three names: `ZI_S_bq_bombdamageYodare_a/b/c`,
  drool off a bomb-damaged creature. That is drool, not an explosion.
- **`Burst` outranks `Fire`.** Ten names, e.g. `ZF_S_bombRoom00_fire`,
  `ZF_S_HBomb02_fire00`. This one predates the rest and is deliberate. Moving
  `Fire` above `Burst` turns every explosion into a persistent fire — which is
  exactly what happened while this section was being written, and the test suite
  caught it.

Reading the effect's *name* is not tagging in the sense rule 1 forbids: the
name is the game's own identity for the effect, shipped in the game's own
table, at exactly the granularity the game itself uses. Tagging is hashing an
asset the game never named and hand-authoring an answer.

---

## 4. Exclusions — and which ones you may want to overrule

Five exclusions are structural and should stay:

1. **Not drawn** (§3). The game's own on/off, for free — and as of 2026-08-07
   this means all four of the things the game does to turn an effect off, on
   **both** collection paths rather than only on the sweep. `emitterIsLive`
   is the single implementation: `StopDraw`/`Delete`, zero particles, global
   alpha below `minAlpha`, and **global particle scale at zero**.

   That last one and the both-paths part are the fixes for two of the three
   defects seen in game on 2026-08-07. `collectSimple` applied *none* of these
   gates, so wolf-only dig markers — which the game hides by setting the shared
   emitter's alpha to zero (`dPa_fsenthPcallBack`, `d_particle.cpp:1955`) — were
   lighting the ground in Hyrule Field while invisible. And `d_a_e_db` hides the
   Deku Baba's drool by ramping global particle scale to zero
   (`d_a_e_db.cpp:1871-1877`) while leaving alpha at `0xFF` for the emitter's
   whole life, so no alpha test could ever have caught it.
2. **2D and menu groups.** Emitters in the groups drawn by `draw2Dgame`,
   `draw2Dfore`, `draw2Dback`, `draw2DmenuFore`, `draw2DmenuBack` are screen
   space — a "position" for them is meaningless. Excluded by group ID.
3. **Non-additive.** §3.
4. **Named as a substance that is never a light** — `Class::Excluded`, §3.1.
   Two words, `yoda` and `taieki`, 48 names between them, both unambiguous:
   drool and body fluid.

   This one exists because of an observation, and it is worth recording what it
   cost the design. The Deku Baba was seen lighting rooms from its jaw joints on
   2026-08-07. Its drool reaches the rule through the *level* path, where the
   real resource colour is used — so it genuinely passed `additive && glow`,
   which means **drool is authored additively** (inference from it passing, not
   read: the `.jpa` is not in this repo). §3 leans on additive blending as the
   thing that separates fire from smoke. A wet surface is authored additively
   too, so it reads as glossy. **Additive means "does not occlude what is behind
   it", which is true of a flame and equally true of saliva** — the clause is
   weaker evidence of emission than §3 claims.

   Kept deliberately narrow for that reason. The plausible next words are much
   bigger hammers — `smoke` alone is 161 names, `sand` 121, `shibuki` 69 — and
   widening this is a decision to take with a classification report in hand, not
   from a list of words that sound like substances. `effLightsExcluded` counts
   what this refuses each frame; a non-zero count in a room that reads under-lit
   is the signal it is too wide.
5. **Distance and budget.** Beyond `maxDistance` from the camera, or past
   `maxLights` this frame, ordered by weight. A light that contributes nothing
   still costs a light-manager entry and an RTXDI slot. Both settings treat
   **0 as "no limit"**, which is worth knowing before dragging `maxLights` down
   to turn the system off — that is what `effectLights` is for, and 0 does the
   opposite. The budget bounds what is *returned*, grace-period sites included,
   not merely what is refreshed.

The last two are judgement calls, and are **options** rather than constants because
they are the ones most likely to be overruled:

6. **One-shot bursts are off by default.** An explosion's fire emitter lives
   for a handful of frames. A light that appears and vanishes inside a fifth
   of a second is a flash — sometimes exactly right (a bomb *should* flash),
   often a flicker artefact. Turn `effectLightBursts` on to include them.
   *This is the exclusion most likely to be wrong for this game*: TP's bomb
   and Ball-and-Chain impacts are dramatic enough that a flash may read well.
7. **Vanilla lights with no effect at all are not forwarded.** §5 adopts a
   vanilla light's *parameters* when it corroborates an effect. A vanilla
   light with no effect near it is the case the old mirror got wrong — it is
   where the faked placements live — so by default nothing is emitted for it.
   `effectLightOrphanPolicy` can turn them back on, either all of them or
   only those outside any effect cluster.
   *You may want to overrule this*: some real light sources in this game are
   pure `LIGHT_INFLUENCE` with no particle at all (dungeon fill lights,
   glowing crystals, Midna). Those go dark under the default. The honest
   position is that we do not yet know how many there are — the readout in
   §7 counts them so the first session tells us.

---

## 5. Parameters: derive from the game where the game knows, configure where it does not

Each **site** (§6) resolves its parameters in this order.

### 5.1 Adopt a vanilla light — "derived"

If one of the game's own lights (§2.4, either registry) lies within
`adoptRadius` of the site, adopt its colour. If it came from the point
registry, adopt its `mPow` as the reach too and mark the site *derived*.
Nearest wins; a given light is adopted by at most one site.

The readouts report the two separately (`effLightsDerived` counts reach,
`effLightsVanilla` counts what was available from each registry), because
"took the game's colour" and "took the game's reach" are different amounts of
trust and it should be visible which one a scene is running on.

Radiance uses the same solve the local-light mirror already uses, so numbers
tuned there carry over unchanged:

```
radiance = reach² · kNewLightEndValue / (π · radius²) · intensityMultiplier
```

`kNewLightEndValue = 0.01` is Remix's own "still perceptible" threshold
(`rtx_lights.h`), not a knob of ours. `reach` is `mPow`. The
`derivedIntensity` multiplier defaults to **19**, which is not a fudge: the
game loads `dKy_GXInitLightDistAttn(info, mPow * 0.001f, 0.99999f,
GX_DA_STEEP)`, i.e. `attenuation(D) = 1 / (1 + 10·D²/mPow²)`, so `mPow` is
where the light falls to a ninth of peak, and applying Remix's end threshold
to that curve puts the real reach at ≈4.3× `mPow` — ≈19× the radiance. The
same number was reached independently by testing on 2026-07-29.

### 5.2 Otherwise, derive colour from the effect and size from settings —
### "undetermined"

Colour comes from the emitter (§2.2). Between the primary and environment
registers, the one with **more chroma** is used, falling back to the primary;
a near-black environment colour is ignored. This is the case the fire arrow
lands in — enemy fire arrows register no `LIGHT_INFLUENCE` at all, so the
orange comes from `IT_JN_arwFir_fire00`'s own palette.

Radius and reach come from the per-class settings, scaled by
`undeterminedIntensity`. **Both multipliers exist and are separate on
purpose**: the derived path's job is to map the game's units onto Remix's
scale, and the undetermined path's job is to pick a size out of nothing. They
will not want the same number, and tying them together guarantees that tuning
one breaks the other.

### 5.3 Vertical offset

`position.y += verticalOffset(class)`. The emitter sits where the effect is
*generated* — for a totem that is the top of the pole, at the base of the
flame — and the light belongs a little way up inside the flame. One setting
per class, in world units, applied after adoption so it applies to derived and
undetermined sites alike.

---

## 6. Sites: one light per fire, not one per emitter

A bonfire is five emitters at one point (`d_a_obj_maki.cpp:56`):

```cpp
static u16 eff_id[] = {0x8204, 0x8205, 0x8206, 0x8207, 0x8208};
```

`ZI_S_maki_fire_a`, `_b`, `_c`, `_d`, `_ind` — flame core, layers, embers.
Five lights at one point is five times the cost for none of the benefit, and
their alphas animate independently, so the sum flickers.

So candidates are **clustered**: emitters within `mergeRadius` of each other
merge into one *site*. The site takes the position of its highest-weight
member (not the centroid — a centroid drifts as members come and go, and a
light that drifts has visible shadow swim), and the colour of that member.

Site identity has to be **stable across frames** or Remix re-creates the light
every frame: a Remix light is keyed by hash, and a changing hash is a new
light with no temporal history. Sites are therefore tracked like the existing
local lights are — matched to last frame's sites by proximity and class, and
given a persistent id on first sight. A site whose members all disappear is
destroyed after a short grace period, which also stops a fire that is
re-spawned every few frames from churning.

---

## 7. Instrumentation — the part that makes the next iteration cheap

Per [rule 2](../CLAUDE.md#2-a-question-we-would-have-to-ask-the-owner-is-a-defect-in-the-logging),
this ships with the logging that answers the questions we would otherwise have
to ask, because several of §3's claims are inference and one play session can
settle all of them.

**Per-frame readouts**, pushed as `rtx.dusklight.env.*` and shown in the
overlay's Dusklight tab:

| Readout | Answers |
| :-- | :-- |
| `effLightsEmitters` | how many emitters were alive |
| `effLightsCandidates` | how many passed §3 |
| `effLightsSites` | how many sites after clustering |
| `effLightsDrawn` | how many reached Remix |
| `effLightsDerived` | how many adopted a vanilla light |
| `effLightsOrphans` | vanilla lights with no site near them — **the number that decides whether §4.6's default is right** |
| `effLightsCulled` | dropped by distance or budget |

Bursts are recorded in the report even though they are excluded from lighting,
because that report is the thing meant to settle whether excluding them is
right. Each request also **clears the memory**, so a press covers everything
seen since the last press rather than filling once and then capping for the
rest of the session.

**One press, five sections, every open question answered.** *Log Full Effect
Light Report* in the Dusklight tab (`effectLightReportCommit`, an action
option). The design goal is literally "send the log and nothing else is
needed" — if a question about this system cannot be answered from one press,
that is a defect in the report rather than a question for the owner.

| Section | What it settles |
| :-- | :-- |
| **counters** | the whole chain, plus the bridge's own `creates`/`destroys` — which were counted since the system landed and printed **nowhere** until 2026-08-07. `creates` counts light *updates*, so it is the number that prices an animating light |
| **effects** | one line per distinct effect since the last press: name, blend configuration, colours, the **measured** chroma and luma the rule cut on, which **keyword** picked its class, and a verdict naming *which clause* refused it — `no(opaque)` / `no(colour)` / `no(name)` / `LIT-if-bursts`. Plus the **animation configuration**, so you can see whether an effect's colour is even *capable* of animating, and `maxFrame`/`life`/`age`/`particles` for how long it lives |
| **sites** | every light this frame: position, how many emitters merged into it, and **the distance to the game light it adopted** — the one number the burst design turns on and which had never been measured |
| **game lights** | every light the game registered and which effect took it. Adoption is **exclusive**, so this is what shows a short-lived effect stealing a torch's light and leaving the torch 19× dimmer |
| **trace** | a rolling ring of how each light changed over the last few seconds |

Two properties worth knowing:

- **The trace is RETROSPECTIVE.** Do the thing first, *then* press. A line is
  written only when a site appears, goes, or its radiance moves more than 2% —
  the bridge's own update threshold — so a steady torch is one line and an
  explosion is many. That is what lets a 512-entry ring cover minutes of play
  instead of eight frames, and it is why you do not have to arm a trace and
  hope the timing lands.
- **The effects memory clears on each press; the trace does not.** A press
  covers every effect seen *since the last press* rather than filling once and
  capping for the session. Clearing the trace too would mean two presses in a
  row lose the very thing the second was asking about.

Bursts are recorded even though they are excluded from lighting, because that
report is the thing meant to settle whether excluding them is right. So is
`Class::Excluded` — a wrong exclusion shows up as a line rather than as a room
that quietly went dark.

Every section is capped and says so when it truncates. Nothing in the report
is read back for a decision; it is display-only, and `setBridgeCounters` exists
only to let the bridge hand over numbers it owns.

**Regression signature.** If the additive clause is wrong in the permissive
direction, the symptom is lights on smoke and water spray — look for sites
whose class is `Other` and whose colour is grey. If it is wrong in the strict
direction, the symptom is a fire with no light and `effLightsCandidates` far
below `effLightsEmitters`; the report will name the effect that was rejected.

---

## 7.1 One interaction worth knowing about

An additive particle draw already gets a small emissive contribution from Remix
without this system: `calculateAlphaState` classifies additive blending as
`emissiveBlend`, and `rtx.enableEmissiveBlendEmissiveOverride` (default on)
gives it a flat `rtx.emissiveBlendOverrideEmissiveIntensity` of 0.2
(`rtx_instance_manager.cpp`, `rtx_options.h`). That branch sits *above* the
Dusklight material rule in the same `else if` chain, so **a fire sprite is
already glowing a little, and the Dusklight self-illumination rule never sees
it.**

This does not conflict with anything here. That path makes the *sprite* emit;
this system makes a *light* that casts shadows and reaches surfaces the sprite
does not touch. But it does mean a fire is never completely unlit even with
this system off, which is worth knowing before concluding from a screenshot
that effect lights are working.

---

## 7.2 Why this is the only fine-grained light indoors

Worth stating once, because it sets how much these lights are carrying.

The scene has three light sources. The **sun/moon distant light** is gated on
`dKy_SunMoon_Light_Check()`, so it is off in interiors and in twilight. The
**generated sky** is outdoor fill only — and per
`DusklightAtmosphere.md` §14.1, Remix has no dome light type at all, so a sky
contributes *only through ray miss* and is never NEE-sampled. The **ambient
grade** is off by default and has never been reached in a test.

An effect light is a **sphere** light, which is NEE-sampled like any other. So
indoors, these are not a garnish on top of an existing lighting solution —
between them and whatever the mirror is doing, they are the lighting solution.
That is the argument for getting their placement right, and also the reason
`effLightsOrphans` matters: every registered light this policy drops in an
interior is light nothing else replaces.

---

## 8. What happens to the old mirror

`rtx.dusklight.game.localLights` stays exactly as it is, defaulting off. It is
the comparison path: turning it on and this system off reproduces the previous
behaviour, which is the only way to judge whether a placement improved.

They are **not** meant to run together — every fire would get two lights, one
of them in the wrong place. Nothing enforces it, because "both on" is a
legitimate thing to look at once. But **inheriting** it is not: anyone who
tuned the old mirror has `localLights = True` saved, and the first launch after
this lands doubles every fire, which reads as the new placement being wrong.
The overlay therefore says so loudly whenever both are enabled, rather than
leaving it in a paragraph.

---

## 9. Settings

All under `rtx.dusklight.game.*`, read by the game every frame, with a
matching entry in the game's own `config.json` so a value survives without
Remix. The overlay hosts them in the Dusklight tab.

| Setting | Default | What it does |
| :-- | :-- | :-- |
| `effectLights` | on | the system |
| `effectLightIntensity` | 1.0 | **multiplies every light this system makes**, derived and undetermined alike — the master brightness |
| `effectLightDerivedIntensity` | 19.0 | multiplier for sites that adopted a vanilla light (§5.1) |
| `effectLightDerivedRadius` | 10.0 | emitter radius for those, world units |
| `effectLightUndeterminedIntensity` | 1.0 | multiplier for sites with no vanilla light |
| `effectLightUndeterminedReach` | 400.0 | how far an undetermined light should reach, world units |
| `effectLightUndeterminedRadius` | 8.0 | emitter radius for those |
| `effectLightFireOffset` | 15.0 | upward offset for `Fire` sites |
| `effectLightGlowOffset` | 0.0 | upward offset for `Glow` sites |
| `effectLightMergeRadius` | 60.0 | how close two emitters must be to become one site |
| `effectLightAdoptRadius` | 250.0 | how close a vanilla light must be to be adopted |
| `effectLightMaxLights` | 32 | per-frame budget |
| `effectLightMaxDistance` | 12000.0 | cull distance from the camera |
| `effectLightBursts` | off | include one-shot effects (§4.5) |
| `effectLightVolumetric` | 1.0 | how much a light contributes to fog relative to surfaces; above 1 a flame hazes the air without getting brighter on the walls |
| `effectLightMinChroma` | 0.50 | the "reads as a glow" thresholds (§3) — same functions and defaults as the material rule |
| `effectLightMinLuma` | 0.70 | |
| `effectLightReportCommit` | action | dump the classification report (§7) |

The same names exist in the game's own `config.json` under `game.*`, which is
what a value falls back to when Remix's option is unreachable — except
`effectLightReportCommit`, which the bridge reads directly and which has nothing
to fall back to, being an action rather than a value.

**`orphanPolicy` is deliberately not in that table.** §4.6 describes it and §10
lists it under what is not built; it exists only as a field on the internal
`Params` struct (`effect_lights.hpp:77`) that nothing writes and nothing reads.
It is **not** an option: not declared in the fork, not a `ConfigVar`, not in the
overlay, not settable from `rtx.conf`. An earlier revision of this table listed
it with a default, which would have sent somebody looking for a switch that does
not exist. `tools/check-remix-protocol.py` now fails on that class of mistake.

---

## 10. What is verified and what is not

Following [rule 5](../CLAUDE.md#5-say-what-was-verified-and-what-was-not).

**Tested in game 2026-08-07 — it works, and then it found three real defects.**
The first report was "it works"; a longer session named three places where it
lit something it should not have. All three are fixed below, and the design lost
an assumption in the process — see §4 exclusion 4.

| Reported | Cause, read from source | Fix |
| :-- | :-- | :-- |
| Hyrule Field: lights stuck in the ground, no visible source | Wolf **dig-spot markers**. `d_a_obj_digholl.cpp:76-80` and `d_a_obj_digplace.cpp:108-113` request `ZI_J_O_digTga_a`/`_b` every frame at ground level for every dig spot within 1000 units of the player, ungated on wolf form. The game hides them by setting the shared emitter's alpha to zero, and `collectSimple` checked no liveness state at all | §4 exclusion 1: one `emitterIsLive` for both paths |
| Gerudo Desert: sand-swimming enemies emit light | `d_a_e_sw.cpp`, the **Sand Worm** — 15 `ZM_S_SandWorm*` effects, all `Class::Other`, all spawned at alpha `0xFF` | **not fixed.** Needs the report: whether these are additive is unreadable here |
| Baba: lights at odd parts | `d_a_e_db.cpp`, the **Deku Baba**. Its three drool emitters are anchored to the **jaw joints** (`p_idx[] = {2,2,6}` = `MOUTH_1, MOUTH_1, MOUTH_2` per `assets/GZ2E01/res/Object/E_db.h`), a drip effect goes on the ground under `current.pos` — which for a Baba is the *head* — and a body-fluid effect at stalk node 4 of 12 | §4 exclusion 4, plus the zero-scale gate |

Two further defects turned up while reading, neither reported:

- **`Class::Lava` was unreachable.** `lava`/`magma`/`youdo` match zero of 3,205
  names; the game spells it `yogan`. Every lava column was `Class::Other`.
- **The simple path's colour test was a tautology.** `recordSimple` stored the
  caller's global colour and never multiplied in the resource's own, unlike the
  sweep — and all 27 `dComIfGp_particle_setSimple` call sites pass
  `g_whiteColor`. So the colour judged was literally `(1,1,1)`: chroma 0.00,
  luma 1.00, `readsAsGlow` unconditionally true. **`isAdditive` was deciding
  alone on the entire path that carries every wall torch in the game.**

That last one is why the obvious first move — tighten the colour thresholds —
would have done nothing for the reported ground lights and could have put out
the bonfire while leaving the torches lit.

It is worth being exact about what the original "it works" settles, because it
is easy to read it as more than it is.

*What it does settle:* lights appear, at the effect origins, and the result is
good enough that the owner is merging it. Since the system produces nothing at
all unless the rule accepts an emitter, **the additive clause is no longer pure
inference** — this game does author its fire the way §3 assumed, at least for
whatever was on screen.

*What it does not settle*, and what the readouts and the report exist for:

- **which** effects were accepted and which were refused. No classification
  report has been read. An effect that never lights still looks like an effect
  that has no light, and only the report tells them apart.
- whether the **spot registry** is being adopted in practice — `effLightsVanilla`
  answers it, and nobody has looked. The reading in hole 2 below says it should
  be; a reading is not a measurement.
- how many of the game's own lights the orphan policy is dropping
  (`effLightsOrphans`), which is the number that decides whether §4.6's default
  needs its escape hatch.
- whether any **default** is right rather than merely acceptable. None was
  tuned; §9's values are what shipped.
- the specific cases the design was written around — the lantern, a fire arrow,
  a bonfire's five emitters collapsing to one — were not individually confirmed.

None of that is a reason to hold the merge. It is the list §3b of the test
playbook exists to work through on a session where somebody is looking for it,
and every item on it is answerable from the Dusklight tab or one log line.

**Read in source, cited above.** The emitter table's completeness and reach.
`mGlobalTrs` being the effect origin. The colour path. The effect ID space and
its names. `StopDraw` being what the lantern's oil gates. Both light
registries, their contents, the authored torch values, and the per-frame
liveness flag on the spot list. `mPow` being a real radius. That `dPa_RM`'s
`0x8000` bit selects a bank rather than a namespace. The two spawn paths and
the fact that the simple one shares an emitter. The blend-mode accessors.

**CI-green on both sides** at the matching protocol-7 pair — dusklight
`bf87551c`, dxvk-remix `70a6d482`. (dusklight's run reads "failure" because its
MSVC **arm64** job was cancelled without ever being assigned a runner; every
other config passed. `CLAUDE.md` records that state, because it looks exactly
like a broken build and is not one.)

**Compiles, on two harnesses, neither of which is the real build.**

`tools/syntax-check-remix.sh` cross-compiles `effect_lights.cpp`,
`remix_bridge.cpp` and `d_particle.cpp` with **MinGW**, against the game's own
headers. MinGW matters rather than being a detail: the bridge is wrapped in
`#if defined(_WIN32)`, so a native Linux `g++` preprocesses every light system,
every option read and every API call away and then reports success. That is
precisely what happened on 2026-08-06 — four compile errors in
`remix_bridge.cpp` reached CI because the local check had never seen the file.
The script is checked against those four: it catches the bogus type, the
missing include, MSVC's capture-less-lambda rule, and a wrong format-string
argument count. It is `-fsyntax-only` under GCC, so it will not catch every
MSVC-ism and nothing at link time; **CI remains the authority.**

`tests/effect_lights/run.sh` compiles the module against *stub* headers and
runs it under ASan and UBSan. It **cannot** catch a stub that has drifted from
the real declaration — only the reading above says those match, which is why
the MinGW check exists alongside it. 45 assertions cover: a bonfire's five emitters merging to one
site; opaque smoke at the same point adding nothing; a grey additive effect
being rejected and a white-hot one accepted; a nearby game light being adopted
for colour and reach *without* moving the light off the effect origin; a
distant one being counted as an orphan instead; site identity surviving a
member joining; `StopDraw` extinguishing a light; bursts excluded by default;
the budget dropping and reporting; three torches sharing one emitter still
getting three lights; records not surviving into the next frame; a
fire-coloured but non-additive effect still being refused; the grace period
holding a light rather than letting it be destroyed and rebuilt; and `reset()`
dropping everything at once.

**Inference, not measurement.** That fire and glow effects in *this game* are
authored with an additive destination factor and smoke is not (§3) — this is
the load-bearing one, and §7's report exists to settle it in one session.
That 60 units groups a bonfire's five emitters without grouping two
neighbouring torches. That 32 lights is a sensible budget.

**Known holes, in the order they are likely to bite.**

1. **A callback-driven emitter's position may be stale.** `dPa_control_c::set`
   only re-positions an existing emitter when its level callback is null
   (`src/d/d_particle.cpp:1766`). The lantern's swing emitter is fine — its
   `dPa_hermiteEcallBack_c` sets the translation itself — but the other
   `dPa_levelEcallBack` subclasses were not checked. Signature: a light left
   behind where an effect used to be.
2. ~~**Whether the spot registry is readable at bridge time is unknown.**~~
   **Resolved by reading, 2026-08-06.** Processes execute in ascending list-ID
   order (`cTrIt_Method`, `c_tree_iter.cpp:12-21`, over the 16 lists of
   `g_fpcLn_Queue`). Kankyo is **list 1** (`d_kankyo.cpp:8420`); the torch
   actors are **list 3** (`d_a_obj_lv1Candle00.cpp`, `d_a_obj_fireWood2.cpp`, `d_a_obj_maki.cpp`) and
   Link is **list 5** (`d_a_alink.cpp`). So `exeKankyo` clears the flags before
   any of them run, the actors set them, and `dusk::remix::tick()`
   (`m_Do_main.cpp:327`) runs after all of it. The flags are live.
   `effLightsVanilla` is kept anyway — it costs nothing and it turns a reading
   into a measurement, which this project has reason to prefer.
3. **A burning bonfire has no registered point light at all**, by two
   independent routes, and the reading is stronger than "may" —
   `d_a_obj_maki.cpp` says so twice.

   *It cuts the light it just registered.* Create registers one at `:232` and
   then calls `daObj_Maki_Execute` at the end of create (`:235`). The
   `field_0x57e == 0` branch of Execute — `57e` is 0 exactly while the fire is
   **burning** (`:51`, and it is set to 1 when the fire is hit at `:80` or
   already switched off at create, `:202`) — cuts the light on that very first
   frame: `if (mLightObj.mPow > 0.1f) { dKy_plight_cut(...); mPow = 0.0f; }`
   (`:107-110`). Create set `mPow` to 500 at `:230`, so the guard passes.

   *And in Bulblin Camp it is never registered.* The `dKy_plight_set` at `:232`
   sits behind `strcmp(dComIfGp_getStartStageName(), "F_SP118") != 0 &&
   field_0x57e != 1` (`:225`). `F_SP118` is Bulblin Camp
   (`src/dusk/map_loader_definitions.h:277`). The five fire emitters at `:56`
   (`0x8204`-`0x8208`) carry **no** such check — they spawn on the burning
   branch on every stage.

   So a Bulblin Camp bonfire burns with five fire emitters and nothing in the
   registry, and every other bonfire burns with five emitters and a light that
   was cut a frame after it was made. This is the clearest single case for the
   system existing, and it is why bonfires run *undetermined*: §5.2's settings
   supply their size, and their colour comes from the effect.

   Read directly from source and not observed running; whether it matches
   retail or is a decomp artefact is **unverified**. `effLightsOrphans` and the
   classification report settle it in one session.
4. **Lights lead geometry by one frame.** Actor draw methods enter models into
   J3D draw buffers; the GX/D3D9 commands for them are issued by
   `mDoGph_Painter` at the *start* of the next iteration
   (`f_pc_manager.cpp:75`). `dusk::remix::tick()` runs at the end of the
   current one. So the D3D9 stream Remix builds its scene from carries state
   from frame *k-1* while the lights submitted alongside it carry state *k*.
   For anything static this is invisible. For a swinging lantern the light
   leads the lamp by one frame — 16 to 33 ms — which is named here so it is
   recognised rather than rediscovered. It applies to the local-light mirror
   and the celestial light equally; it is a property of the frame, not of this
   system.
5. **`d_a_obj_lv3Candle` never calls `dKy_plight_set` at all** — it fills a
   `LIGHT_INFLUENCE` and only ever cuts it — and spawns its flame
   unconditionally. A permanently burning torch with no registered light is
   exactly the case the old mirror could not see, and it is the strongest
   single piece of evidence for this system existing.

**Not verified at all:** every default in §9 beyond the two inherited from the
local-light mirror. They are starting points chosen to be visible rather than
correct.

**Not built yet:**

- The `orphanPolicy` setting described in §4.6 is plumbed through `Params` but
  does nothing — orphans are counted and dropped. It is there so the readout can
  prove whether it is needed before the behaviour is written.
- **Flicker.** `LIGHT_INFLUENCE::mFluctuation` (§2.4) is read past and ignored,
  exactly as the old mirror ignored it. It is 1.0 on every torch and 100 on
  bombs, so it carries little signal on the surfaces that get lights, and
  honouring it would defeat the bridge's radiance epsilon by definition — every
  flickering light would re-enter the light manager every frame. That is a cost
  question rather than a quality one **on our fork**, since
  `addExternalLight` now carries the RTXDI buffer index across an overwrite; on
  a stock Remix runtime it would also be permanent temporal noise. Listed here
  because "the game flickers its torches and we do not" is a real difference
  from vanilla, and one nobody has yet decided is wrong.
