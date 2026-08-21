# Direct3D 9 fixed-function rendering mode (RTX Remix)

Dusklight has a **D3D9 fixed-function** graphics backend, implemented in Aurora,
whose purpose is to feed **RTX Remix**: a clean fixed-function command stream
Remix can capture and path-trace — `SetTransform` matrices,
`DrawIndexedPrimitiveUP` geometry, `SetTexture` diffuse maps, alpha-tested
cutouts, and fixed-function **indexed vertex blending** for skinning (rest-pose
vertices, stable mesh hashes). No Remix SDK build and no 32-bit bridge process
are involved: the game is x86_64 and loads `d3d9.dll` directly.

**The raw D3D9 image is never shown to a player**, so it is not something to
evaluate and its fidelity is not a design goal. Where the stream cannot carry
something faithfully enough to reach Remix, the answer is to implement it *in
Remix*. Two exceptions still have to rasterize correctly: the **HUD** (Remix
rasterizes UI draws) and **alpha** (Remix reads the stage's alpha to build
opacity and the alpha test). Full statement:
`extern/aurora/docs/dx9/remix-material-interface.md` §0.

**Which `d3d9.dll`.** Every `rtx.dusklight.*` and `rtx.bloom.dusklight*` option
below exists **only in our dxvk-remix fork** (`src/dxvk/rtx_render/rtx_dusklight_*`).
Stock Remix will run the game and path-trace it, but those keys are simply
unknown to it. The game and the DLL are also a single protocol.
**Protocol is at 18.** Build both from the same commit point and read the
Dusklight status strip before debugging anything else.

The GX→D3D9 mapping spec and the living list of unsupported effects are in
`extern/aurora/docs/dx9/` (start with `README.md`).

## Selecting the backend

Windows only. Any of: `dusklight --backend d3d9`;
`backend.graphicsBackend = "d3d9"`; Prelaunch → Graphics Backend → *D3D9
(Fixed-Function)*, which is only selectable while running on another backend.

Place our fork's `d3d9.dll` and the rest of its runtime next to the executable.
**The game must be launchable without the prelaunch picker** — set
`backend.isoPath` in `config.json` or pass `--dvd <path>`, or it exits with "No
DVD image specified". Recommended launch:
`dusklight --backend d3d9 --dvd <path-to-game.rvz>`.

## The `rtx.conf`

Create/edit `rtx.conf` next to the executable. Commented-out lines are defaults,
shown so it is obvious which switch to reach for.

