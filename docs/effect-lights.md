# Effect lights — Remix sphere lights at the origin of the game's own effects

Fires, lava and glows get real sphere lights, placed at the **origin of the JPA
effect that draws them** rather than at the position of whatever point light the
game registered. A GameCube point light casts no shadow, so its position was
free to be wrong; a path tracer casts a real shadow from exactly where the light
is. That distinction is the whole system, and it is why the local-light mirror
that preceded this was removed at protocol 17.

The decision layer is in this repo, `src/dusk/effect_lights.cpp`; the bridge
submits the result through the Remix API. **Nothing in the fork classifies
anything.** Verification status for everything here lives in
[`remix-open-issues.md`](remix-open-issues.md), which is the project's single
ledger — do not start a second one in this file.

Section numbers below are cited from code comments; do not renumber them.

---

## 1. The one idea

> **The game already decides, every frame, where fire and glow exist and whether
> they are on. It expresses that decision as JPA emitters. Read the emitters.**

A bonfire, Link's lantern, a fire arrow and a carried torch all fall out of one
sweep with no per-case code. Why emitters rather than actors or particles, and
the two spawn paths that must be read separately, are at
`src/dusk/effect_lights.cpp:20-35`.

## 2. Where the data is

`dPa_control_c`'s emitter list is central and complete: an emitter carries its
world position in `mGlobalTrs`, its live colour registers, its status flags and
its resource id. The effect id space is one flat namespace of 3,205 names in
`d_particle_name.cpp`, which is what makes classification by name possible.

**The vanilla lights are still read, for two facts and not a third.**
`pointlight[]`/`efplight[]` carry an authored colour and
`LIGHT_INFLUENCE::mPow`; they are read for colour and reach and **not** for
position, which is the entire point of the system.

---

## 3. The rule: which emitters earn a light

A list of effect IDs would be a tag table with extra steps — wrong the first time
an effect is reused, and never finishable. So the decision is a rule over data the
game already carries, in the same shape as the material self-illumination rule
(`extern/aurora/docs/dx9/remix-material-interface.md` §9):

> An emitter earns a light when it is **drawn** (the game is showing it right
> now) **AND additive** (its blend adds light rather than covering what is
> behind it) **AND its colour reads as a glow** (saturated, or near-white-hot)
> **AND it is not excluded** (§4).

**Drawn** is why the lantern needs no special case: the oil meter is already
wired to `StopDraw` by `daAlink_c::setLight`, and we just read it.

**Additive** is `GX_BM_BLEND` with destination `GX_BL_ONE`, and `ONE` and
nothing else. Why nothing else, and **why this clause is inference rather than
measurement**, are argued at `src/dusk/effect_lights.cpp:664-690` — better than
here, and at the function that implements it. The short version: the `.jpa`
assets are not in this repo, so this reads the format's semantics rather than
this game's authoring, and §7's report is what settles it.

**Reads as a glow** uses **the same two functions and the same default
thresholds as the fork's material rule** — unnormalized chroma and Rec.601 luma
(`rtx_dusklight_emissive.h`). That is the point: one judgement asked in two
places. An earlier revision used normalized chroma and Rec.709 and so was
quietly asking a different question with the same words.

What the 2026-08-07 session established is that the rule accepts *something*, so
this game does author fire additively. What it did not establish is *which*
effects, because no classification report has been read.

### 3.1 Class comes from the name — and what Class is actually for

The rule decides **whether**. The effect's own name decides **what kind**. There
is no `classify()` function and never was; the rule is written out inline twice,
and `classifyByName` at `src/dusk/effect_lights.cpp:420-445` explains what Class
does and does not feed, why the keyword order is load-bearing, and why reading
the game's own name for its own effect is not "tagging" in the sense the
project's rules forbid. The keyword lists, the dead-keyword counts and the guard
that enforces them are at `:484-553`; the five things a class decides are one
table, `kClassTable`, which replaced three `switch` statements that had drifted.

