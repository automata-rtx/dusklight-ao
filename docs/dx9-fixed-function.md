# Direct3D 9 fixed-function rendering mode (RTX Remix)

Dusklight has a **D3D9 fixed-function** graphics backend, implemented in Aurora,
whose purpose is to feed **RTX Remix**: a clean fixed-function command stream
Remix can capture and path-trace — `SetTransform` matrices,
`DrawIndexedPrimitiveUP` geometry, `SetTexture` diffuse maps, alpha-tested
cutouts, and fixed-function **indexed vertex blending** for skinning (rest-pose
vertices, stable mesh hashes). No Remix SDK build and no 32-bit bridge process
are involved: the game is x86_64 and loads `d3d9.dll` directly.

**The raw D3D9 image is never shown to a player**, so it is not something to
evaluate and its fidelity is not a design goal. Remix's renderer is the product;
D3D9 is the feed. Where the stream cannot carry something faithfully enough to
reach Remix, the answer is to implement it *in Remix* — the API or the fork —
not to contort D3D9. Two exceptions still have to rasterize correctly: the
**HUD** (Remix rasterizes UI draws rather than path-tracing them) and **alpha**
(Remix reads the stage's alpha to build opacity and the alpha test). Full
statement: `extern/aurora/docs/dx9/remix-material-interface.md` §0.

**Which `d3d9.dll`.** Every `rtx.dusklight.*` and `rtx.bloom.dusklight*` option
below exists **only in our dxvk-remix fork**
(`src/dxvk/rtx_render/rtx_dusklight_*`). Stock Remix will run the game and
path-trace it, but those keys are simply unknown to it. The game and the DLL are
also a single protocol — currently **17** — so build both from the same commit
point and read the Dusklight tab's protocol line before debugging anything else.

The complete design, GX→D3D9 mapping spec, architecture notes, and the living
list of unsupported effects live in the Aurora repo:
**`extern/aurora/docs/dx9/`** (start with `README.md`, track state in
`progress.md`).

## Selecting the backend

Windows only. Any of:

- CLI: `dusklight --backend d3d9`
- Config: `backend.graphicsBackend = "d3d9"`
- Settings menu: Prelaunch → Graphics Backend → "D3D9 (Fixed-Function)"
  (only selectable while running on another backend — see limitations)

For Remix: place our dxvk-remix fork's `d3d9.dll` (and the rest of its runtime)
next to the Dusklight executable and launch with the d3d9 backend.

## Running under RTX Remix — required rtx.conf settings

Create/edit `rtx.conf` next to the executable:

```ini
# REQUIRED for game input. Remix's default ("new") GUI input method creates an
# invisible overlay window that registers raw keyboard input with
# RIDEV_NOLEGACY, which suppresses normal Windows key messages for the whole
# process - SDL (and therefore the game) stops receiving keyboard input from
# the moment the Remix splash appears, while Remix's own hotkeys (Alt+X)
# keep working. The old method routes input through a window-proc hook that
# always forwards messages to the game. This option is read at startup only.
rtx.useNewGuiInputMethod = False

# --- Kankyo-driven look (see kankyo-remix.md) ---

# Render the game's bloom/mono state pushed by the kankyo bridge.
rtx.bloom.dusklight = True

# FOG. With Remix's own defaults the game's fog is captured and then consumed
# by neither path (composite fog is skipped while volumetrics are enabled, and
# Remix's fog remap is off), i.e. fog silently does nothing. Pick ONE of the
# two blocks below; the ATMOSPHERE block is the recommendation.

# ATMOSPHERE - RECOMMENDED. One medium derived from the game's own palette
# drives the volumetrics, the fog and the sky together, with the froxel grid
# sized from the game's fog range and the game's own ramp taking over past
# where that grid stops. This is the fork answering a question stock Remix
# could not: its 20 m froxel grid and its fog remap's endpoints (calibrated for
# another game) are bypassed entirely while this is on. Phases A and B are
# TESTED GOOD (2026-07-28); phase C, the physical sky blend
# (rtx.dusklight.atmosphere.physicalSky, off by default), was run 2026-07-29 -
# scattering confirmed, final verdict still blocked by the sky/fog defect in
# the fork's documentation/DusklightAtmosphere.md. See also docs/kankyo-fog.md.
rtx.dusklight.atmosphere.enable = True
rtx.volumetrics.enable = True

# Depth-fog mode, for comparison only: Remix's composite applies the exact
# linear ramp the game authored, but ONLY while volumetrics are off. These
# three do nothing alongside the atmosphere above - do not set both.
#rtx.volumetrics.enable = False
#rtx.maxFogDistance = 10000000
# Captured fog colour -> pre-tonemap radiance. Calibrate once (start ~1.0
# with auto exposure disabled; the Remix default 0.25 is very dim).
#
# CHANGED 2026-08-17: this only does anything while
# rtx.dusklight.atmosphere.enable is FALSE. With the atmosphere on, the fork
# feeds the depth path the same resolved radiance the volumetric path uses and
# skips this scale entirely - because the two paths aiming at different colours
# is the defect that was fixed. Use fogRadianceScale below instead.
#rtx.fogColorScale = 1.0

# Generated sky. All three together, or you get more than one sky at once:
# the generated dome, the game's own dome, and Remix's auto-detected probe.
rtx.dusklight.atmosphere.skyEnable = True
rtx.dusklight.game.hideVrbox = True
rtx.skyAutoDetect = None

# Calibrated 2026-07-28. densityScale is the first thing to reach for if the
# fog reads THICK or THIN everywhere at once - it is one number over the whole
# scene. For a fog that is the wrong BRIGHTNESS everywhere, reach for
# fogRadianceScale below instead.
# skyIntensity was raised 1.0 -> 6.0 because the palette is sRGB-decoded
# before it is scaled, which the original anchor arithmetic had missed.
#rtx.dusklight.atmosphere.densityScale = 1.0
#rtx.dusklight.atmosphere.skyIntensity = 6.0

# zHalfMin: value unchanged, MEANING CHANGED 2026-08-17. It is a floor on the
# distance at which the medium is matched to the game's ramp - the ramp's own
# midpoint, or this, whichever is further out. It is NOT a half-density floor
# any more: the match is to the game's actual opacity at that distance, which
# is 0.5 only while this clamp is idle. Where it does bite - every scripted fog
# bank, e.g. the Lost Woods mist tag's -2000..200 - the medium is now 4.46x
# denser than it was, which is the correction rather than a regression.
# In fogRampMode 2 this stops steering the fog's appearance and only steers how
# much the fog dims a light shining through it.
#rtx.dusklight.atmosphere.zHalfMin = 100

# Fog level and colour convention. ALL FOUR ADDED OR CHANGED 2026-08-17, and
# all four are UNTESTED IN GAME. The fork's own headers are the authority;
# documentation/DusklightAtmosphere.md sections 5.1-5.4 has the derivations.
#
# fogRadianceScale: the level knob for BOTH fog paths now. If the fog changed
#   brightness when this landed, this is the number to move - the depth path
#   lost a x0.25 it used to carry and gained an exposure correction.
#rtx.dusklight.atmosphere.fogRadianceScale = 1.0
#
# fogRampMode: 0 handover (legacy A/B baseline), 1 top-up (THE DEFAULT), 2 exact
#   ramp - the medium is given the game's own fog curve as its extinction field
#   instead of one scalar. 2 is untested in game; 0 and 1 are unchanged by this
#   pass. Those three switches (mode 1 + fogColorDirectional = True +
#   fogColorSpace = 0) revert everything that HAS a switch - but two parts of
#   the 2026-08-17 work are straight corrections with NO revert: the
#   rtx.fogColorScale bypass, and the sigma anchor now matching the game's
#   actual opacity instead of assuming 0.5. Both are unconditional in code.
#rtx.dusklight.atmosphere.fogRampMode = 1
#
# fogRampFloor: mode 2's divergence clamp, as a fraction of the ramp's range in
#   front of the camera. Read only in mode 2. 0 is legal.
#rtx.dusklight.atmosphere.fogRampFloor = 0.01
#
# fogColorSpace: 0 = decoded (CORRECT), 1 = raw (THE DEFAULT). Raw is the
#   reading the legacy depth fog has always shown and therefore the only one
#   anyone has judged; decoded is about 2.3x darker at a mid grey and ~3.2x at
#   the levels this game's fog palette actually uses, so switching
#   means recalibrating fogRadianceScale in the same sitting.
#rtx.dusklight.atmosphere.fogColorSpace = 1
#
# fogColorDirectional: lets the far fog sample the sky dome in the VIEW
#   direction - aerial perspective proper, a sunset warming the haze on the
#   sun's side. Off by default since 2026-08-17 because it gives the fog's near
#   and far halves two different colours. Turning it on is a look preference and
#   costs the exactness, not a bug fix.
#rtx.dusklight.atmosphere.fogColorDirectional = False

# Sun/moon elevation. The game's own arc peaks at 59 degrees, which leaves a
# path tracer without a usable overhead sun. 80 is the settled value - short
# of 90 on purpose, since at exactly 90 the azimuth flips instantly at noon.
rtx.dusklight.game.celestialNoonElevation = 80

# Stops the game dropping geometry the camera cannot see. A path tracer still
# needs it: a wall culled because you turned away stops occluding and light
# leaks through the gap. Tested - it works and visibly helps.
rtx.dusklight.game.disableFrustumCulling = True

# Effect lights: a sphere light at the origin of the effect that draws the fire.
# On by default, so nothing is needed here - the line is shown so it is obvious
# which switch to reach for. Tested in game 2026-08-07.
#rtx.dusklight.game.effectLights = True

# The local point-light mirror that preceded the effect lights was REMOVED at
# protocol 17 (2026-08-16), on both sides. If an older rtx.conf of yours still
# carries its keys, delete the lines - Remix no longer declares them, so they
# do nothing but sit in the file looking like live settings.

# Stops the game's sun/moon/star billboards. Tested 2026-07-29: this is what
# fixes shadow coverage wandering with the camera at night. The billboards are
# anchored to the camera eye, so the 80 m moon quad hanging 800 m away along
# the moon's own direction was occluding every shadow ray cast toward the moon
# light. Costs the visible moon and stars; the generated sky paints that region
# and the moonlight comes from the distant light, not the billboard.
rtx.dusklight.game.hideSkyBillboards = True

# Whether the line above also takes the stars. Defaults True, which is what that
# switch has always done, so leaving this out changes nothing. Set it False - with
# hideSkyBillboards still True - to get the star field back while the moon quad
# stays hidden. Only the sun packet draws that quad, so this separates the half
# that was measured from the half that never was; the stars include a
# hand-placed 13-star constellation.
#
# The overlay checkbox for this only started working at protocol 17 (2026-08-16):
# the fork had declared the option and drawn the box since 2026-08-11, but the
# bridge never read the name, so the control moved a value the game never saw.
# It is read now and takes effect live. Still UNTESTED either way - the recipe is
# remix-test-playbook.md section 4b, and the answer belongs back in this comment.
#rtx.dusklight.game.hideStarBillboards = False

# Grass, one draw per blade instead of one batch per room. Off by default
# because it costs exactly what the batching saves - a draw call per blade,
# paid on the CPU in dense grass. Turn it on when you want grass that Remix can
# identify: stable per-blade hashes make the blades taggable, replaceable with
# authored geometry, and able to hold denoiser history.
#
# Note this trades the opposite way from the weather particles, which were
# batched in 2026-08-08 to fix exactly the draw-call cost this option spends.
# The difference is whether there is a stable identity to lose: grass has a
# per-blade display list whose positions do not move, particles move every
# vertex every frame and never had one. Batching is wrong for the first and
# right for the second.
#rtx.dusklight.game.perBladeGrass = True

# Recommended for calibration: fix exposure so thresholds/fog read stably.
#rtx.autoExposure.enabled = False

# MATERIALS. Aurora encodes the colour a GX surface presents into the otherwise
# unused halves of D3DMATERIAL9; the fork reads it back. The next two default ON
# and are listed here because they are the switches to flip when a surface looks
# wrong. Design: extern/aurora/docs/dx9/remix-material-interface.md §9-§10.

# Two-colour ramps: lerp(colourA, colourB, texture), this game's dominant
# material shape - one rupee texture yielding seven rupee colours. The fork
# evaluates the GX combiner from both endpoints instead of approximating it
# with a single D3D9 texture op. Turn it OFF to compare against the
# approximation (a multiply goes black where the texture is dark; an add drives
# the bright end to white - Goron Mines lava reads red-and-white instead of
# red-to-orange). CI-green, untested in game as of 2026-08-04.
#rtx.dusklight.rampMaterials = False

# Self-illumination. GX has no emissive term and no single GX fact identifies an
# emitter, so this is a conjunction rather than a score. A surface emits when
# its TEV colour program never reads the rasterized channel (self-lit - which is
# NOT the same as the channel's lighting flag being off; the Goron Mines lava
# has lighting on and never reads it), AND it has a colour of its own authored
# in GX constants rather than being a bare texture pass-through, AND that colour
# reads as a glow - saturated OR near-white-hot. Replayed over a measured Goron
# Mines session that is 6 materials of 77, every lava and fire surface, nothing
# else. Nothing here needs tuning; brightness is the one dial. colorSource picks
# what an emitter glows: 0 reconstructed albedo (default - the two-colour ramp,
# so the texture drives the colour), 1 albedo texture through its own op,
# 2 flat presented colour. All live in the F1 overlay, and every candidate is
# logged (dusklight.emis) accepted or rejected with the fact that decided it.
# Rev 4 is CI-green and untested in game as of 2026-08-05.
#rtx.dusklight.emissive.enable      = True
#rtx.dusklight.emissive.colorSource = 0
# NOT "intensity" - that name was renamed to brightness, and the old one silently
# does nothing. It is a target brightness rather than a multiplier; default 10.0.
#rtx.dusklight.emissive.brightness  = 10.0

# Water. The game recognises its own water by J3D material name
# (dKy_bg_MAxx_proc) - GX carries nothing that says "water", and a texture-hash
# rule would be wrong by construction because this game reuses water textures on
# non-water draws. So the game marks each draw and aurora packs the three facts
# (role, MAxx tag, layer) into D3DMATERIAL9::Power. A marked surface becomes a
# translucent material instead of the opaque white sheet the legacy conversion
# produced; the camera-projected fake reflection layer (MA02/MA10) is dropped,
# because Remix traces that reflection for real and the painted one is a second
# refracting interface just above the first.
#
# Transmittance Distance is the dial to reach for first - it decides how quickly
# water reads as deep. UV Tiling is the second, on a large lake: a ripple texture
# stretched once across Lake Hylia reads as a smear. The five hide*Layer switches
# are all OFF by default; a body of water is several stacked draws and which one
# should be the surface is a look decision. All live in the F1 overlay under
# Dusklight Remix > Water, which is the better place to tune them.
#
# UNTESTED IN GAME on this transport (rebased 2026-08-11). If water still renders
# as a white sheet, check power= on the dusklight.water log lines: zero there,
# with dusk.matname lines present in the game log, means the mark did not survive.
# Design: extern/aurora/docs/dx9/remix-material-interface.md §11.
#rtx.dusklight.water.enable             = True
#rtx.dusklight.water.transmittanceMeasurementDistance = 200.0
#rtx.dusklight.water.uvTiling           = 1.0
#rtx.dusklight.water.hideProjectedLayer = True

# Material translation report (Remix half; aurora's half is always on). Turn it
# on for any session where a surface is the wrong colour - it prints what each
# material became on the way through D3D9, so a log answers the question instead
# of someone describing pixels. Bounded, and free when off. Note that grp= does
# NOT work - it prints "-" for every material, because the hook was at a draw
# scheduling point rather than an issuing one - so identifying a material still
# means reading its texture size, format and ramp endpoints.
# Format: extern/aurora/docs/dx9/material-report.md
#rtx.dusklight.matrep = True

# AMBIENT GRADE: re-applies the colour of the game's ambient term, which the
# path tracer replaced. Off by default; turn it on once the rest of the
# bridge is behaving, so a colour shift is never ambiguous about its source.
#rtx.dusklight.grade.enable = True

# The kankyo bridge also drives a sun/moon distant light through the Remix
# API. Its settings live in Remix's own Dusklight tab (and here as
# rtx.dusklight.game.*), NOT in the game - the game's debug UI is not drawn
# at all in this mode. With fallbackLightMode = 1 the Remix fallback light
# yields automatically while the sun/moon exists.
#
# Keep it at 1 rather than 0. Indoors the sun/moon is gated off and the only
# lights are the effect lights above, so a room whose fires the classifier
# refuses goes black at 0 - which is the right setting when you are DEBUGGING
# effect lights (see the test playbook section 3b) and the wrong one for play.
rtx.fallbackLightMode = 1
```

**Sun/moon elevation.** The game's orbit is a great circle tilted 31 degrees
off vertical (`setSunpos`: 48000 z-radius against 80000 xy), so the sun peaks
at **59 degrees** and never higher. Against baked lighting that is fine;
under a path tracer it means midday never gets an overhead sun and noon
shadows stretch about as far as mid-afternoon ones.
`game.celestialNoonElevation` (`rtx.dusklight.game.celestialNoonElevation`)
sets that peak directly — 90 puts the sun straight up at noon. It moves the
visible body as well as the light, so the two cannot disagree, and it
touches **nothing** about time of day: `daytime`, the palette schedule and
every dawn/dusk/night transition run off `dComIfGs_getTime()` and
`l_time_attribute`, which never look at the orbit. Sunrise and sunset
elevations barely move either (14.8° → 15.0° at the extreme), so those
transitions look the same.

**Sky billboards — recommended ON.** The sun, moon and stars are drawn at a
fixed offset from the camera eye, so as world geometry they travel with the
player. Anything Remix captures from them as ordinary geometry becomes an
occluder that follows the camera — which only shows at night, since stars and
the moon are the only sky billboards drawn then.

**Two packets, one switch — and only one of them was tested.** This paragraph
said `dKyr_drawStar` drew the moon until 2026-08-11. It does not, and the
distinction is the whole point of the switch:

| Packet | Drawn by | What it puts in the world |
| :-- | :-- | :-- |
| sun packet | `dKyr_drawSun` | the **moon quad** — 8000 units across, hung at `eye + moon_pos`, 80000 units out along the direction the moon light arrives from. Also the daytime sun and its lens flare, 250–850 units and close to the camera |
| star packet | `dKyr_drawStar` | up to 1200 scattered triangles. Thirteen of them are the hand-placed `hokuto_position` constellation array, ~151 units across at ~39000 out; the rest are ~0.5 units across at ~300 out. It reads `moon_pos` only to fade out stars that land near the moon on screen, and emits nothing there |

*hokuto* (北斗) is the **Northern Dipper** — the Big Dipper — so the symbol has
been read as "the game placed a Big Dipper". **Do not repeat that without
checking it.** The array's own in-tree comment says *Cassiopeia and Orion*, and
the data agrees with the comment rather than the symbol: the 13 entries fall
into a group of **5** and a group of **8**, on opposite sides of the sky, and
the first five are drawn larger than the rest. A dipper is seven stars in one
place. What is verified is that thirteen stars were placed by hand; which
asterisms they are is the comment's claim, not the game's.

**Tested 2026-07-29: this is real, and `hideSkyBillboards` fixes it.** Shadow
coverage that wandered as the camera moved stops wandering. The measurement
predicted it — an 80 m quad 800 m away, sitting in the same direction the moon
light arrives from, intersects a moving band of every shadow ray cast toward
the moon — so the cause is confirmed rather than merely masked.

**That test did not separate the two packets**, and the switch turned both off
together, so "the stars occlude" has never been either shown or ruled out. The
arithmetic says they should not: they cover roughly 0.05% of the sky hemisphere,
scattered, and the ones that would sit in front of the moon are already faded
out by the game itself. That is inference, not a measurement.
`rtx.dusklight.game.hideStarBillboards` (default `True`, i.e. today's
behaviour) exists so one play session can settle it — set it `False` with
`hideSkyBillboards` still `True`. Recipe: `remix-test-playbook.md` §4b.

An earlier revision of this section suggested tagging the billboard textures as
Sky "to fix it properly". For the sun and the moon that is still *possible* —
unlike the vrbox dome those draws are textured — but it is no longer the
recommendation: it keeps a real quad in the world and depends on an untested
assumption about whether a Sky-tagged draw still renders while the generated
dome has replaced the sky probe. **For the stars it is not possible at all**:
`dKyr_drawStar` binds no texture (`GX_TEXMAP_NULL`), so there is no hash to
categorise. If you want the moon back, the clean route is to paint it into the
generated dome, where it is visible, correctly placed, contributes its own
light and cannot cast a shadow. Not built.

**Frustum culling (off by default).** The game drops geometry outside the
camera's view, which is right for a rasterizer and wrong for a path tracer:
a wall culled because you turned away stops occluding, and light leaks
through where it used to be. `rtx.dusklight.game.disableFrustumCulling` (or
`game.disableFrustumCulling`) makes every frustum test report "visible",
which covers actors, room geometry and grass in one switch — they all go
through `mDoLib_clipper::clip`. It costs exactly what the culling was
saving, so it is off by default. Remix's own
`rtx.antiCulling.object.enable` is the cheaper half measure: it retains
objects it has already seen rather than stopping them being dropped.

**Effect lights (game-side, on by default — leave them on).** Aurora does not
forward GX lights to D3D9, so Remix sees no light from the game itself;
outdoors the sun/moon light covers that, but interiors and night fall through
to Remix's fallback light. `rtx.dusklight.game.effectLights` puts a sphere
light at the **origin of the effect that draws the fire** — the point the flame
is generated from — and takes its colour, and where the game authored one its
reach, from whatever light the game registered nearby. Keep
`rtx.fallbackLightMode = 1` so the fallback light yields to them.
Tested in game 2026-08-07. Design and settings:
[`effect-lights.md`](effect-lights.md).

**The local point-light mirror is gone — there is nothing to set.** It mirrored
the same point-light list at the position the game put each light, which is
where the faked placements live: under a rasterizer a point light casts no
shadow, so the artists could offset it from the flame, sink it into geometry,
or use one light for three, and none of it reads as wrong until a path tracer
casts a real shadow from the exact point. It was superseded on 2026-08-06 and
kept only so the two placements could be compared; that comparison was run and
decided (effect lights tested in game 2026-08-07), and the mirror was **removed
at protocol 17 on 2026-08-16** — game side, fork options and overlay section
alike. Delete its keys from any `rtx.conf` you are carrying forward.

**Two of its numbers survive it, as effect-light defaults.** The intensity
factor of 19 is the one to know: a naive reading uses `mPow` as the light's
reach, and it is not — `mPow` is where the game's attenuation curve falls to
1/11 of peak, so the light carries about 4.3× further, which is ~19× the
radiance. Testing found 19 to be the minimum giving usable light, so the
derived number and the measured one agree. Radius 10 clears the Forest Temple
light posts without the emitter clipping through them. Both carried over
unchanged into `effectLightDerivedIntensity` and `effectLightDerivedRadius`.

Set them together: the radiance is solved so the light still reaches the same
distance, so a larger emitter needs less of it, and changing one alone moves
the brightness as well as the softness.

Still ignored, and it was the mirror's gap too: `mFluctuation`, the per-light
flicker, because applying it would mean re-creating every flickering light
every frame.

**Sky setup — use the generated sky, not texture tagging.** Tagging the vrbox —
the game's word for its skybox dome, `d_a_vrbox.cpp` — by *texture* hash in the
dev menu does not work: it is painted from a handful of vertex colours with no
texture bound, so there is no texture content to hash.

*(An earlier revision went further and said it therefore could not be
categorised at all. That does not follow, and it was corrected on 2026-07-29:
`rtx.skyBoxGeometries` categorises by **geometry** hash and needs no texture.
See `dxvk-remix/documentation/DusklightAtmosphere.md` §14.9 — the general lesson
is that "no texture, therefore untaggable" is wrong, and there are three
categorisation routes rather than one.)*

The generated sky remains the recommendation, because it is built and tested: it
reads the same kankyo palette colours over the bridge, builds a lat-long dome
and registers it as a dome light, which is where the sky fill light comes from:

```
rtx.dusklight.atmosphere.skyEnable = True
rtx.dusklight.game.hideVrbox      = True   # or you will see both skies
rtx.skyAutoDetect                 = None   # or a second, dimmer sky rasterizes behind it
```

All three together, or not at all. `rtx.skyBrightness` stops mattering once
this is on — it scales the LDR probe that the dome light replaces; use
`rtx.dusklight.atmosphere.skyIntensity` (6.0) instead.

The dome is submitted through the Remix API, and **as of 2026-08-04 API-submitted
assets are capturable and replaceable in our fork** — mesh hashes are derived
from the submitted vertex/index data rather than a creation-order counter, and
external draws consult the replacement material. Upstream they are neither, so
older notes saying an API mesh cannot be captured or replaced are describing
stock Remix. **CI-green; no capture has been taken in game yet**, so if you are
relying on it, take one and say what happened.

Notes:

- **Camera / world space:** the game hands its camera matrix to the backend
  every frame (`J3DSys::setViewMtx` → aurora `GXSetViewMtx`), so Remix sees a
  real VIEW transform, world-space geometry, and object→world skinning bones.
  Without this (older builds), Remix's camera manager rejected every draw
  ("Unknown camera"), which scattered skinned character parts and disabled
  Remix features like Anti-Culling.
- If characters look wrong under Remix, check the Remix log for
  `Cannot decompose the matrices for a skinned mesh` or
  `draw call has bones but no blend weight buffer` — both indicate a stale
  build of this branch (fixed in aurora checkpoints 3.3/3.4).
- **VRAM growing without bound / textures flickering in the
  categorize-textures tab** indicates a stale build (fixed in aurora
  checkpoint 3.5): the backend now keeps D3D9 texture objects stable across
  frames (content-addressed cache; per-size EFB copy targets) because Remix
  tracks textures by object and holds references across frames.
- **The game's own bloom switches itself off** while the kankyo bridge is
  active — `bloom_c::draw()` returns immediately (`src/m_Do/m_Do_graphic.cpp`)
  whatever `game.bloomMode` says. It is a screen-space EFB filter chain; the
  path tracer replaces that class of effect, and the filter quads would only
  overlay raster-derived blur on top of Remix's output. Nothing to set by hand;
  use Remix's own bloom — see "Dusklight bloom in Remix" below.
- **Resizing the window blacks out briefly, by design.** Remix does not
  re-derive its UI overlay from a D3D9 device `Reset` — the HUD would keep the
  scale it had when the device was created — so the backend fully recreates
  the device on a size change instead. Remix restarts its renderer and the
  texture cache rebuilds, which is the pause you see. The change is debounced,
  so dragging a window edge stays responsive and rebuilds once you let go.
- **Keep experiment tags out of `rtx.conf`.** Categories set while
  investigating (`rtx.ignoreTextures`, `ignoreTransparencyLayerTextures`,
  `terrainTextures`, …) persist across runs and silently hide or reclassify
  textures in later sessions. Clear them before judging a new build.
- **`rtx.particleTextures` is not a performance control**, and expecting it to
  be one cost a session (2026-08-07). It decides which TLAS a draw lands in and
  how the resolve loop treats it — nothing about per-draw cost. If a dense
  effect is slow and tagging it as a particle changes nothing, the cost is
  **per draw, not per pixel**; read `dx9.draws` in the log
  (`extern/aurora/docs/dx9/material-report.md`) rather than reaching for
  another category.
- **Tagging something as UI is a diagnostic, not a fix.** UI draws are
  rasterized and never enter the raytraced scene, so if that makes the frame
  rate fine, the answer is draw count. As a *setting* it costs the effect its
  path-traced lighting, and for anything the game draws twice — snow, whose
  planar-reflection copies are real geometry — a screen overlay composites both
  copies unconditionally and it looks wrong.

> **The table below is now automatic.** With the kankyo bridge active
> (`game.remixKankyoBridge`, on by default under Remix) the game pushes its
> live bloom/mono state into `rtx.dusklight.env.*` every frame and the
> Dusklight bloom follows it (`rtx.bloom.dusklightFollowGame`). The manual
> values below still apply when the bridge is off or the game isn't
> running. Design: [`kankyo-remix.md`](kankyo-remix.md); debug via
> the **Dusklight tab** in Remix's own ImGui overlay (Alt+X).

## Dusklight bloom in Remix

The game's own bloom is off under Remix (above), so the "Dusk" bloom mode — the
one the settings menu offers as an alternative to Classic — was ported into our
dxvk-remix fork as a post-processing option. Turn it on in
the Remix UI under **Rendering → Post-Processing → Bloom → Dusklight Bloom**,
or in `rtx.conf`:

```ini
rtx.bloom.dusklight = True
```

The port keeps the game's parameter names and ranges, so values can be
carried straight across from `game.bloom*` / the ImGui Bloom window:

| rtx.conf option | Default | Game equivalent |
| :-- | :-: | :-- |
| `rtx.bloom.dusklightThreshold` | 0.5 | `mPoint` (128/255) |
| `rtx.bloom.dusklightBlurSize` | 64 | `mBlureSize` |
| `rtx.bloom.dusklightBlurRatio` | 128 | `mBlureRatio` |
| `rtx.bloom.dusklightFalloff` | 0.25 | upsample alpha base |
| `rtx.bloom.dusklightSaturationPoint` | 1.0 | 8-bit clip per pass |
| `rtx.bloom.dusklightTint` | 1, 1, 1 | `mBlendColor` rgb |
| `rtx.bloom.dusklightScreenBlend` | False | `mMode == 1` |

## Ambient grade in Remix

The game's environment system also computes an *ambient* colour per area,
per time of day and per weather, and on the original hardware it tinted
every surface in the scene during shading. Path tracing lights the scene
itself, so that tint is simply gone under Remix even though the game still
computes it. **Rendering → Post-Processing → Dusklight Ambient Grade**
puts its colour back, as one multiply over the frame just before bloom:

```ini
rtx.dusklight.grade.enable = True
```

| rtx.conf option | Default | What it does |
| :-- | :-: | :-- |
| `rtx.dusklight.grade.strength` | 0.65 | How far towards the ambient tint the image is graded |
| `rtx.dusklight.grade.chromaOnly` | True | Grade colour only, not brightness — keeps auto exposure out of the loop |
| `rtx.dusklight.grade.actorAmbientWeight` | 0.25 | Mix between the game's actor and background ambients |
| `rtx.dusklight.grade.maxDarkening` | 0.35 | How far down the grade may take the image |
| `rtx.dusklight.grade.maxBrightening` | 2.0 | How far up the grade may take the image |

It is fed by the bridge (`rtx.dusklight.env.actorAmbient` / `bgAmbient`)
and does nothing without it, so it is off by default. The panel prints the
resolved tint live, which is the quickest way to tell "the grade is doing
nothing" from "the grade is doing nothing *visible*".

Leave `chromaOnly` on unless you have disabled auto exposure. With it on, a
neutral ambient (roughly: noon) resolves to exactly white and the pass
skips itself, so the grade only ever costs you something when the game's
mood has actually moved off neutral. With it off, the ambient drives
overall brightness too — closer to the original at night and indoors, but
auto exposure will spend the next second undoing it.

## Dusklight bloom options

`rtx.bloom.steps` (labelled Radius in the UI) sets how deep the pyramid goes.
The game runs **five blur passes** over six pyramid levels (its `divStart` 2
through `divNum` 6), so **`rtx.bloom.steps = 6`** reproduces it — Remix's
default of 5 is one short, which both narrows the halo by a level and
changes how the total gain is distributed across the passes. `rtx.bloom.burnIntensity`
still scales the final composite. `rtx.bloom.luminanceThreshold` is *not* used
in this mode — Dusklight thresholds by subtracting from each channel rather
than by weighting with luminance, which is what keeps coloured highlights
saturated, so it gets its own threshold option.

What makes it look different from Remix's default bloom, in `draw2()` order
(`src/m_Do/m_Do_graphic.cpp`):

1. **A luminance-keyed mask, not a threshold on each channel.** Three TEV
   stages build a greyscale key out of the framebuffer through the swap
   tables and then multiply the *original* colour by it:
   `source = colour × saturate(0.25R + 0.25G + 0.5B − mPoint)`. Two
   consequences: the bloom carries the source's own hue instead of drifting
   towards whichever channel was brightest, and a saturated but dim colour
   does not bloom at all — pure red at full intensity has a key of 0.25 and
   never clears the default 0.5 threshold. The weights are not a standard
   luma either: blue counts double red or green, which is why blue
   highlights in this game bloom far more readily than their brightness
   alone would suggest.
2. **A ring blur at every pyramid level.** Eight taps evenly spaced around a
   circle, no center tap, at a fixed screen-space radius. Remix's default
   pyramid gets all of its blur from the downsample kernel alone.
3. **Per-level gain that is allowed to clip.** The total brightness is spread
   over the passes as its N-th root, and each pass saturates. The comment in
   `draw2()` is explicit that the clipping is deliberate — it is what gives
   bright sources their washed-out white cores.
4. **Geometrically weighted upsample.** Each level is folded into the one
   above it with weight `falloff^(1/level)` instead of being summed at full
   strength, so the wide levels sit under the narrow ones.

One deliberate deviation: the first (threshold) pass uses Remix's 13-tap
downsample kernel rather than the game's plain copy. The input there is
raytraced HDR colour, and a single bright pixel with a box filter makes the
whole bloom flicker frame to frame.

**`dusklightThreshold` and `dusklightSaturationPoint` are display values.**
With `rtx.bloom.dusklightDisplaySpace` on (the default) the whole pyramid
runs after tone mapping on gamma-encoded 0..1 colour, exactly as the
original ran on its finished 8-bit framebuffer. So the threshold is a
fraction of display white and maps straight from the game's `mPoint/255`,
and the saturation point of 1.0 clips at white the way the 8-bit
intermediates did — which is what gives bright cores their washed out look.
`rtx.bloom.dusklightThresholdScale` should stay at 1.0 in this mode; it
exists for the pre-tonemap path, where the scene's range is open-ended and
the threshold has no fixed meaning.

Turning `dusklightDisplaySpace` off moves the pass back before tone mapping,
which is useful only for comparison. In that mode the threshold, the
clipping, the screen blend and the base weight are all being applied to
open-ended linear radiance, and none of them mean what they meant.

## Game-side behavior & limitations in D3D9 mode

- **RmlUi menus (settings/prelaunch UI) are unavailable** — Aurora's RmlUi
  backend renders through WebGPU, which is not initialized in this mode. All
  UI document creation is skipped at startup (`game_main` gates on
  `dusk::ui::initialize()`), including the prelaunch game picker, the
  first-run preset window, and the crash-report consent dialog. Configure via
  the config file or CLI, or switch settings while running a WebGPU-family
  backend. The in-game HUD/menus (J2D, drawn through GX) work.
- **The game must be launchable without the prelaunch picker**: set
  `backend.isoPath` in `config.json` (to your .rvz/.iso) or pass
  `--dvd <path>` on the command line. With neither, the game exits with
  "No DVD image specified, unable to boot!". Recommended D3D9 launch:
  `dusklight --backend d3d9 --dvd <path-to-game.rvz>`.
- **Mods are disabled entirely on this backend.** Mod graphics stages
  (`push_custom_draw`, `create_pass`, `resolve_pass`) are inert here since
  WebGPU is never initialized, and a native mod that touches the renderer
  crashed the process at load — so every mod search directory is dropped when
  the active backend is D3D9 (`m_Do_main.cpp`), landing on the same "no mods
  found" path a clean install takes. The log line is
  `D3D9 backend: mods are unsupported here, skipping mod discovery`.
  **`config.json` is not rewritten**, so the same config moves between a
  modded build and a D3D9 test build with no edits either way. Remix's path
  tracer replaces the graphics mods' effects wholesale.
- **HD texture replacement packs work in this mode** as of 2026-08-05,
  **tested good 2026-08-06**. Drop `.dds` files named the usual
  `tex1_{w}x{h}_{hash}_{fmt}.dds` way into `<ConfigPath>/texture_replacements/`
  and launch on the D3D9 backend with our fork's `d3d9.dll`. Nothing else to
  turn on: `game.enableTextureReplacements` and `game.remixTextureReplacements`
  both default to true, and both are read at launch — there is no live toggle.

  **The pack's bytes never go through D3D9.** The game hands each file to Remix
  through the API and tags each draw with which replacement it wants; Remix
  loads the file itself and swaps it in. That matters for one reason above all:
  Remix's texture hash — the key for the categorization grid, for every
  `rtx.conf` category list and for every USD material binding — is the hash of
  the D3D9 texture, which stays the game's own. **Installing, changing or
  removing a pack does not move a single hash.** Tags authored without a pack
  stay valid with one.

  Two consequences to plan around:

  - **`.dds` only.** Remix's asset loader rejects `.png`, which the game's own
    registry accepts. PNG entries are skipped and logged
    (`texrep: skipping <file> - Remix loads .dds only`). BC1/BC3/BC7/BC5 are all
    fine — D3D9's format limits do not apply, because D3D9 never sees them.
  - **Packs shipped inside a mod still do not load**, because mod discovery is
    skipped wholesale on this backend (see the mods bullet above). The user
    directory is the only route.

  This also sharpens the **HUD**, which a Remix USD mod cannot: Remix rasterizes
  UI draws instead of path-tracing them, so they never reach material
  replacement. Turn `rtx.dusklight.texrep.applyToRaster` off to isolate a
  HUD-only regression.

  **Expect a slow first launch.** With a pack installed the first run spends a
  long period at poor performance before the replacements appear; later runs
  have them immediately. Expected, not a fault — but note that *every* first
  launch of this runtime is slow, pack or no pack, because Remix compiles
  shaders and caches the result to disk. The pack adds its own cost on top
  (Remix keeps no on-disk cache of textures, so every `.dds` is re-read each
  launch), and which of the two dominates has not been measured. A reboot
  separates them: it clears the OS file cache while keeping the shader cache.
  `extern/aurora/docs/dx9/texture-replacements.md` §9.

  If a pack appears to do nothing, the Dusklight tab's **HD Texture Pack**
  section says which half is at fault — it reports the game's counts and Remix's
  separately, because "never handed over" and "handed over then ignored" look
  identical otherwise. Design and failure modes:
  `extern/aurora/docs/dx9/texture-replacements.md`.
- **Frame interpolation should be disabled** — its presentation-camera path
  depends on pass resolves that no-op in this mode.
- **ImGui dev overlay is headless** — game-side ImGui code runs (no crashes),
  but nothing is rendered. Together with the RmlUi note above, this means the
  game **cannot draw any UI at all** in this mode.

  The recovery is a **separate Dusklight overlay hosted by Remix, opened with
  F1** — the same key the game's own overlay used. It is independent of Remix's
  own menu: either can be open without the other, and both can be open at once.
  Three tabs:

  | Tab | Contents |
  | :-- | :-- |
  | Dusklight Remix | everything that changes the image, in collapsible sections, plus a Requirements list naming the Remix options these depend on and an overrides list naming the ones they take over |
  | Warp | region + level dropdowns by plain-English name, a Warp button, room/point/layer under a collapsed header, and the time-of-day controls |
  | Controls | placeholder, nothing built |

  The game hosts its settings as `rtx.dusklight.game.*` options and polls them
  every frame; readouts come back as `rtx.dusklight.env.*`. Full write-up:
  `dxvk-remix/documentation/DusklightOverlay.md`.

  Three game features that were otherwise unreachable in this mode are back:
  **warp** (plain-English level names, driven from the game's own destination
  table), **recording mode** (hides the HUD, silences the music) — which
  previously required editing `config.json` and restarting, and could only be
  turned *on* that way, never back off while running — and the **clock**.

  The clock is a slider plus Midnight / Sunrise / Noon / Sunset presets and a
  **Freeze Time** switch. The day is 360 degrees, so 15 is an hour: 0 midnight,
  90 sunrise, 180 noon, 270 sunset. Nothing else reaches the time of day —
  `timeScale` in `d_kankyo.cpp` is a frame-delta normalizer, not a speed
  control. Freeze before shooting any A/B pair, or the sun has moved between
  the two shots and part of the difference is the clock rather than the setting
  under test. It reuses `using_time_control_tag`, the same flag `d_a_kytag11`
  sets for a stage whose sky must not move, so it also holds the Twilight Realm
  clock and skips the reset to midnight that entering twilight normally does.
- **Input no longer falls through an open overlay.** Remix's own
  `rtx.blockInputToGameInUI` cannot work on this setup: it sends a window
  message across the **32-bit bridge**, which a 64-bit game loading `d3d9.dll`
  directly never receives. The bridge now carries the intent instead
  (`rtx.dusklight.uiActive` → `PADBlockInput`), which also suppresses the held
  state on release so no key is left stuck down. Turn it off with
  `rtx.dusklight.blockGameInput = False`.
- EFB color copies and offscreen passes are real (StretchRect /
  render-target textures); depth-format copies still use a neutral
  white/alpha-0 placeholder, and post-processing (bloom etc.) is skipped by
  design. See `extern/aurora/docs/dx9/unsupported-effects.md` for the full
  list and Remix-side compensation notes.
- GPU skinning: both the PNMTXIDX matrix-palette path (all normal characters)
  and the `GXSetSkinning` extension path (`src/dusk/gpu_skinning.cpp`, the two
  `J3DSkinDeform` actors) map to fixed-function indexed vertex blending —
  Remix receives rest-pose vertices plus per-draw bone matrices.

## Branches

All three repos develop on `Fixed-Function-dev`, and dusklight's
`extern/aurora` pin tracks the matching aurora branch.

**`CLAUDE.md` at each repo root is the authority on branch rules** — including
what happens to session branches and the containment check that must pass before
any branch is deleted. This section deliberately does not restate them, because
an earlier revision of this file went stale against `CLAUDE.md` and told readers
to do something that had been revoked.
