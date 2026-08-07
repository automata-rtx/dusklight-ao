# Remix integration: open issues and untested work

The volatile half of the Remix effort. [`kankyo-remix.md`](kankyo-remix.md) is
the stable design reference; [`remix-test-playbook.md`](remix-test-playbook.md)
is how to run a test session. [`remix-history.md`](remix-history.md) is an
unmaintained archive of old session notes — a last resort for checking whether
something was already investigated, never a source of fact.

**Rule for this file: an entry states its evidence and its confidence.** Three
separate sessions have written a plausible story here as though it were a
finding, and one of those shipped a fix that did nothing. If a cause is
inferred rather than read, say so.

**Second rule, because several entries below predate it.** The raw
fixed-function D3D9 image is never shown to a player — it is the feed, and
Remix's renderer is the product. So where an entry says a surface is "correct
in raw D3D9", that is *evidence* about where a defect lives, never a
requirement being met. Only the HUD and alpha still have to rasterize
correctly, and where the D3D9 stream cannot carry something the answer is to
implement it in the fork rather than to approximate it in D3D9. Full statement:
`extern/aurora/docs/dx9/remix-material-interface.md` §0.

## State, as of 2026-08-05

CI baselines: dusklight/aurora green on all 8 targets (Windows MSVC x86_64 +
arm64, macOS x3, Linux x2, Android); the Remix fork green on its 3 Windows
configs, **including everything landed on 2026-08-04 and 2026-08-05**: the
two-colour ramp, the self-illumination rule, selective vertex colour, API asset
capture/replacement, and the blend-class reporting. The aurora half is
additionally **syntax-checked** in both the d3d9-on and d3d9-off configs; the
fork half has no cross-compilable harness, so its CI run is the only syntax
check it gets.

**Tested in game so far:** the 2026-08-04 material work was run twice, and both
runs are what produced the findings below. **Nothing landed on 2026-08-05 has
been run** — that is the self-illumination rule, the emitted-colour default, the
instrumentation fixes and the blend reporting.

**`dxvk-remix/RtxOptions.md` was regenerated 2026-08-05 and is now three rows
stale.** It had documented 40 of the 130 `rtx.dusklight.*` options for weeks;
the regeneration reconciled it against the `RTX_OPTION*` declarations in `src/`
in both directions. Since then the 2026-08-06 fog rework added
`rtx.dusklight.atmosphere.mediumFraction` and `.fogLog` and changed
`.multiScatteringScale`'s default from 0.25 to 1.0, none of which are in the
file. Two caveats stand: the build that produced it also carried the unmerged
`claude/dx9-high-res-textures` branch, so eight `texrep` rows describe options
not on this branch yet.

It is **generated, never hand-edited**. A row that reads badly means the
`RTX_OPTION` description string in `src/` reads badly — fix it there and
regenerate with `DXVK_DOCUMENTATION_WRITE_RTX_OPTIONS_MD=1`. Nothing in CI
enforces this, so it goes stale silently; regenerate it at checkpoints.

**Blob shadows are suppressed under Remix, and it is TESTED (2026-08-06):** the
flat quads under dropped items are gone. Landed 2026-08-05 (`rtx.dusklight.game.blobShadows`,
default off, live in the overlay under Geometry). The flat discs the game paints
under rupees, hearts and pots approximate a shadow Remix traces for real from the
same geometry, so drawing them puts a painted shadow on top of a correct one.
Dropped at registration in `dDlst_shadowControl_c::setSimple`, so no draw call is
issued rather than one being hidden downstream. **The game's projected shadows
(`dDlst_shadowReal_c` — Link, major actors) are a separate system and are
untouched.** The reasoning that made the blob suppression correct — a painted
shadow drawn on top of a traced one — transfers to them word for word, and it is
now a confirmed reading rather than a prediction. Not done because nobody has
asked and Link's shadow is a far more visible change than a rupee's; it is the
same one-line suppression in `dDlst_shadowControl_c::setReal` if wanted.

**The volumetric fog's falloff was reworked on 2026-08-06 — fork-side only, and
it has now had one run.** Reported symptom: the volumetric fog did not match the
game's falloff while the depth-based fog did. Reading the fork's derivation
against `mFogNear`/`mFogFar` found three arithmetic faults, none of which needed
a test session to establish:

1. **The near field was over-fogged**, by up to +0.27 opacity where the game's
   ramp shows none, because the medium's density was matched at a single
   distance and the ramp's flat-zero dead zone before `mFogNear` was ignored.
   This is the reported symptom.
2. **The fog never closed to opaque** — 0.75–0.81 at `mFogFar` instead of 1.0 —
   so distant terrain never fully dissolved into the sky.
3. **The near and far halves of the fog aimed at colours 4.4× apart**, so fog
   read dark wherever real lighting was not filling the difference in. Worst in
   exactly the areas with the most strongly tinted fog, which are the dark
   interiors.

Nothing on this side changed: the bridge already pushes the right values and the
protocol is untouched at 6.

**The 2026-08-06 run (Hyrule Field → Lake Hylia → South Faron) settled the
numbers and corrected two of my own claims.** The log alone did it; nothing was
described by eye. Values are in [`kankyo-fog.md`](kankyo-fog.md) §5.1 — the
first real `mFogNear`/`mFogFar` this project has had, since they live in stage
`.dzs` data. Three results:

- **`mFogNear` is zero or negative in every area measured.** The "clear air,
  then fog" shape that produces fault 1's headline +0.27 error **did not
  occur.** In these ramps the old error was +0.02 to +0.04 — real, but the
  smallest of the three faults. Faults 2 and 3 were the large ones here.
- **The froxel grid covers only 10–21% of the ramp**, because Remix clamps it at
  120 m while the game's fog runs 600–1200 m. So the ceiling on how volumetric
  the fog can be is `rtx.dusklight.atmosphere.froxelMaxDistanceMaxMeters`, not
  the fraction knob. Untried.
- Two of the four rows **could not be attributed to an area**, because nothing
  on the wire names the current stage. Recorded as a protocol-7 item in
  `DusklightOverlay.md` §6 "Open" rather than worked around by asking about the
  route — it is a logging defect by rule 2, and it will bite every future
  atmosphere log the same way.

Acted on: the density is now bounded by an error budget
(`rtx.dusklight.atmosphere.maxNearHaze`, 0.02) rather than by a fixed fraction,
because the measured cost of a fixed fraction ranged over two orders of
magnitude between those four areas. That roughly doubles the volumetric share in
three of them and holds the fourth at the budget. The fog log's dedup key was
also fixed: `mFogFar` eases continuously, and against a 1-unit key every frame
was a new derivation — the whole 24-line budget went in **0.23 seconds**, inside
an area already logged.

Still owed a run: **Goron Mines and the Forest Temple** (near, dense, and where
a positive `mFogNear` is most likely), and **Lake Hylia inside the kytag01 fog
bank** rather than beside it. `remix-test-playbook.md` §0g.