```ini
# REQUIRED for game input. Remix's default ("new") GUI input method registers
# raw keyboard input with RIDEV_NOLEGACY, which suppresses normal Windows key
# messages for the whole process - SDL, and therefore the game, stops receiving
# keyboard input the moment the Remix splash appears while Remix's own hotkeys
# keep working. Read at startup only.
rtx.useNewGuiInputMethod = False

# --- Kankyo-driven look (see kankyo-remix.md) ---

# Render the game's bloom/mono state pushed by the kankyo bridge.
rtx.bloom.dusklight = True

# FOG AND ATMOSPHERE - RECOMMENDED. One medium derived from the game's own
# palette drives volumetrics, fog and sky together, with the froxel grid sized
# from the game's fog range and the game's ramp taking over past where that grid
# stops. Stock Remix's 20 m grid and its fog remap's endpoints (calibrated for
# another game) are bypassed entirely while this is on. With Remix's own
# defaults instead, the game's fog is captured and then consumed by neither
# path - it silently does nothing.
rtx.dusklight.atmosphere.enable = True
rtx.volumetrics.enable          = True

# Depth-fog mode, for comparison ONLY - do not set both. Remix's composite
# applies the exact linear ramp the game authored, but only while volumetrics
# are off. rtx.fogColorScale does nothing at all while the atmosphere is on:
# the fork feeds the depth path the same resolved radiance the volumetric path
# uses, because the two paths aiming at different colours was the defect.
#rtx.volumetrics.enable = False
#rtx.maxFogDistance     = 10000000
#rtx.fogColorScale      = 1.0

# Generated sky. All three together, or you get more than one sky at once: the
# generated dome, the game's own vrbox dome, and Remix's auto-detected probe.
# rtx.skyBrightness stops mattering once this is on - it scales the LDR probe
# the dome light replaces; use skyIntensity instead.
rtx.dusklight.atmosphere.skyEnable = True
rtx.dusklight.game.hideVrbox       = True
rtx.skyAutoDetect                  = None

# Calibrated 2026-07-28. densityScale is the first thing to reach for if the fog
# reads THICK or THIN everywhere at once; for fog that is the wrong BRIGHTNESS
# everywhere, reach for fogRadianceScale. skyIntensity is 6.0 rather than 1.0
# because the palette is sRGB-decoded before it is scaled.
#rtx.dusklight.atmosphere.densityScale = 1.0
#rtx.dusklight.atmosphere.skyIntensity = 6.0

# zHalfMin: a floor on the distance at which the medium is matched to the game's
# ramp. NOT a half-density floor - the match is to the game's actual opacity
# there, which is 0.5 only while this clamp is idle. Where it bites (every
# scripted fog bank) the medium is 4.46x denser than before 2026-08-17, which is
# the correction rather than a regression. The whole derivation, including the
# worked Lost Woods case, is at fork rtx_dusklight_atmosphere.cpp:371-412.
#rtx.dusklight.atmosphere.zHalfMin = 100

# Fog level and colour convention. THE DEFAULTS BELOW WERE RUN IN GAME
# 2026-08-18 and the owner reported improvements, so fogRadianceScale,
# fogColorSpace = 1 and fogColorDirectional = False are covered as they ship.
# fogRampMode 2 and fogRampFloor are NOT - mode 2 is not the default and nothing
# switched it on, so both stay UNTESTED IN GAME. Each option's own description
# in the fork's rtx_dusklight_atmosphere.h is the authority on what it does;
# derivations are in DusklightAtmosphere.md sections 5.1-5.4. What is worth
# knowing here and nowhere else:
#
# - fogRadianceScale is the level knob for BOTH fog paths now.
# - Mode 1 + fogColorDirectional True + fogColorSpace 0 reverts everything that
#   HAS a switch. Two parts of the 2026-08-17 work are straight corrections with
#   NO revert, unconditional in code: the rtx.fogColorScale bypass, and the
#   sigma anchor matching the game's actual opacity instead of assuming 0.5.
# - fogColorSpace 1 (raw) is the default because it is the reading the legacy
#   depth fog has always shown and therefore the only one anyone has judged.
#   Decoded is ~3.2x darker at the levels this palette uses, so switching means
#   recalibrating fogRadianceScale in the same sitting.
#rtx.dusklight.atmosphere.fogRadianceScale    = 1.0
#rtx.dusklight.atmosphere.fogRampMode         = 1
#rtx.dusklight.atmosphere.fogRampFloor        = 0.01
#rtx.dusklight.atmosphere.fogColorSpace       = 1
#rtx.dusklight.atmosphere.fogColorDirectional = False

# Sun/moon elevation. The game's own arc peaks at 59 degrees, which leaves a
# path tracer without a usable overhead sun. 80 is the settled value, tested in
# game 2026-07-28 - short of 90 on purpose, since at exactly 90 the azimuth
# flips instantly at noon. It moves the visible body and the light together and
# touches nothing in the day cycle.
rtx.dusklight.game.celestialNoonElevation = 80

# Stops the game dropping geometry the camera cannot see. A path tracer still
# needs it: a wall culled because you turned away stops occluding and light leaks
# through the gap. Tested - it works and visibly helps. It costs what the culling
# saved. Remix's rtx.antiCulling.object.enable is the cheaper half measure.
rtx.dusklight.game.disableFrustumCulling = True

# Effect lights: a sphere light at the origin of the effect that draws the fire,
# taking its colour - and where the game authored one, its reach - from whatever
# light the game registered nearby. Aurora forwards no GX lights, so without
# this an interior falls through to Remix's fallback light. On by default.
# Tested in game 2026-08-07. Design: effect-lights.md.
#rtx.dusklight.game.effectLights = True

# The local point-light mirror that preceded effect lights was REMOVED at
# protocol 17 (2026-08-16), both sides. Delete its keys from any rtx.conf you
# carry forward - Remix no longer declares them, so they sit in the file looking
# like live settings and do nothing.

# Stops the game's sun/moon/star billboards, which are drawn at a fixed offset
# from the camera eye and so travel with the player. Tested 2026-07-29: this is
# what fixes shadow coverage wandering at night - the 8000-unit moon quad hangs
# 80000 units out along the moon light's own direction, so it occluded every
# shadow ray cast toward the moon. Costs the visible moon and stars.
rtx.dusklight.game.hideSkyBillboards = True

# Whether the line above also takes the stars. Default True, i.e. today's
# behaviour. Set it False - with hideSkyBillboards still True - to get the star
# field back while the moon quad stays hidden. Only the sun packet draws that
# quad, so this separates the half that was measured from the half that never
# was. UNTESTED either way; recipe in remix-test-playbook.md.
#rtx.dusklight.game.hideStarBillboards = False

# Grass, one draw per blade instead of one batch per room. Off by default
# because it costs exactly what the batching saves. Turn it on when you want
# grass Remix can identify: stable per-blade hashes make blades taggable,
# replaceable and able to hold denoiser history. This trades the OPPOSITE way
# from the weather particles, which were batched in 2026-08-08 to fix this same
# cost - the difference is whether there is a stable identity to lose. Grass has
# a per-blade display list whose positions do not move; particles move every
# vertex every frame and never had one.
#rtx.dusklight.game.perBladeGrass = True

# Recommended for calibration: fix exposure so thresholds and fog read stably.
#rtx.autoExposure.enabled = False

# MATERIALS. Aurora encodes the colour a GX surface presents into the otherwise
# unused halves of D3DMATERIAL9 and the fork reads it back. Both default ON and
# are listed because they are the switches to flip when a surface looks wrong.
# Design: extern/aurora/docs/dx9/remix-material-interface.md sections 9-10.
#
# Two-colour ramps: lerp(colourA, colourB, texture), this game's dominant
# material shape - one rupee texture yielding seven rupee colours. Off compares
# against the single-op approximation it replaces (a multiply goes black where
# the texture is dark; an add drives the bright end white - Goron Mines lava
# reads red-and-white instead of red-to-orange). CI-green, untested in game.
#rtx.dusklight.rampMaterials = False

# Self-illumination. A conjunction, not a score - the rule, the measurement it
# was derived from and why nothing here needs tuning are in the fork's
# rtx_dusklight_emissive.h header comment. brightness is the one dial, and it is
# a target brightness, not a multiplier. colorSource picks what an emitter
# glows: 0 reconstructed albedo (default, so the texture drives the colour),
# 1 albedo texture through its own op, 2 flat presented colour. Every candidate
# is logged (dusklight.emis) accepted or rejected with the fact that decided it.
# Rev 4 tested in game 2026-08-06.
# NOTE: emissive.intensity was renamed to brightness; the old name is silent.
#rtx.dusklight.emissive.enable      = True
#rtx.dusklight.emissive.colorSource = 0
#rtx.dusklight.emissive.brightness  = 10.0

# Water. The game marks its own water draws by J3D material name and aurora
# packs three facts (role, MAxx tag, layer) into D3DMATERIAL9::Power; a marked
# surface becomes translucent instead of the opaque white sheet the legacy
# conversion produced. The MAxx taxonomy, why it comes out WHITE rather than
# merely wrong, and why every constant here is an option are all in the fork's
# rtx_dusklight_water.h:24-58 header comment - it is better than any restatement.
#
# Transmittance Distance is the dial to reach for first; UV Tiling is second on
# a large lake, where one ripple texture stretched across Lake Hylia reads as a
# smear. The five hide*Layer switches are all OFF by default - a body of water is
# several stacked draws and which one should be the surface is a look decision.
#
# UNTESTED IN GAME on this transport (rebased 2026-08-11). If water still renders
# as a white sheet, check power= on the dusklight.water log lines: absent there,
# with dusk.matname lines present in the game log, means the mark did not survive.
#rtx.dusklight.water.enable                           = True
#rtx.dusklight.water.transmittanceMeasurementDistance = 200.0
#rtx.dusklight.water.uvTiling                         = 1.0
#rtx.dusklight.water.hideProjectedLayer               = True

# Material translation report (Remix half; aurora's half is always on). Turn it
# on for any session where a surface is the wrong colour - it prints what each
# material became on the way through D3D9, so a log answers the question instead
# of someone describing pixels. Bounded, and free when off. grp= carries the
# material's own authored name (Mat:MA00_Gake and friends) but is OFF by
# default - set DUSK_MAT_LABELS=1 in the environment before launching to get it,
# no rebuild needed. Without it, identifying a material means reading its
# texture size, format and ramp endpoints.
# Format: extern/aurora/docs/dx9/material-report.md
#rtx.dusklight.matrep = True

# AMBIENT GRADE: re-applies the colour of the game's ambient term, which the
# path tracer replaced. Off by default; turn it on once the rest of the bridge
# is behaving, so a colour shift is never ambiguous about its source.
#rtx.dusklight.grade.enable = True

# The kankyo bridge also drives a sun/moon distant light through the Remix API.
# Keep fallbackLightMode at 1 rather than 0: indoors the sun/moon is gated off
# and the only lights are the effect lights, so a room whose fires the classifier
# refuses goes black at 0 - which is the right setting when you are DEBUGGING
# effect lights and the wrong one for play.
rtx.fallbackLightMode = 1
```

