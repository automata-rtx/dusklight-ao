# Direct3D 9 fixed-function rendering mode (RTX Remix)

Dusklight has an experimental **D3D9 fixed-function** graphics backend,
implemented in Aurora, whose sole purpose is compatibility with **RTX Remix's
standard d3d9.dll runtime** (no Remix SDK involved). It intentionally trades
visual fidelity for a clean fixed-function D3D9 command stream that Remix can
capture and path-trace: `SetTransform` matrices, `DrawIndexedPrimitiveUP`
geometry, `SetTexture` diffuse maps, alpha-tested cutouts, and fixed-function
**indexed vertex blending** for skinning (rest-pose vertices, stable mesh
hashes).

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

For Remix: place the RTX Remix runtime's `d3d9.dll` next to the Dusklight
executable and launch with the d3d9 backend.

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

# FOG: pick ONE mode. With Remix defaults the game's fog is captured but
# consumed by neither path (composite fog is skipped while volumetrics are
# enabled, and fog remap is off) - i.e. fog silently does nothing.
#
# Faithful mode below is the RECOMMENDED setting today. Volumetric mode
# cannot currently reproduce the game's fog: the froxel grid only reaches
# rtx.volumetrics.froxelMaxDistanceMeters (default 20 m = 2000 game units)
# and the fog remap's endpoints are calibrated for another game. See
# docs/kankyo-fog.md and the fork's documentation/DusklightAtmosphere.md.

# Faithful mode: exact linear ramp, vanilla look.
rtx.volumetrics.enable = False
rtx.maxFogDistance = 10000000
# Captured fog colour -> pre-tonemap radiance. Calibrate once (start ~1.0
# with auto exposure disabled; the Remix default 0.25 is very dim).
rtx.fogColorScale = 1.0

# Volumetric mode (alternative, currently WORSE - see the note above). This
# is also the "Phase 0" calibration experiment from DusklightAtmosphere.md:
# it costs nothing at runtime (raising froxelMaxDistance redistributes the
# fixed 64 depth slices, it does not allocate), and it answers whether the
# mismatch is range or curve shape before any code is written.
# If distant terrain still refuses to close up, that is the linear-ramp vs
# exponential-extinction difference, and only the planned range split fixes it.
#rtx.volumetrics.enable = True
#rtx.volumetrics.enableAtmosphere = True
#rtx.volumetrics.froxelMaxDistanceMeters = 200
#rtx.volumetrics.enableFogRemap = True
#rtx.volumetrics.enableFogColorRemap = True
#rtx.volumetrics.fogRemapMaxDistanceMaxMeters = 300

# Recommended for calibration: fix exposure so thresholds/fog read stably.
#rtx.autoExposure.enabled = False

# AMBIENT GRADE: re-applies the colour of the game's ambient term, which the
# path tracer replaced. Off by default; turn it on once the rest of the
# bridge is behaving, so a colour shift is never ambiguous about its source.
#rtx.dusklight.grade.enable = True

# The kankyo bridge also drives a sun/moon distant light through the Remix
# API. Its settings live in Remix's own Dusklight tab (and here as
# rtx.dusklight.game.*), NOT in the game - the game's debug UI is not drawn
# at all in this mode. With fallbackLightMode = 1 the Remix fallback light
# yields automatically while the sun/moon exists.
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

**Sky billboards (diagnostic).** The sun, moon and stars are drawn at a
fixed offset from the camera eye (`dKyr_drawStar`:
`moon_pos = camera->view.lookat.eye + envlight->moon_pos`), so as world
geometry they travel with the player. Anything Remix captures from them as
ordinary geometry becomes an occluder that follows the camera — which would
only show at night, since stars and the moon are the only sky billboards
drawn then. Tag those textures as **Sky** to fix it properly;
`game.remixHideSkyBillboards` skips drawing them entirely, which is a
one-click way to test whether that is what you are looking at.

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

**Local lights (game-side, off by default).** Aurora does not forward GX
lights to D3D9, so Remix sees no light from the game itself; outdoors the
sun/moon light covers that, but interiors and night fall through to Remix's
fallback light. `rtx.dusklight.game.localLights` mirrors the game's live
point-light list — torches, braziers, lanterns, campfires, Midna, bomb
flashes and the dungeon lights — into Remix sphere lights, with intensity
derived the same way Remix derives it for a legacy D3D9 light. Tune with
`rtx.dusklight.game.localLightIntensity` and `…localLightRadius` in the
Dusklight tab. Keep `rtx.fallbackLightMode = 1` so the fallback light
yields to them.

**Sky setup (one-time):** tag the vrbox textures as Sky in the Remix dev
menu (texture categories): sky dome, both cloud layers, horizon haze, and
the sun/moon billboards. The vrbox uses the main camera, so
`rtx.skyAutoDetect` won't reliably catch it — texture tagging will. Once
tagged, the sky renders into Remix's sky probe with kankyo's palette tints
(so time-of-day sky colour reaches reflections/GI); adjust with
`rtx.skyBrightness`.

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
- **Recommended: set Bloom to Off** (Settings → Bloom, or
  `game.bloomMode` in the config) when running under Remix. The classic
  bloom is a screen-space EFB filter chain; the path tracer replaces this
  class of effect, and the filter quads only overlay raster-derived blur on
  top of Remix's output. Use Remix's own bloom instead — see
  "Dusklight bloom in Remix" below.
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

> **The table below is now automatic.** With the kankyo bridge active
> (`game.remixKankyoBridge`, on by default under Remix) the game pushes its
> live bloom/mono state into `rtx.dusklight.env.*` every frame and the
> Dusklight bloom follows it (`rtx.bloom.dusklightFollowGame`). The manual
> values below still apply when the bridge is off or the game isn't
> running. Design: [`kankyo-remix.md`](kankyo-remix.md); debug via
> the **Dusklight tab** in Remix's own ImGui overlay (Alt+X).

## Dusklight bloom in Remix

The game's own bloom has to be off under Remix (above), so the "Dusk" bloom
mode — the one the settings menu offers as an alternative to Classic — was
ported into our dxvk-remix fork as a post-processing option. Turn it on in
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
- **Frame interpolation should be disabled** — its presentation-camera path
  depends on pass resolves that no-op in this mode.
- **ImGui dev overlay is headless** — game-side ImGui code runs (no crashes),
  but nothing is rendered. This is why the Remix-facing settings live in
  Remix's own **Dusklight tab** rather than in the game's debug windows: in
  this mode the game cannot draw a UI at all, so anything that needs tuning
  against the path-traced image has to be reachable from Remix's overlay.
  The game hosts them as `rtx.dusklight.game.*` options and polls them every
  frame.
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

This work lives on the `Fixed-Function` branch of `dusklight-ao` and
`aurora-ao` — the integration branch, which only advances by merging the
working branch `Fixed-Function-dev` at tested checkpoints. All development
happens on `Fixed-Function-dev` (both repos; dusklight's `extern/aurora`
pin tracks the matching aurora branch). The lineage includes the GPU
skinning work and shares its fork-point ancestry with `ao`/`main`, so
mainline updates can be backported by merge/cherry-pick.