**A height cutoff in the Lake Hylia volumetrics — mechanism found 2026-08-07,
attribution inferred, fix untested.** Reported as *"a harsh transition that
didn't look natural"* between the lower and upper parts of the screen, not
always present and worst in the morning when the fog intensifies.

The fork forces Remix's planet-atmosphere shell on outdoors, and **that shell's
ceiling is an absolute world height, not a height above the player.** It was
sized from `mFogFar` — and a scripted fog bank drives `mFogFar` *down*:
kytag01 blends it toward `200` units, two metres
(`src/d/actor/d_a_kytag01.cpp:94` → `d_kankyo.cpp:2510`). So as the bank engages
the ceiling falls from 700 m to the option's 30 m floor, and the medium becomes
a 30 m slab in a 775 m bubble that travels with the player. Everything above it
loses its in-scatter outright.

Fixed fork-side by sizing the shell from the camera's own height plus a fog
depth, so it is always outside the range the fog can be seen through. Nothing on
this side changed. **What was read: the shell geometry, the height derivation
and kytag01's values. What was inferred: that this is the artifact reported.**
Two `ONCE` tripwires now assert the conditions that made it possible, so a
recurrence names itself.

Regression signature to watch: the bank at full strength should now genuinely
extinguish the sun, so the inside of it should read as *white* — lit by the
fog's own colour. If it reads *dark* instead,
`rtx.dusklight.atmosphere.zHalfMin` caps how dense the medium may get.

**One setting note from the same log:** `rtx.autoExposure.enabled` was **True**
in the effective config. The playbook baseline sets it False, because every
brightness judgement is undone by auto exposure before it can be seen. It does
not affect the fog *geometry* measured above, which is why those results stand.

**Two fork guards fire only in CI**, and both have now cost a round:
`CheckRtInstanceSize` (any field added to `RtSurface` grows `RtInstance`;
release-only, so no container check sees it) and `hashStructByMemory`'s padding
assert (this one *is* checkable locally). Listed in the fork's `CLAUDE.md`.

**Protocol is at 6.** When you bump it, bump `kRequiredProtocol` in the fork's
`showDusklightRemixTab` in the same commit.

**The two live rendering defects:**

1. **Materials are coloured, and two-colour ramps are now reproduced exactly**
   (issue 8). Tested 2026-08-04: rupees, hearts and lava all carry colour, but
   the lava read red-and-white because no *stock* Remix texture op expresses a
   lerp between two constants. The fork now evaluates the GX colour combiner
   directly from both endpoints — exact for every ramp material, not just lava.
   **CI-green, untested in game.**
   `extern/aurora/docs/dx9/remix-material-interface.md` §10.
2. **The fog medium dims the generated sky** (issue 4). Cause verified in the
   composite; `skyFogMode` ships two candidate treatments and one is meant to be
   deleted once they have been compared.

**Also open:** the wolf-senses overlay covering the screen (issue 5), the
world-space UI billboards (issue 6), grass shading (issue 7), item drop-shadows
as black quads (issue 11), the ambient grade, and the Controls tab.

**Landed 2026-08-04 alongside the ramp; CI-green, untested in game:**

- **Vertex colour is forwarded selectively** rather than withheld outright — GX
  says per draw whether a stream is material colour or baked lighting, and the
  fork now sets `isVertexColorBakedLighting` per draw instead of globally.
  Materials whose "vertex colour" is a constant were losing that colour
  entirely; they no longer are. §7c of the interface doc.
- **API-submitted assets are capturable and replaceable.** Their mesh hashes are
  content-derived instead of a creation-order counter, and external draws now
  consult the replacer. This is a fork change with no upstream equivalent; it
  matters because the "do it in Remix" half of the philosophy
  (`extern/aurora/docs/dx9/remix-material-interface.md` §0) is only safe if
  anything pushed through the API can still be authored over later. Supersedes
  the old claim that API-submitted meshes and textures are not taken in captures
  and cannot be replaced. **No capture has been taken since**, so this is the
  one item in the list with no evidence at all behind it; the check is two
  captures either side of a relaunch — `remix-test-playbook.md` §0f.
- **HUD fade-in alpha** (issue 12) — the fading constant now rides TFACTOR's
  alpha instead of being discarded.

**Self-illumination (issue 9): rev 4 is TESTED IN GAME (2026-08-06) and works.**
Three earlier revisions cut on a weighted score and all three missed the lava,
which scores 0.00. Rev 4 drops the score entirely: self-lit (no TEV colour stage
reads the rasterized channel) AND a colour of its own AND that colour reading as
a glow. Replayed over the last log that is **6 of 77 materials, every lava and
fire surface, no false positives, nothing to tune** — and in game it caught the
lava and read as properly molten.

`emissive.brightness` was dialled to **10.0**, now the default. Calibrated in
one dark interior; a bright exterior may want less, and none has been looked at.
Not confirmed by that session: whether the per-material derivation holds for a
small pickup as well as for lava (the report named the lava only).

**`grp=` does not work and has been removed.** It was meant to end "which of
these logged materials is the thing on screen?", and every material in that
session reported `grp=-`. The push was in `fpcDw_Execute`, which is where a
process draw is *scheduled*, not where GX commands are *issued* — actor draw
methods enter models into a J3D draw buffer and `dDlst_list_c` walks it later.
Doing it properly needs the label carried to the draw buffer's execution.
**So identifying a material is still a guess**, which is why the lava geysers
remain unexplained.

#### Confirmed working in-game

- **The bridge connects.** Owner log: `RTX Remix detected; kankyo bridge
  active (remixapi 0.6.4)` and `registered D3D9 device with the Remix API`,
  with no missing-export warning, which also proves the `getRtxOptionValue`
  export mechanism works.
- **Dusklight bloom — "massively improved"** after the four fidelity fixes
  and the 100× composite fix. This is the one part of the look that is now
  confirmed close to the real thing.
- The kankyo feed is live and its colour tracks time of day.
- GX→D3D9 fog reaches Remix. Faithful mode consistent; volumetric mode
  over-reactive to kankyo's near/far.
- Vanilla Remix post FX work once their strengths are raised well above the
  near-invisible defaults. No Remix bug.
- The sun/moon distant light runs and produces "interesting results". No
  report of inverted shadows, so the handedness is *probably* right — Flip
  Direction remains in the tab if that turns out wrong.

Added **2026-07-29**:

- **The clock — slider, presets and Freeze Time.** "Flawlessly and as
  expected." Freeze is now available to every A/B from here on, which is what
  made the Phase C reading below trustworthy.
- **Warp.** "Exactly as intended, no issues."
- **Local point lights.** Forest Temple first room, `found 5 / drawn 4 /
  tracked 4`. Needs `localLightIntensity` 19 and `localLightRadius` 10.
- **`hideSkyBillboards`, and with it the night shadow wandering.** Works, and
  confirms the moon-quad cause.
- **Aerial perspective under the physical sky.** Distant terrain reads
  correctly blue — the C3 dome-sampled far-fog colour doing exactly its job.
- **Dense fog.** Lake Hylia morning fog is "suitably intense", which is the
  first evidence from the dense end of the σ mapping. See the note in
  `DusklightAtmosphere.md` §13 about what this does and does not close.