**Keep experiment tags out of `rtx.conf`.** Categories set while investigating
(`rtx.ignoreTextures`, `terrainTextures`, …) persist across runs and silently
hide or reclassify textures in later sessions. Clear them before judging a build.

**`rtx.particleTextures` is not a performance control**, and expecting it to be
one cost a session (2026-08-07). It decides which TLAS a draw lands in, nothing
about per-draw cost. If a dense effect is slow and tagging it as a particle
changes nothing, the cost is **per draw, not per pixel** — read `dx9.draws` in
the log. Likewise **tagging something as UI is a diagnostic, not a fix**: UI
draws are rasterized and never enter the raytraced scene, so if that makes the
frame rate fine the answer is draw count.

## Sky, and why tagging is not the route

Tagging the vrbox — the game's word for its skybox dome, `d_a_vrbox.cpp` — by
*texture* hash does not work: it is painted from vertex colours with no texture
bound, so there is no texture content to hash. (It does not follow that it cannot
be categorised at all; `rtx.skyBoxGeometries` categorises by **geometry** hash.
"No texture, therefore untaggable" is wrong, and there are three categorisation
routes rather than one.)

The generated sky is the recommendation because it is built and tested: it reads
the same kankyo palette colours over the bridge, builds a lat-long dome and
registers it as a **dome light**, which is where the sky fill light comes from.