| Class | Reading | Note |
| :-- | :-- | :-- |
| `Lava` | 溶岩 *yogan* — the game does **not** spell it `lava` | pillars, embers and 飛沫 *shibuki* splash. **There is no lava-surface emitter at all**; the pool is a material, handled by the fork's self-illumination rule |
| `Excluded` | `yoda`, `taieki` — drool and body fluid | refused outright: the game names it as a substance that is never a light |
| `Burst` | 爆破 *bakuha*, explosion | one-shot; **off by default**, because a two-frame light reads as a flicker |
| `Lantern` | カンテラ *kantera* | Link's lamp and only it — the one class that can be given settings of its own |
| `Fire` | 炎 *honoo*, 松明 *taimatsu*, 薪 *maki* | takes a small upward offset: the emitter sits at the fuel, the light belongs in the flame |
| `Spark` | きらきら *kirakira*, plus the Shadow Insect's `elecat` | sub-second and tiny; takes Glow's offset and weight, so the split moves nothing today |
| `Glow` | `hikari`, `light`, `glow`, `aura`, `shine` | no offset; usually smaller and cooler |
| `Other` | anything else that passed §3 | global defaults |

**Measure before touching a keyword list.** `Lantern` and `Spark` were added on
2026-08-13 and the blast radius was measured, not estimated: replaying both
classifiers over all 3,205 names, **exactly 26 names move** and **not one enters
or leaves `Excluded`, `Burst`, `Lava` or `Other`.** Re-run that scan rather than
guessing. The `Lava` row's old description, "large, dim, wide", was simply wrong —
the failure mode `scripts/check_invariants.py` cannot catch.

### 3.2 The Shadow Insect

闇虫 *yami mushi* (`d_a_e_ym.cpp`) — the twilight bug Wolf Link hunts. **Its body
is drawn only under wolf senses; its electric spark is not**, so the spark is the
only sign of the insect in normal view.

**Nothing here newly lights anything** — those eight effects were already ungated
`Class::Other`. They moved to `Class::Spark` so they could be named, counted and
switched, so `effectLightSparks` is an **undo** switch, on by default.
`effectLightSparkHold` is a renderer setting rather than a look setting: the
shortest spark window is 5–15 frames, shorter than the base grace period, so
without it a bouncing bug destroys and re-creates its light every time and never
accumulates any temporal reuse.

**What is not known from source, and must come from a log:** whether these
effects pass the additive-and-glow rule at all — the `.jpa` assets are not in
this repo. `effLightsSparks` reports `seen N lit N`; `seen > 0, lit == 0` is the
negative result, and if that is what comes back, **classification is not the
lever**: record what the rule saw and leave the names alone. Neither this insect
nor the Tear of Light calls `dKy_plight_set` at all, so both land on the
*undetermined* branch.

**The Tear of Light is measured and deliberately NOT built.** Its emitters
already classify `Glow` and `Other`, neither gated, so it may already be lit and
the only open question is a log rather than a design. Bundling it with the spark
would have put two look changes behind one switch and made neither judgeable.

---

## 4. Exclusions — and which ones you may want to overrule

Four exclusions are structural and should stay: **not drawn** (§3, implemented
once in `emitterIsLive` for both collection paths), **2D and menu groups**
(a world position for a screen-space emitter is meaningless), **non-additive**
(§3), and **named as a substance that is never a light** (`Class::Excluded`).

The last one exists because of an observation that cost the design an
assumption. The Deku Baba was seen lighting rooms from its jaw joints on
2026-08-07, and its drool genuinely passed `additive && glow`. **Additive means
"does not occlude what is behind it", which is true of a flame and equally true
of saliva** — so that clause is weaker evidence of emission than §3 claims, and
the answer is the name and the report rather than a threshold tweak. The same
session's other two false positives — an effect hidden by zeroing the shared
emitter's alpha, and one hidden by ramping global particle *scale* to zero while
leaving alpha at `0xFF` — are why `emitterIsLive` watches scale as well as alpha
and runs on both collection paths.