- **No further crashes on level entry** across a long session that included
  many warps and room transitions.

#### Open issues

0. **CLOSED 2026-07-29 — local point lights work.** Forest Temple, first room:
   `Registered by the game: 5   drawn this frame: 4   tracked: 4`.

   No single change is identifiable as "the fix" — the diagnostics pass that was
   supposed to *narrow* the bug appears to have carried it, most plausibly the
   `efplight[0..4]` array that the first implementation never read at all, or
   the NaN guards added at the same time. Recorded honestly: this closed without
   a proven root cause, so if local lights ever regress, start by re-reading
   both arrays rather than assuming the old diagnosis.

   **What the visit settled that matters more than the bug:**

   - **`localLightIntensity` must be 19, not 1.** That is the minimum giving
     usable light, and it is *exactly* the derived alternative reading of the
     attenuation curve — `reach = mPow·√((maxColorByte − 1)/10)`, ~4.3× further
     for a torch and therefore ~19× the radiance. A number derived from the
     game's own `dKy_GXInitLightDistAttn` call landed on the value testing
     picked independently. The conservative default was the wrong bet.
   - **`localLightRadius` 10 is safe**, not just 4. No clipping through the
     Forest Temple light posts, and a larger emitter softens the falloff.

   Both are still non-default. See issue 3.

   **The loose end:** `found 5` but `drawn 4`. One light was seen and then
   rejected on the way through. Candidates, in order of likelihood: a light with
   zero `mPow` or a black colour failing the brightness/reach test (benign and
   correct), a NaN caught by the new guard (worth knowing about), or an
   `efplight` slot holding a stale pointer. Not urgent — four working lights lit
   the room — but it is a one-line logging change to find out, and "benign" is
   currently an assumption rather than a finding.

1. **Crash entering some levels.** `EXCEPTION_ACCESS_VIOLATION` reading
   `0x10`, entirely inside `d3d9.dll` on a Remix-owned worker thread (the
   outermost frames are `BaseThreadInitThunk` / `RtlUserThreadStart`), during
   a cutscene transition right after a camera cut. Local lights were off in
   that run. **Bisect not yet run**: `rtx.dusklight.game.bridgeEnable = False`
   settles whether any of this is ours; if it still crashes, the
   NRC-on-camera-cut path is next (`rtx.neuralRadianceCache.enable = False`).
   NaN guards were added to both light paths regardless — plausible as the
   fix, not demonstrated.

   **Downgraded 2026-07-29.** A long session with many warps and room
   transitions produced no crash at all, including with local lights on — the
   configuration the original crash did *not* have. That is real evidence for
   the NaN guards having been the fix, but it is not proof: the original crash
   was intermittent and tied to a cutscene camera cut, so absence over one
   session is weak. **Do not close this.** If it recurs, the bisect is still
   the first move (`bridgeEnable = False`, then
   `rtx.neuralRadianceCache.enable = False`).
2. **CLOSED 2026-07-29 — the night shadow wandering was the moon billboard.**
   `hideSkyBillboards` stops it. The measured hypothesis below predicted
   exactly this, so the cause is confirmed rather than merely worked around,
   and the analysis is kept in full because it is the reasoning that found it.

   Eliminated earlier: the light direction (locking it changes nothing), NRC
   (persists under ReSTIR), and brightness (persists with moon intensity
   raised).

   **The numbers that identified it**, read out of `d_kankyo_rain.cpp` and
   `d_kankyo.cpp` on 2026-07-28:

   | Fact | Value | Source |
   | :-- | :-- | :-- |
   | Moon quad edge | **8000 units = 80 m** | `d_kankyo_rain.cpp:2614` (`f32 size = 8000.0f`) |
   | Distance from the eye | **80000 units = 800 m** | `d_kankyo.cpp:1770-1784`, orbit radius |
   | Anchored to | **the camera eye** | `d_kankyo_rain.cpp:2431`, `spB4 = camera->view.lookat.eye + envlight->moon_pos` |
   | Direction it sits in | **the moon's orbital direction** | same, `moon_pos` from `setSunpos` |
   | Blend | `GX_BM_BLEND`, SRCALPHA / INVSRCALPHA, alpha test `> 0` | `d_kankyo_rain.cpp:2567-2568` |
   | Moon drawn when | `daytime > 285 || daytime < 67.5` | `dKyr_moon_arrival_check`, `:2284` |

   The direction it sits in is the same direction the distant light comes
   from — `distant_light.slangh:78` samples at `position - direction·100000`.
   So every shadow ray cast toward the moon light sets off straight at an
   80 m quad hanging 800 m away that travels with the player. The set of
   world points whose moon-direction ray passes through it is a prism of the
   quad's cross section, offset from the camera: an ~80 m band of "shadow"
   that slides across the world as you walk. That is the reported symptom
   almost exactly.

   **Why it is night-only is geometric, not a matter of degree.** There is no
   equivalent sun quad. `dKyr_drawSun` sets `draw_sun` but the visible sun is
   the lens-flare system (`dKyr_sun_move`, `lenz_packet`), whose sprites are
   **250–850 units** and sit near the camera — two orders of magnitude less
   area, and not planted out along the light direction. So the mechanism
   simply is not present by day.

   `rtx.dusklight.game.hideSkyBillboards` both tests and fixes this in one
   click, and costs only the visible moon and stars — the generated sky
   already paints that part of the image, and the moonlight comes from our own
   distant light, not from the billboard.

   **Keeping the moon visible is possible.** Unlike the vrbox dome, these
   billboards are textured, so `rtx.skyBoxTextures` can categorise them, and
   `InstanceCategories::Sky` is explicitly excluded from visibility rays —
   `instance_definitions.h:93`, *"Sky excluded as often it should not be traced
   against when calculating visibility"*. Tagging by hash works independently
   of `rtx.skyAutoDetect` (`rtx_types.cpp:409` is the explicit path,
   `:594` the auto one), so `skyAutoDetect = None` does not block it.
   **Untested caveat:** whether a Sky-tagged draw still renders visibly while
   our generated dome light has replaced the sky probe is not established —
   check before relying on it.

   The third option, if the moon is wanted back: draw it into the generated
   sky texture. It already knows the celestial direction, and a moon painted
   into the dome is visible, correctly placed, contributes its own light, and
   is structurally incapable of casting a shadow. Not built — **and now that
   the billboard is confirmed guilty, this is the way to get the moon back,
   not the Sky-tagging route above.** Tagging keeps a real quad in the world
   and depends on the untested caveat; painting into the dome removes the
   object entirely.

3. **Local light defaults are wrong in the shipped build.** Testing settled
   `localLightIntensity` at **19** (from 1.0) and `localLightRadius` at **10**
   (from 4.0), and neither is the default, so a fresh install still comes up
   with lights too dim to be worth having. The intensity value is not a taste
   call — it is the derived reading of the game's own attenuation curve, and it
   is now the one with evidence behind it.

   Note the two interact: the radiance is solved so the light still reaches the
   same distance, so a larger radius needs *less* radiance. 19 and 10 were
   tested together and should ship together rather than being applied one at a
   time.

   Open question alongside it: whether `localLights` should now default **on**.
   It is proven working and it is the only thing lighting interiors and night,
   but it is also the newest of the light paths.

