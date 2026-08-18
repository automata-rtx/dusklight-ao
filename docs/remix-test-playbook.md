# Remix test session sheet

**What to run next, in order.** Not a catalogue of everything that has ever been
tested — [`remix-open-issues.md`](remix-open-issues.md) is the ledger of what is
owed a run, and this file is the order to run it in.

**Logs are the deliverable, not impressions.** The project rule is that a
question we would have to ask you is a defect in the logging. Play, then send
both files:

- the game log, `<CachePath>/logs/<timestamp>.log`
- Remix's log, `rtx-remix/logs/remix-dxvk.log`

Reading guide for the material lines:
`extern/aurora/docs/dx9/material-report.md`.

**Before anything:** F1 → the Dusklight status strip must say *Connected*,
*protocol agrees*, *device registered*. If it says either side is older, the two
came from different commits — rebuild both, or every result below is noise.

---

## The rule that costs sessions when it is ignored

**Untested work cross-contaminates a test session.** Nearly everything in the
ledger is shipped-but-unrun, and several items change the same pixels: the
ambient grade double-counts against the dome light's fill, the fog rework and the
emissive rule both move brightness, and a per-blade grass switch moves frame
rate. A session that has three untested things on at once produces one
uninterpretable impression instead of three answers.

So: **one variable per look.** Start from the baseline below rather than from
whatever was last saved — the F1 panel persists now (closed issue 17), which is
exactly why "whatever was last saved" is no longer a known state.

---

## Baseline `rtx.conf`

One file for the whole session; every test is a delta done live in the overlay.
`dx9-fixed-function.md` carries the full annotated conf; this is the subset a
test session needs pinned.

```ini
rtx.useNewGuiInputMethod = False

rtx.dusklight.atmosphere.enable    = True
rtx.volumetrics.enable             = True
rtx.dusklight.atmosphere.skyEnable = True
rtx.dusklight.game.hideVrbox       = True
rtx.skyAutoDetect                  = None

rtx.bloom.enable    = True
rtx.bloom.dusklight = True
rtx.bloom.steps     = 6

rtx.dusklight.game.celestialNoonElevation = 80
rtx.dusklight.game.disableFrustumCulling  = True

rtx.autoExposure.enabled = False   # every judgement below is a brightness judgement
rtx.fallbackLightMode    = 1

rtx.dusklight.matrep          = True   # material translation report
rtx.dusklight.emissive.enable = True
rtx.dusklight.emissive.log    = True
rtx.dusklight.rampMaterials   = True
```

### Must be OFF for a clean read

```ini
# Double-counts against real sky fill now that the dome lights the scene.
# This is the one that will mislead you if left on.
rtx.dusklight.grade.enable = False

# Remix's own fog remap: dead code while the atmosphere is on, so a value
# here is a false lead rather than merely redundant.
rtx.volumetrics.enableFogRemap      = False
rtx.volumetrics.enableFogColorRemap = False
```

`rtx.fogColorScale` is bypassed entirely while the atmosphere is on, so it is not
a knob any more; `fogRadianceScale` is the level control for both fog paths.

**Clear `rtx.dusklight.water.wavesAsBlend` out of the live conf before any water
test** — it was reverted to default `false` the day it landed, and a default
revert cannot reach a config file that still sets it `True`.

---

## The queue

### 1. Kakariko Village crashes on entry — two warps, one session

Reproduces 100%, blocks a whole area, and is the only item here where the
outcome is a crash rather than a judgement. Full entry, ruled-out table and the
two mistakes not to repeat: [`remix-open-issues.md`](remix-open-issues.md),
Kakariko section.

Load in Hyrule Field, warp to Kakariko **twice**: once as normal, once with
`rtx.opacityMicromap.enable = False`. Send the crash report and the Remix log
from each. Backtraces symbolize since 2026-08-16, so the first warp names the
faulting function and the second answers the OMM question — two answers from one
window, which matters because they are otherwise asked a week apart.

Do not symbolize anything by hand. That was asked once and it was wrong.

### 2. Water

The largest visible defect and the one whose diagnosis is already fully in the
log, so it costs one walk and no judgement calls. Stand at any lake (Lake Hylia
is the reference), look at the surface, walk in.

**The one number that answers whether the wire survived its 2026-08-11 rebase:**
`power=` on each `dusklight.water` line in the Remix log. `power=921` reads as
MA09 / waves / surface. If the game logs `dusk.matname` lines and Remix logs no
`dusklight.water` at all, the packing lost them and nothing downstream means
anything.

`dusklight.water.replaced` is what says the mark landed — a hand-authored
replacement wins *before* the water check, so translucency on some water is not
evidence. Regression signatures, worst first, are in issue 15.

**Glance at HD texture packs in the same session** — they shared that side band
until the rebase. A pack that stops applying means the two features are still
colliding.

### 3. Fog — the defaults are settled; `fogRampMode` 2 is not

The shipping default path was run on 2026-08-18 and the owner reported
improvements. **Mode 2 was not covered** and is the open half.

Set `rtx.dusklight.atmosphere.fogRampMode = 2` and compare against the default
`1` in the same place. Mode 2 gives the medium `σ(d) = 1/(end − d)`; its
regression signature is that the top-up residual must be flat **within the froxel
grid's reach** and varies by construction past it — and past-the-reach is the
common case outdoors, a ~120 m grid ceiling against an open-world `fogEndZ` near
33,500 units. Do not read variation out there as a defect.

