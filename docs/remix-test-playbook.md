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

**§§1–5 were run on 2026-07-29: four passed and §5 ran partially. §0 is the
outstanding one** — it covers everything that landed on 2026-08-04, all of which
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
| Emissive Intensity | The one dial. If the lava glows but does not light the room, raise it; if it blows out, lower it. |
| Emitted Colour | Leave on *Reconstructed Albedo*. It is the two-colour ramp — `lerp(FF0000, FFFE63, texture)` — so the texture drives the colour. The other two are there to compare against, not to use. |
| Emissive Surfaces Enabled | Untick for an A/B against no emission at all. |

| What you see | Reading |
| :-- | :-- |
| Lava glows red-to-orange with visible crust, and lights the room | Worked. |
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

### 3. Local point lights — RESOLVED

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

Warp to **Forest Temple → Forest Temple** (`D_MN05`). `d_a_ep` registers its
light on actor init regardless of whether the flame is lit
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