4. **The fog medium dims the generated sky.** Reported 2026-07-29 at frozen
   noon with `physicalSky` on, and again — worse — in Lake Hylia morning fog
   with it off. The visible sky reads dim and "grimier" than it should, and
   there is a seam where distant terrain is convincingly blue but the sky
   immediately above the silhouette is duller. Lowering
   `atmosphere.densityScale` improves it markedly, which is what identified the
   mechanism.

   **The cause is verified in the composite, and it is an asymmetry between the
   two halves of our own fog:**

   | Half | What it does to a sky pixel |
   | :-- | :-- |
   | The far ramp, `applyFog` (`composite.comp.slang:625`) | **Exempts it.** `if (primaryMiss) return;`, with a comment saying running the ramp on the sky "would drive it to full fog and replace the sky with a flat colour" |
   | The volumetric half, `applySkyContribution` (`:585`) | **Fogs it.** `domeLightArgs.radiance * sampleDomeLightTexture(...) * volumeAttenuation`, where `volumeAttenuation` is the froxel transmittance over the *whole* grid — and the froxel in-scatter is already in `radianceOutput` before the sky is added |

   So a sky pixel comes out as *(in-scatter over the full grid depth)* +
   *(dome radiance × transmittance over the full grid depth)*. Someone thought
   carefully about not fogging the sky in one path; the other path does it
   anyway, by a different route.

   **Why it is much worse for us than for stock Remix:** §14.2 — our medium is
   deliberately *far* denser than air, because the game's fog is an artistic
   device that reaches full opacity in tens of metres. `exp(-σ · gridDepth)`
   with that σ is a large number, and all of it lands on the sky. Stock Remix's
   near-clear default medium would barely show it.

   It also explains the seam exactly. Distant terrain fades toward the far
   ramp's colour, which per C3 samples the dome *in the view direction* — the
   right colour, hence "convincing". The sky beside it is attenuated dome plus
   `fog_col`-tinted in-scatter — a different treatment of the same far field.
   Two descriptions of one day, which is precisely what §0 exists to prevent.

   **The owner's instinct was right and the mechanism was not.** The report
   suspected the sky needed tagging as Sky in Remix and that the lack of a
   texture forced it API-side. The first half is correct — the sky is not being
   treated as "at infinity, exempt from fog". But `InstanceCategories::Sky`
   cannot reach this: the generated sky is not captured geometry at all, it is
   a dome light sampled on ray miss. There is nothing to tag even in principle.
   The exemption has to happen in the composite, where half of it already does.

   Fix shape, not yet written: bound the sky's volume attenuation instead of
   applying the full grid depth. Exempting `primaryMiss` outright matches the
   far ramp and is the smallest change; a `skyFogWeight` scalar is better,
   because a genuinely foggy day *should* veil the sky — just not by the amount
   a 100 m-opacity artistic medium implies. Either is one guarded branch, in
   the style §11 requires.

5. **Wolf senses renders as an opaque overlay.** Entering senses puts up a
   heavy black surround with a pure white centre where the see-through region
   should be — the screen is effectively covered. Not investigated yet.

   Consequence for the backlog: **this blocks the wolf-senses route to testing
   the mono overlay and composite base weight, but not the twilight route.**
   Those two effects are reached by bloom tables 1/2 (twilight) as well as 3
   (senses), so that test should be done in a twilight zone instead and is not
   gated on this.

