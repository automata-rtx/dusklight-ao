# The game's names are romanized Japanese

Every identifier in `src/d/`, `src/f_op/`, `src/f_pc/`, `src/m_Do/` and
`libs/JSystem/` is the original Japanese team's name, preserved 1:1 by the
decompilation. They are romaji — Japanese words in Latin letters — mixed with
abbreviated Japanese and English spelled by ear. **Read as English they produce
confident, wrong answers**, and that has cost this project real time.

Our own code (`src/dusk/`, aurora's `lib/dx9/`, the fork's `rtx_dusklight_*`) is
ordinary English `camelCase`. This document is about the code we *read*.

---

## 1. Romanization is inconsistent — this is the grep trap

There is no single romanization. The tree mixes **kunrei-shiki** (`si`, `tu`,
`ti`, `sya`, `zi`) with **Hepburn** (`shi`, `tsu`, `chi`, `sha`, `ji`) — and
does so **for the same word**:

| Word | In C code | In effect / asset IDs |
| :-- | :-- | :-- |
| 飛沫 spray | `dKyr_drawSibuki`, `setSibukiEffect` | `ZI_S_canoe_shibuki_a` and **68 more** — 69 of 3,201 effect names |
| 胞子 spore | `dKyr_housi_init`, `dKyw_drawHousi` | `ZI_J_houshiTest` |

**So the two halves of one feature are spelled differently, and either spelling
alone finds half the system.** Verified in this tree, not recalled.

**When a search comes back empty, try the other romanization before concluding
the thing does not exist.** For s/t/c sounds: `si↔shi`, `tu↔tsu`, `ti↔chi`,
`sya↔sha`, `zi↔ji`.

### ⚠ And set the locale first, or you will find none of the Japanese

```sh
export LC_ALL=C.UTF-8      # before any grep -P on kana or kanji
```

The default locale in a session container is `POSIX`. Under it, `grep -P` with a
kana/kanji character class matches **nothing** — no error, no warning, exit 1,
zero results. With the locale set, the same command returns 419 files from
`src/d` alone. Found by accident on 2026-08-10, and it is the most likely reason
the game's own labels went unread for this project's whole history: every search
came back empty and was read as "not there".

**An empty grep for Japanese is not evidence of absence until the locale is set.**

---

## 2. Misspelled English is a second naming system — do not correct it

Independently of the Japanese, the original names contain English spelled by
ear. These are **load-bearing identifiers, not defects**:

| In the tree | Intended | Where |
| :-- | :-- | :-- |
| `wether` | weather | `dKyw_wether_move`, the whole `d_kankyo_wether.cpp` |
| `Schejule` | schedule | `dKyd_lightSchejule` — the time-of-day light table |
| `Sord` | sword | `dKy_SordFlush_set` |
| `Blure` | blur | `SetBlureActor`, `ResetBlure` (the bloom path) |
| `parcent` | percent | `dKy_get_parcent` |
| `vectle` | vector | `dKyr_get_vectle_calc` |
| `resorce` | resource | `d_resorce.cpp` |
| `tresure` | treasure | `d_tresure` |

Renaming breaks the match with `zeldaret/tp`, which `docs/code-conventions.md`
asks us to upstream fixes to, and silently breaks anyone's grep. **If you need a
readable name, add a comment — never a rename.** The same applies to every game
symbol, misspelled or not.

`dalkmist` (dark mist) is the ninth, and it is the one worth a warning: it looks
like a light and is not. `DALKMIST_INFLUENCE` is position + radius + slot index
with **no colour and no intensity**, unlike the `LIGHT_INFLUENCE` declared beside
it, and its only reader is `d_a_kytag12.cpp`, where a sphere is a **hole in the
Palace of Twilight's fog that forces wolf form**. Two of the three actors that
register one are named `d_a_tag_lightball` and `d_a_obj_swLight`, which is
exactly the trap. Treating these as light sources would be inventing intent.

---

## 3. Short codes are Japanese initials, and many are simply unknown

`d_a_e_*` (enemies), `d_a_b_*` (bosses) and many `d_a_obj_*` names are two- or
three-letter codes: `d_a_e_rd`, `d_a_b_gnd`, `d_a_obj_so`. Some decode from
Japanese, some from English, **many are not known**. Do not reverse-engineer one
from its letters and build on the guess — decode it from what the file *does*,
and if the meaning is still not established, write "unknown".

