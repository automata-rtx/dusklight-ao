# Remix integration: archive

> ## ⚠ Last resort. Do not read this to find out how anything works.
>
> **This file is unmaintained by design.** It is a dump of point-in-time session
> notes, kept only so that reasoning which was expensive to produce is not
> destroyed. Entries are **not** updated when the thing they describe changes.
>
> **Assume anything here is out of date until you have confirmed it elsewhere.**
> It contains status snapshots that were true on the day they were written,
> phase plans that have since been superseded, and at least one diagnosis that
> was later proved wrong. Formatting is inconsistent because the content was
> written across many sessions for different purposes, and some of it is
> improperly formatted; it is not worth fixing.
>
> **Entries written before 2026-08-04 may also carry a design philosophy that
> has since been retired** — treating "the raw D3D9 image stays correct" as a
> requirement, or fixed-function limits as a ceiling. Neither holds: the raw
> image is never shown to a player, it exists only as the feed into Remix, and
> where the D3D9 stream cannot carry something the answer is to change the fork.
> `extern/aurora/docs/dx9/remix-material-interface.md` §0.
>
> **Read one of these instead:**
>
> | Question | Document |
> | :-- | :-- |
> | What is broken right now? | [`remix-open-issues.md`](remix-open-issues.md) |
> | How does the design work? | [`kankyo-remix.md`](kankyo-remix.md) |
> | How do I test it? | [`remix-test-playbook.md`](remix-test-playbook.md) |
> | Why is this surface the wrong colour? | `extern/aurora/docs/dx9/remix-material-interface.md` |
> | What does this game symbol's name *mean*? | [`japanese-naming.md`](japanese-naming.md) — the names are romanized Japanese, and this archive glosses none of them |
>
> **The only good reasons to open this file:** you are about to re-investigate
> something and want to know whether it was already investigated; or a live
> document cites an entry here by name. Nothing routes here for a fact — if you
> find yourself citing this file as evidence, find the claim in a maintained
> document instead, or verify it against the source yourself.

### Status log

- **2026-07-29 — the backlog session. Five features run, five passed, two bugs
  closed, one real defect found.** No code changed; this entry is the results.

  **Passed:** the clock ("flawlessly and as expected"), warp ("exactly as
  intended"), local point lights (`found 5 / drawn 4 / tracked 4` in the Forest
  Temple), `hideSkyBillboards`, and aerial perspective under the physical sky.
  Lake Hylia's morning fog came out "suitably intense", the first look at the
  dense end of the σ mapping. No crashes across the whole session.

  **Two bugs closed.** Local point lights work; no single change is identifiable
  as the fix, which is recorded rather than glossed. The night shadow wandering
  is fixed by `hideSkyBillboards`, and because the 2026-07-28 measurement
  predicted exactly that, the cause is confirmed rather than worked around —
  the 80 m camera-anchored moon quad really was eating the shadow rays.

  **Two numbers settled, neither of them a default yet.**
  `localLightIntensity` **19** and `localLightRadius` **10**. The 19 is the more
  interesting result: it is precisely the *derived* alternative reading of the
  game's attenuation curve that was written down on 2026-07-28 as "arguably more
  faithful" and then not shipped, because a too-dim scene is easier to diagnose
  than a blown-out one. Testing picked the derived number independently. The
  conservative default was the wrong bet and open issue 3 says so.

  **The defect that came back in their place is open issue 4**, and it is worth
  the entry on its own. The sky is being dimmed by our own fog, and the reason is
  an asymmetry inside the composite: `applyFog` exempts sky pixels *explicitly*,
  with a comment about not driving the sky to a flat colour — and then
  `applySkyContribution` multiplies the dome by the full-grid volume attenuation
  anyway, with the froxel in-scatter already added on top. One half of the fog
  was taught the lesson and the other was not.

  Two things follow that are easy to miss. First, this is **not** a Phase C
  problem, even though Phase C is where it was noticed — it shows without
  `physicalSky` and it is worse in Lake Hylia. Phase C's blend cannot be judged
  until it is fixed. Second, the reported instinct — "the sky must be tagged as
  Sky in Remix, and there is no texture, so it has to be done API-side" —
  identified the right *symptom class* (the sky is not being treated as exempt
  from fog) and the wrong *mechanism*: the generated sky is a dome light sampled
  on ray miss, not captured geometry, so there is nothing to tag even in
  principle. The fix belongs in the composite.

  **Also logged:** the wolf-senses overlay renders as an opaque white disc
  (issue 5), and the world-space UI billboards — targeting arrow and torch
  fires — appear only intermittently and are absent from Remix's texture
  categorization screen entirely (issue 6).

