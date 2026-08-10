# What the naming finding changes about the work we have already done

An audit of every Remix and DX9 feature on `Fixed-Function-dev`, asking one narrow
question: **where did reading a Japanese name as if it were English lead us to build
the wrong thing, document the wrong thing, or miss something the game was already
telling us?**

Companion to [`japanese-naming.md`](japanese-naming.md), which is the reference. This
file is the result of applying it. The work items it produces live in
[`japanese-naming-worklist.md`](japanese-naming-worklist.md) as ready-to-run prompts.

**Status: investigation only. No behaviour was changed to produce this document.**

---

## 0. The short version

The game's source is written in Japanese — not the comments, the *names*. Every design
decision about this game's look has been made by reading those names as English. Mostly
that is harmless. Sometimes it produces a confident, specific, wrong answer, and because
the answer looks reasonable it gets written into a document and the next session builds
on it.

What the audit found, in proportion:

- **A handful of real errors**, of which two were already costing us: one sends the
  owner to the wrong place to run a measurement we still need, and one is a rendering
  premise the game contradicts.
- **A larger amount of game data we never picked up**, because its name meant nothing.
  This is the bigger category and the one that matters for where the project is going.
- **One strategic thing** that changes what this project can offer: the game ships the
  original artists' own tuning panel, in Japanese, and it is a specification for what
  our overlay should expose.
- **A lot of "this is fine"**, recorded deliberately in §6 so nobody pays to
  re-investigate it.

The single most important line in this entire audit is not a finding. It is:

```sh
export LC_ALL=C.UTF-8
```

Without it, `grep -P` on Japanese silently matches nothing — no error, no warning, zero
results. 496 files under `src/` and `include/` contain literal Japanese, including the
original developers' own labels for their own fields. That one missing environment
variable is the most likely reason nobody on this project ever found them: every search
came back empty and read as "not there".

---

## 1. How to read the severity tags

| Tag | Meaning |
| :-- | :-- |
| **RECORD WRONG** | A document says something untrue. No code depends on it. Cheap to fix, and worth it, because the next decision gets made from that document. |
| **BEHAVIOUR WRONG** | Code implements a premise the game contradicts. |
| **DATA ON THE FLOOR** | The game computes something useful and we never asked for it. Nothing is broken; something is missing. |
| **NOT A PROBLEM** | Checked, correct, do not spend a session on it. |

Most of what follows is the first and third. **That is a good shape**: it means the
working system is largely working, and the wins are in what we have not yet picked up
rather than in what we have to tear out.

---

## 2. The five ways a name misleads

The audit was set up to look for four failure modes. It found a fifth.

1. **Misread name** — a Japanese word read as English, producing wrong semantics.
   *(kasumi "inner/outer" read as far/near when the game means near/far.)*
2. **Single-romanization gap** — a search or word list covering one spelling when the
   tree uses both. *(The effect-name table spells droplet `shizuku` 29 times and
   `sizuku` 26 times.)*
3. **Unused game data** — state the game maintains that we never extract.
4. **Opaque name, never investigated** — nobody looked, because the name meant nothing.
5. **Decomp-assigned English that contradicts the original label** — *new.* Not every
   misleading name is Nintendo's. The decompilation *invents* English names for fields
   whose originals were not recovered, and those inventions are a later reader's guess.

Class 5 has a clean rule attached, and it is worth internalising:

> **An `m`-prefixed field name in game code is a hypothesis. A `genSlider` label is
> evidence. When they disagree, the label wins.**

Verified instance:

```
src/d/d_kankyo.cpp:5058
  mctx->genSlider("雲影の濃さ ", &g_env_light.mFogDensity, 0, 0xff);
                     ^ "cloud shadow density"       ^ the decomp called it mFogDensity
```

`g_env_light.mFogDensity` is not fog density. Nothing of ours reads it, so there is no
live defect — but it is a trap sitting directly in the path of the cloud work.

Counts for the environment state struct (`include/d/d_kankyo.h`, parsed mechanically):

| | |
| :-- | --: |
| fields in `dScnKy_env_light_c` | 621 |
| original-style `snake_case` — the game's own symbols, trustworthy | 324 |
| decomp-assigned `mCamelCase` — a later reader's guess | 40 |
| still literally unnamed `field_0x…` | 257 |

**41% of the game's environment state has no name at all.** Nobody has ever established
what it does.

---

## 3. The strategic finding: the original artists' tuning panel

The game ships the debug panels its own developers used, with their own labels.
Mechanically counted across `src/` and `include/`:

