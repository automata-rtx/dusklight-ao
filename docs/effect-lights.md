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

### 3.1 Class comes from the name — and what Class is actually for

The rule above decides **whether**. The effect's own name decides **what kind**:

| Class | Name evidence | Why it is a separate class |
| :-- | :-- | :-- |
| `Lava` | `lava`, `magma`, `youdo`, **`yogan`, `yougan`** | 溶岩 *yogan*, lava. **Not "large, dim, wide"** — see the correction below |
| `Excluded` | `yoda`, `taieki` | **refused outright.** The game names it as a substance that is never a light source |
| `Burst` | `bakuha`, `explo`, `bomb`, `baku` | one-shot; **off by default**, because a two-frame light reads as a flicker |
| `Lantern` | `kantera` | **Link's lamp, and only it.** カンテラ *kantera*. The one class that can be given settings of its own — §5.4 |
| `Fire` | `fire`, `honoo`, `hono`, `kaen`, `flame`, `taimatsu`, `maki`, `torch`, `ablaze`, `kagarib` | wants a small upward offset — the emitter sits at the fuel, the light belongs in the flame |
| `Spark` | `kira`, `pika`, `spark` | きらきら *kirakira*, glitter. Sub-second and tiny where a glow is steady and soft. **Takes Glow's offset and Glow's weight**, so the split moves nothing today |
| `Glow` | `hikari`, `light`, `glow`, `aura`, `shine` | no offset; usually smaller and cooler |
| `Other` | anything else that passed §3 | global defaults |

**`Lantern` and `Spark` were added on 2026-08-13**, and the whole blast radius
was measured rather than estimated: replaying both classifiers over all 3,205
names in `d_particle_name.cpp` before and after, **exactly 26 names move** — 5
from `Fire` to `Lantern`, 21 from `Glow` to `Spark` — and **not one name enters
or leaves `Excluded`, `Burst`, `Lava` or `Other`.** Re-run that scan rather than
guessing if either list is touched again.

**The `Lava` row's old description, "large, dim, wide", was wrong** and is
corrected here rather than quietly deleted. The 21 names in that class are lava
*pillars* (柱 `bashira`), *embers* (火の粉 `hinoko`) and *splash* (飛沫
`shibuki`) — localised columns and sparks, not an area source. **There is no
lava-surface emitter at all:** the pool itself is a material, handled by the
fork's self-illumination rule, not by this system. Every number around that row
was right; only the prose was wrong, which is the failure mode
`scripts/check_invariants.py` cannot catch.

**Class feeds exactly five things**, four of them since 2026-08-11 and the
fifth since 2026-08-13. All five live in **one table now**, `kClassTable` in
`effect_lights.cpp` — it replaced three separate `switch` statements that had to
be kept in step by hand and had already drifted once, which is how the wrong
entry below survived as long as it did:

1. **Two gates.** `Excluded` refuses a candidate outright; `Burst` is skipped
   unless `effectLightBursts` is on. Nothing else about the class decides
   whether a light exists.
2. **The vertical offset** (`classOffset`). `Fire`, `Lantern` and `Burst` take
   `effectLightFireOffset`, `Glow` and `Spark` take `effectLightGlowOffset`, and
   `Lava`, `Other` and `Excluded` take nothing.
3. **The merge tie-break** (`classWeight`, §6). The highest-weight member of a
   site donates its position, colour and effect id: `Fire` 4 = `Lantern` 4 >
   `Lava` 3 > `Glow` 2 = `Spark` 2 > `Burst` 1.5 > `Other` 1. **Equal weights
   fall through to the lower effect id**, which is exactly the rule two members
   of one class always used — so the two new ties are decided by a fixed number
   rather than by sweep order. `Lantern` gains +1 and outranks `Fire`, but
   **only while `effectLightLanternSeparate` is on**: otherwise Link standing at
   a bonfire would hand the site to his lamp for no reason.
4. **Site identity across frames.** A site only matches last frame's site if the
   class agrees, so a name that changed class would start a new site — and a new
   Remix light hash, losing that light's temporal history. **This is why
   `Lantern` is keyed on the word and not on an effect id:** the game destroys
   one emitter and creates another every time Link swings the lamp
   (`d_a_alink.cpp:14875` vs `:14883`), and both names carry `kantera`.
5. **One class can be solved separately.** `Lantern`, and nothing else — §5.4.

**It does not select the fallback radius or reach for any other class.** That
keys on whether a vanilla light was adopted: `derivedRadius` when one was,
`undeterminedReach` / `undeterminedRadius` when none was, §5. Neither branch
reads the class.

**And `effectLightGlowOffset` defaults to 0.0, the same as what `Other` gets**,
so `Glow`, `Spark` and `Other` currently behave identically in all five. Worth
knowing before spending time on which of the three a name lands in: at stock
settings that question cannot move a pixel. It becomes a real question only if
the glow offset is turned up.

**Ten of the thirty keywords match none of the game's 3,205 effect names** —
`lava`, `magma`, `youdo`, `bakuha`, `honoo`, `hono`, `taimatsu`, `kagarib`,
`pika`, `shine` — and are deliberately left in the source. An earlier version of
this section said three; that was the `Lava` row alone.
`scripts/check_invariants.py` (`effect-light-keywords`) now owns the list,
replays every keyword over `d_particle_name.cpp` on each push, and fails **in
both directions** — a new keyword that matches nothing, and one of these ten
starting to match.