- **2026-07-28 — the clock is reachable, and it can be stopped.** Bridge
  protocol **3 → 4**. Warp tab gains a time-of-day slider, four presets
  (Midnight 0, Sunrise 90, Noon 180, Sunset 270 — the day is 360 degrees, so
  15 is an hour) and **Freeze Time**.

  Freeze is the one that matters. Every comparison shot taken so far has had a
  moving sun in it, so part of every measured difference has been the clock
  rather than the setting under test. There was no way to reach the time of day
  at all: `timeScale` (`d_kankyo.cpp:2370`) is a frame-delta normalizer, not a
  speed control, and the game's settings screen is not drawn in this mode.

  Three implementation notes worth keeping, because each replaced a version
  that would have been subtly wrong:

  1. **Freeze reuses the game's own mechanism.** `using_time_control_tag` is
     what `d_a_kytag11` sets for a stage whose sky must not move, and
     `setDaytime` already tests it (`d_kankyo.cpp:1577`). Setting it ourselves
     means the freeze takes a branch the game exercises every frame rather than
     a second one beside it that would have to be kept in step. **Consequence
     to know:** it also holds `dark_daytime` and skips the `daytime = 0` the
     darkworld branch applies, so a freeze carried into the Twilight Realm
     keeps the light-world time instead of snapping to midnight. Right for a
     comparison, not a description of the game.
  2. **The request is a value plus a counter, not a bare value.** Acting on the
     value alone pins the clock there every frame and it can never run on;
     acting on the value *changing* makes asking twice for the same time do
     nothing the second time — which is exactly what pressing a preset button
     twice is. Same shape as the warp commit, including latching the first
     count seen without acting on it.
  3. **The counter is incremented in the UI, not read off the option.** The
     warp button does read-modify-write, which is fine for something pressed at
     most once a frame. A slider fires on many consecutive frames, and
     read-modify-write only stays monotonic if every deferred set lands before
     the next read. The slider also only syncs from the game while it is *not*
     held, or the value coming back over the bridge a frame or two late fights
     the hand holding it.

- **2026-07-28 — the control plane: a separate F1 overlay, and warp.** Bridge
  protocol **2 → 3**. Full write-up in
  `dxvk-remix/documentation/DusklightOverlay.md`; only the game-side facts are
  repeated here.

  The game never draws its own UI in fixed-function D3D9 mode, so everything the
  game owns had to be reachable from somewhere that *is* drawn. Remix now hosts
  a **separate overlay on F1**, independent of Remix's own menu — either can be
  open without the other, both can be open at once.

  - **Input blocking finally works.** `rtx.blockInputToGameInUI` never could
    have worked here: it sends a window message across the **32-bit bridge**,
    and a 64-bit game loading `d3d9.dll` directly never receives it. That is
    why input has always fallen through to the game with a menu open. Remix now
    publishes `rtx.dusklight.uiActive` and the game calls
    `PADBlockInput(...)`, which suppresses the held state on release so nothing
    sticks down. Gated by `rtx.dusklight.blockGameInput` (default on).
  - **Warp** (`updateWarp()` in `remix_bridge.cpp`). The overlay sends indices;
    the game resolves them against `src/dusk/map_loader_definitions.h` and
    pushes back plain-English names pipe-delimited. The table stays in one
    place, so the list the overlay shows is by construction the list the warp
    travels on. Fires `dComIfGp_setNextStage` when
    `rtx.dusklight.warp.commit` **changes**, and latches the first value seen
    without acting, so a game restarting under a still-running Remix does not
    teleport on connect.
  - **Layer `-1`, not `0`.** `dComIfGp_setNextStage` folds `>= 15` to `-1` but
    **nothing folds 0 to -1** — 0 is a real layer. Ours initially defaulted to
    0, which would have landed in the wrong version of any stage whose default
    layer is not 0. Fixed; bounds `[-1, 14]` on both sides.

    Re-checked 2026-07-28 after the default was questioned: the game's own warp
    menu uses -1 in **all four** places it touches the layer — the
    `WarpSelectionState` initializer (`src/dusk/ui/warp.cpp:20`),
    `reset_selection` (`:109`), the picker list (`:292`) and every
    `clamp_indices` path — with `kMinLayer = -1`, `kMaxLayer = 14` (`:12-13`).
    There is no site where Dusklight defaults the warp layer to 0, so matching
    the game means -1.
  - **Recording mode** is now a live toggle. It is a game setting whose only
    other route was editing `config.json` and restarting — and only in one
    direction, since a value set there could not be turned back off while
    running.

  Also landed: the Dusklight settings moved out of Remix's post-processing
  section into their own tab with collapsible sections; a **Requirements**
  section naming every Remix option these features depend on but do not own
  (with buttons); and a **What this overrides** section naming the Remix
  options that will appear to do nothing while the atmosphere is on. Both exist
  because "I changed it and nothing happened" has cost this project real time
  more than once.

  Removed from CI: the **Remix x86 bridge** component. Dusklight is 64-bit and
  never used it.

- **2026-07-28 — Phase 0 run, Phase C landed.** Phase A and B tested in game
  and reported as *"a massive, frankly monumental success"*: fog range, shape
  and per-area scaling all confirmed. One fix came out of it — `skyIntensity`
  1.0 → **6.0**, because the anchor arithmetic forgot the palette is
  sRGB-decoded before scaling, which takes a mid blue from 0.5 to about 0.2.
  Phase C (physical Hillaire sky blended against the palette) is implemented
  and CI-green but **has never been seen running**.

  Settled by testing: `celestialNoonElevation` = **80** (short of 90 on
  purpose — at exactly 90 the azimuth flips instantaneously at noon), and
  `disableFrustumCulling` **works and visibly helps light leakage**.