**If you want the moon back, the clean route is to paint it into the generated
dome** — visible, correctly placed, contributing light, and structurally
incapable of casting a shadow. Not built. Tagging the billboard textures is not
the recommendation and for the stars is not possible at all: `dKyr_drawStar`
binds `GX_TEXMAP_NULL`, so there is no hash to categorise.

The dome is submitted through the Remix API, and **as of 2026-08-04
API-submitted assets are capturable and replaceable in our fork** — mesh hashes
come from the submitted vertex/index data rather than a creation-order counter,
and external draws consult the replacement material. Upstream they are neither.
**CI-green; no capture has been taken in game yet.**

## Dusklight bloom and the ambient grade in Remix

The game's own bloom switches itself off while the kankyo bridge is active —
`bloom_c::draw()` returns immediately whatever `game.bloomMode` says. It is a
screen-space EFB filter chain and the path tracer replaces that class of effect.
Use Remix's port instead: **Rendering → Post-Processing → Bloom → Dusklight
Bloom**, or `rtx.bloom.dusklight = True`.

`rtx.bloom.steps` (Radius in the UI) sets pyramid depth. The game runs five blur
passes over six levels, so **6 reproduces it** — Remix's default of 5 is one
short. `rtx.bloom.luminanceThreshold` is *not* used in this mode; the port has
its own threshold option, and `dusklightThreshold` / `dusklightSaturationPoint`
are **display** values, because with `dusklightDisplaySpace` on (the default) the
pyramid runs after tone mapping on gamma-encoded 0..1 colour, exactly as the
original ran on its 8-bit framebuffer.

**What makes this bloom look different is in the shader, better stated than any
paraphrase here:** the luminance-keyed mask, its non-standard weights (blue
counts double), the ring blur at every level and the deliberate per-level
clipping are all in the header of
`dxvk-remix/src/dxvk/shaders/rtx/pass/bloom/bloom_dusklight_downsample.comp.slang`.
Every option's own description is what the F1 tooltip renders; the declarations
in the fork are the authority on defaults.

**The ambient grade** (`rtx.dusklight.grade.enable`) puts back the game's
per-area, per-time, per-weather ambient tint, which path tracing simply removed,
as one multiply just before bloom. Fed by the bridge, so it does nothing without
it, so it is off by default. Leave `chromaOnly` on unless auto exposure is
disabled: with it on, a neutral ambient resolves to exactly white and the pass
skips itself. **It double-counts against the dome light's fill, so it must be
tested alone** — see the playbook.

