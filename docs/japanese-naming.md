# The game's names are romanized Japanese

Twilight Princess was written by a Japanese team, and the decompilation this port
is built from (`zeldaret/tp`) reproduces the original build 1:1 — including its
**symbol names**. So the identifiers in `src/d/`, `src/f_op/`, `src/f_pc/`,
`src/m_Do/` and `libs/JSystem/` are not English names that happen to look odd.
They are **Japanese words written in Latin letters** (romaji), abbreviated
Japanese, and — separately — misspelled English.

`dKyr_drawSibuki` is not a typo for anything. It draws 飛沫 *shibuki*, spray.

**This file is the canonical statement for all three of our repos.** The
companions in `dusklight-mods` and `aurora-ao` point here rather than
re-explaining it, and each covers only what its own repo does with the finding.

---

## 0. Provenance, and what is verified

This document was brought over from the `Fixed-Function-dev` branch of this same
fork, where it was written for a separate RTX Remix experiment. **Every count and
citation below was re-derived against this tree (`main`) rather than copied**, and
where our measurement disagrees with the original it says so and gives the command.
Nothing here is inherited on trust.

**What is verified and what is gloss.** Every backticked game symbol in this file
is checked to exist in this tree by `tools/check_japanese_naming.py`, so the
glossary cannot rot silently as the port moves. **The Japanese readings and
meanings are our gloss, not something read out of the tree**; they are here because
they are useful, not because the codebase asserts them. Where a reading is
uncertain it says so.

Three failure modes this exists to prevent, all cheap to avoid once you know the
rule:

1. **Searching in the wrong alphabet.** A grep for `sibuki` finds the C functions
   and **none** of the 69 effect IDs, which spell it `shibuki`. §3.
2. **"Fixing" a name.** `dKyw_wether_move` looks like a misspelling to correct. It
   is the name of the weather system — 265 occurrences across 28 files here, plus
   the reference decomp. §4.
3. **Inventing an etymology.** `d_a_ep` is not "environment particle". Nobody here
   knows what `ep` stands for, and a document that says "unknown" is worth more
   than one that guesses.

---

## 1. How a name is put together

Names are `prefix` + `subsystem` + `verb/noun`, and the prefix tells you which
layer you are in. This part is consistent, even where the words are not.

| Prefix | Layer | Examples |
| :-- | :-- | :-- |
| `d_` / `d…_` | the Zelda game layer | `d_kankyo.cpp`, `dKy_setLight`, `dComIfGp_getStage` |
| `d_a_` | an **actor** (anything with a lifecycle in the world) | `d_a_alink.cpp` (Link), `d_a_obj_*`, `d_a_npc_*`, `d_a_e_*` (enemies), `d_a_b_*` (bosses), `d_a_tag_*` (invisible trigger volumes) |
| `d_bg_` | background / collision | `d_bg_s_acch.cpp` |
| `d_msg_`, `d_menu_`, `d_meter_` | text, menus, HUD | `d_msg_scrn_jimaku.cpp`, `d_meter2.cpp` |
| `f_op_` | framework **op**eration — the process *classes* | `f_op_actor.cpp`, `f_op_kankyo.cpp`, `f_op_scene.cpp` |
| `f_pc_` | framework **p**ro**c**ess control — create/execute/draw/delete | `f_pc_draw.cpp`, `f_pc_creator.cpp` |
| `m_Do_` | the machine layer — graphics, audio, controller, memory card | `m_Do_graphic.cpp`, `mDoExt_modelEntryDL` |
| `J…` | **JSystem**, Nintendo's shared middleware, in `libs/JSystem/` | `J3D*` (3D), `J2D*` (2D), `JKR*` (heaps), `JUT*` (utility), `JPA*` (particles), `JAI`/`JA*` (audio) |
| `c_` | common helpers | `cXyz`, `cM_`, `cLib_` |

Function prefixes compress the same idea: `fopAcM_` = f_op **Ac**tor **M**anager,
`fopKy_` = f_op **Ky**(ankyo), `fpcDw_` = f_pc **Dr**a**w**, `dComIfGp_` =
d_com_inf **G**ame **p**rocess (the `dComIfG*` family is the global-access façade:
`dComIfGp_` for the play process, `dComIfGs_` for save data, `dComIfGd_` for draw
lists).