| | |
| :-- | --: |
| `genSlider` | 3,903 |
| `genLabel` | 1,249 |
| `genCheckBox` | 421 |
| `genButton` | 312 |
| files carrying a panel | 149 |

`d_kankyo.cpp` alone — the environment system — has **291 sliders**. Each one is a
machine-readable binding:

```
genSlider("<Japanese label>", &<live game field>, <min>, <max>)
```

a label written by the people who authored the game's look, the exact variable, and the
range they considered sane. Pulled straight out of `d_kankyo.cpp`:

| Label | Means | Field | Range |
| :-- | :-- | :-- | :-- |
| `影響率(0%-200%)` | influence rate | `mActorLightEffect` | 0–200 |
| `地形ライト影響率` | terrain light influence rate | `bg_light_influence` | 0.0–2.0 |
| `雲影の濃さ` | **cloud shadow density** | `mFogDensity` | 0–255 |
| `水面α` | **water surface alpha** | `bg_amb_col[1].a` | 0–255 |
| `ウソFog` | **"fake fog"** | `bg_amb_col[3].a` | 0–255 |
| `補佐α` | auxiliary alpha | `bg_amb_col[2].a` | 0–255 |
| `● 前かすみ` | **front (near) haze** | `vrbox_kasumi_outer_col` + α | 0–255 |
| `● 奥かすみ` | **back (far) haze** | `vrbox_kasumi_inner_col` + α | 0–255 |
| `● 上雲カラー` | upper cloud colour | `vrbox_kumo_top_col` | 0–255 |
| `● 下雲カラー` | lower cloud colour | `vrbox_kumo_bottom_col` | 0–255 |

Three of those BG ambient *alphas* read as meaningless noise in English. In Japanese
they are a water-surface term and a "fake fog" term. None of the three is exposed by us.

**Why this matters more than any single defect.** The stated end state for this project
is that remastering becomes *tweaking values we have exposed, plus new art assets*. The
question "which values are worth exposing?" already has an authoritative answer shipped
inside the game: the panel its own artists used to answer it. It has been invisible
because it is written in a language nobody here reads, behind a locale flag nobody set.

**Honest limits, so this is not oversold.** Most of the 3,903 are gameplay, not
rendering (`d_meter_HIO.cpp` alone is 1,323). These are debug panels, and whether the
original host tool can be driven in the PC port is a separate and unanswered question —
irrelevant to the value here, which is that the bindings are readable *statically*, and
that is all a specification needs to be. The deliverable is a curated shortlist, **not**
a wholesale port of a debug menu into the Remix overlay.

---

## 4. Findings

Ordered by what they cost us today, not by how interesting they are. The work item in
brackets is the prompt in
[`japanese-naming-worklist.md`](japanese-naming-worklist.md).

### 4.1 The test plan has been sending us to the wrong place — **RECORD WRONG** [P1]

The dense-fog worked example — the one regime the atmosphere calibration still needs and
has never visited — is attributed to **Lake Hylia** in five places across two repos,
including the instruction the owner follows during a play session
(`kankyo-fog.md:226`).

The game says otherwise, twice, and one of them is in English:

```
src/d/actor/d_a_kytag01.cpp:1-4    /** d_a_kytag01.cpp   Sacred Grove Mist Tag */
src/d/actor/d_a_kytag01.cpp:202    OS_REPORT("\n迷いの森　霧タグの…")
                                     迷いの森 = the Lost Woods,  霧タグ = fog tag
```

It is the **Lost Woods / Sacred Grove**. And the blend runs the opposite way from the
documents' mental model: the distance term is `0` inside the inner range and `1` beyond
the outer one, and it is the *weight* of the whiteout override — so the tag marks a
**clear centre** and the fog is strongest **away** from it. Looking at the tag makes the
fog weaker, not stronger.

`DusklightAtmosphere.md` ledger row C0 records that `zHalfMin` and `froxelRangeScale`
"remain unchallenged rather than confirmed" because that regime was never visited. This
is why.

Note this one did not even need the Japanese. The file's own English header says Sacred
Grove. Nobody read it.

### 4.2 A correction landed in the docs and not in the code — **BEHAVIOUR WRONG** [P2]

On 2026-08-10 the fork's `kasumiInner`/`kasumiOuter` descriptions were corrected: the
game splits its two horizon haze bands **front/back**, not by sun position, and
confusingly `outer` is the **near** band. That correction reached the option descriptions
and the `.md` files. It did not reach the shader that consumes both values:

```
src/dxvk/shaders/rtx/pass/dusklight/dusklight_sky.comp.slang:179-186
  // The game keeps two horizon colours, one for the sun's side and one for away
  // from it, and this is what chooses between them.
  horizonColor = lerp(cb.kasumiOuter, cb.kasumiInner, sunProximity)
```