6. **World-space UI billboards appear only intermittently.** The targeting
   arrow and the fire billboards in torch-lit areas appear to share a fate:
   they show up at the same times, and the fire billboards were seen appearing
   during room transitions in the Forest Temple. The targeting arrow looked
   correct only while actively targeting, and not always even then — it
   depended on player and camera position. Under shadow it goes dim and reads
   wrongly, which suggests it is being lit as ordinary world geometry when it
   is meant to be unlit UI.

   **Neither is visible in Remix's texture categorization screen.**

   **Investigated 2026-07-29. The mechanism is Remix's RTX injection boundary,
   and it is not an aurora bug.** A further clue narrowed it: this only ever
   happens **while the letterbox black bars are up** — Z-targeting or a dungeon
   door transition — though bars do not guarantee it.

   **The boundary, verified in the fork:**

   | Step | Code |
   | :-- | :-- |
   | The first **orthographic, z-write-disabled** draw on the primary RT is classified UI | `isRenderingUI()`, `d3d9_rtx.cpp:559` |
   | That classification returns `Rasterized` **and sets `triggerRtxInjection`** | `makeDrawCallType`, `:519` |
   | From then on, `internalPrepareDraw` early-returns for **every remaining draw in the frame** — `Ignore` if `rtx.skipDrawCallsPostRTXInjection`, else `PreserveDrawCallAndItsState` | `:576-591` |

   A post-injection draw never enters the raytraced scene, so **its textures are
   never categorised**. That is precisely the "not in the categorization screen"
   symptom, and it means **no dev-menu tagging can ever reach these draws** —
   the same shape of trap as the vrbox sky, arrived at by a different route.
   `rtx.uiTextures` is checked inside `isRenderingUI()`, which is only reached
   *before* injection, so tagging is unavailable exactly when it would be needed.

   **Where these two draws sit in the frame** (`m_Do/m_Do_graphic.cpp`):

   | Line | Draw | Projection |
   | :-- | :-- | :-- |
   | 2257 | `drawCopy2D` | 2D |
   | 2642 | `drawXluList2DScreen` | perspective (explicitly re-set) |
   | **2689** | **`drawOpaList3Dlast` — the targeting cursor** | **perspective** |
   | **2714** | **`particle_draw2Dgame`** (JPA group 14) | **ortho** |
   | 2717 | `trimming()` — the letterbox bars | **ortho + `GXSetZMode(GX_FALSE, …)`** |
   | 2722 | `calcFade` | ortho |
   | 2824+ | HUD — `draw2DOpa` / `OpaTop` / `Xlu` | ortho |

   `trimming()` at 2717 is a textbook `isRenderingUI()` trigger: ortho, z-write
   off, on the primary RT. It is also **where the letterbox is defined** — the
   bars are sized from `view_port->scissor` against the viewport, and on PC the
   guard around it is compiled out, so "bars visible" is exactly "the D3D9
   scissor is smaller than the viewport".

   **Two things this settles.**

   1. **The targeting cursor is not UI, and never was.** `d_attention.cpp:1619`
      creates a real J3D model (`NoticeCursor`, yellow and red variants with
      BCK/BPK/BRK/BTK animations) and submits it with `dComIfGd_setList3Dlast()`
      under a perspective projection. So "it goes dim under shadow" is the
      *correct and expected* result of path-tracing it — a game-side fact, not a
      Remix misclassification. `DB_LIST_3D_LAST` has exactly one producer in the
      whole game and one draw site.
   2. **The cursor and the flames are not one system.** The flames are JPA
      "simple" particles (`dComIfGp_particle_setSimple` — `d_a_ep.cpp:495`; the
      Forest Temple's are `d_a_obj_lv1Candle00`), and they are *not* in the
      group-14 2D pass. So "they appear together" is not a shared code path; it
      is a shared *position relative to the injection boundary*.

   **Ruled out, each with evidence:**

   - **2D draw list overflow.** `dDlst_list_c::set` silently drops when full
     (`d_drawlist.cpp:1989`, `if (p_start >= p_end) return 0;`) and the lists are
     fixed-size (`mp2DXlu[32]`, `mp2DOpa[64]`, `mp2DOpaTop[16]`, `mpCopy2D[4]`).
     A real hazard, and worth remembering — but every caller is HUD, menu or
     message code, not these two.
   - **`GXPeekZ`.** Aurora implements it via a depth-snapshot path
     (`lib/dolphin/gx/GXCpu2Efb.cpp`). Neither element uses it; the sun lens
     flare and the insects do.
   - **`GX_DEBUG_GROUP`.** Calls through in both configurations
     (`include/helpers/gx_helper.h:24`) — it is not swallowing the draws.

   **What is still open, stated plainly:** the boundary explains the
   intermittency, the "appear together", and the categorization absence. It does
   **not** yet explain why the letterbox specifically helps — `trimming()` sits
   *after* both draws, so the bars cannot themselves be the trigger that saves
   them. Something else that correlates with letterbox must be issuing an ortho
   z-write-off draw *earlier* in those frames. The transition wipe
   (`dDlst_list_c::wipeIn` / `calcWipe`), the fade, and `drawCopy2D` at 2257 are
   the candidates.

   **Also worth flipping around before assuming which way is the bug.** For a UI
   arrow, "correct" probably means flat and unlit — which is the *rasterized*,
   post-injection path. "Dim under shadow" is the *path-traced*, pre-injection
   one. So the arrow may be behaving correctly precisely when it lands **after**
   injection, and the goal is to get it there reliably rather than to rescue it
   into the raytraced scene.

   **Three experiments that would settle it, cheapest first:**

   1. Log `m_drawCallID` at the moment injection triggers, with and without
      bars. Two numbers, and it is settled completely.
   2. `rtx.skipDrawCallsPostRTXInjection = False` — post-injection draws still
      rasterize. If the arrow and flames become reliably visible but flat, the
      boundary is confirmed and the question becomes which look is wanted.
   3. `rtx.drawCallRange` to bisect the frame and find the injection index
      directly from the dev menu, with no rebuild.

   **PINNED 2026-07-29 — the flame half is parked, and it probably is not this
   issue.** The owner corrected a load-bearing detail: the "bright white circle"
   at a lit torch is **not** the animated fire, it is a separate circular
   sprite. The torch emits three named resources at one position —
   `ZI_J_O_fire_a.jpa` (`0x100`), `ZI_J_O_fire_b.jpa` (`0x101`) and
   `ZI_J_O_kagerou.jpa` (`0x103`, heat haze), `d_a_ep.cpp:423-431`.

   Since `fire_a` and `fire_b` are emitted back to back at the same position in
   the same frame, an injection boundary cannot stably separate them — so the
   flame's problem is a property of that draw rather than its frame position.
   The competing explanation is that the white circle **is** a fire sprite
   saturated to white by aurora's compare-mode TEV approximation — the same
   approximation that whitens ground textures in raw D3D9. That whitening is no
   longer itself a defect (the raw image is never shown); it is the *shared
   mechanism* that makes it a suspect here, where the artifact does reach Remix.
   Full write-up, including the two experiments that decide ownership (read the
   aurora `warn_once` log at a torch; A/B raw D3D9 against Remix as a
   diagnostic), is in
   `aurora-ao/docs/dx9/unsupported-effects.md` §"PINNED — the torch flame".

   The targeting-arrow half of this issue is unaffected and still belongs here.

7. **Grass patches shade wrongly under Remix; fine in raw D3D9.** Reported
   2026-07-29: blades glow in the dark, or come out too dark, with a very
   delayed lighting response, generally reading as a different material from the
   rest of the scene. Replacing the billboard blades with real geometry is
   blocked because their hashes are unstable.

   **The hash instability has a specific cause and an existing fix path.**
   `dGrass_packet_c::draw` (`src/d/actor/d_grass.inc`) has two paths. The
   **batched** one — the default for standing grass — merges every blade into
   four buckets and emits them as a single immediate-mode
   `GXBegin(GX_TRIANGLES, GX_VTXFMT1, GX_AUTO)` stream under
   `GXLoadPosMtxImm(identity)`, i.e. all blades pre-transformed into world space
   in one dynamic vertex stream. Its positions change whenever *any* blade
   moves, is cut, regrows or changes bucket, and Remix's
   `rtx.geometryAssetHashRuleString` defaults to
   `positions,indices,geometrydescriptor` — so the asset hash churns constantly
   and the whole patch is one unidentifiable instance.

   The **per-blade** path, currently used only for regrowing blades, calls
   `GXCallDisplayList(mp_Mkusa_9q_DL, …)` with a per-blade
   `GXLoadPosMtxImm(get_model_mtx(...))`: static geometry plus a transform,
   which gives a **stable hash and one instance per blade**. The Remix-friendly
   path already exists in the same function; the batching optimisation is what
   takes it away.

   **BUILT 2026-07-29 — `rtx.dusklight.game.perBladeGrass`, protocol 5.** Off by
   default, because it costs exactly what the batching saves. Turning it on
   draws each blade from its display list with its own position matrix, so every
   blade is a separate instance with a hash that holds still. That is the
   prerequisite for everything else wanted here — tagging, replacement, and
   denoiser history — and it addresses the delayed lighting directly, since an
   instance whose identity churns every frame cannot carry history at all.

   Regrowing blades still go through the existing per-blade block below the
   batch loop, which owns their TEVREG2 alpha ramp; the new path skips them
   rather than duplicating it. **Untested in game.**

   **The other two symptoms are separate and worth testing independently:**

   - **Glow in the dark.** First suspect is emissive blend translation.
     `rtx.enableEmissiveBlendModeTranslation` defaults **true**, and
     `rtx_instance_manager.cpp:718-760` promotes several blend factor pairs to
     `kAlphaEmissive` — notably `SRC_ALPHA / ONE` and premultiplied
     `ONE / ONE_MINUS_SRC_ALPHA`. Plain `SRC_ALPHA / ONE_MINUS_SRC_ALPHA` is
     *not* promoted, so this hinges on the blend mode in grass's material
     display list, which is a binary blob and has to be read at runtime rather
     than from source. **One-click test:** toggle
     `rtx.enableEmissiveBlendModeTranslation = False` and see whether the glow
     stops; if it does, the fix is to tag grass rather than to disable the
     feature globally, since real particles want it.
   - **Too dark / distinct shading.** Aurora's Remix hint stage advertises
     `colour = TEXTURE * DIFFUSE`, `alpha = TEXTURE` (`dx9_tev.cpp:928-960`), so
     grass albedo becomes texture × raw vertex colour. Aurora never evaluates
     the GX light model (`D3DRS_LIGHTING = FALSE`), while grass is drawn with
     GX lighting *on* — `GXSetChanCtrl(GX_COLOR0, GX_TRUE, GX_SRC_VTX, …)` plus
     a per-blade `GXSetChanAmbColor` from kankyo. So a vertex colour that was
     authored as one input to a lighting equation is being consumed as finished
     albedo.

     **Partly addressed 2026-08-04 — CI-green, untested in game.** Lighting
     *on* is exactly the case §7c now forwards as authored material colour, so
     the fork should be setting `isVertexColorBakedLighting = false` for these
     draws and Remix should stop normalising the stream away and relight it
     instead. **Inference from the `GXSetChanCtrl` call above, not measured** —
     `vtxUse=` on grass's `matrep.sum` line says which verdict it actually got,
     and that is the first thing to read. Either way it does not supply the
     per-blade ambient, which is still not translated. If grass changes
     brightness or saturation this round, this change is why.

     Worth checking whether grass ends up in Remix's alpha-blend
     transparency path rather than as an opaque cutout — a blended surface is
     lit quite differently from an opaque one, which would explain "distinct
     from the rest of the scene" on its own.

8. **Rupees, hearts and Goron Mines lava render greyscale under Remix; correct
   in raw D3D9.** Reported 2026-07-29. **A fix shipped on 2026-07-29, was tested,
   and did nothing.** Re-investigated 2026-08-03; the earlier diagnosis was wrong
   in its central claim.

   **The mechanism, read on both sides.** Remix rebuilds a material from one
   texture stage. An argument it cannot decode resolves to identity — `vec3(1.0)`
   — so a lost colour term *bleaches* rather than darkens. That part of the old
   entry was right.

   What was wrong was where the loss happens. Remix decodes
   `MODULATE(TEXTURE, TFACTOR)` perfectly well, and `materialize()` already hands
   a material's first constant to TFACTOR. **These materials were being read
   correctly until aurora's own Remix hint stage overwrote them.** The hint wins
   the stage Remix reads and can only advertise `TEXTURE × DIFFUSE`, with
   `DIFFUSE` substituted as opaque white on meshes carrying no vertex colours.
   Texture × white is exactly a greyscale rupee.

   **Why the shipped fix was a no-op.** `389e4d5` is correctly engineered — the
   D3D9 state it emits does match what Remix accepts, and it would have applied
   the tint. But every part of it is gated behind `albedo_tint()`, which requires
   the lead op to be exactly `MODULATE` (rejecting the routine `MODULATE2X`), no
   added term, and no complement — and which reduces the stage chosen by
   `preferred_albedo_stage()`, a function that deliberately skips the luminance
   textures the fix exists to repair. With the predicate false the commit emitted
   zero additional D3D9 state: identical stream, identical pixels.

   **The game-side fact that makes this a whole class rather than three
   objects.** The seven rupee colours are seven byte-identical resource rows —
   same archive, same model, same animation — differing only in which animation
   frame is held (`d_item_data.cpp`). One model, one texture, seven colours.
   Hearts are the same shape; lava is the same with the animation playing. So
   the colour lives in a TEV colour register or konst, which aurora *can* see —
   nothing is hidden from us.

   **First attempt 2026-08-03 (hint made conditional): tested, and it was safe
   but nearly inert — 3 materials out of 111.** The log said exactly why, and it
   was a wrong *model*, not a coding error.

   **The material shape we assumed does not exist here.** Every attempt so far
   looked for `greyscale texture × one tint colour`. Measured over 111 real
   materials, the dominant shape is a **two-colour ramp**:
   `lerp(colourA, colourB, textureIntensity)` — the texture selects between two
   authored colours, which is how one rupee texture yields seven rupee colours.
   A multiply is only the special case where colourA is black. **104 of 111
   materials reported no tint**, and the 6 that were detected were exactly the
   black-floored ones — which came out of Remix correctly coloured. Same code,
   same session, split precisely along that line.

   **Second attempt 2026-08-04: evaluate instead of pattern-match.** The GX
   colour pass is now computed with the texture pinned to black and to white,
   and the two endpoints drive both the colour advertised and the *operation*: a
   multiply cannot represent a ramp with a coloured floor (it falls to black
   where the texture is dark, which darkens the surface), so those use `ADD`,
   which Remix also decodes. Simulated against the captured materials: **30 now
   carry colour, up from 6.** Also fixed: the albedo stage selector now prefers
   a stage that *reads* its texture over one that merely binds it. **Untested in
   game.**

   **Third attempt, same day: reproduce the ramp instead of approximating it.**
   The two-colour ramp is now exact. No *stock* D3D9 texture op expresses
   `lerp(colourA, colourB, texture)`, but the fork is ours: aurora ships the
   second endpoint in the unused half of `D3DMATERIAL9` and the fork's shader
   evaluates `mix(rampLo, rampHi, albedo)`, which is the GX combiner
   `a·(1−c) + b·c` itself. That covers every ramp material, not just lava. The
   op choice above (`ADD` for a coloured floor, `MODULATE` for a black one)
   survives as the fallback for materials the ramp declines — `ramp=` in the log
   says which applied. `rtx.dusklight.rampMaterials` (F1 → Materials →
   *Reproduce Two-Colour Ramps*) is the A/B. **CI-green, untested in game.**
   `extern/aurora/docs/dx9/remix-material-interface.md` §10.

   The framing worth keeping from this: "Remix cannot express X" was true of
   stock Remix and was treated as permanent for a week. Check whether a
   constraint is real *for this fork* before designing around it — §0 of the
   interface doc.

   **What to look for, since the risk changed with the fix.** Grey means it did
   not fire; the log now prints `out0`/`out1` — what the surface *should* be —
   next to what we advertised, so a mismatch is readable rather than guessable.
   With the ramp on, a material that reads flat, or whose light and dark ends
   are inverted, means the endpoints were swapped rather than approximated.

   Full account of the interface and the rules that follow from it:
   `extern/aurora/docs/dx9/remix-material-interface.md`.

   The general rule that survives from the old entry: **when a material tint can
   go to either constant slot, prefer TFACTOR** — only one of the two survives
   into Remix.

9. **Emissive surfaces.** Raised 2026-08-03 (Goron Mines lava is unlit as well
   as grey). Tested in game 2026-08-04 and again 2026-08-05. **Rev 3 is in the
   tree, CI-green, untested in game.**

   **The finding that matters, and it kills the premise twice over.** Rev 1's
   rule required GX lighting to be *disabled*; in the Goron Mines every
   candidate lava material has **`lit=1`**. Rev 2 replaced the predicate with a
   score — and the 2026-08-05 log established that **the main lava pool scores
   `0.00` on every one of the three signals**: lighting on, channel colour from
   the vertex stream, no over-range stage. Identical in both sessions'
   `matrep.sum` output (`mk=D572C706…` and `mk=1C95BA4B…`, 32×32 `GX_TF_IA8`,
   `tfactor=FFFF0000`, ramp `FF0000 → FFFE63`).

   Combined with the earlier measurement (69 of 117 materials unlit in a mixed
   scene): **no single GX fact identifies an emitter, and on the surface this
   feature exists for they all read zero.**

   What the 2026-08-04 rule caught at its 0.70 default: three brown materials
   (`965744`, `784537`, `8A503E`) — the false-positive family already flagged as
   likeliest. The lava-family materials that *do* score (0.50 and 0.75, colours
   `FF0000` / `FF6432` / `FF6B00`) are the smaller pools and fire, not the main
   surface.

   **The signal itself was wrong, and that is the actual root cause.** Rev 2
   scored `colorChannelConfig[GX_COLOR0].lightingEnabled == false` — a statement
   about the *channel*, not about whether the TEV program consumes it. The
   lava's colour program is `cc=[C2, C1, TEXC, ZERO]` then a pass-through, with
   **no raster input anywhere in it**: its colour is fixed regardless of the
   lights, which is what self-lit means. Measured over 77 materials in the
   2026-08-05 session — 45 have the channel flag off, 36 never read `RASC`, and
   **10 have lighting on and still never read it**, the lava among them. It was
   also wrong the other way: lighting off *with* `RASC` read and
   `matSrc=GX_SRC_VTX` is baked room lighting (§7c) and scored the full 0.50.
   Rev 3 scores "no TEV colour stage reads `GX_CC_RASC`/`RASA`" and reports
   `ras=` next to `lit=` so the two can never again be read for each other.

   **Replayed over that log, the shipped defaults accept 9 of 77 materials** —
   all four lava and fire surfaces (`D572C706`, `1C95BA4B`, `9866CEA2`,
   `40D45477`, `7E31CACC`) plus three browns. The one accepted material that
   *does* read the raster channel is the likeliest false positive, so raising
   the threshold to 0.50 drops exactly it. Rejections: 30 never evaluated, 21
   too grey, 15 vertex-stream colour, 2 too dark.

   **Rev 4 — a rule, not a score.** Self-lit is the basis, as it should have
   been from the start, but it is not sufficient: 9 of the 20 evaluated self-lit
   materials in that session are EFB copies and full-screen quads, and a white
   screen blit must not light the room. Every one of those is a bare texture
   pass-through with no colour of its own. So:

   > **self-lit** (no TEV colour stage reads the rasterized channel)
   > **AND has a colour of its own** (authored in GX constants — not mixed from
   > the vertex stream, not a plain `black → white` pass-through)
   > **AND that colour reads as a glow** (`chroma ≥ 0.50` **or** `luma ≥ 0.70`)

   The **or** in the last clause is what finally killed the brown false
   positives: an authored glow is a strong colour or it is near-white-hot, and a
   muted mid-tone is a surface colour.

   **Replayed over the 2026-08-05 log: 6 of 77 materials, no false positives,
   nothing to tune** — `D572C706`, `1C95BA4B`, `9866CEA2` (the lava, `FF0000`),
   `40D45477` (`FF6B00`), `7E31CACC` (`FF6432`) and `EFF68502` (`FFF0A0`, a warm
   glow texture). Rejections: 30 not evaluated, 27 read the lit channel, 9 no
   colour of their own, 5 neither saturated nor bright.

   `threshold`, `requireAuthoredColor`, `minLuma` and `minChroma` are gone.
   What remains is `enable`, `intensity`, `colorSource` (default reconstructed
   albedo — the ramp, so the texture drives the colour) and the two glow
   constants. **Only `intensity` is expected to be touched.**

   The evidence score is still computed and logged; nothing decides on it.

   **Identification is still the weak point.** `grp=` does not work — see the
   State section above — so which logged material is the geyser, the pool or the
   fire is inferred from texture size, format and ramp endpoints, not known.
   That is why the grey geysers remain unexplained.

   Design and all three measurements:
   `extern/aurora/docs/dx9/remix-material-interface.md` §9. Log format:
   `extern/aurora/docs/dx9/material-report.md`.

   **Emitters illuminate rather than merely glowing, but only because the
   surface flag is set.** `NEECacheUtils.shouldSampleObject` is
   `isEmissiveBlend || isEmissive`, so next-event estimation reaches an emissive
   triangle only when `RtInstance::surface.isEmissive` is true; the fix sets it.
   That flag and one debug view are its only readers — post-FX's own motion-blur
   `isEmissive` is computed from radiance in `geometry_resolver.slangh` and is a
   different flag, despite the name. (An earlier revision of this entry claimed
   otherwise.)

10. **Vertex colour carries baked lighting on *some* draws, and was being
    forwarded to Remix on all of them.** **RESOLVED IN CODE 2026-08-04, not yet
    in game — and it settled a claim the docs had backwards, twice.**

    Testing `rtx.vertexColorIsBakedLighting` was decisive: turning that
    normalisation *off* made shaded areas visibly **darker**, which means the
    vertex colours do carry baked lighting and shadow. `kankyo-remix.md` had
    stated the opposite ("GX lighting isn't baked into vertices, so there is no
    double-counting risk"); that line is now corrected.

    A path tracer relights the scene, so baked lighting in the albedo
    double-counts. The first response was to stop advertising vertex colour to
    Remix at all — the real D3D9 stages still used it. **Superseded the same
    day:** withholding it always was too blunt and threw away real material
    colour. GX states the difference per draw, in the colour channel's control:

    | GX state | The stream is | What aurora does |
    | :-- | :-- | :-- |
    | lighting **enabled**, `CLR0` present | authored material colour, which GX would then multiply by computed lighting | **forwards it** — the path tracer supplies the lighting |
    | lighting **disabled**, `CLR0` present | the finished channel output, where this game bakes room light and shadow | **withholds it** |
    | no `CLR0` attribute | a constant from the channel's material colour register | **evaluates it** into the material |

    Aurora ships the verdict in `D3DMATERIAL9::Specular.r` and the fork sets
    `isVertexColorBakedLighting` per draw from it; Remix's global
    `rtx.vertexColorIsBakedLighting` still governs draws aurora does not mark.
    `vtxUse=` on `matrep.sum` prints which of `material`, `bakedLight` or
    `const` applied. `extern/aurora/docs/dx9/remix-material-interface.md` §7c.
    **CI-green, untested in game.**

    **The regression signature:** a surface that gains or loses brightness and
    saturation this round is this change. The case that remains a judgement call
    is a draw with lighting disabled whose vertex colour is genuinely authored —
    a per-vertex tint or fade on an effect. Those are withheld today, so a lost
    colour gradient means `vtxUse=bakedLight` on a draw that wanted `material`.

    The original observation about that option, kept because it explains why the
    experiment was worth running:

    The option defaults **on**, and what it does is normalise vertex colour —
    dividing each component by the largest, then mixing toward that result. The
    effect is to remove all vertex-colour *brightness* and roughly 40% of its
    *saturation*, on every draw. It is a global transform, not per-material,
    which is precisely why it could be ruled out as the cause of a defect
    affecting only three objects.

    Its intent is sound for a game that bakes lighting into vertex colours.
    **Corrected 2026-08-04:** the paragraph that stood here argued this game
    does not — "aurora never evaluates the GX light model, so a vertex colour
    here is authored material colour, not baked light". The first half is true
    and the conclusion does not follow: the game bakes the light into the
    vertices itself, which is what the test above measured. Kept because that
    reasoning is why the global option looked safe to leave on.

    **Free to test, no build required:** `rtx.vertexColorIsBakedLighting = False`
    in `rtx.conf` still flips the *default*, but it no longer decides the whole
    scene — draws aurora marks as material colour are already exempt from it, so
    an A/B on this option now measures only the unmarked remainder.

    The fix built for it is per-draw rather than global, as above. **The signal
    is the pair, not the lighting bit alone** — `lit=0` with `matSrc=GX_SRC_VTX`
    is the baked shape, and it is 44 of the 117 materials measured for issue 9.
    An earlier version of this paragraph named only the lighting bit, which
    would also have caught every emitter.

11. **Item drop-shadows render as a black quad under Remix.** Long-standing;
    reported again 2026-08-04, and it cannot be tagged away in the dev menu.

    **Inference, not yet verified.** The game draws those round shadows as a
    *projected* texture, and stock Remix does not support projected texture
    transforms — `unsupported-effects.md` R6, which logs
    `Use of projected texture transform detected`. With the projection dropped
    the quad samples flat, so the whole rectangle takes one dark value. That
    fits every reported property, including why no texture tag reaches it: the
    defect is in the transform, not the texture.

    **Cheap confirmation:** the `matrep.gx` line for the shadow material will
    show a `GX_TG_MTX3x4` texgen, and aurora already emits
    `texgen: camera-space source without invertible world` for the related case.
    Confirm before building anything.

    **Likely fix, once confirmed:** stop drawing them under Remix, which is the
    policy already applied to moya cloud shadows and sky billboards — the path
    tracer casts real shadows, so the projected fake is redundant as well as
    broken. That is a game-side switch alongside `hideVrbox` /
    `hideSkyBillboards`. Note this is *not* a case of accepting a Remix
    limitation: teaching the fork the projected transform is available and the
    ramp work shows it is a reasonable move. It is not worth doing here because
    the effect being emulated is one the path tracer produces for real — if some
    other projected-texture effect turns out to be wanted, the fork is the
    place to put it (`remix-material-interface.md` §0).

12. **HUD fade-in effects drew as opaque squares.** Reported 2026-08-04: A-button
    prompts and Epona's spur icon appeared as an expanding rectangle with the
    effect in the middle and no transparency around it.

    **Cause found in the log and fixed 2026-08-04.** 18 of 111 materials have an
    alpha pass of `konst × texture-alpha` — a constant fading the texture's own
    alpha, which is exactly how an effect fades in. Aurora's hint stage
    advertised the texture's alpha *unconditionally*, discarding the constant,
    so the quad reached Remix fully opaque. The scale now rides TFACTOR's alpha
    channel, which the colour tint does not use. **CI-green, untested in game.**

    **This is the one 2026-08-04 change that touches opacity, and opacity is one
    of the two things that still has to rasterize correctly** — Remix reads the
    stage's alpha to build the alpha test. Regression signature: alpha-tested
    foliage, grates or grass going solid or vanishing. That would outrank every
    other result in the session.

#### Built and CI-green but NEVER RUN

*This list was five items long on 2026-07-28 and is two on 2026-07-29. It had
also accumulated a duplicated "promoted out" block, which has been folded away —
everything tested now lives in "Confirmed working in-game" above.*

- **The ambient grade** — off by default, still never reached. Unproven that the
  ambients arrive sane (watch the tab's readout and the grade's "Resolved tint"
  line) and whether 0.65 strength reads as mood or as a cast. Deliberately
  skipped on 2026-07-29.

  One caution that has grown teeth since it was written: the grade
  double-counts against the dome light's fill, so it must be tested *alone*, and
  ideally not until open issue 4 is fixed — the sky's contribution to the image
  is currently wrong, so grading on top of it would be tuning against a moving
  target.
- **The Controls tab** — a placeholder with no functionality at all. Needs live
  key capture, binds crossing the bridge in both directions (the game owns the
  current binds, so the overlay has to read them before it can show them), and
  a decision on who owns conflict resolution — doing it in both places means
  two different answers.

**Partly reached:**

- **Mono overlay and composite base weight** — the wolf-senses route is blocked
  by open issue 5 (the senses overlay covers the screen). The **twilight route
  is not blocked** and is how this should be tested: bloom tables 1/2 drive the
  same golden tint, 37.5 % desaturation and 0xD2 base dim.

**Remaining unknowns for local lights**, now that they work (open issue 3
carries the settings): the churn cost in a busy room is still unmeasured, and
`mFluctuation` — the per-light flicker amount, 1.0 on every torch and 100 on
bombs — is still ignored, because applying it would mean a re-create every frame
for every flickering light.

**"The sun seems tied to Link" — investigated 2026-07-26, no tie found, and
since narrowed.** Four things were checked and none can carry a dependency on
the player:

1. `setSunpos` (`d_kankyo.cpp:1666`) has **no rotation term at all** — the
   orbit is a function of `daytime` and an eye translation that cancels in
   the direction. (An earlier numerical check varied camera *position* over
   720 times and would not have caught an orientation dependency, so this was
   re-read rather than re-run.)
2. `dKy_SunMoon_Light_Check()` (`d_kankyo.cpp:10974`) keys on stage name and
   darkworld state only.
3. Remix's `direction` convention is the one we push:
   `distant_light.slangh:78` samples at `position - direction·100000`, so it
   is the direction light *travels*, and our `-toBody` is correct.
4. Aurora hands Remix true world space (`world = modelView · viewInv`,
   `dx9_draw.cpp:354/361/505`).

Also ruled out: the clock (~0.6°/s of sun motion — visible over a minute, not
over a lap) and vanilla's Link-following light (`D3DRS_LIGHTING = FALSE`;
aurora never evaluates the GX light model, so a light the game sets up at
runtime reaches neither the D3D9 albedo nor the vertex stream). **Narrowed
2026-08-04:** that argument rules out the *runtime* light only. Vertex colours
in this game *do* carry baked lighting where the artist put it there — issue
10 — so it does not generalise into "nothing in the stream carries light".
The day case is now believed correct; what remains is night-only and is open
issue 2.

**First-run checklist for the sun/moon light:** Remix's Dusklight tab should
report the device registered and `Drawing: SUN`. Walk past a lantern — the sun direction
must not move (that is the whole point of deriving it from the orbit
rather than the game's shadow-light selection). Watch a dawn (daytime
~67.5–75) for the moon→sun crossfade. If shadows fall from the wrong side,
tick Flip Direction; if that fixes it, the sign belongs in the code.