- **2026-07-27 — atmosphere Phase A + B landed. Untested, and the calibration
  pass that should have preceded them was skipped.** One participating medium
  derived from the game's palette now drives the volumetrics, the fog and a
  generated sky together, instead of three systems deriving their own and
  disagreeing. Bridge protocol **1 → 2**. Design and the full compromise ledger:
  `dxvk-remix/documentation/DusklightAtmosphere.md`; the game-side data in
  `docs/kankyo-fog.md`. Everything defaults off.

  Three things found on the way in that are worth not rediscovering:
  - **Remix keeps only the first fog state it sees each frame**
    (`rtx_scene_manager.cpp:609`), and this game sets fog *per object* — so
    which of a room's states won was decided by submission order. That was
    listed here as a hypothetical risk under IV.4; it is real, and the pushed
    override is now the mechanism rather than the fallback.
  - **`g_env_light.hide_vrbox` is not a usable "no sky" signal.** Only the
    vrbox actor writes it, so in stages without one — every interior, which is
    where the question matters — it holds whatever the last outdoor area left.
    The bridge recomputes the test instead.
  - **The moya haze billboards were already disabled on this backend**
    (`dKankyo_cloud_Packet::draw`, `d_kankyo_wether.cpp:119`), so the
    double-count they were expected to cause cannot happen. A switch for it was
    written and then removed rather than ship a control that does nothing.

  **Phase 0 was never run**, so `zHalfMin`, `froxelRangeScale` and
  `skyIntensity` are analytic first guesses. Before concluding a result is
  wrong, read `DusklightAtmosphere.md` §13 — it explains how to run that
  calibration on a build that already has this change, and how to tell a
  mis-set constant (wrong everywhere) from a bad mapping (wrong per area).
- **2026-07-27 — bloom fidelity pass (confirmed good in-game).** Four errors
  in the port plus a fifth in the composite; owner reports the result
  "massively improved". Details in "Bloom fidelity" below. The composite one
  is worth repeating here because it explains everything that came before it:
  the Dusklight path inherited Remix's fixed `0.01` attenuation, which is
  calibrated for Remix's own broadly-gathering pyramid. Ours was 100× too
  faint, so every brightness knob had to be pinned to compensate and it still
  read as a weak wash — which is why turning it *off* looked closer to the
  original.
- **2026-07-27 — sun/moon elevation cap lifted** (`game.celestialNoonElevation`,
  default = vanilla). **Untested.** Write-up in `docs/sun-elevation.md`.
- **2026-07-27 — geometry switches added** (`game.disableFrustumCulling`,
  `game.remixHideSkyBillboards`). Both **untested**, both off by default. The
  first is for occlusion the path tracer needs and the game throws away; the
  second is the one-click test for open issue 2.
- **2026-07-26/27 — controls moved into Remix's Dusklight tab.** The game's
  ImGui is never drawn in D3D9 mode, so every setting built for this work was
  behind a window that cannot appear. See "Where the controls live" below.

- **Phase 0 + Phase 1: implemented** (game: `src/dusk/remix_bridge.{cpp,hpp}`,
  vendored `include/remix/remix_c.h` @ 0.6.4, `game.remixKankyoBridge`
  config var, Remix Bridge debug window, `bloom_c::draw()` skipped while the
  bridge is active; fork: `rtx.dusklight.env.*` group (NoSave — verified the
  save path filters NoSave at `RtxOptionImpl::writeOption`, so game-fed
  values never reach user.conf), mono prepass shader, composite base
  weight, `rtx.bloom.dusklightFollowGame` + `dusklightThresholdScale` +
  manual mono/base-weight knobs).
- **Phase 2: implemented** (aurora checkpoint 3.17: `apply_fog_state()` in
  `lib/dx9/dx9_draw.cpp` forwards GX fog to `D3DRS_FOG*` per draw; ortho/UI
  draws stay fog-off). **Fog fidelity evaluation — read before testing:**
  - Remix has *two* consumers for captured D3D9 fog, and with Remix's
    **default settings neither fires**: composite's depth fog early-outs
    whenever volumetrics are enabled (`rtx.volumetrics.enable` defaults to
    True), and the volumetric fog remap defaults to off. Fog silently does
    nothing until a mode is chosen:
  - **Faithful mode** — `rtx.volumetrics.enable = False` (composite depth
    fog, on via `rtx.enableFog` by default). Reproduces the exact
    `D3DFOG_LINEAR` ramp `(end−d)/(end−start)` — identical maths to
    `GX_FOG_PERSP_LIN` — and it fogs by **radial distance**, which matches
    vanilla better than plain view-Z because TP keeps `GXSetFogRangeAdj`
    (the radial correction) enabled. Two knobs: `rtx.fogColorScale`
    (default 0.25; the captured gamma colour is used as linear pre-tonemap
    radiance, so with auto exposure off start near 1.0 and calibrate once)
    and `rtx.maxFogDistance` (default 65504 — raise it; TP fog ends exceed
    it and geometry past the cutoff gets no fog at all).
  - **Volumetric mode** — keep volumetrics on and set
    `rtx.volumetrics.enableFogRemap = True` +
    `rtx.volumetrics.enableFogColorRemap = True`. Kankyo's fog colour
    becomes the participating medium's transmittance colour (light shafts,
    real scattering); the distance mapping is *not* the linear ramp
    (fog end remapped through `rtx.volumetrics.fogRemap*Meters`, which
    interact with `rtx.sceneScale`). Prettier, physically consistent,
    less literal.
  - Expected weak points to watch on first test: fog colour shifting with
    exposure/tonemap (calibrate `fogColorScale`, or disable auto
    exposure), the first-fog-wins capture picking a stray draw (watch the
    Remix dev menu fog panel), and underwater palettes (very dense fog)
    tripping `rtx.volumetrics.waterFogDensityThreshold` and flipping modes.
- **Phase 3: implemented, untested** (fork: `DxvkDusklightGrade`, its own
  `RtxPass` dispatched immediately before the bloom rather than folded into
  it, so `rtx.bloom.enable = False` does not silently take the grade with
  it; `rtx.dusklight.grade.*` response options; a CPU-resolved constant
  tint, so the shader is one multiply and the pass skips itself when the
  tint is neutral. Bridge: `actorAmbient`/`bgAmbient` pushed from
  `g_env_light`). Two design points that changed from the draft during
  implementation are written up in IV.3: the grade runs *before* the mono
  overlay (that is the order the GC had — ambient at shading time, mono in
  the post pass), and the response rails act on the tint's level before
  they act per channel (a per-channel-only floor flattens a night ambient
  to grey). Defaults ship with `enable = False`.