---

## 4. The debug builds are labelled in Japanese, and that is a dictionary

496 files under `src/` and `include/` carry literal kana/kanji: the original
team's own HIO debug-panel labels, sitting one per field in declaration order.
**They are the most authoritative documentation in this tree** — a member name is
a decompiler's hypothesis, a label is the authors' word.

### A header name is not an authored name

The single most useful rule to come out of applying this document to our own
work, and it is not about Japanese at all:

> **Function and global-data symbols are the original team's. Struct member
> names are not.** The decompilation *reconstructs* them, so a member name is a
> **hypothesis until an authored string agrees with it** — an HIO slider label, a
> `dDbVw_Report` format, a CSV column header, an `OS_REPORT`.

The worked example started the audit: `vrbox_kasumi_inner_col` / `_outer_col`
were described backwards for months, and that was never a misread Japanese word —
it was a header name trusted like a function symbol. The game's own labels
(前 / 奥) settled it in minutes once anyone looked.

**Apply this opportunistically**, when you are already in a field. Not as a
sweep. Corollary from the same review: a `field_0x` member is **unaudited data,
not absent data** — 51 of the 233 declared members of `dScnKy_env_light_c` have
no name, so a search driven by names is structurally blind to them, including
two live light systems. Enumerate by offset when completeness matters.

### Three fields where the label contradicts the decomp's name

| Field | Label | Reading | What it actually is |
| :-- | :-- | :-- | :-- |
| `mFogDensity` | 雲影の濃さ (`d_kankyo.cpp:5058`) | *kumokage no kosa*, cloud shadow density | **Nothing to do with fog.** Blended from `cloud_shadow_density` and consumed by `dKy_cloudshadow_scroll`, which writes it into a TEV konstant on MA00/MA01/MA16 terrain and scrolls the painted cloud-shadow texture. Forced to −1 during wolf senses. |
| `mPow` | 影響範囲 (`d_kankyo.cpp:7898`) | *eikyou han'i*, range of influence | A **distance**, not an intensity. `mPow` reads in English as power, and the two are not interchangeable when converting to a path tracer's radiance. Why the code sides with the label: `src/dusk/effect_lights.cpp:1978-1980`. |
| `mDemoAttentionPoint` | 被写界深度 ／ 注目点 (`d_kankyo.cpp:7623-7625`) | *hisyakai-shindo*, depth of field | A depth-of-field **focus bias**, −1 at the back, 1 at the front — not the lock-on camera, which is `d_attention.cpp` and unrelated. `m_Do_graphic.cpp` maps it onto a TEV colour alpha and uses its **sign** to put the blur in front of the focus plane or behind it. |

### Two negatives, kept because a negative is expensive to re-derive

Ten of the effect-light classifier's thirty keywords match zero effect names, and
they are **not** romanization misses — every spelling of `taimatsu`, `kagaribi`
and `honoo` was re-checked in both systems and every one is zero, because the
game used English or a different Japanese word entirely. Do not "helpfully" add
spellings: `src/dusk/effect_lights.cpp:490-505` has the counts, the guard that
enforces them, and why deleting the dead words is not worth doing either.

And **the game has no emissive vocabulary at all** — 発光, 自発光, 輝き and 光量
return nothing across `src/`, `include/` and `libs/`, so there is no game-side
"this glows" flag to translate and the self-illumination rule is correctly built
out of GX state instead.

---

## 5. Reading a name changes decisions — the water case

Every one of these changed a call, and the name had said it all along:

| Name | Reading | What it meant |
| :-- | :-- | :-- |
| `cc_MA06_nami_v_x` | nami — wave | a wave pass, **not** interchangeable with the murk beside it |
| `cc_MA06_mizugiwa_v_x` | mizugiwa — water's edge | the shoreline |
| `cc_MA06_NigoriWater_v_x` | nigori — turbidity | the murky body |
| `cc_MA09_mera_v` | mera — shimmer | the shimmer pass |
| `ce_MA03_WaterKasan_v_x` | kasan (加算) — **addition** | an additively blended pass — and every material carrying it measured `SRC_ALPHA,ONE` |
| `cd_MA03_Funsui_v` | funsui — fountain | an object, not a lake layer |

