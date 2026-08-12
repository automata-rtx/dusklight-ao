# The original team's kankyo tuning panel

The game ships the debug panels its own artists used to author the environment,
with their own Japanese labels still attached to the exact variables they moved.
Each binding is machine readable:

```
genSlider("<Japanese label>", &<live game field>, <min>, <max>)
```

— a label written by the people who authored the game's look, the field, and the
range they considered sane. This document is the **rendering-relevant** part of
that panel, extracted mechanically and then curated.

Read [`japanese-naming.md`](japanese-naming.md) first, especially §6 (why these
labels are the best translation source in the tree) and §8b (why a struct member
name is a hypothesis and a slider label is not).

> **`export LC_ALL=C.UTF-8` before grepping any of this.** The container default
> is `POSIX`, under which `grep -P` on a kana/kanji class matches nothing, with
> no error and exit 1.

---

## 0. How this was made, and the counts

Extracted with a throwaway parser (balanced-paren argument splitting, so the
many multi-line calls survive; kept out of the repo). Every row below is
generated from that extraction and cited to `file:line`. Nothing here was
transcribed by hand.

**Counts re-derived 2026-08-11, across `src/` and `include/`:**

| | count | note |
| :-- | --: | :-- |
| `genSlider(` calls | **3903** | in **149** files |
| `genSliderID(` calls | 9 | a different overload; takes an id, not a pointer |
| `genCheckBox(` calls | 421 | |
| `genLabel(` calls | 1249 | |
| files carrying any of the three | 171 | |

**Two published numbers disagreed and both are right.** The worklist says
"3,903 across 149 files"; the P9 prompt says "3912 across 171 files". A plain
`grep -c genSlider` returns **3912** because `genSliderID` contains `genSlider`
as a substring — 3903 + 9. And 171 is the file count for *any* of the three
calls, while 149 is the file count for `genSlider` alone. Both reconcile
exactly. **The numbers used here are 3903 / 421 / 1249, 149 slider files.**

Largest panels: `d_meter_HIO.cpp` 1323 sliders, `d_a_alink_HIO.inc` 982,
**`d_kankyo.cpp` 291**, `d_a_horse.cpp` 80, `d_cam_param.cpp` 79. The first two
are gameplay and are out of scope.

### Refuted: three of the four files the prompt names have no panel at all

The instruction was to start with `d_kankyo.cpp`, `d_kankyo_wether.cpp`,
`d_kankyo_rain.cpp` and `m_Do_graphic.cpp`. Measured:

| file | genSlider | genCheckBox | genLabel |
| :-- | --: | --: | --: |
| `src/d/d_kankyo.cpp` | 291 | 41 | 353 |
| `src/d/d_kyeff.cpp` | 8 | 0 | 0 |
| `src/d/d_kankyo_wether.cpp` | **0** | 0 | 0 |
| `src/d/d_kankyo_rain.cpp` | **0** | 0 | 0 |
| `src/d/d_kankyo_data.cpp` | **0** | 0 | 0 |
| `src/d/d_kankyo_debug.cpp` | **0** | 0 | 0 |
| `src/m_Do/m_Do_graphic.cpp` | **0** | 0 | 0 |

**The entire environment tuning surface is one file plus eight sliders.**
`d_kankyo.cpp` hosts every `genMessage` for the whole kankyo system, including
the weather sections — the cloud, thunder, lightning, haze and fog-wall panels
all live in `dKankyo_navyHIO_c::genMessage` there, and `d_kankyo_wether.cpp`
does not reference `g_kankyoHIO` even once. `d_kyeff.cpp:46-56` adds eight
sliders of its own (`dKyeff_HIO_c`). Weather **data** lives in the other files;
weather **controls** do not.

So the corpus for this document is **340 slider + checkbox bindings**
(332 in `d_kankyo.cpp`, 8 in `d_kyeff.cpp`), across 295 distinct expressions.

---

## 1. The limit, established rather than assumed

**The panel is not compiled into this port, in any configuration.**

- Every `genMessage` in `d_kankyo.cpp` sits inside a single `#if DEBUG` block
  opened at `d_kankyo.cpp:4969` and closed at `:8190`.