- **Phase 4 (partial): sun/moon distant light implemented.** The bridge now
  drives one Remix distant light through the light API
  (`CreateLight`/`DrawLightInstance` each frame; device registered via a new
  `aurora_dx9_get_device()` accessor, re-registered after resize-recreation).
  It is a true `remixapi_LightInfoDistantEXT` (Remix's dedicated sun/moon
  light type, mapping to `RtDistantLight`) — that struct carries only a
  direction, angular diameter and radiance, with no position, so the light
  is infinitely far by construction rather than "very far away".

  The direction is derived **analytically from time of day**, not from any
  world position. `setSunpos` places the body on an ellipse around the
  camera eye (`offset = (sin a · 80000, −cos a · 80000, −cos a · 48000)`,
  `sun_pos = eye + offset`); the eye cancels in the offset and the radii
  cancel under normalization, leaving `normalize(sin a, −cos a, −0.6 cos a)`
  — verified identical to differencing `sun_pos` against the camera to
  4.4e-16 across the full day at several camera positions. So no arc, no
  position and no camera enter the code path, and it keeps working in the
  stages where `setSunpos` declines to update `sun_pos`.

  Sun while 67.5 < daytime < 292.5, moon otherwise (same orbit, half a day
  out of phase), crossfaded over ±7.5 daytime units — the window edges
  coincide with the body dipping below the horizon, so the fade completes
  as it sets. Deliberately **not** driven by the game's shadow-light
  selection, which snaps to nearby lanterns. Gated on `dKy_SunMoon_Light_Check()`
  (outdoor stages only; false in twilight/interiors). Tuning:
  `game.remixSunMoonLight` (on), `game.remixSunIntensity` (5),
  `game.remixMoonIntensity` (0.3), `game.remixCelestialAngle` (2°), all
  live-editable in Remix's Dusklight tab, plus a debug direction-flip
  checkbox in case game→Remix handedness needs the sign. Sun tint is
  vanilla's constant actor sun diffuse (126,110,89 normalized); moon is a
  cool counterpart. With `rtx.fallbackLightMode = 1` (NoLightsPresent) the
  fallback light yields automatically once this light exists.