So the generated sky's horizon rotates with the sun's compass bearing even when the
palette is static, and places the **far** band at the sun. Because that image is also the
dome light and the fog target colour, the sky light and the fog tint inherit the rotation.
The shader's struct comments (`dusklight_atmosphere.h:53,60`) still assert the wrong
premise as fact.

A fourth game-authored confirmation, stronger than the three already cited and not yet in
`japanese-naming.md`: the original team's own slider panel.

```
d_kankyo.cpp:6369  genLabel("● 前かすみ")   over the kasumi_OUTER sliders  (前 = front/near)
d_kankyo.cpp:6392  genLabel("● 奥かすみ")   over the kasumi_INNER sliders  (奥 = back/far)
```

**This one is partly my own error from the previous session.** I corrected the
descriptions, reported it as "description strings only, no behavioural change", and did
not check whether a consumer had implemented the wrong premise. One had.

### 4.3 Three shipped statements about bloom invert the code — **RECORD WRONG** [P3]

- `rtx.bloom.dusklightThreshold`'s description is the inverse of its shader — and
  `dx9-fixed-function.md` calls that option *"the one knob that needs tuning per setup"*.
  The key is blue-weighted and display-referred, so pure red at 1.0 has a key of 0.25 and
  never clears the default of 0.5.
- `kankyo-remix.md:214-215` says the mono pass and the base-image weight are **unported**.
  Both shipped — and the same document says so 180 lines later. A session reading §I.5 as
  the statement of what exists can spend a window reimplementing a wired feature.
- Wolf senses exercises **neither** the mono overlay nor the base weight — both sit at
  neutral defaults while senses is active. The fork's option text and a test note both
  assume otherwise, so a planned test would show nothing.

The port itself is in better shape than its documentation: the mono pass, the base weight
and the blue-weighted threshold are all implemented correctly.

### 4.4 The material identity hook we recorded as unbuilt is already in the tree — **DATA ON THE FLOOR** [P8]

Three documents say `grp=` cannot work and that a correct implementation "labels at
draw-buffer execution… Nobody has built it." It is built:

```
libs/JSystem/src/J3DGraphBase/J3DPacket.cpp:214-227
  void J3DMatPacket::draw() {
      mpMaterial->load();
  #if DEBUG && TARGET_PC
      snprintf(buf, sizeof(buf), "Mat: %s", mpMaterial->mMaterialName);
      GXPushDebugGroup(buf);
  #endif
      callDL();                        <-- the draw is ISSUED here
```

It sits exactly where the documents say it would have to, it pushes **the material's own
authored name**, and it is compiled out of every build the owner tests with, because CI
artifacts are not `DEBUG`.

Why that name is worth having — verified, not speculation. The game's own environment
system dispatches on material names every frame (`dKy_bg_MAxx_proc`,
`d_kankyo.cpp:11399`, matching `MA00`…`MA17` at `:11508`), and those names carry
semantics: `MA00_Gake` (崖 cliff), `MA00_Kusa` (草 grass), `MA00_Enkei_Tree_Color`
(遠景 distant scenery). Remix currently cannot tell a cliff from grass from water — all
are opaque legacy materials with a TEV-derived albedo — and the only alternative route is
hash-tagging textures, which is what rule 1 exists to avoid and which cannot distinguish
two uses of one texture.

**This is the single most promising item in the audit for the project's stated goal.**

### 4.5 The bridge drops the sky's alpha channels — **DATA ON THE FLOOR** [P6]

`formatColorS10` (`remix_bridge.cpp:242-251`) formats only R, G, B. Three of the sky
palette colours carry an authored alpha that the game blends every frame exactly like the
RGBs, and that the original team exposed as sliders. Meanwhile the sky shader *guesses*
the number those alphas state:

```
dusklight_sky.comp.slang:79-80
  hazeLevel = dot(kasumiInner + kasumiOuter, luma) * 0.5f
  // "a good proxy for how thick the air reads in a given palette"
```

Two palettes wanting the same hue at different densities currently arrive identical.

### 4.6 The colour-pattern blend arrives one third complete — **DATA ON THE FLOOR** [P7]

Two documents describe the "gather" fields as a *second, independent* palette blend. The
game copies them into the primary fields and clears them (`d_kankyo.cpp:4788-4828`) —
they are the **input** to the one blend. That sentence hides a real gap: the bridge pushes
`wether_pat1` alone and neither `wether_pat0` nor `pat_ratio`, so the fork cuts hard on a
single index while every colour it blends against moves continuously.