**Before adding a keyword, size it over all 3,205 names.** `effect_lights.cpp:543-553`
carries three worked rejections — `tsubu`, `iwa` (which is 団扇 *uchiwa*, a **fan**,
not 岩 rock) and `smoke` — and the widening warning with its disjoint `shizuku` /
`sizuku` counts is at `:520-527`.

---

## 5. Parameters: derive from the game where the game knows, configure where it does not

**The headline finding, from a read-only survey of the authored JPA data on
2026-08-13: nothing the artists authored is photometric.** The blocks carry a
colour ramp, a particle size, a spawn volume, an emission rate, a lifetime and a
particle count, and not one of them is a brightness. So:

> **Hue, extent and persistence come from what the artists authored. Radiance
> comes from the game's own `LIGHT_INFLUENCE::mPow` where there is one, and from
> settings where there is not.**

`mPow` is the single genuinely photometric authored number anywhere in the game —
a reach in world units, verified at `d_kankyo.cpp:924` and `:3536`, and
consistently 500 across `fireWood`, `fireWood2`, `maki`, `ep`, `lv3Candle` and
`poCandle`. Note the game's own debug panel calls it 影響範囲 *eikyou han'i*,
range of influence: it is a **distance**, not an intensity, and reading `mPow` as
English "power" is how the two get confused.

**The whole chain, authored value to radiance — written once, here.** This is the
only place it is spelled out; `effect_lights.hpp` says so and points at this
block. The classification report prints it back with the session's live numbers
substituted in, so "is a multiplier being applied twice" is answerable from a log
without anyone reading source.

```
class     = classifyByName(the effect's own name)                        §3.1

hue       = the adopted game light's colour                    ↓ this section
            else the effect's AUTHORED colour ramp   (authoredColor, on)
            else the emitter's live registers                  ↓ this section

reach     = ( the adopted light's mPow  |  undeterminedReach )
              × mass ^ massExponent
              × effectLightReachScale                                    ← multiplier

radius    = ( derivedRadius | undeterminedRadius ),
              grown to the AUTHORED extent when authoredRadius is on
              × effectLightRadiusScale                                   ← multiplier

radiance  = reach² · 0.01 / (π · radius²)
              × ( derivedIntensity | undeterminedIntensity )
              × effectLightIntensity                                     ← multiplier
```

The three global multipliers are `effectLightIntensity` / `ReachScale` /
`RadiusScale` — one per value the system derives from the game, all defaulting to
1.0, all applied to both the derived and the undetermined branch.
`effectLightDerivedReach` was **retired at protocol 14** and folded into
`ReachScale`; a config still setting it is inert, which the replacement option's
own description says.

**Do not add a per-class brightness column.** `effect_lights.cpp:1517-1522`
carries the prohibition *and* the argument for it, sitting directly above
`enum OffsetSource` — which is exactly where someone would go to add one. Prose
cannot improve on that placement; read it there.

**Hue and extent are read off the immutable authored blocks rather than the live
emitter** (`effectLightAuthoredColor`, default on; `effectLightAuthoredRadius`,
default off). The full argument — why the live register is *actively wrong*
rather than merely different, the exact split between immutable and overwritten
fields with the actor-setter counts, and **the one place these must not be
swapped**, because the accept test has to read the live values or it would light
effects the game has faded to invisible — is at
`src/dusk/effect_lights.cpp:745-766`. It is better placed than any restatement
here: it sits on the function that does the reading.

Taking the ramp's most saturated entry changes **hue only**; radiance is
normalised to the colour's brightest channel.

---

## 6. Sites: one light per fire, not one per emitter

A bonfire is five emitters at one point (`d_a_obj_maki.cpp:56`) — flame core,
layers, embers — whose alphas animate independently, so five lights there cost
five times as much and flicker. Candidates within `mergeRadius` merge into one
**site**, and site identity is then held stable across frames, because a Remix
light is keyed by hash and a changing hash is a new light with no temporal
history. The site takes the position of its **highest-weight member, not the
centroid**; why, and what the grace period is for, are at
`effect_lights.cpp:2229-2237` and `:2525`.

