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
```

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
  top of Remix's output.
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
  but nothing is rendered.
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