- **Phase 4 (local lights): implemented, untested.**
  > **Superseded — this is now open issue 0, a known bug, not merely
  > untested.** Two things also changed after this entry was written: the
  > bridge reads `efplight[0..4]` as well (the game keeps local lights in
  > **two** arrays, and reading only the first quietly loses lights in exactly
  > the rooms that have fewest), and three diagnostics were added that name
  > which failure mode is in play. Read open issue 0, not this paragraph.

  The bridge mirrors
  `g_env_light.pointlight[0..99]` — everything registered through
  `dKy_plight_set`: torches, braziers, lanterns, campfires, Midna, bomb
  flashes, and the dungeon lights — into Remix sphere lights, created and
  destroyed as their actors come and go.

  **Why this matters more than it sounds.** Aurora deliberately does not
  forward GX lights to D3D9 (unsupported-effects #16: "Remix relights
  everything"), so Remix sees *no* game light at all. Outdoors the
  sun/moon distant light now covers that. Indoors and at night nothing
  did: the scene fell through to Remix's fallback light. These are the
  lights those scenes were lit by.

  **Intensity is not a tuning constant.** It reuses Remix's own
  legacy-light conversion (`LightUtils::calculateIntensity`): work out how
  far the original light was meant to reach, then solve for the radiance a
  sphere light of fixed radius needs to still be perceptible there —
  `radiance = reach² · 0.01 / (π · radius²)`. The game hands us the reach
  directly, because `LIGHT_INFLUENCE::mPow` *is* that distance
  (`dKy_light_influence_id` treats "closer than mPow" as "inside this
  light"). So these lights land in the same intensity range as the lights
  of any other Remix title rather than in a range we invented. A torch
  (`mPow` 500, colour AF5D00) resolves to radiance ≈ 49.7, 26.4, 0 at the
  default 4-unit radius — which is also Remix's own default radius for
  converted point lights.

  **The one real judgement call, and it is worth ~19×.** `mPow` is not
  where the light ends, it is where it reaches 1/11 of peak. The game loads
  these as `dKy_GXInitLightDistAttn(info, mPow·0.001, 0.99999, GX_DA_STEEP)`
  → `k0 = 1, k1 = 0, k2 = (1−b)/(d²b)` → `attenuation(D) = 1/(1 + 10D²/mPow²)`.
  Applying Remix's own end threshold (1/255 of the light's brightness) to
  that curve instead gives `reach = mPow·√((maxColorByte − 1)/10)`, which is
  4.3× further for a torch and therefore ~19× the radiance.

  That second reading is arguably *more* faithful, and it is the one Remix's
  philosophy points at: it deliberately ignores a legacy light's `Range` in
  favour of its attenuation curve, because `Range` was usually an
  optimization rather than the light's real extent — and `mPow` is exactly
  that kind of optimization. It is not the default for two reasons: the game
  never applied a point light beyond its influence radius anyway (each
  tevstr gets *one* light, chosen by proximity, so the long tail was rarely
  realized), and a scene that comes up too dim is far easier to diagnose
  than one that comes up blown out. **If the lights read as weak, set
  `game.remixLocalLightIntensity` to about 19** — that is a derived number,
  not a guess, and the slider reaches it.

  Identity is the `LIGHT_INFLUENCE`'s address, mixed into a 64-bit hash: it
  lives inside its actor, so it holds still exactly as long as the light
  does. Re-creates are epsilon-gated on position and radiance (0.5 world
  units, ~6mm at TP's scale) so a carried torch does not cross the API lock
  every frame. Lights whose actor is gone are destroyed, which is what
  keeps Remix's external-light map from growing all session as rooms load.
  On resize the handles are dropped without destroying — they belonged to
  the device that went away with them.

  Settings: `game.remixLocalLights` (**off** by default — third unverified
  system, same reasoning as the grade), `game.remixLocalLightIntensity`
  (1.0), `game.remixLocalLightRadius` (4.0), all live in Remix's Dusklight
  tab with drawn/tracked counters.

  Not done: `mFluctuation` (the flicker amount; every torch sets 1.0, bombs
  100) is ignored for now — applying it would mean a re-create every frame
  for every flickering light. Worth revisiting once the base look is
  calibrated.
- **Sky (Phase 4 remainder): SUPERSEDED — this paragraph described tagging,
  which turned out to be impossible.** Left in place, struck through, because
  it was the plan of record for a while and its failure is the reason the
  generated sky exists.

  > ~~Manual tagging is the right mechanism, and it now fixes two things.
  > Besides being the missing fill light, it is the proper fix for the
  > night-only wandering shadows… One-time setup in the Remix dev menu
  > (texture categories → Sky): tag the vrbox sky dome, both cloud layers
  > (kumo), the horizon haze (kasumi) and sun/moon billboard textures. Once
  > tagged, the sky raster draws land in Remix's sky probe *with their TEV
  > tints*… scale with `rtx.skyBrightness`.~~

  **Why it cannot work — CORRECTED 2026-07-29, this reasoning was wrong.** The
  claim was: Remix categorises by hashing *texture content*; the vrbox is
  painted with vertex colours and has no texture; therefore no hash, therefore
  no category, therefore neither dev-menu nor programmatic tagging can reach it.

  The premise is true and the conclusion does not follow. Texture hashing is one
  of **three** routes to a category, and the other two need no texture:
  `rtx.skyBoxGeometries` tags a captured draw by its **geometry** hash
  (`rtx_types.cpp:416`), and `REMIXAPI_INSTANCE_CATEGORY_BIT_SKY` declares the
  category outright on geometry submitted through the Remix API
  (`remix_c.h:457`). Full write-up and the evidence in
  `dxvk-remix/documentation/DusklightAtmosphere.md` §14.9.

  **This does not undo Phase B1.** The generated dome light was the right answer
  for a different reason than the one recorded — it gives HDR sky radiance that
  feeds GI, which a rasterized sky probe does not — and it is tested and working.
  What is retired is the *argument*, not the architecture. Anyone reaching for
  "we cannot tag that, it has no texture" should check §14.9 first.

  **What replaced it:** Phase B1. The same palette colours cross the bridge as
  numbers, Remix builds a lat-long dome from them and registers it as a dome
  light (`rtx.dusklight.atmosphere.skyEnable`), and the game's own dome is
  switched off (`rtx.dusklight.game.hideVrbox`). That is the fill light, it is
  tested, and it gets kankyo's colours into reflections and GI by a route that
  never depended on hashing anything. `rtx.skyBrightness` is irrelevant under
  it — use `atmosphere.skyIntensity`.

  The one live remnant is the sun/moon/star billboards, which *are* textured
  and so could be tagged. `hideSkyBillboards` removes them instead; see open
  issue 2.
- **Owner test feedback (first bloom/fog session), to address:**
  - Dusklight bloom renders and tracks time of day, but doesn't yet look
    like the game's — calibration pass pending (threshold scale vs. the
    scene's HDR range, gain distribution, and the burnIntensity=5 +
    blurRatio=255 test values need re-baselining once the sun light lands).
  - Volumetric fog mode reacts more strongly to kankyo's fog near/far than
    expected; faithful mode is consistent.
    `rtx.volumetrics.enableFogMaxDistanceRemap = False` (owner already set
    it) is the intended lever — it pins the medium's density and leaves
    only the colour game-driven. Revisit defaults after the light exists.
- **Owner tuning note:** auto exposure may simply be disabled for reference
  (`rtx.autoExposure.enabled = False`) instead of clamping it — with AE off
  the pre-tonemap range is fixed, which makes `dusklightThresholdScale`
  calibration straightforward and makes the base-weight dimming read
  exactly as authored. The chroma-only default for the Phase 3 grade
  matters less in that configuration but remains the right default for
  AE-on setups.

### Where the controls live (and why they are not in the game)

The game's ImGui is **never drawn in D3D9 mode** — its overlay renders
through WebGPU, which is not initialized here
(`docs/dx9-fixed-function.md`, limitations). So the "Remix Bridge" debug
window built in Phase 0 is invisible in the one mode the whole feature
exists for, and every instruction to open it was unfollowable. That was a
real design error, caught by the owner rather than by us.

The controls therefore live in **Remix's own ImGui overlay**, in a
`Dusklight` tab, as ordinary `rtx.dusklight.game.*` options. The game reads
them back every frame through a `getRtxOptionValue` export on the Remix DLL
— the Remix API only *writes* config variables, and extending
`remixapi_Interface` with a getter would break its ABI (its size is
asserted), so this rides the same plain `__declspec(dllexport)` mechanism
the fork already uses for `writeMarkdownDocumentation`.

Direction of travel:

- **Remix → game**: the settings (`rtx.dusklight.game.*`), polled each frame.
  The game's own `game.remix*` config values remain as the fallback for a
  Remix build without the export, and for backends where the bridge is inert.
- **Game → Remix**: state (`rtx.dusklight.env.*`), pushed as before. Light
  status — azimuth, elevation, day/night, fade, device registration, local
  light counts — is pushed too, purely so the tab can display it.

The game-side Remix Bridge window is kept: it still works on the WebGPU
backends, where it is the only way to edit the fallback values.

### Two protocol bugs found on first contact (2026-07-26)

Both surfaced the moment the owner ran the new Remix build against an older
game build, and both were mine.

1. **The diff cache assumed exclusive ownership.** `push()` only calls
   `SetConfigVariable` when a value changes, which is right for cost and
   wrong for correctness: `rtx.dusklight.env.*` are **NoSave**, so anything
   that rebuilds Remix's user layer — saving settings from its UI, a config
   reload — drops them back to their defaults. The cache then never pushes
   them again, and Remix reports the bridge as disconnected *forever* while
   the game is convinced it is connected. Fixed by verifying instead of
   assuming: the bridge reads its own heartbeat back each frame and clears
   the cache if it is missing (one getter call, recovers next frame), with a
   blind full re-push every 120 frames as the fallback for a Remix build
   without the getter.
2. **Build skew was indistinguishable from breakage.** The tab's only state
   was "is the game reporting anything", which is false in every failure
   mode. A game that connects but predates `rtx.dusklight.game.*` looks
   identical to one that never connected — except its controls silently do
   nothing. The game now stamps `rtx.dusklight.env.protocol`, and the tab
   distinguishes connected-and-current, connected-but-too-old, and absent.

Standing rule this leaves behind: **the game and the Remix DLL are one
protocol and have to be updated together.** The tab says so when they are
not.

### Crash on entering some levels (2026-07-26) — evidence, not yet a cause

Owner logs (`dusklight20260726214829`, `remixdxvk`). What the logs establish:

- **The bridge is connected**: `RTX Remix detected; kankyo bridge active
  (remixapi 0.6.4)` and `registered D3D9 device with the Remix API`. The
  `getRtxOptionValue` export resolved — there is no warning about it, which
  also proves the export mechanism works.
- **Build skew, reversed**: game `8b89e4f` against Remix
  `remix-main+4779899c`. `SetConfigVariable(rtx.dusklight.env.protocol)
  failed (1)` — error 1 is `GENERAL_FAILURE`, which
  `remixapi_SetConfigVariable` returns when the option does not exist, and
  `protocol` landed one Remix commit later. Harmless in itself.
- **Local lights were off** (`rtx.dusklight.game.localLights` defaults false
  and is not in the owner's rtx.conf), so that subsystem is not implicated.
  The sun/moon distant light *was* running.
- **The crash is entirely inside `d3d9.dll` on a Remix-owned worker thread**:
  all frames are in `d3d9.dll` and the outermost two are KERNEL32
  `BaseThreadInitThunk` / ntdll `RtlUserThreadStart`, i.e. a thread whose
  entry point is in Remix, not the game. `EXCEPTION_ACCESS_VIOLATION`
  reading address `0x10` — a null pointer plus a small member offset. No
  game frames at all.
- **Context**: a cutscene transition (`ZEV event [BSPTRANS]`,
  `entering_event=true`, Midna's `s_md` models loading), immediately after a
  Remix camera cut, which re-initializes the Neural Radiance Cache
  (`NRC SDK: Loading the default network config data`) — on a worker thread.

Ruled out along the way: aurora's view inverse is guarded by a determinant
check and would have logged `camera view matrix not invertible`, which it
did not. Remix's own `Attempted invert a non-invertible matrix` fired 19
seconds earlier and is not adjacent to the crash.

Hardened regardless, because both were real defects:

- `getRtxOptionValue` took no lock while Remix resolves options on its own
  thread at frame end. Now takes the same update mutex those writes do.
- The bridge fed positions and radiances to Remix without checking them for
  NaN. Remix validates radius and radiance for sign and range but **not**
  for NaN, and a NaN reaching its acceleration structures takes the renderer
  down on a worker thread with a backtrace that says nothing about where it
  came from — which is the shape of crash we are looking at. Both light
  paths now skip a light whose values are not finite.

*Next step is a bisect, not more analysis*: `rtx.dusklight.game.bridgeEnable
= False` turns off every push and both lights. If it still crashes, nothing
of ours is involved and the NRC-on-camera-cut path is the next suspect
(`rtx.neuralRadianceCache.enable = False`).

### Bloom fidelity: four errors in the port (2026-07-26)

The owner compared against vanilla Dusklight and reported the bloom simply
does not look like it. Re-derived the effect from the TEV setup in
`bloom_c::draw2()` (`m_Do_graphic.cpp:1456`) rather than from the earlier
reading, and found four things wrong, one of them fundamental.

**1. Wrong colour space — the fundamental one.** The effect was authored
against the EFB: an 8-bit framebuffer holding *finished display colours*.
Every part of it is defined against that. The threshold is a fraction of
display white. The intermediate buffers clip at white, and that clipping is
what gives bright cores their washed-out look. The screen blend asks "how
close to white is this pixel already", and the composite's base weight is a
blend alpha against a 0..1 image. We were running the whole thing on
open-ended **linear pre-tonemap radiance**, where none of those four mean
what they meant — and where blurring concentrates halos far more tightly,
because blurring linear radiance weights bright pixels enormously more than
blurring display values does. Fixed by moving the Dusklight pyramid to run
**after tone mapping**, in gamma space (`rtx.bloom.dusklightDisplaySpace`,
default on). This also makes the whole effect exposure-independent, which
is why `dusklightThresholdScale` existed at all.

**2. The threshold was the wrong operation entirely.** Decoding the three
TEV stages, with swap tables `R,R,R,G` and `B,B,B,A` mixed by `HALF`:

```
key    = 0.25*R + 0.25*G + 0.5*B
source = colour * saturate(key - mPoint)
```

It is a **luminance-keyed mask multiplied by the original colour**, not a
per-channel subtraction. The port did the latter, which is close to the
opposite in character: it shifts every bloomed highlight towards its
dominant channel, where the original preserves hue exactly. It also blooms
things the original refuses to — saturated red at full intensity has a key
of 0.25 and never clears the default 0.5 threshold, but the port bloomed it
at half strength. Note the weights: **blue counts double**, which is a real
and distinctive part of the look.

**3. `rtx.bloom.steps` should be 6, not 5.** The game runs five blur passes
over six levels (`divStart` 2 → `divNum` 6). Our default of 5 gives four,
which narrows the halo a level *and* changes the per-pass gain, since the
total is distributed as its N-th root.

**4. The upsample exponent was off by one.** The original is
`falloff^(1/(i - divStart + 1))` with `divStart = 2`, i.e. `1/(i-1)`; we
used `1/i`, leaving every level slightly too faint.

Deliberately kept: the 13-tap downsample on the threshold step, instead of
the original's point sample. A single bright pixel with a box filter makes
the bloom crawl frame to frame, and that trade is worth more than the
exactness.

### Phase 0 — plumbing (dusklight)
1. Vendor `remix_c.h` from the fork into `include/remix/` (pin 0.6.4;
   comment the exact-minor rule).
2. `src/dusk/remix_bridge.{cpp,hpp}`: init/availability, diff-cached
   `setVar`, `tick()` wired into the main loop after kankyo draw; config
   var `game.remixKankyoBridge`; log lines on init/degrade.
3. ImGui "Remix Bridge" debug window (values, per-key override, push
   counter). Files added to `files.cmake`.
   - *Acceptance*: under Remix, heartbeat visible in Remix's dev menu
     (`rtx.dusklight.env.enable = True`); on stock D3D9/other backends the
     module logs "not under Remix" and goes dormant; zero calls when values
     are static.

### Phase 1 — bloom + mono (fork + bridge)
1. Fork: add `dusklightMonoColor/MonoAmount/MonoLumaMode/BaseWeight`
   options; new grade stage in `DxvkBloom::dispatch` (IV.3, mono +
   baseWeight only at this phase); RtxOptions.md rows; UI rows under
   Post-Processing → Bloom.
2. Fork: add `rtx.dusklight.env.*` option group (NoSave) + verify NoSave
   exclusion in `RtxOptionLayer::save()`; fix if needed.
3. Bridge: push the bloom block (IV.2 table); skip `bloom_c::draw()` when
   bridge active; update `docs/dx9-fixed-function.md` (drop the manual
   bloom table — it's now automatic).
   - *Acceptance*: walking Ordon dawn→noon→dusk visibly re-tunes Remix
     bloom continuously; entering twilight snaps the golden bloom + 37.5 %
     desat + base dim without touching the UI.

### Phase 2 — fog (aurora)
1. Implement GX→D3D9 fog in `dx9_draw.cpp` per the mapping doc (LIN first;
   EXP/EXP2 if any stage uses them — audit says PERSP_LIN only).
2. Verify capture in Remix dev menu (fog states panel); calibrate
   `rtx.fogColorScale` starting point; ship rtx.conf template values.
3. MinGW syntax harness both configs; aurora submodule bump dance per
   CLAUDE.md (aurora dev → dusklight dev, pin SHA).
   - *Acceptance*: Faron morning haze and Lanayru evening fog reappear with
     palette-correct colour, fading over distance pre-tonemap; toggling
     `rtx.enableFog` kills it.

### Phase 3 — ambient grade (fork + bridge)  — implemented, untested

Steps 1 and 2 are done; step 3 needs the game running.

1. ✅ Fork: `DxvkDusklightGrade` (`rtx_render/rtx_dusklight_grade.{h,cpp}`,
   `shaders/rtx/pass/dusklight/dusklight_grade.{h,comp.slang}`), dispatched
   from `RtxContext` immediately before the bloom. Response options
   `rtx.dusklight.grade.{enable,strength,chromaOnly,actorAmbientWeight,
   maxDarkening,maxBrightening}`, UI under Rendering → Post-Processing →
   Dusklight Ambient Grade (which also prints the resolved tint live).
2. ✅ Bridge: `rtx.dusklight.env.actorAmbient` / `bgAmbient` pushed from
   `g_env_light.actor_amb_col` / `bg_amb_col[0]` — the fully blended
   per-frame values, after the four-way palette blend, event add-colours
   and global ratios. BG layer 0 is the main room layer, the one the game
   itself reuses when it needs "the" background ambient (`d_a_mirror`).
3. ⬜ Tune defaults on the four canonical test scenes: Ordon noon (should
   be ≈ neutral), Ordon dusk (warm shift), Faron rain (cool desat), any
   twilight zone (full look together with Phase 1).
   - *Acceptance*: time-of-day/weather grade the path-traced frame; bridge
     off ⇒ image identical to pre-phase baseline.

**Shipped off by default.** `rtx.dusklight.grade.enable` defaults to
false. Phase 3 changes scene *tint* and the Phase 4 sun/moon light changes
scene *lighting*; both are unverified at runtime, and turning them on one
at a time is the difference between a five-minute bisect and an afternoon.

**What the defaults do**, from a simulation of `resolveGrade()` (the
ambients are illustrative, not measured from stage data — TP's palettes
live in `.dzs` files, not in the repo):

| ambient (actor / bg) | `chromaOnly` on (default) | `chromaOnly` off |
| :-- | :-- | :-- |
| neutral grey 180,180,180 | 1.000 1.000 1.000 *(pass skipped)* | 0.809 0.809 0.809 |
| noon, faint cool | 0.982 1.001 1.039 | 0.841 0.855 0.885 |
| dusk, warm | 1.228 0.954 0.779 | 0.841 0.688 0.590 |
| night, cool dark | 0.876 1.003 1.332 | 0.578 0.579 0.694 |
| rain, desaturated cool | 0.943 1.009 1.076 | 0.611 0.641 0.670 |
| twilight, gold | 1.127 1.007 0.578 | 0.845 0.769 0.578 |
| pure red (a real debug state) | 1.650 0.578 0.578 | 1.420 0.578 0.578 |
| black ambient | 1.000 1.000 1.000 *(pass skipped)* | 0.578 0.578 0.578 |

Two properties to hold on to: a neutral ambient resolves to exactly white
and skips the dispatch (so noon costs nothing and changes nothing), and the
rails contain the pathological red case that would otherwise resolve to a
4.7× red multiplier.

### Phase 4 — sun/moon light + sky (aurora + fork + bridge)
As designed in IV.7: `dxvk_RegisterD3D9Device` hook in aurora, distant
light lifecycle in the bridge, vrbox tint investigation, dungeon lights
stretch goal.

### Phase 5 — polish

- ✅ **XFog evaluation — nothing to do, and forwarding it would be wrong.**
  GX fog is computed from projected depth (planar); `GXSetFogRangeAdj`
  adds a per-column correction table whose whole purpose is to make that
  planar depth behave like *radial* distance, so fog does not thin out at
  the screen edges. Remix's composite fog already measures radial
  distance — `viewDistance = length(viewPosition)`
  (`composite.comp.slang:700`), fed straight into the `D3DFOG_LINEAR`
  ramp. So the correction is already applied by construction; forwarding
  the table would double-correct.

  TP does keep it on: `mFogAdjEnable = true` at kankyo init
  (`d_kankyo.cpp:1257`) and `GxXFog_set()` runs immediately after every
  scene `GFSetFog(GX_FOG_PERSP_LIN, …)` (`d_kankyo.cpp:9459`). Every
  `GXSetFogRangeAdj(GX_DISABLE, …)` in the game is on a 2D/UI/menu/movie
  path where fog is off anyway. Aurora records the same conclusion at
  `lib/dx9/dx9_draw.cpp:178`.
- ✅ **rtx.conf template + documentation pass** — `dx9-fixed-function.md`
  carries both fog modes, the bloom table, the ambient grade table and the
  local light notes; this doc's status log and verification section are
  current.
- ✅ **Re-baseline the owner's bloom values** — superseded by the fidelity
  pass. With the composite fix in, `burnIntensity` belongs at **1.0** (it is
  no longer attenuated 100×), `steps` at **6**, and threshold/blur/ratio come
  from the game feed. `dusklightThresholdScale` should stay at 1.0 now that
  the pass runs in display space.
- ⬜ **HDR threshold calibration table per area** — largely obviated by the
  move to display space, since the threshold now has a fixed meaning. Revisit
  only if areas still disagree.
- ✅ **Sky tagging** — closed as impossible, and replaced. The game's sky dome
  carries no texture for Remix to hash, so it can never be categorised; the
  generated dome light (Phase B1) supplies the fill light instead, and it is
  tested. See the superseded Phase 4 bullet above for why this was carried as
  the top item for as long as it was.

### CI coverage note
The fork's workflow only built `main` and `release/**`, so a `claude/**`
branch got no build until its PR opened. `claude/**` is now in the push
triggers, which is what gives the Phase 3 grade a compile check without
opening a pull request for it.

**The x86 bridge steps were removed from the fork's workflow on 2026-07-28.**
Dusklight is 64-bit and loads `d3d9.dll` directly, so it never used the bridge.
Nothing in this project needs it, and building it was pure CI time. (This is
also the reason `rtx.blockInputToGameInUI` never worked here — see the input
note in `docs/dx9-fixed-function.md`.)

### Test/verification strategy
- Owner tests via the GitHub Actions "Build Windows (MSVC x86_64)"
  artifact (game/aurora) and the fork's Actions build (Remix DLL) — keep
  both CI green per phase; land in `Fixed-Function-dev` at checkpoints.
- Every phase has a hard off-switch (`game.remixKankyoBridge`,
  `rtx.dusklight.grade.enable`, `rtx.enableFog`) so regressions bisect in
  minutes.
- Debug affordances: game-side bridge window (Phase 0), Remix dev menu
  option inspection, and `rtx.dusklight.env.*` visible in RtxOptions UI.

### Risks / open questions
1. `remixapi_InitializeLibrary` export presence in *our* built DLL —
   sanity-check exports once (it rides `__declspec(dllexport)`, not the
   .def file).
2. NoSave behaviour of `RtxOptionLayer::save()` — verify before Phase 1.3.
3. First-fog-wins capture robustness — plan B documented (IV.4).
4. HDR calibration of threshold/tint responses is taste work — the knobs
   exist precisely so it can be done live in the Remix UI.
5. Quality-preset layer outranks the User layer for `UserSetting`-flagged
   options — none of our target options carry that flag today; keep it
   that way for `rtx.dusklight.*`.