**`Ky` is `kankyo` is 環境 is "environment".** That single abbreviation carries most
of what our graphics work cares about: `dKy_` (core), `dKyw_` (wether/weather),
`dKyr_` (the particle draw routines), `dKyd_` (data tables), `kytag01`…`kytag17`
(the invisible actors that override environment state per area), and
`fopKy_IsKankyo`. It is a first-class process class in the framework, alongside
actors, cameras, messages and scenes.

---

## 2. …and then the words are Japanese

Once you strip the prefix, the noun is as likely to be Japanese as English:

```
dKyr_drawSibuki        飛沫 shibuki   = spray/splash    (rain hitting surfaces)
dKyr_housi_init        胞子 hōshi     = spore           (the drifting motes)
dKyw_drawVrkumo        雲   kumo      = cloud           (vrbox kumo → skybox clouds)
vrbox_kasumi_inner_col 霞   kasumi    = haze            (the horizon band)
g_env_light.mMoyaCount 靄   moya      = mist/haze
dKy_itudemo_se         いつでも itsudemo = "any time"   (the always-on ambient SE)
```

All six are in the environment system our graphics work drives. They are not
obscure corners: `mMoyaMode` is what Effect Remover's projected-shadow feature
switches on, and `mpVrkumoPacket` is what scrolls the terrain shadow overlay it
also removes.

---

## 3. Romanization is inconsistent — this is the grep trap

There is no single romanization. The tree mixes **kunrei-shiki** (`si`, `tu`, `ti`,
`sya`, `zi`) with **Hepburn** (`shi`, `tsu`, `chi`, `sha`, `ji`) — and does so **for
the same word**. Measured on this tree:

| Word | In C code | In the effect-ID table (`src/d/d_particle_name.cpp`, 3201 names) |
| :-- | :-- | :-- |
| 飛沫 spray | `Sibuki` ×21 | `shibuki` ×69, `sibuki` ×**0** |
| 胞子 spore | `housi` in `dKyr_housi_init`, `dKyw_drawHousi` | `houshi` ×4, `housi` ×**0** |
| 雫 droplet | — | `shizuku` ×29 **and** `sizuku` ×26, in the same table |

So the two halves of one feature are spelled differently, and **either spelling
alone finds half the system.** The droplet row is the sharpest case: neither
spelling is a substring of the other, both are live, and they sit in one file.

```sh
rg -c shibuki src/d/d_particle_name.cpp   # 69
rg -c sibuki  src/d/d_particle_name.cpp   #  0  <- and 21 hits in the C code
```

Other kunrei spellings you will meet: `suisya` (水車 water wheel), `tatigi` (立ち木
standing tree), `ihasi` (橋 hashi, bridge), `tubo` (壺 tsubo, pot — hence
`ootubo`/`kotubo`, big/small pot). Hepburn shows up in the same directories:
`katatsumuri` (snail), `kamakiri` (mantis), `hakusha` (拍車 spur).

**Practical rule: when a search comes back empty, try the other romanization before
concluding the thing does not exist.** For s/t/c sounds that is `si↔shi`, `tu↔tsu`,
`ti↔chi`, `sya↔sha`, `zi↔ji`.

---

## 4. Misspelled English is a second naming system

Independently of the Japanese, the original names contain English spelled by ear.
These are **load-bearing identifiers**, not defects:

| In the tree | Intended | Where |
| :-- | :-- | :-- |
| `wether` | weather | `dKyw_wether_move`, `d_kankyo_wether.cpp` — 265 occurrences, 28 files |
| `Schejule` | schedule | `dKyd_lightSchejule` — the time-of-day light table |
| `Sord` | sword | `dKy_SordFlush_set` |
| `Blure` | blur | `SetBlureActor`, `ResetBlure` |
| `parcent` | percent | `dKy_get_parcent` |
| `vectle` | vector | `dKyr_get_vectle_calc` |
| `resorce` | resource | `d_resorce.cpp` |
| `tresure` | treasure | `d_tresure` |

**Do not correct them.** Renaming breaks the match with `zeldaret/tp`, which
`docs/code-conventions.md` asks us to upstream fixes to, and silently breaks
anyone's grep. If you need a readable name, add a comment — not a rename.

---

## 5. Short codes are Japanese initials