### 4.7 Two planned features are aimed at mechanisms that do not exist — **RECORD WRONG** [P5]

- `fog_avoid_tag` appears never to touch fog. A permanent compromise row (C6) and a
  planned heterogeneous-fog feature are both aimed at it.
- `kPalaceOfTwilightColpat = 9` is sourced from `dKy_sense_pat_get` — the wolf-*sense*
  vision pattern, a different index space — and the fork bypasses its entire physical sky
  on that literal. Whether any stage actually runs colpat 9 lives in `.dzs` stage data and
  is **unknown** from this checkout.

Both need confirming before anything is deleted; the prompt says so.

### 4.8 Four author-made twilight grades ship in the data and cannot be reached — **DATA ON THE FLOOR** [P10]

Bloom presets 32–35 are marked "vacant" by the panel that describes the table and
**named as four twilight/senses looks** by a different panel — complete with mono amounts
and, for 33, the same base dimming twilight uses. They read as empty slots only because
the label naming them is in Japanese, in a different menu.

### 4.9 The effect-lights word lists rot silently — **RECORD WRONG, mostly harmless** [P4]

Ten of thirty classifier keywords match zero of the 3,205 effect names; the source
comment and the document say three. **The live words cover everything the dead ones
would have**, so nothing is visibly wrong — the defect is that the exact failure which
made `Class::Lava` unreachable dead code survives unnoticed in three more lists, with no
mechanism to catch it.

One genuine trap sits in the document that guides the one decision a name can make:
`docs/effect-lights.md` §4 sizes candidates for widening the `Excluded` list in a single
romanization. Droplet is spelled **both** ways in that same table — `shizuku` 29 names,
`sizuku` 26, neither a substring of the other. Following that guidance literally would
exclude 29 droplet effects and leave 26 still lighting the room.

---

## 5. What NOT to do

The risk in an audit like this is that it generates work. Several findings are true and
**not worth acting on**, and saying so is part of the result.

### Do not rewrite the effect-lights classifier

Ten of its thirty keywords match zero effect names, and the temptation is to fix the
word lists. Don't. Whether a light exists is decided by blend mode and colour, **not by
name**. The name only sets a vertical offset and a merge tie-break, and at stock
settings the Glow offset is `0.0` — identical to Other. **Most classification questions
in that system cannot currently move a pixel.** Add a mechanical guard so the lists stop
rotting silently, correct the document, stop.

One exception deserves respect: `Excluded` is the only class that can refuse a light
outright. That list is currently clean. If it is ever widened, both romanizations go in
the same commit — otherwise you exclude 29 droplet effects and leave 26 lighting the room.

### Do not chase `burn`, `hibana`, `nessen`, `kagerou`

Real fire/spark names classified as Other. The payoff is a 15-unit vertical offset, and
a bare `burn` would also catch a smoke effect and a scorch decal. Log them; decide from
the classification report of a real session. Measure-first, not a work item.

### Do not expose 3,903 sliders

See §3. The panel is a specification, not a shopping list.

### Do not touch local lights

Being retired. Any finding that would deepen it is out of scope by definition. Note that
the effect-lights branch already handles the transition correctly — it keeps
`localLights` present and **off**, as the only way to A/B the new placement, and warns in
the overlay when both are on. Deleting it is a separate, later cleanup.

### Do not "fix" the dead keywords by adding romanizations

`taimatsu`/`taimatu`, `kagaribi`/`kagari`, `honoo`/`honou`/`homura` match zero names in
**both** spellings. The game used English (`fire`, `torch`) or different Japanese words
entirely (`maki` 薪, `kantera` カンテラ, `kaen` 火炎). This is the one place the
romanization lens returns a *negative*, and it belongs in the code comment so a future
session does not helpfully add spellings that match nothing.

### Do not rename anything in the game tree

Restated because this audit will tempt someone. `mFogDensity` is misleadingly named and
it **stays**. Fix the documentation, add a comment, leave the identifier alone.

---

## 6. Checked and correct — do not re-litigate

This section exists so the same ground is not paid for twice. Everything here was
examined and found sound.

**The self-illumination rule contains no Japanese and the lens changes nothing about it.**
`evaluate_self_lit` reads only GX/TEV enums from Nintendo's SDK, and the fork's
`accepts()` reads only floats out of `D3DMATERIAL9`. There is no game symbol in that
decision path to misread. Nor does the game *have* an emissive concept to translate —
searching in Japanese (発光 / 自発光 / 輝き / 光量) returns nothing.