Two lessons that generalise:

- **`kasan` is the case to remember.** The blend state was measured a session
  before anyone read the name. Reading the vocabulary first would have saved the
  measurement.
- **A numeric tag is coarser than the name.** `MA06` alone covers the waves, the
  shoreline *and* the murk; a control that cut on the tag was built, recommended,
  and would have deleted two of the three.

When adding a classifier over these names, match `_word` and `Word` (the
convention lowercases after the tag and capitalises inside a compound) rather
than a bare substring, so `minami` is not read as `nami` — and make
"unrecognised" mean **leave it alone**.

---

## 6. Working glossary

Terms this project's documents and code paths actually touch. **The symbols are
mechanically checked to exist by `scripts/check_invariants.py`; the readings are
our gloss.** Add an entry when a session spends time working one out.

### The environment system — what the Remix work drives

| Romaji | Japanese | Meaning | A symbol in this tree |
| :-- | :-- | :-- | :-- |
| kankyo | 環境 | environment | `d_kankyo.cpp`, `fopKy_IsKankyo` |
| kumo | 雲 | cloud | `vrbox_kumo_top_col`, `dKyw_drawVrkumo` |
| kasumi | 霞 | haze, the horizon band | `vrbox_kasumi_inner_col` |
| moya | 靄 | mist | `mMoyaCount` |
| housi | 胞子 | spore, drifting motes | `dKyr_housi_init` |
| sibuki | 飛沫 | spray, splash | `dKyr_drawSibuki` |
| yogan | 溶岩 | lava — the game does **not** spell it `lava` | (effect-light keyword) |
| kantera | カンテラ | lantern, itself a loanword | `d_a_obj_kantera` |
| maki | 薪 | firewood — the bonfire | `d_a_obj_maki` |
| yami | 闇 | darkness | `d_a_tag_yami` |
| vrbox | *(English, "VR box")* | the skybox dome | `d_a_vrbox.cpp` |
| kytag | 環境 + tag | per-area environment override actor | `kytag01_class` |

### Times of day

`l_time_attribute` blends six palette slots, and the game names and pins all six
itself: 朝０/朝１ *asa* morning, 昼 *hiru* **midday**, 夕０/夕１ *yuu* evening,
夜 *yoru* night. **昼 is the one that keeps being mistranslated** — it is midday,
not "afternoon" and not "noon": its pure window is 09:00–16:00 and its pin is
11:00. Corrected 2026-08-11 in three places; one gloss at `d_kankyo.cpp:6843`
still says "noon". The pins and the schedule are in `kankyo-remix.md` §I.1.

### Frequently met elsewhere

| Romaji | Japanese | Meaning | A symbol |
| :-- | :-- | :-- | :-- |
| hahen | 破片 | fragment, debris | `ep_hahen_s` |
| tubo | 壺 | pot, jar | `d_a_obj_oiltubo` |
| kage | 影 | shadow | `d_a_obj_kage` |
| taru | 樽 | barrel | `d_a_obj_gpTaru` |
| hasi / hashi | 橋 | bridge | `d_a_obj_ihasi`, `d_a_obj_thashi` |
| suisya | 水車 | water wheel | `d_a_obj_suisya` |
| tatigi | 立ち木 | standing tree | `d_a_obj_tatigi` |
| sekizo | 石像 | stone statue | `d_a_obj_sekizo` |
| kekkai | 結界 | barrier, ward | `d_a_obj_lv8KekkaiTrap` |
| seirei | 精霊 | spirit | `d_a_npc_seirei` |
| jimaku | 字幕 | subtitles | `d_msg_scrn_jimaku.cpp` |
| takara | 宝 | treasure | `d_a_obj_takaraDai` |

---

## 7. Rules

1. **Never rename a game symbol** to make it read as English, and never
   "correct" a misspelling. §2.
2. **Search in both romanizations** before concluding a thing does not exist,
   and `export LC_ALL=C.UTF-8` before searching for kana or kanji. §1.
3. **Gloss a name the first time a document uses it**, then use it bare. A
   reader who does not know the word cannot look it up — it is not English.
4. **A member name is a hypothesis; an authored label is evidence.** §4.
5. **Do not romanize anything new.** The convention applies to the code we read.