`d_a_e_*` (enemies), `d_a_b_*` (bosses) and many `d_a_obj_*` names are
two-or-three-letter codes: `d_a_e_rd`, `d_a_b_gnd`, `d_a_obj_so`. Some decode from
Japanese names, some from English ones, and **many are simply not known**.

Do not reverse-engineer one from its letters and then build on the guess. Decode it
from what the file *does* (§7) and, if the meaning still is not established, write
"unknown".

---

## 6. The debug builds are labelled in Japanese, and that is a dictionary

> ### ⚠ How to search for Japanese without getting a wrong answer
>
> The container's default locale is `POSIX` (`locale` prints `LC_CTYPE="POSIX"`).
> Under it, PCRE runs in non-UTF mode and the three obvious ways to search for
> Japanese fail in three **different** ways. Measured on this tree, this container:
>
> | Command | POSIX | `LC_ALL=C.UTF-8` | Failure mode |
> | :-- | --: | --: | :-- |
> | `grep -rlP '\p{Han}' src include` | **0** | 427 | **silent** — exit 1, no error, reads as "not there" |
> | `grep -rlP '[\x{3040}-\x{30ff}\x{4e00}-\x{9fff}]' src include` | **0** | 496 | errors to *stderr*, exit 2; stdout is empty, so a pipeline reads it as zero |
> | `grep -rlP '[ぁ-んァ-ヶ一-龥]' src include` | **507** | 496 | **worst** — matches byte ranges, returns 11 false positives, and *looks* like it worked |
> | `rg -l '\p{Hiragana}\|\p{Katakana}\|\p{Han}' src include` | **496** | 496 | none — ripgrep is locale-independent |
>
> **So there are two safe answers, and prefer the first:**
>
> 1. **Use ripgrep** (`rg`, and the Claude Code `Grep` tool, which is ripgrep).
>    It is correct under either locale — including `\p{…}` classes. Sessions that
>    searched with the `Grep` tool were never affected by any of this.
> 2. If you must shell out to `grep -P`, `export LC_ALL=C.UTF-8` first.
>
> **An empty `grep -P` for Japanese is not evidence of absence, and a non-empty one
> is not evidence of presence.** This is the §3 rule again, one level lower down.
> The 11 false positives above are all files containing non-Japanese multi-byte
> UTF-8 (our own `src/dusk/` UI code, mostly) whose bytes fall inside the ranges.

**496 files under `src/` and `include/` contain literal kana/kanji** — not
romanized, actual Japanese. It is in the HIO debug sliders, the developer overlay,
the CSV exporters and a great many `OS_REPORT` strings:

```
src/d/d_kankyo.cpp:5003   mctx->genSlider("雲影の濃さ ", &g_env_light.mFogDensity, 0, 0xff);
                                            ^ "cloud shadow density"
```

This is the best translation source in the tree, because it is the original authors
labelling their own fields. **When a name is ambiguous, find where the debug menu
prints it.**

### The tuning panel is a specification

The game ships the debug panels its own developers used, with their own labels.
Counted on this tree:

```sh
rg -c 'genSlider' src include | awk -F: '{s+=$2} END{print s}'    # 3912
```

| | this tree | the `Fixed-Function-dev` doc said |
| :-- | --: | --: |
| `genSlider` | 3,912 | 3,903 |
| `genLabel` | 1,249 | 1,249 |
| `genCheckBox` | 421 | 421 |
| `genButton` | 313 | 312 |
| files carrying a panel | 178 | 149 |
| `genSlider` in `d_kankyo.cpp` alone | **291** | 291 |

(The small deltas are branch drift and a different definition of "carrying a
panel"; the numbers above are what this tree measures today. Re-run the command
rather than quoting either figure from memory.)

Each slider is a machine-readable binding — `genSlider("<Japanese label>", &<live
game field>, <min>, <max>)`: a label written by the people who authored the game's
look, the exact variable, and the range they considered sane. Pulled from
`d_kankyo.cpp` on this tree:

