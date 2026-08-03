# Remix integration: open issues and untested work

The volatile half of the Remix effort. [`kankyo-remix.md`](kankyo-remix.md) is
the stable design reference; [`remix-test-playbook.md`](remix-test-playbook.md)
is how to run a test session; [`remix-history.md`](remix-history.md) is what
previous sessions established, including several lessons that were expensive.

**Rule for this file: an entry states its evidence and its confidence.** Three
separate sessions have written a plausible story here as though it were a
finding, and one of those shipped a fix that did nothing. If a cause is
inferred rather than read, say so.

## State, as of 2026-08-03

CI baselines: dusklight/aurora green on all 8 targets (Windows MSVC x86_64 +
arm64, macOS x3, Linux x2, Android); the Remix fork green on its 3 Windows
configs. **The 2026-08-03 material work is not yet CI-verified** — it was written
in a container with no MinGW toolchain, so the usual syntax harness could not
run. Treat the first CI run as the syntax check.

**Protocol is at 6.** When you bump it, bump `kRequiredProtocol` in the fork's
`showDusklightRemixTab` in the same commit.

**The two live rendering defects:**

1. **Materials lose their colour** — rupees, hearts, lava (issue 8). Root-caused
   2026-08-03 after a shipped fix failed; a corrected fix and the material report
   are in the current build, **untested**.
2. **The fog medium dims the generated sky** (issue 4). Cause verified in the
   composite; `skyFogMode` ships two candidate treatments and one is meant to be
   deleted once they have been compared.

**Also open:** no route exists by which an opaque captured draw can be made
emissive (issue 9), the wolf-senses overlay covering the screen (issue 5), the
world-space UI billboards (issue 6), grass shading (issue 7), the ambient grade,
and the Controls tab.

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
   saturated to white by aurora's compare-mode TEV approximation, the same
   defect already suspected for the white ground. Full write-up, including the
   two experiments that decide ownership (read the aurora `warn_once` log at a
   torch; A/B raw D3D9 against Remix), is in
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
     albedo. Worth checking whether grass ends up in Remix's alpha-blend
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

   **Fixed 2026-08-03**, by making the hint conditional: it is suppressed when
   Remix would already reconstruct the albedo we meant, tint included
   (`remix_decodes_albedo()`, `dx9_tev.cpp`). Deliberately restricted to
   single-stage materials — on a multi-stage material a later stage may change
   the colour, and Remix reading only one would be confidently wrong.
   **Untested in game.**

   **The regression to watch for is surfaces going dark, not staying grey.** A
   material whose GX first stage is `texture × dark konst` and whose alpha is
   plainly the texture's alpha will now be read by Remix directly rather than
   through the hint, and if later GX stages brightened it, Remix does not see
   them. The material report names every material where the hint was suppressed,
   so this is diagnosable from the log rather than by hunting.

   Full account of the interface and the rules that follow from it:
   `extern/aurora/docs/dx9/remix-material-interface.md`.

   The general rule that survives from the old entry: **when a material tint can
   go to either constant slot, prefer TFACTOR** — only one of the two survives
   into Remix.

9. **Nothing can make an opaque captured draw emissive.** Raised 2026-08-03:
   Goron Mines lava is unlit as well as grey. Investigated on both sides; the
   route does not merely produce the wrong answer, **it does not exist at any
   point in the chain.**

   **Game side — the signal is there and is discarded.** GX carries a per-draw
   colour-channel lighting bit that comes straight out of the model file: the
   artist marking a surface as self-lit. Aurora decodes the whole colour-channel
   state and the D3D9 backend reads exactly one field of it, not that one. A
   self-lit lava material and a lit rock therefore emit byte-identical D3D9
   state.

   **Remix side — three routes, none usable.** `rtx.legacyMaterial.enableEmissive`
   is global (it would make everything emit). The emissive-blend override
   requires alpha blending, which opaque lava structurally is not.
   `rtx.lightConverter` is keyed on texture hash — exactly the per-texture
   tagging this project exists to avoid — and reads a D3D9 material aurora never
   sets. There is no `Emissive` instance category in the enum at all.

   **The one working precedent.** Instances categorised `WorldUI` get their
   material patched to emit their own albedo texture at a fixed intensity
   (`rtx_instance_manager.cpp`). That is exactly the mechanism wanted, hardwired
   to one category and one hardcoded number.

   **Design direction, not built:** carry the GX lighting bit per draw to the
   fork over the same channel the albedo tint should use, and add an `Emissive`
   instance category patched like `WorldUI` with a `rtx.dusklight.*` intensity so
   it is dialable from the F1 overlay rather than by rebuilding. Detail:
   `extern/aurora/docs/dx9/remix-material-interface.md` §9.

   **Two couplings that must be respected.**

   - **The colour fix has to land first.** The emissive colour goes through the
     same fixed-function stage ops as the albedo, so pointing emission at a
     bleached albedo makes the lava glow *white*.
   - **"Unlit" is not the same as "emissive".** HUD, 2D, particles, rain and
     fades are all unlit too, so the bit must be conjoined with perspective +
     opaque + depth-writing. And the game forces interior ambient to black in
     several cases, which gave its artists every reason to author ordinary
     interior geometry as unlit as well — on real hardware, lit-with-black-
     ambient and unlit look identical. **Count how often the rule would fire
     before building on it**; the material report's `lit=` field makes that a
     log question rather than a guess.

   Good news, verified: emissive geometry does *illuminate*, not merely glow.
   The NEE cache performs next-event estimation on emissive triangles and is on
   by default, so no fallback light farm is needed once materials emit.

10. **`rtx.vertexColorIsBakedLighting` is degrading every surface in the game.**
    Found 2026-08-03 while root-causing the greyscale defect; **not** its cause,
    but a real and separate loss.

    The option defaults **on**, and what it does is normalise vertex colour —
    dividing each component by the largest, then mixing toward that result. The
    effect is to remove all vertex-colour *brightness* and roughly 40% of its
    *saturation*, on every draw. It is a global transform, not per-material,
    which is precisely why it could be ruled out as the cause of a defect
    affecting only three objects.

    Its intent is sound for a game that bakes lighting into vertex colours. This
    one does not: aurora never evaluates the GX light model, so a vertex colour
    here is authored material colour, not baked light.

    **Free to test, no build required:** add `rtx.vertexColorIsBakedLighting =
    False` to `rtx.conf` and compare. Expect vertex-coloured surfaces to gain
    saturation and contrast.

    The principled fix is per-draw rather than global: the flag is already a
    per-draw field that is merely copied from the global option, and the GX
    colour-channel lighting bit says exactly which draws have genuinely baked
    lighting. That is the same signal issue 9 needs, so the two should be built
    together.

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
over a lap) and baked lighting (`D3DRS_LIGHTING = FALSE`; aurora never
evaluates the GX light model, so vanilla's Link-following light reaches
neither the vertex colours nor the albedo). The day case is now believed
correct; what remains is night-only and is open issue 2.

**First-run checklist for the sun/moon light:** Remix's Dusklight tab should
report the device registered and `Drawing: SUN`. Walk past a lantern — the sun direction
must not move (that is the whole point of deriving it from the orbit
rather than the game's shadow-light selection). Watch a dawn (daytime
~67.5–75) for the moon→sun crossfade. If shadows fall from the wrong side,
tick Flip Direction; if that fixes it, the sign belongs in the code.