**A burning bonfire has no usable registered light at all**, by two independent
routes that `d_a_obj_maki.cpp` states twice: it cuts the light it registered on the
very first frame (`:107-110`, guarded on the fire *burning*), and in Bulblin Camp
(`F_SP118`) it never registers one — while the five fire emitters spawn on every
stage regardless. That is the clearest single case for this system existing, and
it is why bonfires run *undetermined*. Read from source, not observed running.
`d_a_obj_lv3Candle` is the same shape: it never calls `dKy_plight_set` at all and
spawns its flame unconditionally.

---

## 7. Instrumentation — the part that makes the next iteration cheap

Per [rule 2](../CLAUDE.md#2-a-question-we-would-have-to-ask-the-owner-is-a-defect-in-the-logging),
this ships with the logging that answers the questions we would otherwise ask,
because several of §3's claims are inference and one play session can settle all
of them.

The `rtx.dusklight.env.effLights*` readouts are pushed every frame and shown on
the overlay's Readouts tab. **The full list, and which question each one answers,
is `struct Stats` in `src/dusk/effect_lights.hpp`** — every member carries the
question it exists to settle, and a copy here would go stale. Two are worth
naming because they exist to settle a specific open question rather than to be
watched:

- `effLightsOrphans` — vanilla lights with no site near them, which decides
  whether the orphan policy needs the escape hatch that is currently a stub. Some
  orphans are real sources with no particle at all (dungeon fill lights, glowing
  crystals) and those go dark.
- `effLightsVanilla` — whether the spot registry is adopted in practice. The
  *reading* says it is, from process list order; a reading is not a measurement,
  and this project prefers the second.

**One press, five sections, every open question answered.** *Log Full Effect
Light Report* dumps counters, effects, sites, game lights and a retrospective
trace, so the loop is *do the thing, then press*, not *arm a trace and hope*, and
each press covers everything since the last one. Bursts are recorded even though
they are excluded from lighting, because that report is what is meant to settle
whether excluding them is right. **If a question about this system cannot be
answered from one press, that is a defect in the report rather than a question
for the owner.**

**One interaction worth knowing about:** an animating flame updates its light
constantly, and on stock Remix every update would drop the light's RTXDI
temporal history for a frame. The fork's `rtx_light_manager.cpp`
`addExternalLight` carries the buffer index across an overwrite for this reason.

---

## 10. What the defaults are

Every default except the two inherited from the retired local-light mirror — `19`
and `10`, which are the derived reading of the game's own attenuation curve rather
than taste values — is **a starting point chosen to be visible rather than
correct.** None has been tuned. The `rtx.dusklight.game.effectLight*` options
carry their own descriptions, which are what the F1 tooltip renders; the
declarations in the fork's `rtx_dusklight_game.h` are the authority. **Do not
write a count of them here** — a hand-derived number goes stale silently, and
the fork's invariants script prints the live figure on every run.

**Two things are plumbed and do nothing.** The `orphanPolicy` setting is carried
through `Params` but orphans are simply counted and dropped — it is there so the
readout can prove whether the behaviour is needed before it is written. And
`LIGHT_INFLUENCE::mFluctuation`, the per-light flicker amount, is read past and
ignored, because honouring it means re-creating every flickering light every
frame. "The game flickers its torches and we do not" is a real difference from
vanilla that nobody has decided is wrong.

**Two known holes, in the order they are likely to bite.** A callback-driven
emitter's position may be stale — `dPa_control_c::set` only re-positions an
existing emitter when its level callback is null (`d_particle.cpp:1766`); the
lantern's swing emitter is fine, the other `dPa_levelEcallBack` subclasses were not
checked, and the signature is a light left behind where an effect used to be. And
**lights lead geometry by one frame**: actor draw methods enter models into J3D
draw buffers whose GX commands are issued at the *start* of the next iteration,
while `dusk::remix::tick()` runs at the end of the current one. Invisible for
anything static; for a swinging lantern the light leads the lamp by 16–33 ms. It
is a property of the frame, not of this system.

What has and has not been run in game is in
[`remix-open-issues.md`](remix-open-issues.md).