| Line | Label | Means | Field | Range |
| --: | :-- | :-- | :-- | :-- |
| 4977 | `影響率(0%-200%)` | influence rate of the *raw* lights on actors | `mActorLightEffect` | 0–200 |
| 5001 | `地形ライト影響率` | terrain light influence rate | `bg_light_influence` | 0.0–2.0 |
| 5003 | `雲影の濃さ` | **cloud shadow density** | `mFogDensity` | 0–255 |
| 5077 | `ACTOR_Amb` | actor ambient (アクタ) | `actor_amb_col` | 0–255 |
| 5089 | `ＢＧ０_Amb` … `ＢＧ３_Amb` | **four** terrain ambient layers (地形) | `bg_amb_col[0..3]` | 0–255 |
| 5109 | `水面α` | **water-surface alpha** | `bg_amb_col[1].a` | 0–255 |
| 5110 | `補佐α` | auxiliary alpha | `bg_amb_col[2].a` | 0–255 |
| 5133 | `ウソFog` | **"fake fog"** | `bg_amb_col[3].a` | 0–255 |
| 5185 | `（ライト０）―えせポイントライト` | **"fake" point light** (えせ = phoney) | light slot 0 | — |
| 5186 | `（ライト１）―エフェクトライト` | effect light | light slot 1 | — |
| 5191 | `※太陽が存在する場合、設定は上書きされます` | "if a sun exists, this is overwritten" | light slot 2 = **sun** | — |
| 5204 | `※月が存在する場合、設定が上書きされます` | "if a moon exists, this is overwritten" | light slot 3 = **moon** | — |
| 6314 | `● 前かすみ` | **front (near)** haze | `vrbox_kasumi_outer_col` | 0–255 |
| 6337 | `● 奥かすみ` | **back (far)** haze | `vrbox_kasumi_inner_col` | 0–255 |
| 7574 | `■ 影の濃さ` → `通常α` / `接近ＭＡＸα` | projected-shadow density, normal and close-up | `shadow_normal_alpha`, `shadow_max_alpha` | 0.0–1.0 |
| 7446 | `MA09水面てらてら具合` | MA09 water-surface glossiness | `mWaterSurfaceShineRate` | — |

Three of the BG ambient *alphas* read as meaningless noise in English. In Japanese
they are a water-surface term, an auxiliary term and a **"fake fog"** term.

Two more terms this panel settles, both of which we had no word for:

- **`ポリゴンコード` — "polygon code"** (`d_kankyo.cpp:4972-4973`) is the game's own
  name for the `MAnn` material codes our mods switch on. We have been calling them
  "material codes".
- **`えせ` (*ese*, phoney/pseudo) vs `生ライト` (*nama*, raw/live)** is the game's own
  distinction between its fake point lights and its placed lights — the exact
  distinction our realtime mods exist to act on. `d_kankyo.cpp:5359-5362` even has a
  「地形反映えせライト」, a "terrain-reflecting fake light".

**Honest limit, so this is not oversold.** Most of the 3,912 sliders are gameplay,
not rendering. These are debug panels, and whether the original host tool can be
driven in the PC port is a separate, unanswered question — irrelevant to the value
here, which is that the bindings are readable **statically**, and that is all a
specification needs to be.

> ### Worked example: which `kasumi` is which
>
> `vrbox_kasumi_inner_col` and `vrbox_kasumi_outer_col` are two horizon haze bands.
> Nothing about "inner" and "outer" says which is nearer, and guessing from the
> English is how this got recorded wrongly once. The game answers it in four
> independent places:
>
> 1. **The original team's slider panel** — `d_kankyo.cpp:6314` labels the
>    `kasumi_**outer**` sliders `● 前かすみ` (前 = front/near) and `:6337` labels the
>    `kasumi_**inner**` ones `● 奥かすみ` (奥 = back/far).
> 2. **The palette CSV exporter** (`d_kankyo.cpp:6527-6528`) writes a Japanese header
>    row and then the fields in the same order:
>    `空色, 上雲色, 下雲色, 下雲影色, 下雲α, 霞手前色, 霞手前α, 霞奥色, 霞奥α` —
>    `霞手前` (*temae*, near/in-front) lands on `kasumi_outer` and `霞奥` (*oku*,
>    far/behind) on `kasumi_inner`.
> 3. **The debug view** (`d_kankyo_debug.cpp:301,306`) prints the same two fields as
>    `kasumiF` and `kasumiB` — Front and Back — and its neighbours at `:288,293` use
>    the same positional scheme (`CloudU`/`CloudD` for `kumo_top`/`kumo_bottom`), so
>    the letters are systematic rather than incidental.
> 4. **The dome actors**: `d_a_vrbox.cpp:101-104,126` paints `kasumi_inner` onto
>    `vrbox_sora.bmd` (*sora* = 空, sky) and the second dome carries `kasumi_outer`.
>    Two shells, front and back.
>
> So **`outer` is the near band and `inner` is the far one** — the opposite of what
> the English words suggest. No code path anywhere relates either to sun position.

