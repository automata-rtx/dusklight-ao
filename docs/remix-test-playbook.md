# Remix test session playbook

The recipe for a test session, so one does not have to be reconstructed from
scratch each time. Companion documents:
[`remix-open-issues.md`](remix-open-issues.md) is what is currently owed a run;
[`kankyo-remix.md`](kankyo-remix.md) is the design these tests exercise;
[`remix-history.md`](remix-history.md) is an unmaintained archive — do not
consult it for how anything currently behaves.

**Before anything else — logs are the deliverable, not impressions.** Aurora and
the Remix fork both write structured diagnostics, and the project rule is that a
question we would have to ask you is a defect in the logging. Play, then send
both log files:

- the game log, `<CachePath>/logs/<timestamp>.log`
- Remix's log, `rtx-remix/logs/remix-dxvk.log`

Format and reading guide for the material lines:
`extern/aurora/docs/dx9/material-report.md`.


*The backlog is not "what to build" — it is "what to run". This section is the
recipe, so a session does not have to be reconstructed from scratch.*

**§§1–5 were run on 2026-07-29: four passed and §5 ran partially. §3b passed on
2026-08-07 but its diagnostics were not read — see the note under that heading.
§0 is the outstanding one** — it covers everything that landed on 2026-08-04, all of which
is CI-green and untested in game. The passed sections are kept rather than
deleted because they are the re-run recipe when something regresses, and because
§3 and §5 both ended with a setting change that the next session needs to
reproduce.

### Baseline `rtx.conf`

One file for the whole session; every test below is a delta done live in the
overlay. (The block in `dx9-fixed-function.md` sets `rtx.volumetrics.enable`
twice — faithful-fog mode then atmosphere mode. Last wins, so it works, but
this is the unambiguous version.)

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

# Material translation report, Remix half. Aurora's half is always on.
# Costs nothing when off and kilobytes when on; leave it on for any session
# where a surface looks the wrong colour. Also toggleable live in
# F1 -> Dusklight Remix -> Materials.
rtx.dusklight.matrep = True

# Self-illumination. On by default with its own bounded log; these are here so
# a session can start from a known state rather than whatever was last saved.
# Nothing else needs setting: the rule is structural and the defaults are the
# intended configuration. Emissive Intensity in the overlay is the one dial.
rtx.dusklight.emissive.enable = True
rtx.dusklight.emissive.log    = True