## Game-side behaviour and limitations in D3D9 mode

- **The game cannot draw any UI at all.** RmlUi menus render through WebGPU,
  which is never initialized here, so every UI document is skipped at startup —
  the prelaunch picker, the first-run preset window, the crash-report consent
  dialog. The game-side ImGui dev overlay runs without crashing and renders
  nothing. The in-game HUD and menus (J2D, through GX) do work.

  **The recovery is a separate Dusklight overlay hosted by Remix, opened with
  F1** — the same key the game's own overlay used, independent of Remix's own
  menu. Eight topic tabs (Go, Lights, Sky, Surfaces, Scene, Input, Mods,
  Readouts) under a permanent status strip. The game hosts its settings as
  `rtx.dusklight.game.*` options and polls them every frame; readouts come back
  as `rtx.dusklight.env.*`. Full write-up:
  `dxvk-remix/documentation/DusklightOverlay.md`.

  Three otherwise-unreachable game features are back through it: **warp**
  (plain-English level names, from the game's own destination table),
  **recording mode**, and the **clock** — a slider plus Midnight / Sunrise /
  Noon / Sunset presets and **Freeze Time**. The day is 360 degrees, so 15 is an
  hour: 0 midnight, 90 sunrise, 180 noon, 270 sunset. **Freeze before shooting
  any A/B pair**, or the sun has moved between the two shots and part of the
  difference is the clock rather than the setting under test.

- **Mods are disabled entirely on this backend.** Mod graphics stages are inert
  without WebGPU, and a native mod that touches the renderer crashed the process
  at load, so every mod search directory is dropped when the backend is D3D9.
  Log line: `D3D9 backend: mods are unsupported here, skipping mod discovery`.
  **`config.json` is not rewritten**, so one config moves between a modded build
  and a D3D9 test build with no edits either way.

- **HD texture replacement packs work here** as of 2026-08-05, **tested good
  2026-08-06.** Drop `.dds` files named `tex1_{w}x{h}_{hash}_{fmt}.dds` into
  `<ConfigPath>/texture_replacements/`. Nothing to turn on; both settings default
  true and are read at launch, so there is no live toggle.

  **The pack's bytes never go through D3D9** — the game hands each file to Remix
  through the API and tags each draw with which replacement it wants, so Remix's
  texture hash stays the hash of the game's own D3D9 texture. **Installing,
  changing or removing a pack does not move a single hash**, and tags authored
  without a pack stay valid with one.

  `.dds` only; PNG entries are skipped and logged. Packs shipped inside a mod do
  not load, mod discovery being skipped wholesale. This also sharpens the
  **HUD**, which a Remix USD mod cannot, since UI draws never reach material
  replacement; turn `texrep.applyToRaster` off to isolate a HUD-only regression.
  **Expect a slow first launch.** If a pack appears to do nothing, the overlay
  reports the game's counts and Remix's separately, because "never handed over"
  and "handed over then ignored" look identical otherwise. Failure modes:
  `extern/aurora/docs/dx9/texture-replacements.md`.

- **Input no longer falls through an open overlay.** Remix's own
  `rtx.blockInputToGameInUI` cannot work here: it sends a window message across
  the **32-bit bridge**, which a 64-bit game loading `d3d9.dll` directly never
  receives. The bridge carries the intent instead (`rtx.dusklight.uiActive` →
  `PADBlockInput`), also suppressing the held state on release so no key sticks.
  Turn it off with `rtx.dusklight.blockGameInput = False`.

- **Resizing the window blacks out briefly, by design.** Remix does not re-derive
  its UI overlay from a D3D9 device `Reset`, so the backend fully recreates the
  device on a size change; Remix restarts its renderer and the texture cache
  rebuilds. Debounced, so dragging an edge stays responsive and rebuilds once.

- **Frame interpolation should be disabled** — its presentation-camera path
  depends on pass resolves that no-op in this mode.

- EFB colour copies and offscreen passes are real; depth-format copies still use
  a neutral placeholder and post-processing is skipped by design.
  `extern/aurora/docs/dx9/unsupported-effects.md`.

- If characters look wrong, check the Remix log for `Cannot decompose the
  matrices for a skinned mesh` or `draw call has bones but no blend weight
  buffer` — both mean a stale build of this branch. VRAM growing without bound,
  or textures flickering in the categorize-textures tab, is likewise a stale
  build.