---

## 7. Decoding a name you have not seen

In rough order of reliability:

1. **Find where a debug build prints it** (§6). The game labelling its own field
   beats every other method.
2. **Read the header.** `include/d/actor/d_a_*.h` carries the class name and the
   struct layout; a field list usually settles what the actor is faster than the
   name does.
3. **Find the profile / actor name.** Actors are registered with a profile whose
   name string often survives in the data — grep the identifier in `src/d/d_stage.cpp`
   and the profile tables.
4. **Look at the effect IDs it uses.** These are long and descriptive
   (`ZI_S_lk_takishibuki_a`), and — usefully — they are frequently romanized the
   *other* way from the C function, so they double as a translation hint.
5. **Try both romanizations** (§3), then try it as misspelled English (§4).
6. **Check the reference decomp.** `zeldaret/tp` has years of naming work and issue
   discussion that this port inherits but does not restate.

**One caution when reading logs.** Because the game's own diagnostic strings are
Japanese, a game-side `OS_REPORT` or assertion can put Japanese in a log the owner
sends us. That is the game talking, not a corrupted file.

---

## 8. A header name is not an authored name

The single most useful rule to come out of applying this document, and it is not
about Japanese at all.

> **Function and global-data symbols are the original team's. Struct member names
> are not.** The decompilation *reconstructs* member names, so a member name is a
> **hypothesis until an authored string agrees with it** — an HIO slider label (§6),
> a `dDbVw_Report` format, a CSV column header, an `OS_REPORT`.

The worked example is the one that started the audit. `vrbox_kasumi_inner_col` /
`_outer_col` were described backwards for months, and **that was never a misread
Japanese word** — it was a header name trusted like a function symbol, when the
header name is the one artifact in the chain no Japanese developer wrote.

**The live instance, and it reaches our mods.** `g_env_light.mFogDensity`
(`include/d/d_kankyo.h:457`) is **not fog density**:

```
src/d/d_kankyo.cpp:5003    genSlider("雲影の濃さ ", &g_env_light.mFogDensity, 0, 0xff);
                                       ^ cloud shadow density        ^ the decomp called it mFogDensity
src/d/d_kankyo.cpp:4511    k_color.r = g_env_light.mFogDensity & 0xFF;   // inside dKy_cloudshadow_scroll
src/d/d_kankyo.cpp:11456   sp5C.r = (u8)g_env_light.mFogDensity;         // inside dKy_bg_MAxx_proc
```

Both consumers write it into **TEV KColor register 1's red channel** on the
`MA00`/`MA01`/`MA04`/`MA16` terrain materials, and one of them is a function the
game itself named `dKy_cloudshadow_scroll`. Three independent signals — the label,
the function name, the consumers — all say *cloud shadow*, and the header says
*fog*. The header is the one that was reconstructed.

Names still carrying an unchecked semantic claim, listed so nobody re-derives the
list: `mOrigDensity`, `kumo_top_col` / `kumo_bottom_col`, and `dungeonlight_col`
(whose palette source is spelled `plight_col`). **Apply this opportunistically**,
when you are already in a field. Not as a sweep.

Corollary: a `field_0x…` member is **unaudited data, not absent data**. A search
driven by names is structurally blind to every one of them, and
`dScnKy_env_light_c` has a great many. Enumerate by offset when completeness
matters.

---

## 9. Rules

1. **Never rename a game symbol** to make it read as English. §4.
2. **Never assert a meaning you have not established.** "Unknown" is a finding; a
   plausible-looking guess is a liability the next session inherits.
3. **Gloss on first use in a document**, then use the bare name. A reader who does
   not know the word cannot look it up, because it is not English.
4. **Our own code is different.** `src/dusk/` here, `lib/` in aurora, and everything
   in `dusklight-mods` are ours and use ordinary English `camelCase`/`snake_case`.
   The romaji convention applies to the game code we *read*, not to the code we
   *write*. Do not romanize anything new.
5. **Documented is not done.** Correcting a description is a different claim from
   correcting the behaviour built on it. If a wrong reading reached code, say so
   explicitly and separately.