- `DEBUG` defaults to `0` (`include/global.h:29-32`, "define DEBUG if it isn't
  already so it can be used in conditions").
- `d_kankyo.cpp` is in `DOLZEL_FILES` → `GAME_BASE_FILES`, which
  `CMakeLists.txt:428-432` compiles with `NDEBUG=1;DEBUG_DEFINED=0` and never
  with `DEBUG=1`. Only `GAME_DEBUG_FILES` (`CMakeLists.txt:403-418`, a list of
  seven audio/imgui files) gets `DEBUG=1`, and only in a Debug build.

**So nothing here can be driven at runtime, and no claim is made that it can.**
The value is that the bindings are readable *statically*, which is all a
specification needs to be: a label the original authors wrote, beside the exact
field, beside the range they worked in.

The same `#if DEBUG` gating reaches into the consumers. Spot-checked:
`g_kankyoHIO.navy.cloud_sunny_size` is read at `d_kankyo_rain.cpp:4996` inside
`#if DEBUG`, with the `#else` branch hard-coding `0.6f`; the shadow-density
override at `d_kankyo.cpp:2713` is likewise `#if DEBUG`. **This was not audited
across all 278 panel-object bindings** — two were checked, both were DEBUG-only,
and that is stated as two samples, not a survey.

### Where the live state actually is

Of the 340 bindings:

| binds | count | what it is |
| :-- | --: | :-- |
| `g_env_light.*` | **62** | live retail environment state |
| the panel object itself (`this->…`) | 259 | debug scratch |
| `g_kankyoHIO.*` | 19 | debug scratch |

All 41 checkboxes bind panel objects; **not one touches `g_env_light`.**

**62 bindings is the whole rendering-relevant subset**, and **32** of those 62
are already carried to Remix (29 when this was written; the three BG-ambient
alphas of §4 landed 2026-08-12). That is the honest headline of this exercise:
the surface is small, and it is mostly already covered.

---

## 2. The live surface: every `g_env_light` binding

All in `src/d/d_kankyo.cpp`. "To Remix" is against `src/dusk/remix_bridge.cpp`
on this branch (protocol 13; the column was first filled in at protocol 11).

### 2.1 Palette-blended per frame — the mood engine

Rewritten every frame by `setLight()`'s four-way palette blend, so these are
outputs of the environment system, not settings.

| Line | Label | Reading / meaning | Field | Range | To Remix |
| --: | :-- | :-- | :-- | :-- | :-- |
| 5132-5134 | `■ ACTOR_Amb R/G/B` | actor ambient | `actor_amb_col.rgb` | 0–255 | **yes** — `env.actorAmbient` |
| 5144-5146 | `■ BG0_Amb R/G/B` (under `(地形)`, *chikei*, terrain) | terrain ambient | `bg_amb_col[0].rgb` | 0–255 | **yes** — `env.bgAmbient` |
| 5155-5157 | `■ BG1_Amb R/G/B` | ambient layer 1 | `bg_amb_col[1].rgb` | 0–255 | no — §2.1a |
| 5164 | `水面α A` | *suimen* — **water-surface alpha** | `bg_amb_col[1].a` | 0–255 | **yes** — `env.bgWaterAlpha` |
| 5165 | `補佐α A2` | *hosa* — **auxiliary alpha** | `bg_amb_col[2].a` | 0–255 | **yes** — `env.bgAuxAlpha` |
| 5168-5170 | `■ BG2_Amb R/G/B` | ambient layer 2 | `bg_amb_col[2].rgb` | 0–255 | no — §2.1a |
| 5179-5181 | `■ BG3_Amb R/G/B` | ambient layer 3 | `bg_amb_col[3].rgb` | 0–255 | no — §2.1a |
| 5188 | `ウソFog A` | *uso* — **"fake Fog"** | `bg_amb_col[3].a` | 0–255 | **yes** — `env.bgFakeFogAlpha` |
| 5204-5206 | `■ FOG R/G/B` | fog colour | `fog_col.rgb` | 0–255 | **yes** — `env.fogColor` |
| 5215 | `near` | | `mFogNear` | ±2500000.0 | **yes** — `env.fogStartZ` |
| 5216 | `far` | | `mFogFar` | ±2500000.0 | **yes** — `env.fogEndZ` |
| 5056 | `地形ライト影響率` | *chikei raito eikyouritsu* — terrain light influence rate | `bg_light_influence` | 0.0–2.0 | no |
| 5058 | `雲影の濃さ` | *kumokage no kosa* — **cloud shadow density** | `mFogDensity` | 0–255 | no — see §3.1 |
| 6284-6286 | `色 R/G/B` under `● 空の色` | sky colour | `vrbox_sky_col.rgb` | 0–255 | **yes** — `env.skyColor` |
| 6306-6308 | `色 R/G/B` under `● 上雲カラー` | *ue-kumo* — upper cloud | `vrbox_kumo_top_col.rgb` | 0–255 | **yes** — `env.kumoTop` |
| 6328-6330 | `色 R/G/B` under `● 下雲カラー` | *shita-kumo* — lower cloud | `vrbox_kumo_bottom_col.rgb` | 0–255 | **yes** — `env.kumoBottom` |
| 6350-6352 | `色 R/G/B` under `● 下雲影カラー` | lower cloud **shadow** | `vrbox_kumo_shadow_col.rgb` | 0–255 | **yes** — `env.kumoShadow` |
| 6353 | `α` under `● 下雲影カラー` | the cloud **layer's** alpha | `vrbox_kumo_top_col.a` | 0–255 | see §4 (unmerged branch) |
| 6373-6375 | `色 R/G/B` under `● 前かすみ` | *mae-kasumi* — **near** haze | `vrbox_kasumi_outer_col.rgb` | 0–255 | **yes** — `env.kasumiOuter` |
| 6376 | `α` under `● 前かすみ` | | `vrbox_kasumi_outer_col.a` | 0–255 | see §4 |
| 6396-6398 | `色 R/G/B` under `● 奥かすみ` | *oku-kasumi* — **far** haze | `vrbox_kasumi_inner_col.rgb` | 0–255 | **yes** — `env.kasumiInner` |
| 6399 | `α` under `● 奥かすみ` | | `vrbox_kasumi_inner_col.a` | 0–255 | see §4 |

`前/奥` (near/far) mapping onto `outer`/`inner` is the §6 worked example in
[`japanese-naming.md`](japanese-naming.md) — the English words run the opposite
way from the Japanese, and this panel is one of the three sources that settle it.

### 2.1a Where the four BG ambient layers actually go

Traced 2026-08-12, because §4 flag 2 could not be decided without it. **The four
layers are routed two entirely different ways, and only one of the two is
ambient light at all.** That split is what decides which of them are worth
sending to Remix.

**Path A — the RGB triples are ambient light, routed per room model file.**
`setLight_bg` (`d_kankyo.cpp:2886`) blends all four layers into a local
`BG_col[4]` (`:2920-2925`), and `settingTevStruct`'s background branch picks one
of them with `sp54 = tevstrType & 3; field_0x10f0 = BG_col[sp54];`
(`:4199-4200`), which then becomes `tevstr_p->AmbCol` (`:4278`). The types come
from a fixed table in the room actor — `d_a_bg.cpp:336`,
`static int l_tevStrType[6] = {32, 33, 34, 35, 35, 32}`, indexed by the same `i`
that walks the six room model files `model.bmd` … `model5.bmd` (`:122`):

| Room part | Model file | tevstr type | `& 3` → layer |
| --: | :-- | --: | --: |
| 0 | `model.bmd` | 32 (`0x20`) | **0** |
| 1 | `model1.bmd` | 33 (`0x21`) | **1** |
| 2 | `model2.bmd` | 34 (`0x22`) | **2** |
| 3 | `model3.bmd` | 35 (`0x23`) | **3** |
| 4 | `model4.bmd` | 35 (`0x23`) | **3** |
| 5 | `model5.bmd` | 32 (`0x20`) | **0** |

So the layers are genuinely distinct — but they are **ambient light**, which
path tracing replaces outright, and a scene-global readout could not be applied
per room model file by any full-screen consumer regardless. **That is why
BG1/BG2/BG3 RGB are not on the wire, and the acceptable outcome §3 asked for.**

Layer 0 is additionally what the game reuses whenever it wants "the" background
ambient with no geometry in hand — particles (`d_particle.cpp:294`), grass and
flowers (`d_grass.inc:571`, `d_flower.inc:655`), the mirror
(`d_a_mirror.cpp:228`) — which is why one layer was the right one to send first,
and why the original team's panel labels its group `(地形)`, terrain (`:5143`).

**Path B — the three alphas are not ambient at all.** `setLight_bg` overwrites
all four `BG_col` alphas with 255 (`:2931-2934`) before anything is lit, so the
alphas never travel path A. They are storage: three unrelated authored values
sharing a struct with the ambients, blended per frame from their own palette
columns `BG1_amb_alpha` / `BG2_amb_alpha` / `BG3_amb_alpha`
(`include/d/d_stage.h:153-155`) at `d_kankyo.cpp:2456-2466`, and consumed as
**per-material TEV constants** on name-matched background materials — the table
in §4. **Those three are on the wire as of protocol 13.**

> **The claim this section was written to test was half right.** The worklist
> said the four layers are "routed to different material classes". The *alphas*
> are, by J3D material name. The *RGB triples* are not — they are routed by room
> model file, through the lighting channel, and the material-class routing that
> also reads BG1/BG2/BG3 RGB (`:11454`, `:11652`, `:11704`) is a second,
> separate use of the same fields.

### 2.2 The room light registry — deliberately not driven from here

| Line | Label | Meaning | Field | Range |
| --: | :-- | :-- | :-- | :-- |
| 5318-5322 | `色 R/G/B` | colour | `dungeonlight[i].mColor.rgb` | 0–255 |
| 5377-5381 | `位置X/Y/Z` | position | `dungeonlight[i].mPosition` | ±300000.0 |
| 5383 | `ref_distance` | | `dungeonlight[i].mRefDistance` | 0.01–10000.0 |
| 5404 | `カットオフ角度` | cutoff angle | `dungeonlight[i].mCutoffAngle` | 0.001–90.0 |
| 5406 | `X角度(紫軸)` | X angle (purple axis) | `dungeonlight[i].mAngleX` | ±360.0 |
| 5408 | `Y角度(緑軸)` | Y angle (green axis) | `dungeonlight[i].mAngleY` | ±360.0 |

The section is seven repeats of `● ● ●  ラ イ ト N  ● ● ●` (LIGHT 2 through
LIGHT 7 at `d_kankyo.cpp:5244`, `:5257`, `:5270`, `:5282`, `:5294`, `:5306`),
followed by the *ese* ("fake") terrain-reflecting light at `:5416` — "valid in a
room where a 『terrain-reflecting fake light』 has been placed". **Not a gap.**
[`effect-lights.md`](effect-lights.md) records the settled decision that the
game's own light *placements* are not trusted under a path tracer, because they
were free of consequence under GX shading. Driving them from an overlay would
re-adopt exactly what that system exists to stop using.

### 2.3 Written once per scene, then only read

These four are set in `envcolor_init()` (`d_kankyo.cpp:1243`) and **never
touched by the per-frame palette blend**. That single fact is what makes them
wireable at all — see §6.

| Line | Label | Reading / meaning | Field | Range | Game's value | To Remix |
| --: | :-- | :-- | :-- | :-- | :-- | :-- |
| 6773, 8060 | `■時刻速度` / `時刻速度` | *jikoku sokudo* — time-of-day speed | `time_change_rate` | 0.0–10.0 | `0.012` (`:1494`) | no |
| 7507 | `てらてら率` under `■ MA09水面てらてら具合` (`:7506`) | *tera-tera* — the mimetic for a wet, glossy sheen; "gloss rate" | `mWaterSurfaceShineRate` | 0.0–1.0 | `1.0f` (`:1424`) | no |
| 7590 | `草ライト影響率` under `■ 草てかり調整` (`:7588`) | *kusa raito eikyouritsu* — grass light influence rate (*tekari* = sheen) | `grass_light_inf_rate` | 0.0–2.0 | `1.0f` (`:1318`) | no |
| 5032 | `影響率(0%-200%)` under `「アクタへの生ライトの影響率」` (`:5031`) | *nama raito* — the **raw** light's influence rate on actors | `mActorLightEffect` | 0–200 | `100` (`:1294`) | no — and see below |

**`mActorLightEffect` does not reach Remix and cannot be made to from here.**
It writes `tevstr_p->field_0x374` (`:3765-3766`), which scales GX light colours
through `dKy_light_influence_col` (`:2962`, `:3074`, `:3109`). Aurora holds
`D3DRS_LIGHTING = FALSE` permanently (`dx9_backend.cpp:106`,
[`kankyo-remix.md`](kankyo-remix.md) Part II), so GX light state never becomes
D3D9 light and never reaches Remix. `bg_light_influence` shares that path.
Recorded as a refutation so nobody re-derives it.

### 2.4 One more, and why it is not on the shortlist

| Line | Label | Meaning | Field | Range |
| --: | :-- | :-- | :-- | :-- |
| 7625 | `注目点` under `■ デモ用？　遠目対応　被写界深度` (`:7623`) | focus point, under "for cutscenes? long-distance, **depth of field**" | `mDemoAttentionPoint` | −1.0–1.0 |

See §3.3. Its consumer is the half-resolution Z-textured composite in
`m_Do_graphic.cpp:1148-1245` — an EFB trick of exactly the kind
[`kankyo-remix.md`](kankyo-remix.md) Part II says Remix's pipeline replaces.
Whether that pass even runs under the D3D9 backend is **not established here**,
so it is documented and not wired.

---

## 3. Flag 1 — labels that contradict the decomp's field name

A member name is a decompiler's hypothesis; a `genSlider` label is the original
team's word. **Three found. Every game symbol stays as it is** — only the
understanding changes.

### 3.1 `mFogDensity` is the cloud shadow density (already known — re-verified, and the evidence is stronger than recorded)

```
d_kankyo.cpp:5058   mctx->genSlider("雲影の濃さ ", &g_env_light.mFogDensity, 0, 0xff);
                                      kumokage no kosa = cloud shadow density
```

Already recorded at [`japanese-naming-audit.md`](japanese-naming-audit.md):91-148.
Re-verified, and **two independent confirmations neither document carries yet**:

1. **The palette source field is already correctly named in the decomp.**
   `d_kankyo.cpp:2478-2481` blends `mFogDensity` from
   `prev_pal_start_p->cloud_shadow_density` and its three siblings. The value's
   own source is spelled `cloud_shadow_density`; only the destination was
   guessed at.
2. **The consumer is `dKy_cloudshadow_scroll`** (`d_kankyo.cpp:4546`). At
   `:4566` it writes `k_color.r = g_env_light.mFogDensity & 0xFF` into
   `setTevKColor(1, …)` on materials whose name matches `MA00`, `MA01` or
   `MA16`, and immediately below scrolls their cloud-shadow texture matrix from
   `mpVrkumoPacket`. It is the strength of the painted cloud shadow on terrain,
   and nothing about it touches fog.

Also worth carrying: `:2482-2484` forces it to `-1` under
`checkNowWolfPowerUp()`, i.e. the cloud shadow is turned off during wolf senses.

### 3.2 `LIGHT_INFLUENCE::mPow` is a **range**, not a power — NEW

```
d_kankyo.cpp:7944   mctx->genSlider("影響範囲", &light.mPow, 0.0f, 10000.0f);
                                     eikyou han'i = "range of influence"
```

(`dKankyo_demolightHIO_c::genMessage`. `dKankyo_efflightHIO_c` labels its own
field the same way at `:7898`.)

`影響範囲` is *area/extent of influence* — a distance. `mPow` reads in English
as intensity, and the two are not interchangeable when converting to a path
tracer's radiance. The code sides with the label, four ways:

- `d_kankyo.cpp:924` and `:929` compare it against a distance:
  `closest_plight_dist < g_env_light.pointlight[j]->mPow`.
- `d_kankyo.cpp:1161`: `mInfluence.mPow = mRefDistance * 100.0f`.
- `d_kankyo.cpp:8570`: the inverse, `mRefDist = mPow * 0.01f`.
- `d_a_obj_lv8Lift.cpp:131` assigns it straight from a field another HIO struct
  already calls `mLightRadius`.

**No live defect: our own code already reads it correctly.**
`remix_bridge.cpp:863` is literally `const float reach = influence.mPow;` and
solves radiance from `reach²`. The value of the finding is that it stops the
next reader trusting the name.

### 3.3 `mDemoAttentionPoint` is a depth-of-field focus bias — NEW

```
d_kankyo.cpp:7623   genLabel("■ デモ用？　遠目対応　被写界深度")   hisyakai-shindo = DEPTH OF FIELD
d_kankyo.cpp:7624   genLabel("-1:奥にピント 1:手前にピント")        -1: focus at the back, 1: at the front
d_kankyo.cpp:7625   genSlider("注目点", &g_env_light.mDemoAttentionPoint, -1.0f, 1.0f);
```

"Attention point" collides with an established and unrelated concept in this
tree — `d_attention.cpp` is the camera lock-on system. The field is nothing of
the sort. `m_Do_graphic.cpp:1169-1174` maps it onto a TEV colour alpha
(`-254 + 509 × value`, with the negative half offset by 1.0), and `:1229-1235`
uses its **sign** to swap the composite's blend and alpha-compare so the blur
lands in front of the focus plane or behind it. It is the near/far focus bias of
a depth-of-field pass, exactly as the label above it says.

### Checked and found consistent

Every other decomp-coined name bound to a label in this file agrees with it:
`mFogNear`/`mFogFar`, `mCutoffAngle`, `mAngleX`/`mAngleY`, `mRefDistance`,
`mThreshold`, `mFluctuation` (`ゆらぎ`, *yuragi*, flicker),
`mWaterSurfaceShineRate` (`てらてら率` under `■ MA09水面てらてら具合`),
`mActorLightEffect`. The bloom struct's seven — `mBlurAmount`, `mDensity`,
`mOrigDensity`, `mSaturateSubtract*` — are settled in
[`japanese-naming.md`](japanese-naming.md) §8 and are not restated here.

---

## 4. Flag 2 — exposed by the game, not by us

Of the 62 live bindings, **32 now reach Remix and 30 do not** (29 / 33 when this
section was written). Six of the original 33 were alphas, which are the
interesting ones: `formatColorS10` and `formatColor` (`remix_bridge.cpp:246-265`)
send `r,g,b` only, so the game blends an alpha every frame and the bridge drops
it. **Three of the six are now carried** — the BG-ambient alphas below — and the
other three are the sky-dome alphas on the unmerged branch named next.

**Three of those six are already done on an unmerged branch.** `df83de0` on
`origin/claude/kasumi-naming-correction-w3e204` pushes `kasumiInnerAlpha`,
`kasumiOuterAlpha` and `kumoAlpha` at **protocol 12**. Do not rebuild them, and
do not take protocol 12.

That left **the three BG-ambient alphas**, which are the ones the P9 prompt
named. **All three landed on 2026-08-12, joining protocol 13** — as
`rtx.dusklight.env.bgWaterAlpha`, `bgAuxAlpha` and `bgFakeFogAlpha`
(`remix_bridge.cpp`, beside the `bgAmbient` push). **Pushed and displayed only;
nothing on either side consumes them, so the image must not change.**
Untested in game.

| Field | Label | Meaning | Consumers |
| :-- | :-- | :-- | :-- |
| `bg_amb_col[1].a` | `水面α` (`:5164`) | water-surface alpha | `dKy_murky_set` → `setTevKColor` alpha on the murk material (`:11310`); `dKy_bg_MAxx_proc` → `setTevKColor(3)` (`:11459`) and `setTevColor(1)` alpha on **MA16** (`:11707`); read by `d_kankyo_rain.cpp:6174` |
| `bg_amb_col[2].a` | `補佐α` (`:5165`) | auxiliary alpha | `dKy_murky_set` → `setTevColor` alpha (`:11309`); `dKy_bg_MAxx_proc` → `setTevColor(1)` alpha (`:11457`) |
| `bg_amb_col[3].a` | `ウソFog` (`:5188`) | **"fake fog"** | `dKy_bg_MAxx_proc` → `setTevColor(1)` alpha on **MA13** (`:11687`) and `setTevKColor(3)` alpha on **MA14** (`:11699`) and **MA16** (`:11711`) |

All three are blended per frame from the palette (`d_kankyo.cpp:2456-2466`),
exactly like the colours beside them, and all three feed material colour — which
[`kankyo-remix.md`](kankyo-remix.md) Part II says *does* travel to Remix.

**They are worth carrying even though they already travel per draw**, and this
is the argument, stated as the inference it is: aurora folds a TEV konstant into
`D3DRS_TEXTUREFACTOR` and the stage ops (`lib/dx9/dx9_tev.cpp:1686-1702`), so
what reaches Remix is the combined result on one draw, and **no material name
reaches Remix at all**
(`extern/aurora/docs/dx9/remix-material-interface.md` §9). There is therefore no
way for the fork to look at a draw and say "that alpha is the fake fog". The
scene-global authored value is a thing only the game can state. **Not measured —
inferred from those two documented facts.**

**Protocol: they joined 13 rather than taking 14.** 13 already belongs to this
session's branch (`claude/japanese-naming-worklist-nea1rk`) and everything on it
ships as one build, so a further addition on the same branch joins the number
rather than spending another — see the ladder in the fork's
`documentation/DusklightOverlay.md`. 12 remains the unmerged kasumi branch's and
is still not free.

The other 27 uncovered bindings are the room light registry (§2.2, deliberately
not driven), the BG1/BG2/BG3 ambient RGB triples (**§2.1a: refused on the
routing, not deferred**), and the four §2.3 scalars.

---

## 5. Flag 3 — bindings to a still-unnamed `field_0x…`

The idea is that a label names a field the decomp left as an offset. Measured
across all 149 slider files:

| | count |
| :-- | --: |
| slider/checkbox bindings to a `field_0x…` | **150** |
| …with a real label | **117** |
| …with an **empty** label string | **33** (22 %) |
| …in the kankyo family | **11**, all labelled |

**But it yields nothing at all for the environment struct.** Of the 62 bindings
on live `g_env_light` state, **zero** target a `field_0x…`. The
[audit](japanese-naming-audit.md) counts 257 still-unnamed members in
`dScnKy_env_light_c` (41 % of it); the panel names **none** of them.

The 11 kankyo hits are all members of the panel classes themselves — debug
scratch, not game state. For completeness, since the labels are real:

| Line | Label | Meaning | Field (of `dKankyo_navyHIO_c` unless noted) |
| --: | :-- | :-- | :-- |
| 7551 | `全体` under `■ 強引　水面にごり変更` (`:7549`) | "overall", under *gouin* (forcible) water-*nigori* (turbidity) change | `field_0x2ea` |
| 7552 | `手前` | "near / foreground" | `field_0x2ec` |
| 7685 | `全体α` under `■ 沼` (`:7678`) | "overall alpha", under *numa* (swamp) | `field_0x268` |
| 7691-7693 | `泥２R/G/B` | *doro* — "mud 2" | `field_0x264` |
| 7713 | `加算にチェンジ！！` under `■ 太陽フレア　加算に切替` | "change to additive!!", sun flare | `field_0x215` |
| 6213 | `海抜（地平線）の設定` | sea-level (horizon) setting | `field_0x14` (`dKankyo_vrboxHIO_c`) |
| 6770 | `画面表示` | on-screen display | `field_0x5` (`dKankyo_bloomHIO_c`) |
| 8014 | `←五感発動時にしか見えなくなります！` | "← becomes visible only when the senses fire" | `field_0x1a` (`dKankyo_ParticlelightHIO_c`) |
| 8017 | `インダイレクトより後に移動！` | "moved to after the indirect [texturing]!" | `field_0x19` (same) |

`field_0x2ea` / `field_0x2ec` are only ever assigned in the panel object's
constructor (`src/dusk/stubs.cpp:852-853`, this port's reimplementation of it),
and the section they belong to is DEBUG-gated. **The honest report is: the flag-3
yield on rendering state is zero, and 33 of the 150 tree-wide hits have an empty
label anyway.**

---

## 6. The shortlist

Ranked by "a tuner would reach for this first", not by ease of wiring. Two hard
gates decide what can be built at all, and they are worth stating because they
eliminate most of the list on their own:

- **A field the palette blend rewrites every frame cannot be overridden from the
  bridge.** `dusk::remix::tick()` runs *after* `fapGm_Execute()`
  (`m_Do_main.cpp:319-327`), so anything it writes into `g_env_light` is
  overwritten by the next frame's `setLight()` before the draw. Overriding one
  of those needs an edit at the consumer, which is outside this item's file list.
- **A readout the game pushes needs a protocol bump on both sides in one
  commit.** Fork-side controls the game reads back do not.

| # | Control | Field / line | Direction | Status |
| --: | :-- | :-- | :-- | :-- |
| 1 | Water surface gloss — `てらてら率` | `mWaterSurfaceShineRate`, `:7507` | fork → game reads | **wired** |
| 2 | Grass light influence — `草ライト影響率` | `grass_light_inf_rate`, `:7590` | fork → game reads | **wired** |
| 3 | Clock rate — `時刻速度` | `time_change_rate`, `:6773` | fork → game reads | **wired** |
| 4 | Cloud shadow density — `雲影の濃さ` | `mFogDensity`, `:5058` | needs a consumer-side edit | blocked, see below |
| 5 | "Fake fog" alpha — `ウソFog` | `bg_amb_col[3].a`, `:5188` | game **pushes** | **wired 2026-08-12**, protocol 13 — displayed, not consumed |
| 6 | Water-surface alpha — `水面α` | `bg_amb_col[1].a`, `:5164` | game **pushes** | **wired 2026-08-12**, protocol 13 — displayed, not consumed |
| 7 | Auxiliary alpha — `補佐α` | `bg_amb_col[2].a`, `:5165` | game **pushes** | **wired 2026-08-12**, protocol 13 — displayed, not consumed |
| 8 | Terrain light influence — `地形ライト影響率` | `bg_light_influence`, `:5056` | — | **refuted**: GX-light path, does not reach Remix |
| 9 | Actor light influence — `影響率(0%-200%)` | `mActorLightEffect`, `:5032` | — | **refuted**: same path |
| 10 | Depth-of-field focus bias — `注目点` | `mDemoAttentionPoint`, `:7625` | fork → game reads | not wired: EFB composite, reaching Remix unestablished |
| 11 | Sky-dome layer alphas | `:6353`, `:6376`, `:6399` | game pushes | **already done** on the unmerged protocol-12 branch |
| 12 | BG1/BG2/BG3 ambient RGB | `:5155-5181` | game pushes | **refused, §2.1a**: they are per-room-model-file ambient *light*, which the path tracer replaces, and no full-screen consumer could apply one per model file. Corrected 2026-08-12 — the earlier reason given here, "low value: BG0 is the layer the game itself reuses", was true but was not the reason |
| 13 | Room light registry | `:5318-5408` | — | deliberately not driven — `effect-lights.md` |
| 14 | Bloom / saturation-subtract table | `:7082-7092` | — | already driven at the output end, `japanese-naming.md` §8 |
| 15 | Everything in `dKankyo_navyHIO_c` | 161 sliders | — | debug scratch; the two consumers sampled are both `#if DEBUG` |

**Why 1-3, in one sentence each.**

1. **Water surface gloss.** The game's own single dial for how alive a water
   surface reads, it moves two things at once (the MA09 TEV konstant at
   `:11444-11446` *and* the surface's texture-animation speed at
   `d_a_bg.cpp:373-374`), it is pinned at `1.0` with nothing able to change it,
   and this project has an entire Water section in the overlay already asking
   these questions.
2. **Grass light influence.** Grass covers most of the outdoor world and is the
   first thing that reads wrong when a path tracer relights a scene; the game's
   own rate feeds `GFSetTevColorS10(GX_TEVREG1, …)` in both the grass and flower
   packet draws (`d_grass.inc:705`, `:1046`), which is material colour and
   therefore travels.
3. **Clock rate.** The overlay can already jump the clock and freeze it, but not
   *run* it — and the tab's own text says three of the six palette entries exist
   for a single instant each, so a slow rate is the only way to watch a cross-fade
   happen and a fast one is the only way to see a whole day without scrubbing.

**Why 4 is blocked and what it would take.** Cloud shadow density is the most
interesting single value in the panel — painted shadow competing with traced
shadow is exactly the kind of double-counting this project has already been
caught by once (baked vertex lighting, `kankyo-remix.md` Part II). But
`mFogDensity` is re-blended at `d_kankyo.cpp:2478` every frame, so a bridge
write is clobbered before it is used. It wants a scale applied at the consumer
(`dKy_cloudshadow_scroll`, `d_kankyo.cpp:4566`) reading a mirrored setting — a
three-line edit in a file this item may not touch. **Recommended as the next
item; not built here.**

---

## 7. What was wired

Three fork-side options, all read by the game, **none of which touches the wire
protocol** (protocol stays 11). Each defaults to the game's own value, so
nothing changes until a slider moves.

| Option | Game field | Default | Range |
| :-- | :-- | :-- | :-- |
| `rtx.dusklight.game.waterSurfaceShine` | `g_env_light.mWaterSurfaceShineRate` | `1.0` = the game's `envcolor_init` value | 0.0–1.0, the panel's own range |
| `rtx.dusklight.game.grassLightInfluence` | `g_env_light.grass_light_inf_rate` | `1.0` = the game's value | 0.0–2.0, the panel's own range |
| `rtx.dusklight.game.clockRate` | `g_env_light.time_change_rate` | `1.0` = a multiplier of one on the game's `0.012` | 0.0–20.0 |

Applied in `remix_bridge.cpp`'s `applyKankyoTuning()`, called from `tick()`.
This departs from the usual convention — the bridge normally mirrors a value
into `dusk::getSettings()` and lets the consumer read it — because these three
fields have no consumer of ours, and because all three are `envcolor_init()`
values that nothing rewrites per frame, so a write from `tick()` sticks. Moving
the application to the consumers is the tidier end state.

**The clock rate does not break the wolf's time skip.** `d_a_alink_wolf.inc:4167`
sets `time_change_rate = 1.0f` to fast-forward, and `d_kankyo.cpp:1591-1596`
restores `0.012f` at the next dawn or dusk. The bridge refuses to write while
the field is at or above `0.9f`, which covers that `1.0f` and the stage-select
menu's sentinels at `≥1000.0f` (`d_s_menu.cpp:1429-1453`, read at
`d_kankyo.cpp:1489-1492`) — so a howl runs untouched and the requested rate
re-applies on the frame after the game resets it.

**At `1.0` the clock rate writes nothing at all**, rather than writing the value
the game would have. Both halves of that matter: an unconditional write would
clobber the deliberate `0.0f` the stage-select scene puts in the same field
(`d_s_menu.cpp:1424`), and a control sitting at its default must change nothing;
but a bare "only write when it differs from 1" would leave the last scaled rate
in place after the slider was dragged back. A latch resolves both — one restoring
write when it returns to 1, then silence.

**Untested in game.** All three are CI-shaped only: MinGW syntax check, the
option-name cross-check, and all three invariants scripts. Nobody has looked at
a frame with any of them moved.

---

## See also

- [`japanese-naming.md`](japanese-naming.md) — how to read these names; §6 is
  why the labels are the best source in the tree
- [`japanese-naming-audit.md`](japanese-naming-audit.md) §3 — where this panel
  was first counted, and the `mFogDensity` finding
- [`kankyo-remix.md`](kankyo-remix.md) — what kankyo is and what we drive with
  it; Part II is what the D3D9 stream does and does not carry
- [`effect-lights.md`](effect-lights.md) — why the game's own light registry is
  not driven from its own numbers
