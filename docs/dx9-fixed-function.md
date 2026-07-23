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
- **Graphics mods (WGSL: AO, realtime shadows, …) are inert** — the mod gfx
  stages (`push_custom_draw`, `create_pass`, `resolve_pass`) return false in
  D3D9 mode. Remix's path tracer replaces these effects wholesale.
- **Frame interpolation should be disabled** — its presentation-camera path
  depends on pass resolves that no-op in this mode.
- **ImGui dev overlay is headless** — game-side ImGui code runs (no crashes),
  but nothing is rendered.
- EFB-copy consumers (real-shadow silhouettes, minimap ripple, heat
  distortion) sample a black placeholder in v1; post-processing (bloom etc.)
  is skipped by design. See `extern/aurora/docs/dx9/unsupported-effects.md`
  for the full list and Remix-side compensation notes.
- GPU skinning: both the PNMTXIDX matrix-palette path (all normal characters)
  and the `GXSetSkinning` extension path (`src/dusk/gpu_skinning.cpp`, the two
  `J3DSkinDeform` actors) map to fixed-function indexed vertex blending —
  Remix receives rest-pose vertices plus per-draw bone matrices.

## Branches

This work lives on the `Fixed-Function` branch of `dusklight-ao` and
`aurora-ao` (development branch `claude/dusklight-dx9-fixed-function-6uoy92`),
based on the GPU-skinning branch `claude/gpu-skinning-72pstj`.