---

## 10. Working glossary

Terms our documents and code paths actually touch. **Symbols are mechanically
checked to exist by `tools/check_japanese_naming.py`; the readings are our gloss.**

### The environment system

| Romaji | Japanese | Meaning | A symbol in this tree |
| :-- | :-- | :-- | :-- |
| kankyo | 環境 | environment | `d_kankyo.cpp`, `fopKy_IsKankyo` |
| kumo | 雲 | cloud | `vrbox_kumo_top_col`, `dKyw_drawVrkumo` |
| vrkumo | VR box + 雲 | the drifting skybox cloud packet | `mpVrkumoPacket`, `dKankyo_vrkumo_Packet` |
| kasumi | 霞 | haze (horizon band) | `vrbox_kasumi_inner_col` |
| moya | 靄 | mist / haze — the projected ground shade | `mMoyaCount`, `mMoyaMode` |
| housi | 胞子 | spore (drifting motes) | `dKyr_housi_init` |
| sibuki | 飛沫 | spray / splash | `dKyr_drawSibuki` |
| itudemo | いつでも | "any time", always-on | `dKy_itudemo_se` |
| wether | *(English, by ear)* | weather | `dKyw_wether_move` |
| vrbox | *(English, "VR box")* | the skybox dome | `d_a_vrbox.cpp` |
| kytag | 環境 + tag | per-area environment override actor | `d_a_kytag01.cpp` |
| ese | えせ | fake, phoney — the game's word for its fake point lights | *(label only, `d_kankyo.cpp:5185`)* |
| nama | 生 | raw, live — the game's word for its placed lights | *(label only, `d_kankyo.cpp:5165`)* |
| sora | 空 | sky | `vrbox_sky_col` |
| yami | 闇 | darkness — the Twilight fog | `dKyr_evil_draw` |
| odour | においもや | "smell mist", the wolf-senses scent trail | `dKyr_odour_draw` |

### Terrain / material vocabulary (the `MAnn` polygon codes)

| Romaji | Japanese | Meaning | Where |
| :-- | :-- | :-- | :-- |
| gake | 崖 | cliff | material suffix `MA00_Gake`, `d_a_bg.cpp:383` |
| kusa | 草 | grass | material suffix `MA00_Kusa`, `d_a_bg.cpp:383` |
| enkei | 遠景 | distant scenery | material suffix `MA00_Enkei_Tree_Color` |
| nami | 波 | wave | material suffix on `MA06` |
| nigori | 濁り | turbidity, murk | material suffix on `MA06` |
| mizugiwa | 水際 | water's edge, shoreline | material suffix on `MA06` |
| kasan | 加算 | **addition** — an additively blended pass | material suffix on `MA03` |
| funsui | 噴水 | fountain | material suffix on `MA03` |
| mera | めら | shimmer | material suffix on `MA09` |

The last six are read off material names rather than C symbols, so they are not
mechanically checkable here; they were measured on the water work on
`Fixed-Function-dev` and each one changed a decision there. **`kasan` is the case to
remember**: the blend state was measured a session before anyone read the name, and
the name had said `SRC_ALPHA,ONE` all along.

Note the granularity trap that goes with them: `MA06` alone covers the waves, the
shoreline **and** the murk. A control that cuts on the code is coarser than one that
reads the suffix. When adding a classifier over these names, prefer matching `_word`
and `Word` (the convention lowercases after the code and capitalises inside a
compound) over a bare substring, so `minami` is not read as `nami` — and make
"unrecognised" mean **leave it alone**.

### Frequently met elsewhere