### 4. Fog range at the Lost Woods mist tag — measurement, not a judgement

`zHalfMin` and `froxelRangeScale` are guessed rather than measured. Go to the
Lost Woods / Sacred Grove (`F_SP117`) `kytag01` tag and **walk OUT of the clear
centre** — the tag marks a clear centre, so the intuitive walk-*into*-the-fog
gives the wrong reading.

**Take it in the default `fogRampMode` 1, or record which mode was used**: mode 2
floors `froxelRangeScale` at 1 and does not use `zHalfMin` on the view ray at
all, so a walk-out in mode 2 measures neither setting. Both quantities moved on
2026-08-17, so an earlier reading is not comparable.

While you are in the panel, record the live `fogStartZ` / `fogEndZ` / `fogColor`
and sky colours here, at Hyrule Field, Faron, Lake Hylia, the Goron Mines and the
Forest Temple. Those live in stage data rather than in source, so the readout is
the only way to see what an area asks for, and it turns future tuning into
arithmetic. Procedure: [`kankyo-fog.md`](kankyo-fog.md) §5.

### 5. Two log-only answers, free while you are already playing

Neither needs a setting change and both are minutes from §4, so **one session
settles §4, §5a and §5b together.**

- **5a. The colpat-9 sky guard.** Does it ever fire? Instrumented already
  (`logColpatOnce`). Fires on an outdoor stage → remove it, that stage is losing
  its physical sky for nothing. Never fires → delete it as dead code. *"Probably
  inert" is not an answer.*
- **5b. The Twilight fog draw count.** One `kankyo.particles` line from the
  Palace of Twilight (`D_MN08`), room 0 or 1. Recipe and what each field decides:
  closed issue 13.

### 6. Materials — colour, emission, vertex colour

**Launch with `DUSK_MAT_LABELS=1` in the environment.** Without it every material
line reads `grp=-` and identification falls back to texture size, format and ramp
endpoints — which is how the grey lava geysers came to be unexplained. With it,
`grp=` carries the material's own authored name (`Mat:MA00_Gake`). No rebuild
needed. It names the *material*, not the actor.

Three things to look at, each with a one-flip A/B:

- **Two-colour ramps** (`rtx.dusklight.rampMaterials`). On is the exact GX
  combiner; off is the single-op approximation it replaces. Rupees are the
  reference — one texture, seven colours. A material reading flat, or with its
  ends inverted, means the endpoints were swapped rather than approximated.
- **Self-illumination.** Rev 4 passed on the Goron Mines lava in 2026-08-06. What
  is still open is the grey geysers, and whether the derivation holds for a small
  pickup as well as for lava.
- **Vertex colour** (closed issue 10). `vtxUse=` on each `matrep.sum` line says
  which draws were treated as carrying baked lighting. Regression: a surface
  gaining or losing brightness and saturation this round.

### 7. The ambient grade — alone, or not at all

`rtx.dusklight.grade.enable`. Off by default and never reached. It double-counts
against the dome light's fill, so a session that has it on while judging fog,
emission or sky brightness has produced no usable result for any of them. Give it
its own pass or skip it.

### 8. Room lights — a survey, not a look

`rtx.dusklight.game.roomLights`. Shipped, off by default, unmeasured. The
questions it would settle live in the stage files rather than in code, so it can
only be answered by switching it on in a few dungeon rooms and reporting the
counters. The case for and against is the comment above `roomLights` in
`src/dusk/remix_bridge.cpp:1019`; it is off by default because these are authored
positions and a path tracer casts a real shadow from exactly where they sit.

### 9. Shipped switches nobody has ever flipped

One look each, lowest priority, listed so they are not forgotten:
`perBladeGrass` and `perBladeFlowers` (a frame-rate drop is the expected cost,
not a defect), `waterSurfaceShine`, `grassLightInfluence`, `clockRate`, the Mods
tab, `styleTerm`. Each defaults to the game's own value or to off, so nothing
changes until one moves.

---

## Regression index

These passed. Kept as one-line re-run recipes for when something regresses; none
of them is a queue item.

| What | Recipe | Passed |
| :-- | :-- | :-- |
| Clock, warp, Freeze Time | Overlay → Go tab; move the time slider, warp | 2026-07-29 |
| `hideSkyBillboards` | Night, outdoors; shadows must not wander with the player | 2026-07-29 |
| Physical sky, phases A/B | Outdoors, dawn to dusk | 2026-07-28 |
| Effect lights | Any bonfire or torch; press *Log Full Effect Light Report* | 2026-08-07 |
| HD texture packs | Install a pack; log must read `N created, 0 skipped` | 2026-08-06 |
| Blob shadows | Item drop-shadows must not be black quads | 2026-08-06 |
| Batched weather particles | Rain or snow; `dx9.draws peak` must not reach the thousands | 2026-08-08 |
| `skyFogMode` = Exempt | Dense fog outdoors; the generated sky must not dim | 2026-08-13 |

**The effect-light report is the one worth re-reading even on a pass.** One press
dumps counters, effects, sites, game lights and a retrospective trace; the 2026-08-07
session passed on "it works" and nobody read it, which is why `effLightsOrphans`
and `effLightsVanilla` are still open questions. Do the thing, *then* press.