# Two-colour ramps, reproduced exactly in the fork's shader. On by default;
# stated here so the session starts on rather than on whatever was last saved.
# This is the A/B in 0a - off is the single-op approximation it replaces.
rtx.dusklight.rampMaterials = True
```

**Before anything:** F1 → Dusklight Remix tab must say *"Connected"* and Bridge
→ *"Device registered with the Remix API: yes"*. If it says the game build is
older than the Remix build, the two came from different commits — rebuild both
before testing anything, or every result is noise.

### 00. Water — translucent, refracting, one layer (2026-08-09, UNTESTED IN GAME)

**Run this first.** It is the only untested thing whose diagnosis is already
fully in the log, so it costs one walk and no judgement calls.

> **The wire changed on 2026-08-11 and this recipe now checks it too.** Water's
> three facts used to travel in `D3DMATERIAL9::Ambient.g`/`.b`/`.a`, which HD
> texture packs already owned — merging that as written would have deleted
> texture packs. They are now packed into `Power` as
> `tag * 100 + layer * 10 + role`. The earlier measurements below (the mark
> reaching Remix, the projected layer being dropped) were taken on the **old**
> wire: what they establish about the game marking the right draws still holds,
> but whether the marks arrive at all is open again.
>
> **The one number that answers it:** `power=` on each `dusklight.water` line.
> `power=921` is MA09 / waves / surface. If the game logs `dusk.matname` lines
> and Remix logs no `dusklight.water` at all, the packing lost them.
>
> **Also worth a glance in the same session:** HD texture packs, since they share
> that side band. If a pack stops applying, say so — it would mean the two
> features are still colliding somewhere this rebase did not reach.

**Where this stands.** Two things are now measured rather than hoped for: the
mark reaches Remix (2026-08-08 23:47, 11 water materials), and removing the
camera-projected reflection layer made water read as **consistent** (2026-08-09
10:30). That session found it *bland* instead — with no textures bound, water is
featureless glass. **What this session checks is the fix for that:** the game's
own ripple texture now drives the water's normal, scrolling at the game's rate.

That binding is **reverted** as of 2026-08-09: a colour texture decoded as a
tangent normal is noise, and on a lake with an authored normal map it added a
second one, which Remix does not blend. Water is plain glass until a normal map
is authored against a texture hash; the fork now owns the tiling and scroll
instead, and can drop all but one of a lake's stacked surfaces.

**What to set before judging anything.** In F1 → Dusklight → Water, move
**UV Tiling** until the ripple texture reads at a natural scale from where you
are standing, and adjust **Scroll Speed** to taste. Both are live; no rebuild.
Report the values that worked — those are the result.

**The layer switches are the other thing to play with.** Under the same panel
there is one per pass, named the way the game names them: Shimmer (mera), Waves
(nami), Shoreline (mizugiwa), Murk (nigori), Additive (kasan). All start off. A
lake is drawn as several of these stacked, and stacking refracting interfaces is
not what water is — so try turning them off one at a time and see which one the
lake is better without. **That choice is the result worth reporting**, and it is
a look call rather than a correct answer.

The `layer=` field on each `dusklight.water` line says what each body of water is
actually made of, so a lake made of passes not in that list will say so rather
than quietly ignoring the switches. `tex=WxH` says the size Remix received, which
answers "is that texture really that low resolution" without opening anything.

**Also worth a look this run:** the Hyrule Field puddles, for a seam between the
water and the ground around it. The water's edge pass now keeps its alpha blend
rather than becoming refracting glass, which is what a hard boundary there was.
*Shoreline Keeps Its Blend* in the Water panel turns it off for an A/B. A milky
ring at the edge instead of a hard one is the regression to report.

**What to do.** Walk to any of the large puddles in Hyrule Field, then warp to
Lake Hylia and look at the lake. If a dungeon with a water level is convenient,
raise or lower it once. Quit, send both logs. Nothing here needs the clock.

**What to report:** whether anything that is *not* water became see-through or
disappeared. Those are the two failures the log cannot describe on its own, and
the first has happened before. Everything else is in the log.

The `tag=MAxx` field on every `dusklight.water` line says what each body of
water is made of. That is the field to read before deciding what Hide Surface
Tag should default to.

**What the logs will say.** Four lines trace the mark end to end, so the first
one that is missing is where it died:

| Line | Which log | Means |
| :-- | :-- | :-- |
| `dusk.matname name=… role=surface` | game | the game recognised the material |
| `dx9.water: first SURFACE mark decoded from the FIFO` | game | it survived the FIFO |
| `dx9.water: first SURFACE draw translated (matKey …, power …)` | game | a draw carried it to the device. **`power` should be non-zero** — it is the packed `tag*100 + layer*10 + role` |
| `dusklight.water tex0hash=… tag=MAxx layer=… power=… texXform=… proj=… blend=…` | Remix | Remix received it and decoded the same three facts back |

`PROJECTED` has the same three game-side lines and ends at
`dusklight.water.projected … hidden=1` — the reflection overlay being dropped.
**Compare the two `power=` values.** The game's line says what aurora packed and
Remix's says what it decoded; they are one contract written in two repositories,
and if they ever disagree that pair of numbers is the only place it shows. A
`dusklight.water` line whose `tag=`/`layer=` do not match its own `power=` means
the decode drifted from the encode.

A fifth, `dusklight.water.replaced`, means the mark arrived and a hand-authored
replacement material claimed the draw first. That is intended — it is how a
normal map gets onto the surface — but it is not the water path, so it is
counted separately rather than being silent.

**That open question is now closed, negatively.** Blend state does not separate
the base water pass from the scrolling one in this game — the `SRC_ALPHA,ONE`
group holds both a still pass and scrolling ones, and alpha test does not split
them either. The fields stay on the line because they are cheap, but no rule is
coming out of them.

**Controls, all under F1 → Dusklight → Water:** *Translucent Water* turns the
whole thing off for an A/B, and *Hide Projected Reflection Layer* puts the
painted reflection back — the direct A/B for this session's change. Index of refraction is 1.33 and should not need
touching. **Transmittance measurement distance defaults to 200 and is an
uncalibrated guess** — if the water reads as invisible rather than transparent,
or as too strongly tinted, this is the one number to move, and the value that
looked right is the result worth reporting.

**Regression signature, worst first:** materials that are not water turning
translucent (the mark leaking — this happened on 2026-08-08 and made every
material in the game see-through); water *geometry vanishing* (the new mirror of
that failure, the projected mark leaking onto things that should be drawn);
water invisible rather than transparent (measurement distance); a water body
losing its reflection entirely instead of gaining a traced one.

**Still expected to be wrong, and separate from this:** the ripple *warp* comes
from indirect texturing, which aurora drops, and the TEV two-constants-per-stage
ceiling still bites. Water can be correctly translucent and still animate wrong.
Remix's own dual-layer animated normals (`rtx.translucent.animatedWaterEnable`,
plus the texture hash in `rtx.animatedWaterTextures`) are the intended route,
and they need an authored normal map to do anything —
`translucent_surface_material_interaction.slangh:61`.

### 0. Materials — colour, emission, vertex colour (2026-08-05, UNTESTED IN GAME)

**This is the section to run.** Everything below it has already been run. Four
changes land together in the material path, so one walk covers all of them:
the two-colour ramp (0a), self-illumination (0b), selective vertex colour (0d)
and the HUD fade-in alpha (0e). Only the first two have a control to press —
0d is the one nobody is aiming at and the one most able to change how the whole
scene looks, and 0e is the only one that touches opacity. A fifth change from
the same day (0f, API assets in captures) is not in the material path and needs
a separate two-minute check.

**What to do.** Walk past a rupee and a heart, go into the Goron Mines and look
at the lava, look around some ordinary indoor geometry, quit, send both logs.
Nothing here needs the clock or Freeze Time, so do it first, cold.

**What to report — and it is deliberately short.** The logs carry the numbers;
what they cannot carry is where you were standing. So:

- anything that used to look right and now looks **noticeably darker, washed
  out, more or less saturated, or glowing when it should not**. Roughly where
  is enough.
- if the answer is "nothing looks different at all", say that — it is a real
  result and it points at a different part of the chain.

#### 0a. Colour — two-colour ramps, reproduced rather than approximated

The lava reading "more red than orange/yellow" was not mistuning: its material
is `lerp(red, yellow, texture)` and no *stock* Remix texture op can express a
lerp between two constants, so the bright end was going to white. The fork is
ours, so it now evaluates the GameCube colour combiner itself and the ramp is
exact.

**The A/B is one checkbox:** F1 → Dusklight Remix → Materials → *Reproduce
Two-Colour Ramps*. Off is the old approximation. Worth a look at the lava with
it on and off, because that comparison is the whole result.

| What you see | Reading |
| :-- | :-- |
| Lava reads red-to-orange, rupees and hearts richer | Worked. |
| Lava reads flat, or light and dark inverted | The endpoints are swapped — a one-line fix, and the log names the material. |
| No difference at all with the checkbox | The ramp was declined; `ramp=` in the log says why (most likely `tfTaken`). |

#### 0a-bis. The op approximation, for materials the ramp declines

The first (2026-07-29) was a no-op. The second (2026-08-03) coloured rupees but
rendered their highlights **black**, because it multiplied by the colour where
the material actually adds to it. This round lets the material's floor decide:
a coloured floor gets `texture + colour`, a black floor keeps `texture × colour`.

| What you see | Reading |
| :-- | :-- |
| Rupees and hearts coloured, highlights bright | Worked. |
| Highlights dark or inverted again | The op choice is still wrong; the log gives `out0`/`out1` per material. |
| Still grey | Did not fire; the log names the material. |
| Surfaces washed out or too bright | The `add` branch overshoots on that material — expected direction if the choice is wrong. |
| Foliage becomes solid quads, or grass vanishes | Nothing in the colour path touches opacity, so this should be impossible *here* — but 0e does change alpha this round, so it is the suspect. Most important thing in the session if it happens. |

#### 0b. Self-illumination — now a rule with nothing to dial

Three revisions of this cut on a weighted score and a threshold. All three
missed the Goron Mines lava, which scores 0.00 on every signal that score is
built from. The rule is now structural instead:

> A surface emits when its GX colour program **never reads the lit channel**,
> it has a **colour of its own** (authored in GX constants — not the vertex
> stream, not a bare texture pass-through), and that colour **reads as a glow**.

Replayed over your last log that is **6 materials of 77** — every lava and fire
surface in the room, plus one warm glow texture, and nothing else.

**So there is nothing to set up. Walk into the mines and look.**

| Control | Do this |
| :-- | :-- |
| Nothing | Default is the intended configuration. Just look at the lava. |
| Emissive Brightness | The one dial. Default **10.0**, which is the value the lava was dialled to in game on 2026-08-06 — so a fresh config should already look right and this is here for exteriors, which have not been looked at. |
| Emitted Colour | Leave on *Reconstructed Albedo*. It is the two-colour ramp — `lerp(FF0000, FFFE63, texture)` — so the texture drives the colour. The other two are there to compare against, not to use. |
| Emissive Surfaces Enabled | Untick for an A/B against no emission at all. |

The game no longer draws the flat disc shadows under rupees, hearts and pots —
**tested 2026-08-06, correct.** Remix traces those for real, so the disc was a
painted shadow on top of a correct one. Link's own shadow is a separate system
and is unchanged; if *that* ever looks doubled, it is the same one-line fix.

| What you see | Reading |
| :-- | :-- |
| Lava glows red-to-orange with visible crust, and lights the room | Worked. |
| Lava and a heart both look right at one Brightness setting | The per-material derivation worked; that is the change. |
| One of them still needs a different setting | Send the log — every emitter's `radiance=` is printed, so the ratio is measurable rather than describable. |
| Lava glows but looks flat, one colour | The ramp is not reaching it — `ramp=` and `rampOther=` on the `dusklight.emis` line say so directly. |
| Lava does not glow | The line carries `selfLit=`, `authored=` and the colour numbers, so it says which of the three facts failed. |
| Something obviously wrong glowing | Roughly where is enough; every accepted material is named in the log. |

#### 0c. What the logs answer by themselves, and what they still do not

**`grp=` does not work and has been removed.** It printed `-` for every material
in every session — `fpcDw_Execute` is where a draw is *scheduled*, not issued.
So "which one is the lava?" is still not answered by the log directly; it is
inferred from texture size, format and ramp endpoints. This is the main reason
the grey geysers are still unexplained.

**New this round, and it may reframe the whole feature:** every material line
carries `blend=`. `additive` or `additiveAlpha` means the draw is *already*
emissive by Remix's own rule, claimed before the Dusklight score is consulted —
that is how a console-era renderer draws a flame or a glow halo, and it is the
one thing GX says unambiguously. Nothing to press; just walk past a torch and
the lava as usual. If the log comes back with `blend=additive` on the flames,
the scoring work only ever had to cover opaque emitters like the lava surface.

What *is* answered now, without you describing anything:

- every emissive candidate's score, colour, authored flag, ramp endpoints and
  which gate rejected it
- what changed when you moved a control, because moving one re-reports
  everything
- how many colourless candidates were skipped, as a count rather than 90 lines
  that used to eat the whole log before you reached the mines
- which draws are additive, and therefore already emissive without any of this

Still worth a sentence if you notice it: **anything glowing that obviously
should not** — roughly where is enough, since the accepted materials are all
named in the log.

#### 0d. Vertex colour — now forwarded selectively, and worth watching

Nothing to press. This one changes **the whole scene at once**, which is why it
is the likeliest source of a surprise this session.

Until now aurora withheld vertex colour from Remix on every draw, because
testing proved the vertex colours carry baked room lighting and a path tracer
would double-count it. GX turns out to say which is which per draw: with GX
lighting **on** the stream is authored material colour (forward it — the path
tracer supplies the light), with it **off** the stream is the finished, already
lit result (withhold it). Aurora now sends that verdict per draw and the fork
obeys it instead of the global `rtx.vertexColorIsBakedLighting`.

**The regression signature:** surfaces that gain or lose saturation and
contrast relative to last session. Both directions are possible and both are
useful. Roughly where is enough — every material line carries `vtxUse=`
(`material`, `bakedLight` or `const`), so the log resolves which verdict a
surface got.

The known judgement call, so it can be recognised rather than discovered: a
draw with GX lighting off whose vertex colour is genuinely authored — a
per-vertex tint or fade on an effect — is withheld today. **A lost colour
gradient on an effect is that case**, not a general failure.

#### 0e. HUD fade-ins — the alpha half

A-button prompts and Epona's spur icon drew as opaque expanding rectangles: 18
of 111 materials fade in via `konst × texture-alpha`, and aurora's hint stage
was advertising the texture's alpha alone and dropping the constant. The scale
now rides TFACTOR's alpha channel.

This is the one change in the session that touches opacity, and opacity is one
of the two things that must still rasterize correctly — Remix reads it to build
the alpha test.

| What you see | Reading |
| :-- | :-- |
| Prompts and icons fade in with transparent surrounds | Worked. |
| Still opaque squares | Did not fire; the log names the material. |
| **Alpha-tested foliage, grates or grass go solid or vanish** | Regression from this change. Report immediately — it is the highest-cost failure available this round. |

#### 0f. API-submitted assets in a capture — the untested half of "do it in Remix"

Landed 2026-08-04, **CI-green and never exercised**: API mesh hashes are now
derived from the submitted vertex/index data instead of a creation-order
counter, and `submitExternalDraw` consults the replacer before using the
supplied material. The generated sky dome is the only API-submitted asset we
have, so it is the test subject.

This one matters out of proportion to its size. The whole "where D3D9 cannot
carry it, do it in Remix" half of the design (`extern/aurora/docs/dx9/remix-material-interface.md`
§0) is only safe if what we push through the API can still be authored over
later. Nobody has confirmed that it can.

**What to do**, with `rtx.dusklight.atmosphere.skyEnable = True`:

1. Take a Remix capture (Remix's own menu) somewhere outdoors.
2. Quit, relaunch, stand in the same place, take a second capture.
3. Send both capture folders, or just the two USD file listings.

| What you find | Reading |
| :-- | :-- |
| The sky dome mesh is in both captures **under the same hash** | Worked — it is capturable and stably identifiable, which is what replacement needs. |
| It is in both captures under **different** hashes | The hash is still not content-derived in practice. This is the failure the change exists to prevent, and it is invisible from a single capture — which is why step 2 is not optional. |
| It is in neither capture | The change did not take effect at all. |

Nothing to describe by eye here, and nothing that can regress the image: if this
is inconvenient to run, it can wait for a session that is already taking
captures for another reason.

### 0b. HD texture packs — PASSED 2026-08-06, kept as the regression recipe

> **PASSED 2026-08-06, first try.** Replacements appear, and texture tagging is
> unaffected. Re-run this whenever anything in the texture, material or tagging
> path changes — the tagging check below is the one that would catch the
> expensive kind of regression.

Needs a `.dds` pack in `<ConfigPath>/texture_replacements/`. Nothing to enable:
`game.enableTextureReplacements` and `game.remixTextureReplacements` both
default on and are read at launch.

1. Launch, open the Dusklight tab → **HD Texture Pack**, read both counter rows.
2. Look at the world and the HUD.
3. **The tagging check.** Open Remix's texture categorization list and note a
   few hashes. Quit, move the pack directory aside, relaunch, and compare.

| What you find | Reading |
| :-- | :-- |
| `Game: N selected, N handed over` and `Remix: … substituted` climbing | Working. |
| Handed over > 0 but `0 draws tagged` | The D3D9 stream is not carrying the index — an aurora older than the fork, or a protocol skew. Read the protocol line first. |
| `N selected, 0 handed over` | The game's device never registered with Remix, or the pack is all `.png` — `texrepSkipped` and the game log say which. |
| World sharpens, HUD does not | `applyToRaster` off, or a multi-texture UI draw (only the albedo stage is substituted on the raster path). |
| **Texture hashes differ between pack-on and pack-off** | **A real regression, and the serious one** — it means the pack is reaching D3D9, which silently invalidates every `rtx.conf` category and USD binding. The whole design exists to prevent this. |

**Expect a slow first launch** and a fast second one. Not a fault, but the cause
is not established — Remix compiles shaders on first load and caches them to
disk, so every first launch is slow with or without a pack, and the pack's own
`.dds` reads are *not* durably cached. If a session has a spare reboot, that is
the experiment: **reboot, then launch.** It clears the OS file cache and keeps
the shader cache.

| Result | Reading |
| :-- | :-- |
| Slow again after a reboot | The pack's file I/O is the dominant term. §9's fixes are then worth taking. |
| Fast after a reboot | Shader compilation dominated; the pack's cost is minor and nothing needs doing. |

Either way the game log's gap between `texrep: N replacement(s) selected` and
`texrep: N material(s) created` quantifies the pack's own share.
`extern/aurora/docs/dx9/texture-replacements.md` §9.

Not exercised by the 2026-08-06 run, so still worth covering if a session has
room: a BC7 or BC5 pack, a deliberately-`.png` entry, a window resize (materials
should be re-created), and palette-animated art.

### 0b-ii. Dumping textures under their pack filenames — not a test, a tool

> **This is for making a pack, not for judging a build.** It changes nothing in
> the image. Added 2026-08-11; the capability was already in aurora and was held
> shut by one hardcoded line.

**What it does.** Every texture the replacement registry is asked for and cannot
satisfy is decoded and written to `<cachePath>/texture_dumps/` as a `.dds`,
named with **the exact key a pack file must carry** —
`tex1_<w>x<h>_<texhash>_<fmt>.dds`, or `tex1_<w>x<h>_<texhash>_<tluthash>_<fmt>.dds`
for a palette format. That is the same string aurora parses when it *loads* a
pack, so a dumped file can be edited and dropped straight back into
`texture_replacements/` under its own name. It is also Dolphin's convention, on
purpose. `extern/aurora/docs/dx9/texture-replacements.md`.

**Turning it on.** `game.allowTextureDumps` in the game's `config.json`, default
`false`. Read once, at startup, so it needs a relaunch — there is no live
toggle and it is deliberately not in the Remix overlay.

**Four things worth knowing before you run it:**

- **It writes nothing under D3D9/Remix.** The dump hangs off aurora's GX texture
  resolver (`gx.cpp` `resolve_static_texture` → `find_replacement`). The D3D9
  backend resolves textures through its own cache and asks the registry only for
  an index, on a path that never decodes an image and never dumps. So do the
  dump run in the game's **normal** rendering mode; the keys are backend
  independent, so what you collect there is exactly what the Remix setup wants.
- **Only textures with no replacement are dumped.** With a complete pack
  installed you get nothing, which is correct and is also how you check coverage.
  With no pack installed you get everything — the empty-registry early outs are
  bypassed while dumps are on.
- **Disk use grows for as long as it is on.** One file per distinct key, written
  once each, but a long session across many areas is a lot of keys. Turn it off
  when the pack has what it needs.
- **`game.enableTextureReplacements` must stay on** (it defaults on). With it
  off the game never loads the replacement directory at all.

**What "working" looks like:** `texture_dumps/` fills with files matching the
pack pattern `tex1_{w}x{h}_{textureHash:016x}[_{tlutHash:016x}]_{format}.dds`,
alongside `texture_replacement: missing runtime key …` lines in the game log
naming the same keys. **No directory listing from a real run is recorded here**
— all of the above was read out of the source on 2026-08-11 and has not been
exercised end to end. Paste one when someone runs it.

### 0c. Dense weather particles — PASSED 2026-08-08, one number still missing

> **PASSED 2026-08-08.** Rain and snow, previously unusable, are "far better
> than previously". **But the `dx9.draws` figures were never read**, so the
> draw-count drop the fix predicts is confirmed only by its effect. Step 2
> below closes that in about a minute and is the reason this recipe is kept.

Nothing to enable. Needs weather: **Hyrule Field in rain**, and **Snowpeak
exteriors or Snowpeak Ruins** for snow. Use the clock (§1) if the weather is
time-gated.

1. Stand in each, look at the particles, and move the camera through them.
2. **Read `dx9.draws` in the game log.** It prints once every 600 frames:
   `dx9.draws frames=600 mean=412 peak=1387 - D3D9 draw calls per frame`.
   Capture `peak` while the weather is heavy on screen. This is the whole
   measurement.

| What you find | Reading |
| :-- | :-- |
| `peak` in the low hundreds during heavy rain | **Working as designed** — the whole rain field is one draw. Record the number; it is the baseline every later change is compared against. |
| `peak` in the thousands during heavy rain | The batching is not taking effect and the diagnosis in issue 13 is wrong. Nothing else in this recipe matters until that is explained. |
| Rain or snow invisible, flat-coloured, or with every particle at the same opacity | The per-particle colour is not reaching the TEV stage — the vertex `CLR0` path. This is the regression the batching could plausibly cause. |
| `GXEnd: vertex count mismatch` or a `GX_AURORA_DRAW_SIZED` assertion in the log | A `GXBegin` block is unbalanced. Names the emitter; it is a game-side fix in `d_kankyo_rain.cpp`. |
| Snow shows two sets of flakes moving in opposite directions | **Not a regression** — those are the game's own planar-reflection copies, and they are correct geometry. It only looks wrong if the texture has been tagged as UI, which rasterizes them as a screen overlay. Clear the tag. |

Re-run this whenever anything touches `d_kankyo_rain.cpp`, the immediate-mode
GX path in aurora, or Remix's BLAS/instance handling. Issue 13;
`extern/aurora/docs/dx9/progress.md` §3.32.

### 1. Clock — do this first, it is the tool the rest want

> **PASSED 2026-07-29** — slider, presets and Freeze Time all "work flawlessly
> and as expected". The commit-counter design is validated: a preset pressed
> twice works the second time.

Warp tab → **Time of day**. No config needed.

The day is **360 degrees**: 15 = an hour, 0 midnight, 90 sunrise, 180 noon,
270 sunset. Moon/sun handover ≈ 67–75 (`dKyr_moon_arrival_check`).

- Slider should track the game when released, and not fight you while held.
- **Press Noon twice in a row** — it must work the second time. That is the
  entire reason for the commit counter; if it fails once, the counter is not
  crossing.
- Tick **Freeze Time**: the sun must stop *and* the palette must stop drifting.

Freeze is what makes every A/B below worth anything. Without it the sun has
moved between the two shots.

### 2. Warp

> **PASSED 2026-07-29** — "works exactly as intended, no issues". The
> game-owns-the-table design and the layer `-1` default both hold up in
> practice.

Warp tab. No config.

- Region **Hyrule Field** → Level list shows **1** entry. Region **Ordon** →
  **9**. That is the list-refresh test (a frame or two of lag is expected).
- Level **Ordon Spring** → the text right of the button reads `-> F_SP104`.
- Press **Warp**. Log should show
  `warping to F_SP104 (room 1, point 0, layer -1)`.

| Symptom | Meaning |
| :-- | :-- |
| Warp button greyed out | rooms or points list empty for that level — record which |
| Nothing happens, no log line | commit counter not crossing |
| Nothing happens, log line present | `dComIfGp_setNextStage` fired and the game ignored it — game-side |
| Warps by itself on connect | commit priming failed — report immediately |

### 3. Local point lights — RESOLVED, then SUPERSEDED

> **Do not run this section as a test of the current build.** The mirror it
> exercises defaults **off** since 2026-08-06 and is kept only for A/B against
> §3b. Turning it on without turning effect lights off gives every fire two
> lights, one of them in the old place — which looks exactly like §3b being
> broken. The Dusklight tab warns when both are on.
>
> Kept in full because the A/B is the whole reason the mirror still exists, and
> because the two numbers it settled are now effect lights' defaults too.

> **PASSED 2026-07-29.** Forest Temple, first room:
> `Registered by the game: 5   drawn this frame: 4   tracked: 4`.
> The lights work. Two settings had to move off their defaults and both are
> now recommended values rather than experiments — see open issue 3:
> **`localLightIntensity` 19** (the minimum that gives usable light, which is
> the *derived* alternative reading of the attenuation curve, not a guess) and
> **`localLightRadius` 10** (no clipping through the Forest Temple light posts).
>
> Whatever was wrong in the first report is gone, and the diagnostics are the
> reason this took one visit instead of an evening. The `found 5 / drawn 4` gap
> is the one loose end — see issue 3.

```ini
rtx.fallbackLightMode = 0      # Never. An unlit room goes black, so a working torch is unmistakable
```

Warp to **Forest Temple → Forest Temple** (`D_MN05`). `d_a_ep` — the torch
actor; what `ep` abbreviates is not established, like many of the game's
two-letter actor codes ([`japanese-naming.md`](japanese-naming.md) §5) —
registers its light on actor init regardless of whether the flame is lit
(`d_a_ep.cpp:935`), so `found` should be non-zero if the array is read at all.
Ordon Village at night and the Kakariko bonfire are backups.

**Tick "Local Lights Enabled" and leave it ticked before reading.** `found` is
counted before the enable gate but `running` is set after it, so reading with
the box unticked always reports "not running its light submission" — expected,
not the bug.

Then read `Registered by the game: N   drawn this frame: N   tracked: N` and
the paragraph under it. The three outcomes and what each means are in open
issue 0. If `drawn > 0` but the room is still dark, that is intensity rather
than plumbing — try **Local Intensity 19** (the alternative reading of the
attenuation curve, `remix_bridge.cpp:640-670`).

### 3b. Effect lights — RAN 2026-08-07, PASSED; the diagnostics were not read

> **PASSED 2026-08-07.** The owner's report is "it works", and it was merged on
> the strength of that. Because the system emits nothing unless the rule accepts
> an emitter, this also retires the design's one load-bearing inference: this
> game does author its fire additively, at least for whatever was on screen.
>
> **Nothing below this line was run**, and the section is kept whole rather than
> trimmed because none of it has been answered. In particular **no classification
> report was read**, so which effects light and which are silently refused is
> still unknown — and an effect that never lights is indistinguishable from an
> effect that has no light without it. Same for `effLightsVanilla` (is the spot
> registry being adopted?) and `effLightsOrphans` (how many of the game's lights
> is the policy dropping?). Every one is a single glance at the Dusklight tab.


The system that replaced the mirror above. Design: [`effect-lights.md`](effect-lights.md).
It defaults **on**, and `localLights` now defaults **off** — do not run both, or
every fire gets two lights, one of them in the wrong place.

```ini
rtx.fallbackLightMode = 0      # Never. An unlit room goes black, so a working fire is unmistakable
```

Everything below reads off the **Effect Lights** section of Remix's Dusklight
tab. Nothing here needs the owner to judge a colour or count an artifact; if a
question below cannot be answered from the panel or the log, that is a defect in
the instrumentation, not a question to ask.

**First, before looking at anything: press *Log Effect Classification Report*.**
It writes one line per distinct effect the game has seen so far — name, blend
configuration, colours, class, and whether the rule accepted it. Press it once
early and once after a couple of rooms. That log is the single highest-value
artifact of the session, because the rule's central claim — that this game
authors fire and glow with additive blending and smoke without — is read from
the file format's semantics and has never been checked against this game's
actual assets.

**Warp to Forest Temple → Forest Temple (`D_MN05`)**, the same room the mirror
was validated in. Its torch stands go through the *shared-emitter* spawn path,
which is the half a naive sweep would get wrong, so a room where each torch has
its own light is the headline result. Kakariko's bonfire (five emitters at one
point, which must produce exactly one light) and Ordon at night with the lantern
lit are the other two worth visiting.

Read, in this order:

| Reading | What it means |
| :-- | :-- |
| `emitters → considered → candidates → sites → drawn` | where a light was lost. A big drop at *considered* is normal — most emitters are smoke and screen effects. A drop to zero at *candidates* while you are stood at a fire means the classifier is wrong; the report names which effect it refused |
| `game lights available (point/spot)` | whether the spot registry survives to the bridge. Most of this game's torches and **all** of Link's lantern register there. A spot count that stays 0 while a torch burns answers open question 2 — those sites are falling back to configured defaults instead of the game's own colour. Safe, but worth knowing |
| `game lights with no effect` | how many registered lights the new policy is throwing away. Some are real sources with no particle at all and go dark under the default |
| `culled` | budget or distance. Non-zero in a room full of candles is expected |

**The lantern is the cheapest single check.** Equip it, light it, and watch the
light appear at the flame and follow the lamp as it swings. Then let the oil run
out: the light should go out with the flame, because the same `StopDraw` flag
the game already sets is the first clause of the rule. If it goes out but leaves
a light behind, the grace period is holding a site that should have been let go.

**If it is too bright or too dim,** move `Master Intensity` first — it scales
everything. Only reach for `Derived Intensity` / `Undetermined Intensity` once
you can see which half is wrong: the panel's `reach from the game` count tells
you how many lights each is driving.

**Regression signatures worth naming before you look, so they are recognised
rather than discovered:**

- lights on smoke or water spray → the additive clause is too permissive; look
  for sites whose class is `other` and whose colour is grey in the report
- a fire that strobes → a site is being created and destroyed each frame; check
  whether its position is jittering more than the half-unit epsilon
- a light floating above or beside a fire → `Fire Height Offset`, or the merge
  radius grouping two nearby fires into one
- a light left behind where an effect used to be → a callback-driven emitter
  whose position the game stopped refreshing (`effect-lights.md` §10, hole 1)

### 4. `hideSkyBillboards` — night shadow wandering — RESOLVED

> **PASSED 2026-07-29, and it is the fix.** Turning it on stops the wandering.
> That **confirms the measured hypothesis** rather than merely working around
> it: the 80 m moon quad, anchored to the camera eye 800 m away along the
> moon's orbital direction, was occluding every shadow ray cast toward the moon
> light. The prediction and the observation match, so open issue 2 is closed by
> cause and not just by symptom.
>
> Cost, as predicted: the visible moon and stars go with it. Getting the moon
> back now has a clear route — paint it into the generated dome, where it is
> visible, correctly placed, contributes its own light and is structurally
> incapable of casting a shadow. Not built; see "What to do next".

Freeze the clock at **~330** (night), outdoors, somewhere the wandering has
been seen. Stand still, rotate a full circle, shoot. Toggle Geometry → *Hide
Sky Billboards*, repeat from the same spot.

Success: shadow coverage stops moving with the camera. Cost: the moon and stars
vanish, which is fine — the generated sky paints that region and the moonlight
comes from the distant light, not the billboard.

Failure (still wanders) is a **useful** result: it kills the measured
hypothesis in open issue 2 and points at Remix's denoiser or probe rather than
at captured geometry.

### 4b. Do the stars occlude anything? — NEVER RUN (added 2026-08-11)

> **§4 turned two things off at once and only one of them was ever suspected.**
> The moon quad is drawn by the sun packet; the stars are a different packet
> behind what used to be the same switch. Nobody has looked at the star half on
> its own, so the recommended setup has been deleting the entire night sky —
> including a 13-star constellation the original team placed by hand — to fix a
> problem that was measured on the moon.
>
> **Either answer is a good answer.** "Still wanders" means the stars occlude
> too and the single switch was right; that closes the question for good.

**Setup.** Freeze the clock at **~330** (night, §1), go outdoors, and stand
somewhere the wandering was seen in §4. Then, in the same Geometry section §4
uses:

| Setting | Value | Why |
| :-- | :-- | :-- |
| Hide Sky Billboards | **on** | keeps the moon quad — the measured occluder — out of the world |
| `...Including The Stars` | **off** | the new sub-switch. This is the whole test |

Nothing else changes, and it is one A/B from a single standing position.

> **If the sub-switch does nothing, it is not your setup.** The overlay half and
> the game half of this option landed separately: the checkbox and
> `rtx.dusklight.game.hideStarBillboards` exist in the fork, and the game only
> follows them once the bridge reads that name (and the protocol is bumped to
> match). Until then the setting is reachable game-side only —
> `game.remixHideStarBillboards = false` in the game's `config.json`, which
> needs a relaunch but runs exactly the same test.

**What you should see immediately: stars.** They come back and the moon does
not. If the sky is still empty, the two sides are not talking — check the
Dusklight tab's protocol line before reading anything else into it.

**The measurement.** Stand still, rotate a full circle, and watch shadowed
ground. Then walk twenty paces and do it again.

| What happens | What it means | What to do |
| :-- | :-- | :-- |
| Shadow coverage stays put | The stars do **not** occlude. The switch was two switches. | Leave the sub-switch off in `rtx.conf` and get the night sky back permanently. Record it in the comment at `d_kankyo_wether.cpp` `dKankyo_star_Packet::draw` |
| Shadow coverage wanders again, as in §4 | The stars occlude **too**. The single switch was right all along. | Turn the sub-switch back on and record that in the same comment, so nobody re-opens it. Open issue 2 keeps its answer and gains a second cause |
| Stars visible but sitting *in front of* walls and Link | Separate finding, worth reporting either way: the star shell sits ~300 units from the camera and a path tracer has no sky-list depth reset to hide that | Note it; it does not invalidate the shadow reading above |

**Regression signature to recognise rather than discover:** with the stars
back, the frame gains one batched draw of up to 1200 triangles. If `dx9.draws`
climbs by hundreds instead of one, the batching in `dKyr_drawStar` has been
lost — that is a different bug and issue 13 covers it.

**Prediction on record, so it can be wrong.** The stars cover roughly 0.05% of
the sky hemisphere, are scattered rather than pooled on the light direction,
and the game already fades out any star that lands near the moon on screen. The
moon quad covers about 11 degrees centred exactly on the moon light. So the
expectation is "stays put". **That is arithmetic off the source, not a
measurement** — this test is the measurement.

### 5. Phase C — physical sky — RAN, PARTIAL

> **RAN 2026-07-29 at frozen noon. The scattering works; the sky is being
> dimmed by something else.**
>
> - **Aerial perspective is convincing.** Distant mountainous terrain reads
>   correctly blue. That is C3 working — the far ramp sampling the dome in the
>   view direction — and it is the part that was hardest to predict.
> - **The sky itself comes out dim and "grimier"**, and there is an awkward
>   seam: the mountain silhouette is blue while the sky immediately above it is
>   duller and darker.
> - **Lowering `atmosphere.densityScale` massively improves it**, which is the
>   measurement that identified the cause. The fog was overpowering the sky.
>
> This is **not a Phase C defect** — the same seam shows in Lake Hylia morning
> fog with `physicalSky` off (§8 of the same session), only worse. The cause is
> in the composite and is written up as **open issue 4**. Re-run this test
> after that is fixed; until then Phase C's own blend cannot be judged fairly,
> because part of what you are looking at is the fog eating the sky.

```ini
rtx.dusklight.atmosphere.physicalSky = True
```

The blend is driven by **sun elevation**, not the clock: below 2° entirely the
game's palette, above 28° entirely simulated. So freeze at **180 (noon)** and
A/B `physicalMaxWeight` **0 ↔ 1** — 0 must be pixel-identical to Phase B.

Look at **the shaded side of a wall**, not at the sky: the point of Phase C is
shadow fill, and the sky itself barely changes at noon.

| Symptom | Likely cause |
| :-- | :-- |
| **Inverted gradient** (bright overhead, dark at horizon) | coordinate convention — `cartesianDirectionToLatLongSphere` uses `acos(direction.z)` (+Z pole) against a Y-up world. `DusklightAtmosphere.md` §14.4 |
| Black sky | LUT never populated, or transmittance collapsed |
| Magenta / NaN / fireflies | type conversion in the LUT path (§14.5) |
| Banding | LUT resolution or format |
| Flickers frame to frame | frame-latching (§14.6) |
| Fine, but no fill-light change | dome light not picking up the physical result |

Then watch a sunset through the 28° → 2° band. A visible pop at either
threshold means the ramp needs widening.

---