| Romaji | Japanese | Meaning | A symbol in this tree |
| :-- | :-- | :-- | :-- |
| hahen | 破片 | fragment, debris | `ep_hahen_s` |
| tubo | 壺 | pot, jar | `d_a_obj_oiltubo` |
| ita | 板 | board, plank | `d_a_obj_ita` |
| mato | 的 | target | `d_a_obj_mato` |
| saku | 柵 | fence | `d_a_obj_h_saku` |
| hasi / hashi | 橋 | bridge | `d_a_obj_ihasi` |
| kanban | 看板 | signboard | `d_a_obj_kanban2` |
| kago | 籠 | basket, cage | `d_a_obj_kago` |
| kage | 影 | shadow | `d_a_obj_kage` |
| taru | 樽 | barrel | `d_a_obj_gpTaru` |
| onsen | 温泉 | hot spring | `d_a_obj_onsen` |
| suisya | 水車 | water wheel | `d_a_obj_suisya` |
| maki | 薪 | firewood | `d_a_obj_maki` |
| tatigi | 立ち木 | standing tree | `d_a_obj_tatigi` |
| hasu | 蓮 | lotus | `d_a_obj_hasu2` |
| kaisou | 海草 | seaweed | `d_a_obj_kaisou` |
| ikada | 筏 | raft | `d_a_obj_ikada` |
| sekizo | 石像 | stone statue | `d_a_obj_sekizo` |
| saidan | 祭壇 | altar | `d_a_obj_saidan` |
| koya | 小屋 | hut, shed | `d_a_obj_hbombkoya` |
| hakai | 破壊 | destruction | `d_a_obj_hakai_brl` |
| izumi | 泉 | spring, fountain | `d_a_izumi_gate` |
| kekkai | 結界 | barrier, ward | `d_a_obj_lv8KekkaiTrap` |
| tenbin | 天秤 | balance scale | `d_a_obj_lv6Tenbin` |
| furiko | 振り子 | pendulum | `d_a_obj_lv6FurikoTrap` |
| toge | 棘 | spike | `d_a_obj_lv6TogeTrap` |
| seirei | 精霊 | spirit | `d_a_npc_seirei` |
| kakashi | 案山子 | scarecrow | `d_a_npc_kakashi` |
| saru | 猿 | monkey | `d_a_npc_saru` |
| jimaku | 字幕 | subtitles | `d_msg_scrn_jimaku.cpp` |
| nagaisu | 長椅子 | bench ("long chair") | `d_a_obj_nagaisu` |
| kantera | カンテラ | lantern *(itself a loanword)* | `d_a_obj_kantera` |

Add an entry when a session spends time working one out — that is the whole point
of keeping the list, and the checker will hold the symbol honest.

---

## 11. What this changes for the port and the mods

Stated as what it is: a lens, and **untested** unless it says otherwise.

- **The mods repo has one description the game contradicts.** `dusklight-mods`
  describes the register Effect Remover's terrain-shadow feature pins as "env fog
  density". It is the **cloud-shadow** density (§8). That is corrected in
  `dusklight-mods/docs/japanese-naming.md`, which also records the one thing the
  correction does *not* settle.
- **Coverage searches have been running in one alphabet.** Any past sweep of the
  form "find every place the game does X" is only as complete as its spelling. §3
  gives verified cases where a single spelling finds none of half a feature. How
  many past sweeps that affected is **not established**; the cheap move is to re-run
  a sweep both ways when its completeness actually matters.
- **The game names its own fakery, and our mods exist to remove it.** えせ (fake)
  vs 生 (raw) is the game's own distinction between its pretend point lights and its
  placed ones, and 影の濃さ / リアル影 are its own words for the shadow systems.
  Naming our features against the game's vocabulary rather than ours makes the
  triage in `dusklight-mods/docs/fake_shading_systems.md` checkable.
- **Named-by-meaning grouping is available and mostly unused.** The game tells us
  what a thing *is* in its material name — `Gake`, `Kusa`, `Enkei`, `Nami`,
  `Nigori`. `libs/JSystem/src/J3DGraphBase/J3DPacket.cpp` already pushes that name
  as a debug group at the real draw site, and `mMaterialName` itself is populated in
  **every** `TARGET_PC` build (only the `GXPushDebugGroup` call is `DEBUG`-gated).
  Whether it is worth reading is an open design question, not a plan.
- **Effect IDs are a translation dictionary we already ship.** 3,201 descriptive
  names in `src/d/d_particle_name.cpp`, covering the whole game, often romanized
  opposite to the C code. No session has mined them.

---

## See also

- `dusklight-mods/docs/japanese-naming.md` — the same lens applied to the mods, and
  the corrections it has already produced there
- `aurora-ao/docs/japanese-naming.md` — what of the game's naming survives into GX,
  and why aurora is where a baked TEV meaning becomes *readable*
- `docs/code-conventions.md` — how to mark Dusk changes inside game code
- [`zeldaret/tp`](https://github.com/zeldaret/tp) — the reference decompilation