**They are not romanization misses, so do not "fix" them by adding spellings**
([`japanese-naming.md` §3](japanese-naming.md#3-romanization-is-inconsistent--this-is-the-grep-trap)).
Each was re-checked in kunrei-shiki, in Hepburn and in the obvious variants, and
every spelling is zero: `taimatsu`/`taimatu`, `kagarib`/`kagari`/`kagaribi`,
`honoo`/`honou`/`homura`, `pika`/`pikari`/`pikapika`,
`bakuha`/`bakuhatsu`/`bakuhatu`, `youdo`/`yodo`. The game used English (`fire`
181, `bomb` 171, `glow` 59, `spark` 33, `torch` 1) or a different Japanese word
(`kira` 9, 薪 `maki` 8, カンテラ `kantera` 5, 火炎 `kaen` 4).

Removing them is not worth doing either. A keyword that matches nothing
classifies nothing, so deleting all ten would change not one effect's class —
it would be a diff against a shipping classifier that buys a shorter list, and
the check is what actually stops the next one going unnoticed.

**One further correction, from 2026-08-07.** This section used to say class
"never decides whether a light exists" — `Excluded` does, and nothing else. It
also listed `hit` as `Burst` evidence, which the code never had.

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
- **`Lantern` outranks `Fire`.** All five `kantera` names contain `fire` too, so
  below `Fire` the branch would be unreachable and the report would go on
  printing keyword `fire` for the lantern — which is precisely the gap that made
  a lantern-only setting unbuildable before 2026-08-13.
- **`Burst` outranks `Lantern`.** No name collides today; the ordering is stated
  so the invariant "one-shot violence outranks every steady flame" survives the
  new class.
- **`Spark` sits below `Burst`, deliberately, and this one is a decision rather
  than a fact.** The 20 `ZM_*_BombInsectSpark*` names — the electric bugs — are
  claimed by `bomb` and are therefore `Burst`, and therefore **dark by default**.
  On the evidence they are misclassified: they are persistent, not one-shot.
  Moving `Spark` above `Burst` would fix it and would also light twenty effects
  that are dark today, which is a look change nobody asked for and nobody can
  un-see. The authored persistence now printed in the classification report
  (`persist=Y`, from the authored `maxFrame`) is the measurement that should
  settle it — **it is measured today and acted on by nothing.**

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
   bigger hammers, and widening this is a decision to take with a classification
   report in hand, not from a list of words that sound like substances.
   `effLightsExcluded` counts what this refuses each frame; a non-zero count in a
   room that reads under-lit is the signal it is too wide.

   > **⚠ Size any candidate in BOTH romanizations before you add it.** This is
   > the one list where a name puts a light *out*, so a word counted in one
   > spelling excludes half its effects and leaves the rest still lighting the
   > room — and the failure is invisible, because what you get is a partial
   > result rather than an error. The tree mixes kunrei-shiki and Hepburn for the
   > same word;
   > [`japanese-naming.md` §3](japanese-naming.md#3-romanization-is-inconsistent--this-is-the-grep-trap)
   > is the rule and `si↔shi`, `tu↔tsu`, `ti↔chi`, `sya↔sha`, `zi↔ji` is the
   > substitution.
   >
   > Counts over all 3,205 names, re-checked 2026-08-11, both spellings each:
   >
   > | Candidate | The obvious spelling | The one that gets missed | Together |
   > | :-- | --: | --: | --: |
   > | 飛沫 *shibuki*, spray | `shibuki` 69 | `sibuki` 0 | 69 |
   > | 雫 *shizuku*, droplet | `shizuku` 29 | `sizuku` 26 | **55** |
   > | smoke | `smoke` 161 | 煙 `kemuri` 0 | 161 |
   > | sand | `sand` 121 | 砂 `suna` 1 | 122 |
   >
   > The first two are the kunrei/Hepburn split; the last two are the other half
   > of the same trap, an English word where the Japanese one may also be in use.
   > Each pair here is disjoint, so the totals are sums.
   >
   > **Droplet is the case to remember.** The two sets are disjoint — neither
   > spelling is a substring of the other — so a session that added `shizuku`
   > alone would put out 29 effects and leave 26 doing exactly what it was
   > trying to stop. Spray happens to be safe (the effect IDs spell it Hepburn
   > throughout) but that is luck, not a rule: `dKyr_drawSibuki`, the C function
   > that draws them, is spelled the other way.

   The current list is safe under this rule: `yoda` (45) and `taieki` (3) have
   no kunrei/Hepburn alternative — `yodare` is 31 of the 45, and both words are
   spelled one way throughout.
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

**The headline finding, from a read-only survey of the authored JPA data on
2026-08-13: nothing the artists authored is photometric.** The blocks carry a
colour ramp, a particle size, a spawn volume, an emission rate, a lifetime and a
particle count — and not one of them is a brightness. `rate`, `lifetime` and
`volumeSize` are particle bookkeeping, and their live copies are overwritten by
key blocks and by 119 / 18 / 12 actor setter call sites respectively, so a
mapping from any of them onto a Remix radiance would be a number **this project
invented and then cited the game for**. That is precisely the shape of the three
inferences-recorded-as-findings rule 3 exists to stop.

So the split is deliberate and it is the whole design of this section:

> **Hue, extent and persistence come from what the artists authored. Radiance
> comes from the game's own `LIGHT_INFLUENCE::mPow` where there is one, and from
> settings where there is not.**

`mPow` is the single genuinely photometric authored number anywhere in the game
— a reach in world units, verified at `d_kankyo.cpp:924` and `:3536`, and
consistently 500 across `fireWood`, `fireWood2`, `maki`, `ep`, `lv3Candle` and
`poCandle`. Everything §5.1 does with it stands.

### 5.0 The whole chain, authored value to radiance — written once, here

This is the only place the chain is written out. The classification report
prints it back with the session's live numbers substituted in, so a log can
settle "is a multiplier being applied twice" without anyone reading the source.

```
class     = classifyByName(the effect's own name)                        §3.1

hue       = the adopted game light's colour                              §5.1
            else the effect's AUTHORED colour ramp   (authoredColor, on) §5.5
            else the emitter's live registers                            §5.2

reach     = ( the adopted light's mPow  |  undeterminedReach )
              × mass ^ massExponent
              × effectLightReachScale                                    ← multiplier

radius    = ( derivedRadius | undeterminedRadius ),
              grown to the AUTHORED extent when authoredRadius is on     §5.5
              × effectLightRadiusScale                                   ← multiplier

radiance  = reach² · 0.01 / (π · radius²)
              × ( derivedIntensity | undeterminedIntensity )
              × effectLightIntensity                                     ← multiplier
              , then normalised to the hue's brightest channel

lantern   = when effectLightLanternSeparate is on, a Lantern site takes
            effectLightLantern{Reach,Radius,Intensity} RAW instead —
            no mass boost, and none of the three multipliers above       §5.4
```

**Three global multipliers, one per value this system derives from the game, all
defaulting to 1.0, all applied at one point to both branches.** They exist for
exactly the reason the owner asked for them: an authored value that comes out too
weak or too strong can be corrected while the game runs, without a rebuild.

**One option was retired to get there.** `effectLightDerivedReach` did this job
for the derived branch alone, with the same 1.0 default;
`effectLightReachScale` is the same knob applied to both. **A value set for the
old name in an `rtx.conf` is now inert** — move it. Nothing else was renamed, and
no default changed: at stock settings the chain above computes exactly the
numbers the previous one did.

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

Radius and reach come from the `undetermined*` settings, scaled by
`undeterminedIntensity`. **They are per-*path*, not per-class** — this branch is
chosen by "no vanilla light was adopted", and the site's class is not consulted
here or in the derived branch (§3.1), with the single exception of the lantern
(§5.4). **Both intensity multipliers exist and are separate on purpose**: the
derived path's job is to map the game's units onto Remix's scale, and the
undetermined path's job is to pick a size out of nothing. They will not want the
same number, and tying them together guarantees that tuning one breaks the
other.

The three *global* multipliers (§5.0) are on top of both and are not a
replacement for either.

### 5.3 Vertical offset

`position.y += classOffset(class)`. The emitter sits where the effect is
*generated* — for a totem that is the top of the pole, at the base of the
flame — and the light belongs a little way up inside the flame. In world units,
applied after adoption so it applies to derived and undetermined sites alike.

**Two settings cover eight classes, not one each.** `Fire`, `Lantern` and
`Burst` share `effectLightFireOffset` (15.0); `Glow` and `Spark` take
`effectLightGlowOffset`, which is **0.0** by default; `Lava`, `Other` and
`Excluded` are not offset at all. So at stock settings this is the only class
distinction that changes anything, and it separates fire from everything else —
`Glow`, `Spark` and `Other` land on the same number (§3.1).

### 5.4 The lantern — the one class with settings of its own

**Link's lantern is exactly two effect ids and nothing else**, both spawned from
`daAlink_c::setLight`: `ID_ZI_J_KANTERA_FIRE` = 0x2BC, the still flame
(`d_a_alink.cpp:14883`), and `ID_ZI_J_KANTERA_SWINGFIRE` = 0x362, the swung one
(`:14875`). Both go through `dComIfGp_particle_set` — the level path, one
emitter per instance — at `mKandelaarFlamePos`, the lamp's flame point.

**The identification is exact, not a heuristic.** カンテラ *kantera* is a
loanword, so there is no kunrei/Hepburn variant to miss, and it matches **five**
of the game's 3,205 effect names: the two above, plus `ZI_S_kantera_fire`
(0x2BB), `ZI_S_fs_kantera_a` (0x18B) and `ZI_S_fs_kantera_b` (0x18C), **none of
which has a caller anywhere in `src/` or `include/`** — grepped for both the
`ID_*` constants and the hex ids. The English alternatives were sized before
settling on the word: `lantern` and `ranpu` match zero names, `lamp` matches
four and all four are `ZF_S_k_lampWater*`, which is water. World torches and
candle stands do not share these effects; they use `ZI_J_O_fire_a/b` through the
simple path. The only other actor that touches the lantern flame is the monkey,
and it does it by handing its own matrix to *Link's* lantern
(`d_a_npc_ks.cpp:6610-6621`), producing the same two ids at a different place.

`rtx.dusklight.game.effectLightLanternSeparate` — **off by default**:

- **Off** is today's behaviour, and it is the same code path rather than a copy
  of it. A `Lantern` site is classified, merged, adopted and solved exactly like
  a `Fire` site, **the three global multipliers included**, and its class weight
  is `Fire`'s so the merge tie-break is unchanged too.
- **On**, `effectLightLanternReach` / `Radius` / `Intensity` replace whatever the
  shared chain would have produced, **raw**: the mass boost and all three global
  multipliers are skipped. That is what "its own settings" has to mean to be
  useful — a lantern tuned once stays put while the rest of the world is tuned
  around it.
- **The defaults are the undetermined branch's own** (400 / 8 / 1.0), which is
  where the lantern lands today, so flipping the toggle and changing nothing else
  leaves the light where it was apart from dropping the multipliers.
- **The colour is not overridden either way.** `dKy_WolfEyeLight_set` puts a
  `BOSS_LIGHT` in slot 0 at the same point as the emitter, well inside
  `adoptRadius`, so the lantern adopts the game's own lamp colour — (181, 112,
  40) from `daAlinkHIO_huLight_c0::m`. Nothing in the survey said that colour was
  wrong, so nothing overrides it.

**The trap this design exists to survive:** `dPa_control_c::set` reuses a handle
only while the effect id is unchanged, so switching between the still and swung
flame **destroys one emitter and creates another** (`d_particle.cpp:1755-1786`).
Site identity matches on class and proximity, so a class that flipped on every
swing would mint a new site id and a new Remix light hash each time, and the
lantern would lose its temporal history whenever Link swung the lamp. Keying the
class on the word rather than on an id list covers both ids for free.

### 5.5 What the artists authored, and what is deliberately not read

Two things are read off the loaded JPA blocks — `JPAResource::getBsp()` and
`getDyn()`, parsed once at load and **immutable for the session** — rather than
off the live emitter, whose fields are overwritten every frame by
`JPAResource::calcKey` and by several hundred actor setters.

**Hue, `effectLightAuthoredColor`, default ON.** The authored primary and
environment ramps are `GXColor` tables of exactly `getClrAnmMaxFrm() + 1`
entries, pre-interpolated at load (`JPABaseShape.cpp:1541-1583`) and `NULL`
unless the matching flag is set. The most saturated entry is taken — a fire that
ramps yellow → orange → black should report orange, because the black tail is
the particle dying rather than the colour of the light. Three things move the
*live* register and none of them is the fire changing colour:

1. a global colour animation walking its key frame every frame;
2. the kankyo time-of-day tint the game multiplies into any effect whose
   authored user-work word carries bit 0x20 or 0x40 (`d_particle.cpp:1568-1620`),
   so a torch's colour drifts from dawn to dusk;
3. **the strongest argument** — the shared emitter behind every "simple" effect
   is made continuous when it is created (`d_particle.cpp:807`), so its colour
   cycle free-runs from level load and every torch in the world reads the same
   unrelated phase of it, unconnected to when any of them was lit.

This **changes hue only**: radiance is normalised to the colour's brightest
channel, so no light gets brighter or dimmer from it. It also stops a light
re-entering Remix's light manager every frame — a radiance that moves more than
2% re-creates the light and costs its temporal history, and a fixed hue does not
move. **It is on by default because the live value is actively wrong, not merely
different**, and it has a kill switch for exactly the case where that judgement
turns out to be wrong in game.

**The live colour is still what the ACCEPT TEST reads**, and that must not be
swapped. The game hides an effect by fading its global alpha and its global
colour, so judging "is this drawing light right now" from an immutable authored
value would light effects that are invisible.

**Extent, `effectLightAuthoredRadius`, default OFF.** The authored particle base
size, or the authored spawn volume where that is larger and the volume type is
not `VOL_Point` (whose size means nothing — `JPAVolumePoint` zeroes the offset).
The volume is in emitter-local units, transformed by the emitter's local and
global scale matrices before use, so it is an **extent signal rather than an
exact world measurement** — which is why it may only ever *grow* the sphere,
never shrink it, and is capped at 64 units, the same bound the two configured
radii carry. Radiance is solved to carry to the same reach whatever the radius
is, so this changes softness and near-field falloff rather than range. It is off
by default because that is a judgement about how a fire should look rather than a
correctness fix.

**Not folded in: the emitter's live global particle scale**, even though the
drawn quad is multiplied by it. It is actor-driven at 50 call sites and one of
them ramps it to zero as a fire dies (`d_a_e_db.cpp:1871-1877`), so including it
would put an animating term into the radius — the exact mistake this section
exists to avoid.

**Persistence — measured, printed, and acted on by nothing.** The authored
`maxFrame == 0` means "emits forever", and it is read from the dynamics block
rather than the emitter because `becomeContinuousParticle` forces the live
`mMaxFrame` to 0 on every simple emitter and `becomeImmortalEmitter` does the
same at 42 more sites — so the live field says "persistent" for things that are
not. It appears in the classification report as `persist=Y` and **feeds no
decision**, because the decision it would feed is the `BombInsectSpark` question
in §3.1, and that is a look change to take deliberately with a log in hand.

**Deliberately not derived at all:**

- **radiance from rate × lifetime × particle count.** All three are actor-driven
  and none is photometric. This is the refusal the whole section rests on.
- **the per-particle alpha envelope** (`JPAExtraShape`). Authored and stable, but
  it is a *particle* curve — at emitter level it says nothing about output — and
  `pEsp` is frequently `NULL`.
- **flicker from `LIGHT_INFLUENCE::mFluctuation`.** It is 1.0 on every torch and
  100 on bombs: no signal.
- **`resUserWork` bit meanings.** The word is printed raw and undecoded; the two
  facts already derived from it are used instead.

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
| `effLightsAuthored` | **whether the authored derivations are running at all**, as `colour N  radius N  lantern N`. A colour count of 0 in a room full of *registered* torches is correct — those adopt the game's colour, which wins over both (§5.1). A colour count of 0 with no game lights available means the resources carried no colour, which is a different failure from the setting being off |
| `effLightsClasses` | this frame's sites split by what the game's own name says each effect **is**, as `other N  fire N  lantrn N  glow N  spark N  lava N  burst N  excl N`. `excl` is always 0 here — a refused effect never becomes a site, and `effLightsExcluded` counts those instead. A `lantrn` of 0 while the lamp is lit means the lantern's emitter failed §3's rule that frame, not that the classification is wrong |

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
| **effects** | one line per distinct effect since the last press: name, blend configuration, colours, the **measured** chroma and luma the rule cut on, which **keyword** picked its class, and a verdict naming *which clause* refused it — `no(opaque)` / `no(colour)` / `no(name)` / `LIT-if-bursts`. Plus, since 2026-08-13, an **AUTHORED** group — the effect's own ramp colour, its authored extent and `persist=Y/n` — printed **beside** the live `prm`/`env` columns rather than instead of them, so the difference between what the artists wrote and what the emitter is holding is one subtraction. Plus the **animation configuration**, so you can see whether an effect's colour is even *capable* of animating, and `maxFrame`/`life`/`age`/`particles` for how long it lives |
| **sites** | every light this frame: position, how many emitters merged into it, and **the distance to the game light it adopted** — the one number the burst design turns on and which had never been measured. Since 2026-08-13 each line also says **which source each value came from**: `colour=game/authored/live`, `radius=authored/default`, and `LANTERN` when the site was solved from the lantern's own settings. That is the per-site "authored or defaulted" answer, and without it a hue that came out wrong gave no way to tell whether the authored ramp had even been read |
| **game lights** | every light the game registered and which effect took it. Adoption is **exclusive**, so this is what shows a short-lived effect stealing a torch's light and leaving the torch 19× dimmer |
| **trace** | a rolling ring of how each light changed over the last few seconds |

The **counters** section also prints, since 2026-08-13, this frame's sites split
by class, the three authored-versus-defaulted counts, and **the whole chain from
§5.0 with the session's live numbers substituted in** — so "is a multiplier being
applied twice" is answerable from the log rather than from the source.

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

**The 2026-08-13 rework's own signatures, stated per change so each is
recognised rather than discovered:**

| Symptom | What it means |
| :-- | :-- |
| Fires that were lit go out, or unlit things light up | the **classification** moved something between `Fire` and `Excluded`/`Other`. Only 26 names should have moved, all of them `Fire`→`Lantern` or `Glow`→`Spark`; `effLightsClasses` and the report's class column name them |
| Every fire's hue goes flat, or subtly wrong across a whole area | the **authored ramp** read the wrong entry, or it replaced a colour that was legitimately carrying the kankyo time-of-day tint. Compare the report's AUTHORED rgb against the `prm`/`env` columns on the same line, and turn `effectLightAuthoredColor` off to confirm |
| A light appears where the effect is invisible, in daylight only | the authored colour reached the **accept test**, which it must not — that test reads the live colour precisely because the game hides effects by fading it |
| Lights pulse or strobe, and the bridge's `creates` counter climbs | a per-frame animating value has reached radiance. The authored derivations move this the *right* way, so a new strobe means an animating term was reintroduced, not removed |
| The whole scene is uniformly too bright or too dark | a **multiplier chain applied twice**. The counters section prints the chain with live numbers; check that `reachScale`/`radiusScale`/`intensity` each appear once |
| The lantern's light blinks or re-noises every time Link swings the lamp | the swing changed the site's class, minting a new site id and a new Remix light hash. `effLightsSites` stays flat while `creates` climbs |
| Interiors get dimmer overall, with `effLightsDerived` still non-zero | something replaced the adopted `mPow` on the derived path. That is the one this rework must not do, and the non-zero counter is what makes it easy to miss |
| Fires look softer everywhere | `effectLightAuthoredRadius` is on. It only grows spheres, so this is the expected direction, not a bug |

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

## 8.1 The room's authored lights — a third registry, off by default

Landed 2026-08-12. **CI-green and syntax-checked; never run in game.** It is
off by default and this section explains why that is a decision rather than
caution.

### What they are, and why they are not the mirror

`dScnKy_env_light_c::dungeonlight[8]` (`include/d/d_kankyo.h:259`) is refreshed
**every frame** from the room the player is standing in, out of that room's
`LightVec` stage data — `d_kankyo.cpp:8669-8675`, inside
`dKy_setLight_nowroom_common`. Seven fields cross: `mPosition`,
`mRefDistance`, `mCutoffAngle`, `mAngleAttenuation` (the GX spot function),
`mDistAttenuation`, `mAngleX`, `mAngleY` — plus `mColor`, which is written
separately every frame by the palette blend from `plight_col[i]`
(`d_kankyo.cpp:2486-2500`).

Before this landed **nothing in the port read any of it.** The only other
readers in the whole tree are the game's own debug draw
(`d_kankyo_debug.cpp:788`) and its HIO sliders, and neither is compiled into
any build of this port.

These are **not** the lights §0 and §8 are about. That mirror reads
`pointlight[]` and `efplight[]` — `LIGHT_INFLUENCE`s that *actors* register
through `dKy_plight_set`: torches, lanterns, campfires, Midna. These are placed
by whoever laid the **room** out, in its stage file, and are what lights a
dungeon corridor with no fire in it at all. Three separate registries, and
until now the bridge read two.

> **Do not route this through `DUNGEON_LIGHT::mInfluence`, and this is the one
> trap worth spelling out.** `DUNGEON_LIGHT` embeds a `LIGHT_INFLUENCE` at
> offset `0x2C` — the exact struct the mirror's forwarding loop already speaks,
> so reaching for it looks like a free ride. **The cone fields are at
> `0x18`–`0x24`, outside it.** Routing through `mInfluence` drops the cone
> *structurally*, before it ever reaches any shaping code, and nothing in a
> diff shows it. It is also **dead data**: `mInfluence` is written only inside
> `dungeonlight_init` (`d_kankyo.cpp:1157-1162`) from a table of `y = -99999`
> and colour `{0,0,0}`, and is never re-derived — so anyone who inspects it
> today sees nothing and concludes, wrongly, that the room lights are not
> there.

### The guards are the game's, copied from the sites that set them

Each of these was read where the game applies it, not invented:

| Guard | Where the game does it |
| :-- | :-- |
| room has `LightVec` data at all | `d_kankyo.cpp:8506` (room non-NULL), `:8509-8510` (`getLightVecInfo()` non-NULL) |
| at most **six** slots | `d_kankyo.cpp:8512-8513` — `getLightVecInfoNum()` capped at 6 |
| slots 0–1 are **not room lights** when `dKy_SunMoon_Light_Check()` is TRUE | `d_kankyo.cpp:8629-8645` overwrites both with the sun and moon positions, *after* the room's own were written there. The bridge already submits that light itself |
| a switched-off light | `dKy_lightswitch_check` (`:8483-8497`) returns FALSE and the caller writes `0.000001f` into the reference distance (`:8606-8610`). So one test on `mRefDistance` reproduces the switch gate exactly |
| a black light | the palette can take `plight_col[i]` to zero; a zero colour cannot produce radiance |
| a slot never written | `dungeonlight_init`'s placeholder `y = -99999` is still there |

Slots **6 and 7 are never refreshed** — the refresh loop runs `i < 6`
(`d_kankyo.cpp:8589`) — so those two hold the placeholder for the whole
session. Anyone reading "eight room lights" from the array's size is reading
two that do not exist.

**What this deliberately does not carry, and why it is not an oversight.**
After the room's own lights are placed, `dKy_setLight_nowroom_common` hands any
*still-free* GX light slot to the `BOSS_LIGHT` registry — the twilight and wolf
lights, `d_kankyo.cpp:8714-8724`. Those are written straight into the GX light
slots and **never into `dungeonlight`**, so nothing this reads can see them.
`BOSS_LIGHT` is also the *other* struct in the game that carries a cone, and
§5.1's `gatherVanillaLights` already reads it — for **colour only, deliberately**
(`effect_lights.cpp:1455`). Forwarding it is a separate piece of work with its
own placement question, and is not started.

### The cone — what is transcription and what is approximation

This is the first cone anything in this project has ever had to send, and the
two halves are **not** the same kind of claim.

**Transcription, verified twice each:**

- **The axis.** `dKy_lightdir_set` (`d_kankyo.cpp:565-583`) builds
  `(cos X · cos Y, sin X, cos X · sin Y)` from the two authored angles and only
  then transforms it by `inverseTranspose(view)` for GX — so the vector before
  that transform is **world space**. It is the direction the light *travels*:
  `GXInitLightDir` stores the **negation** of what it is handed
  (`libs/dolphin/src/gx/GXLight.c:204-213`), and independently the game's own
  debug draw (`d_kankyo_debug.cpp:826-832`) draws a beam **from** the light
  position along exactly this vector, 8000 units out. Remix wants the same
  convention — its shaping cosine is `dot(primaryAxis, light-to-surface)`,
  `light_shaping.slangh:60` — so no negation is applied.
- **The angle.** GX puts the zero crossing of every monotone spot function at
  `cos(cutoff)` (`GXLight.c:75-134`), and Remix turns `coneAngleDegrees` into
  `cos(angle)` and cuts there too (`rtx_remix_api.cpp`, `toRtLightShaping`).
  Same number, same meaning.

**Approximation, and nobody has characterised it:**

GX has **four** monotone falloff shapes between the cone edge and the axis and
Remix has **one**.

| GX spot function | Its curve in `cos θ` | What we send |
| :-- | :-- | :-- |
| `GX_SP_FLAT` | `1000·(cos − cosCut)` — a hard step | softness `0`. **Exact** |
| `GX_SP_COS` | `(cos − cosCut)/(1 − cosCut)` — linear | smoothstep over the whole cone |
| `GX_SP_COS2` | that, times `cos` | the same smoothstep |
| `GX_SP_SHARP` | `1 − ((1 − cos)/(1 − cosCut))²` | the same smoothstep |

So the **extent** of the softening is reproduced and its **curve** is not.
`rtx.dusklight.game.roomLightConeSoftness` scales the extent; treat it as a
preference, not a conversion.

`focusExponent` is left at **0, deliberately**. It is not a sharpening term:
Remix mixes the softened falloff *towards 1* by it
(`light_shaping.slangh:97-103`), so raising it makes a cone brighter rather
than tighter, and there is no GX spot function it corresponds to. Inventing a
mapping would have been exactly the confident-and-wrong this project's rule 3
exists to stop.

**Cannot be expressed at all:** `GX_SP_RING1` and `GX_SP_RING2` are dark on
axis and peak at an intermediate angle. Remix's shaping is monotone in the
cosine, so no parameters reproduce a ring. Those lights go out as **unshaped
spheres** — present but wrong — rather than being dropped, on the grounds that
an unlit interior is the worse failure, and `roomLightsUnshapeable` counts how
often that compromise is being made. Whether the game uses them at all is
unknown and is one of the things the log answers.

### The colour is close but not bit-exact, and that is recorded rather than fixed

`dungeonlight[i].mColor` is the environment-wide blend of the palette's
`plight_col[i]` column (`d_kankyo.cpp:2486-2500`), and is what this forwards.
It is **not** exactly what GX ends up loading. When the room's `tevstr` carries
its own light object the game prefers that copy (`:8653-8655`), and that copy is
blended from the **same palette column** but with the tevstr's own `pat_ratio`
and add colour (`:3055-3074`) rather than the environment's. Same hue, slightly
different weight.

Reading the tevstr copy instead would mean tracking which tevstr a given room
light belongs to, for a difference the intensity knob already covers. So this
uses the array whose entire purpose is to hold the current room's light state,
and the discrepancy is written down here rather than chased.

### The intensity derivation is weaker than the mirror's, and says so

The mirror starts from `LIGHT_INFLUENCE::mPow`, which the game really does
treat as a reach. This starts from `mRefDistance` — the room light's authored
`radius` — and that is a **nominal size, not a distance the light stops at**.

The game loads it as `GXInitLightDistAttn(radius, 0.99999f, distFn)`
(`d_kankyo.cpp:8607` and `:8612` fill the slot, `:8472` loads it). With a
reference brightness of `0.99999` the GX coefficients come out at
`k₂ = 1e-5 / radius²` for `GX_DA_STEEP`, so

```
attenuation(D) = 1 / (1 + 1e-5 · (D/radius)²)
```

— the light is still at **99.999 % of peak at the radius**, and would need
about **5000×** it to fall to 1/255. In other words the original room lights
barely attenuate at all; they are a near-flat wash over the room.

A path-traced sphere light falls off physically whatever we set, so **that wash
is not reproducible from this data at any setting.** Expect a bright spot near
the light where the game had an even fill. *That is inference read off the GX
coefficients above, not a measurement* — the play session is what settles it.
`rtx.dusklight.game.roomLightIntensity` starts at the mirror's 19 and is
expected to move.

### The argument against, weighed rather than routed around

§0's criticism applies here in full and is **not answered**: these are authored
placements, a GameCube light casts no shadow, so a room light could be sunk in
a wall or floating over a doorway and nothing would have looked wrong at the
time. A path tracer casts a real shadow from exactly where it sits.

What is different about this registry, and why it was built anyway:

1. It is a **different registry** from the one §0 indicts. §0's argument is
   about lights placed to make a *visible object's* shading read right — a
   flame's light nudged so the flicker falls nicely. A room light has no object
   to be offset from; it is the room's own fill.
2. It is the **only cone data in the game.** `BOSS_LIGHT` also carries one and
   `gatherVanillaLights` reads it for colour only, deliberately
   (`effect_lights.cpp:1455`); nothing has ever sent a cone.
3. Per §7.2, interiors are lit **only** by the effect emitters and Remix's
   fallback. A room with no fire in it has nothing.

**None of that makes it right.** It makes it worth one measurement. Hence: off
by default, with the instrumentation to decide it from a log rather than an
argument.

### Instrumentation — the measurement comes first

Two things, and the first is the important one.

**A survey, logged once per room, whether or not forwarding is on.** The whole
question — six lights a room or two, do they sit on top of the fires the effect
lights already cover, does anything actually carry a cone — is unanswerable by
reading, because the data is in the *stage files* and not in the source. One
session through two dungeons answers it. Bounded per the logging rule: one line
per slot, eight slots, the spot function spelled out **by name**, and a hard
cap of 64 rooms with a notice when it is hit. Sampled **8 frames after**
entering the room, because `getStayNo()` changes as soon as the player crosses
the threshold but `dungeonlight` is not refreshed until the kankyo process runs
— reporting immediately would produce a log confidently describing the room
just left.

**Six readouts**, in the Dusklight tab beside the mirror's:

| Readout | Answers |
| :-- | :-- |
| `roomLightsRunning` | did the submission run at all — separates "switched off" from "nothing here" |
| `roomLightsFound` | how many the room was authored with, as the game counts them |
| `roomLightsDrawn` | how many reached Remix. Lower is normal: switched-off and black lights are skipped exactly as the game skips them |
| `roomLightsTracked` | how many hold a live Remix light |
| `roomLightsShaped` | how many carried a cone — the first number this project has for how much of the game is spotlit |
| `roomLightsUnshapeable` | how many used a ring function and had to lose their cone |

The last two count **submissions**, so they only move while the system is
running; the log answers the same questions with it off.

### Regression signature

- **Every fire gains a second light, offset from the first** → it is
  double-counting with the effect lights, and the room-light half of the pair
  is the one to drop. This is what off-by-default plus `roomLightsFound` /
  `effLightsSites` exist to catch.
- **Shadows arrive from somewhere that is not a visible light** → the authored
  placements do not survive the path tracer, and §0's criticism is real for
  this registry too. **The honest answer is then to say so and leave this off**,
  not to tune around it.
- **A light disappears the moment it gains a cone** → Remix refuses the entire
  light if the shaping fails validation (`RtLightShaping::validateParameters`
  rejects a non-normalized direction), so `CreateLight` returning an error on
  exactly the shaped lights is the signature.
- **Interiors go blinding** → the intensity derivation above; `radius` was not
  a reach.

### Settings

| Setting | Default | What it does |
| :-- | :-- | :-- |
| `rtx.dusklight.game.roomLights` | **off** | the system |
| `rtx.dusklight.game.roomLightIntensity` | 19.0 | scales them; the derivation it corrects is weak, see above |
| `rtx.dusklight.game.roomLightRadius` | 10.0 | emitter radius, world units — changes brightness as well as softness |
| `rtx.dusklight.game.roomLightConeSoftness` | 1.0 | how far the smoothstep reaches into the cone. 0 is a hard edge |

### What is verified and what is not

- **Verified by reading**, with the citation given above in every case: the
  refresh site and its seven fields, all six guards, the axis convention
  (twice, independently), the angle correspondence, the GX spot coefficients,
  the distance-attenuation coefficients, and that nothing else in `src/dusk/`
  reads `dungeonlight`.
- **Inference, marked as such**: that the near-flat GX falloff will read as a
  hot spot under Remix.
- **Unknown until a log**: how many room lights a typical room has, whether any
  carry a cone, whether any use a ring function, and whether they duplicate the
  effect lights.
- **Never run in game.** Nothing here has been seen.

---

## 9. Settings

All under `rtx.dusklight.game.*`, read by the game every frame, with a
matching entry in the game's own `config.json` so a value survives without
Remix. The overlay hosts them in the Dusklight tab.

| Setting | Default | What it does |
| :-- | :-- | :-- |
| `effectLights` | on | the system |
| **`effectLightIntensity`** | 1.0 | **global multiplier — RADIANCE.** Every light this system makes, derived and undetermined alike. Does **not** reach a lantern being solved separately |
| **`effectLightReachScale`** | 1.0 | **global multiplier — REACH.** Replaced `effectLightDerivedReach` on 2026-08-13: same default, same meaning, now applied to **both** branches. A value set for the old name is inert |
| **`effectLightRadiusScale`** | 1.0 | **global multiplier — RADIUS.** Radiance is solved to the same reach whatever the radius, so this changes softness, not range |
| `effectLightMassExponent` | 0.5 | how much a light grows with the amount of fire standing at it — see the note below |
| `effectLightDerivedIntensity` | 19.0 | multiplier for sites that adopted a vanilla light (§5.1) |
| `effectLightDerivedRadius` | 10.0 | emitter radius for those, world units |
| `effectLightUndeterminedIntensity` | 1.0 | multiplier for sites with no vanilla light |
| `effectLightUndeterminedReach` | 400.0 | how far an undetermined light should reach, world units |
| `effectLightUndeterminedRadius` | 8.0 | emitter radius for those |
| `effectLightAuthoredColor` | **on** | take the hue from the effect's own authored colour ramp instead of the emitter's live registers (§5.5). Hue only — nothing gets brighter |
| `effectLightAuthoredRadius` | off | grow the sphere to the effect's authored extent where that is larger (§5.5). Capped at 64 units, never shrinks a light |
| `effectLightLanternSeparate` | off | give Link's lantern its own three values, independent of the shared chain and of all three multipliers (§5.4) |
| `effectLightLanternIntensity` | 1.0 | the lantern's brightness when separated. Ignored when it is not |
| `effectLightLanternReach` | 400.0 | the lantern's reach when separated, world units |
| `effectLightLanternRadius` | 8.0 | the lantern's emitter radius when separated, world units |
| `effectLightFireOffset` | 15.0 | upward offset for `Fire`, **`Lantern` and `Burst`** sites |
| `effectLightGlowOffset` | 0.0 | upward offset for `Glow` **and `Spark`** sites — at the default, all three of `Glow`, `Spark` and `Other` are indistinguishable (§3.1) |
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

**A light now scales with how much fire is actually there.** Until 2026-08-10
nothing did: a five-emitter bonfire and a single candle emitted identically,
which is why large fires read as underwhelming. Each site sums the emitters
merged into it, weighted by their alpha, into a **mass** — 5 for a full bonfire,
1 for a candle, less for anything fading out — and multiplies reach by
`mass ^ effectLightMassExponent`.

The exponent is chosen so the arithmetic means something:

- **0** disables it exactly. `mass^0 == 1`, so every light behaves as it did
  before this existed. That makes it a clean A/B rather than a thing to unwind.
- **0.5**, the default, makes **radiance proportional to mass**, because
  radiance goes as the square of reach. Twice the fire, twice the light — which
  is what summing emitters physically means.
- **1.0** makes reach itself proportional to mass, which grows brightness
  quadratically. Much stronger; there for tuning, not as a default.

**A single full-alpha emitter has mass 1, and 1 to any power is 1**, so every
candle and torch is untouched at every exponent and existing tuning survives.
Only the big ones move.

Mass comes from the emitter's global alpha on the sweep path. On the simple path
each record counts a flat 1.0, because the emitter behind a simple effect is
shared between instances (`isSharedSimpleEmitter`) and its alpha therefore says
nothing about the particular one being measured. The sites section prints both
`mass` and the resulting `boost`, so a log shows whether a bonfire actually
measured as one rather than leaving it to be judged by eye.

**Note what this replaced.** The same alpha used to be multiplied into
`classWeight` to decide which member donated the site's *position*. That made
the position depend on an animating value, so two same-class emitters swapped
the lead whenever their alphas crossed and the light snapped between them by up
to `mergeRadius`. Alpha now scales output and has no say in placement, which is
the right split: alpha is a brightness signal, not a position one.

**Reach and radius are not two ways to say "bigger", and only one of them
changes the shape of the light.** `reach` is a *design* number — the distance
the light is solved to still carry to — and it never crosses the Remix API at
all. `solveIntensity` turns it into a radiance and `p.priority` uses it to order
the `maxLights` budget; those are its only two readers
(`effect_lights.cpp:936`, `:1641`). What Remix actually receives is `radius` and
`radiance`. So raising reach pushes light further **without growing the sphere**,
which is what stops a light inside a wall sconce clipping through the geometry —
that clipping is a radius problem, and the radius is bounded from above by it.

Two consequences worth knowing before tuning, both read from the arithmetic
rather than measured:

- Radiance goes as `reach²`, so `effectLightReachScale` at 2.0 is the same
  brightness as `effectLightIntensity` at 4×. They are **not** independent knobs
  on brightness.
- They are not fully redundant either: reach also feeds the budget sort, so a
  light with more reach outranks a dimmer one when `maxLights` binds. Intensity
  does not enter that.

`effectLightReachScale` multiplies rather than replaces because the game's own
`mPow` is the only thing distinguishing a bonfire from a candle — a fixed reach
on the derived half would flatten every game-authored light onto one size. The
undetermined half has the opposite problem (there is nothing to scale), which is
why `effectLightUndeterminedReach` is an absolute in world units and the
multiplier scales *that*.

**What retiring `effectLightDerivedReach` cost, stated rather than glossed:**
there is no longer a way to scale the derived half's reach *alone*. That was its
only capability the new option does not have, and `effectLightDerivedIntensity`
covers the brightness half of it exactly (radiance goes as reach², so 2× reach ≡
4× intensity) while differing only in the budget sort. If the split turns out to
matter in practice, the honest fix is a second option, not a re-widening of this
one.

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

**Counted mechanically, and now checked on every push (2026-08-11).** The
classifier's 30 keywords replayed over all 3,205 names in
`d_particle_name.cpp`: **10 match nothing** and 20 do, the `Excluded` list is 48
names, and `classKeyword`'s copy of the lists agrees with `classifyByName`'s in
content and order. `scripts/check_invariants.py` (`effect-light-keywords`) holds
all three, and each of its arms was proved to fire by breaking the inputs
deliberately. It changes no behaviour: **no keyword was added, removed or
respelled.** §3.1 says why the dead ten stay.

### The 2026-08-13 rework — what is verified and what is not

**Verified by reading the source, cited in §3.1, §5.0, §5.4 and §5.5.** The
authored colour tables and their exact length and NULL conditions
(`JPABaseShape.cpp:1541-1583`, `:1686-1702`). `getMaxFrame` / `getVolumeSize` /
`getVolumeType` and `VOL_Point == 4` (`JPADynamicsBlock.h:87-89`, `.cpp:143-150`).
The lantern's two effect ids and their single spawn site
(`d_a_alink.cpp:14875,14883`), and that only those two of the five `kantera`
names have any caller in `src/` or `include/`. That `mVolumeSize` passes through
the emitter's local and global scale matrices before use
(`JPAResource.cpp:1342-1355`), which is why the extent is called a signal rather
than a measurement.

**Verified mechanically.** Both classifiers replayed over all 3,205 names before
and after: **26 names move, all `Fire`→`Lantern` or `Glow`→`Spark`, none into or
out of `Excluded`/`Burst`/`Lava`/`Other`.** `classKeyword`'s lists still match
`classifyByName`'s in content and order — the invariants check parses both and
was re-run. The ten dead keywords are unchanged and all ten are still live in the
classifier, so `EFFECT_LIGHT_DEAD_KEYWORDS` needed no edit.

**Verified by compiling.** `tools/syntax-check-remix.sh` (MinGW, the real
headers) passes for `effect_lights.cpp`, `remix_bridge.cpp` and
`d_particle.cpp`, and the protocol sweep passes at 14. **The fork half has no
local compiler; CI is its first.**

**NOT verified — nothing here has been run in game.** In particular:

- **no authored colour has ever been read from a real `.jpa`.** The accessors and
  the table's shape are read from source; that the ramp's most-saturated entry is
  the *right* entry for a light is a design choice, not a measurement. It is the
  single most likely thing to look wrong, and `effectLightAuthoredColor` is the
  switch that isolates it in one press.
- **no authored extent has been seen either**, which is part of why
  `effectLightAuthoredRadius` is off.
- the lantern toggle has not been flipped in game, and the claim that flipping it
  with default values changes nothing but the multipliers is arithmetic, not
  observation.
- `Class::Spark` and `Class::Lantern` have never appeared in a real report.

**Two survey findings deliberately left alone**, recorded so they are not
rediscovered:

- **`tests/effect_lights/run.sh` does not compile**, and has not since this
  module gained its `JPADynamicsBlock.h` include — the stub headers have no such
  file and the stub `JPAResource` has no `getDyn()`. Nothing runs it in CI. It is
  **outside this change's file list**, so it was not fixed here, and this rework
  widens the stub gap further: the module now also calls `getBsp()->isPrmAnm()`,
  `getPrmClr(idx, …)`, `getClrAnmMaxFrm()`, `getMaxFrame()`, `getVolumeSize()`
  and `getVolumeType()`. Repairing that harness is the cheapest way to test any
  of this without a Windows machine and should be the next thing done here.
- **`classify()` does not exist.** Two comments pointed at it; the rule is
  written inline twice, in `collectEmitters` and `collectSimple`. The comments
  are corrected in this change; the duplication is not.

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