**The `D3DMATERIAL9` side-channel map is consistent between the two repos**, including
the one polarity that could have silently inverted: aurora writes
`Specular.r = vertexColorIsMaterial` and the fork inverts it correctly into
`isVertexColorBakedLighting`.

**The mono-pass maths is correct in both directions**, and the CLEAR/SOFT → screen-blend
selection matches the authors' own labels (くっきり *kukkiri* "crisp", やわらか
*yawaraka* "soft").

**The twilight numbers quoted throughout the fork are right** — mono alpha `0x60` ≈ 0.376
and `mOrigDensity` `0xD2` ≈ 0.824 both check out against `d_kankyo_data.cpp`.

**`S_fuwan_sin` is described accurately** and it does reach Remix. (One correction it
produced: `kankyo-remix.md`'s claim of a "~0–3 strings" steady-state push count is not
true in the dark world, where the wobble pushes every frame.)

**`darkworld` is understood correctly**, is not just the Palace of Twilight, and does not
need pushing for the atmosphere's sake — it forces `daytime = 0` so
`resolvePhysicalWeight` already returns 0 before the colpat-9 branch is reachable.

**Reading kankyo's *outputs* rather than re-deriving them is correct**, and it does
inherit every tag: `setLight` folds the fog-range override into the same call, and the
bridge ticks after `fapGm_Execute()`, so the values are settled for the frame.

**`colpat` = `wether_pat1` is the right field**; `wether_pat0` is the missing *prev* half,
not a replacement. **Underwater forcing pselect 8/9 is exactly right**, as is "10 =
special". **All 18 kytags are accounted for** — fifteen are already fully served, and only
kytag16 (`Pikari`, a spot light) carries data the bridge receives in no form at all.

**The effect-lights `Excluded` list is clean** — all 48 matched names are genuinely
drool or body fluid. **`Class::Lava` is genuinely reachable** via `yogan`/`yougan`, and
every candle/torch/lantern actor's effects classify correctly.

**The dead classifier keywords are not romanization misses.** `taimatsu`/`taimatu`,
`kagaribi`/`kagari`, `honoo`/`honou`/`homura` match zero in **both** spellings — the game
used English (`fire`, `torch`) or different Japanese entirely (`maki` 薪, `kantera`
カンテラ, `kaen` 火炎). This is the one place the lens returns a negative, and it belongs
in the code comment so nobody "helpfully" adds spellings that match nothing.

---

## 7. What this audit did NOT cover

Stated plainly, because the instruction was not to assume completeness.

Twelve feature areas were queued. **Five completed**: the sky/vrbox/atmosphere, fog &
colpat & the kytags, effect-lights, material translation, and bloom/mono/twilight. Those
five are the highest-stakes systems and the ones the earlier confirmed defects pointed at.

**Seven were still running when this was written** and are not represented above:

| Area | Why it still matters |
| :-- | :-- |
| Particles & weather | Named explicitly as critical game data; `housi`/`sibuki`/`vrkumo`/`odour`/`mud`/`evil` are romaji and only some are batched |
| Texture replacement, tagging & hashing | The name-vs-hash question, which bears directly on art assets being keyed to meaningful names |
| The overlay, option wire & protocol | Coverage of our 54 pushed fields against the original team's 291-slider panel |
| Time, the light schedule & celestial | The `あき/なつ/ふゆ` (autumn/summer/winter) palette entries are an unexplained lead |
| Shadows, grass & geometry identity | `kage`; whether the game knows more about which shadows are which |
| The other six in-flight branches | Water, hair, transparency, thin g-buffer — romanji risk only |
| The open "unused game data" sweep | 257 of 621 environment fields are still literally unnamed |

Also **not** represented: the adversarial verification pass. Every finding above was
produced by one agent reading the source; a second agent was queued to attack each one
and had not returned. The findings carry file:line citations precisely so each prompt can
re-verify before acting — which is why every prompt in the worklist opens by asking the
session to reproduce the claim and stop if it cannot.

**The four findings I verified personally, line by line, rather than taking on an agent's
word**, are §4.1 (kytag01), §4.2 (the shader), §4.4 (the `J3DMatPacket` hook and the
`MAxx` dispatch), and all of §3 (the tuning panel counts and bindings).

---

## 7. Priority

The ordering rule is **certainty first, then cost, then value**. Documentation
corrections lead not because they matter most, but because they cost nothing, cannot
regress anything, and they remove the wrong information that produced the defects. A
wrong document is what turns one session's guess into three sessions of work.

Full prompts: [`japanese-naming-worklist.md`](japanese-naming-worklist.md).
