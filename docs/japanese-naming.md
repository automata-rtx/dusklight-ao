# The game's names are romanized Japanese

Twilight Princess was written by a Japanese team, and the decompilation this
port is built from (`zeldaret/tp`) reproduces the original build 1:1 — including
its **symbol names**. So the identifiers in `src/d/`, `src/f_op/`, `src/f_pc/`,
`src/m_Do/` and `libs/JSystem/` are not English names that happen to look odd.
They are **Japanese words written in Latin letters** (romaji / "Romanji"),
abbreviated Japanese, and — separately — misspelled English.

`dKyr_drawSibuki` is not a typo for anything. It draws 飛沫 *shibuki*, spray.

**This file is the canonical statement.** The other documents in the three repos
point here rather than re-explaining it.

---

## 0. Why this is worth a document

Every session that reads game code hits this, and reading a name as English
produces confident, wrong answers. Three failure modes, all cheap to avoid once
you know the rule:

1. **Searching in the wrong alphabet.** A grep for `shibuki` finds the effect
   IDs and misses the function that draws them. §3.
2. **"Fixing" a name.** `dKyw_wether_move` looks like a misspelling to correct.
   It is the name of the weather system — 314 occurrences across 29 files here,
   plus the reference decomp. §4.
3. **Inventing an etymology.** `d_a_ep` is not "environment particle". Nobody
   here knows what `ep` stands for, and a document that says "unknown" is worth
   more than one that guesses — project rule 3.

**What is verified and what is gloss.** Every symbol named in this file was
checked to exist in this tree, mechanically, by
`python3 scripts/check_invariants.py` (`japanese-naming` check) — so the
glossary cannot rot silently as the port moves. **The Japanese readings and
meanings are our gloss, not something read out of the tree**; they are here
because they are useful, not because the codebase asserts them. Where a reading
is uncertain it says so.

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
| `f_pc_` | framework **p**ro**c**ess control — create/execute/draw/delete | `f_pc_draw.cpp` (`fpcDw_Execute`), `f_pc_creator.cpp` |
| `m_Do_` | the machine layer — graphics, audio, controller, memory card | `m_Do_graphic.cpp`, `mDoExt_modelEntryDL` |
| `J…` | **JSystem**, Nintendo's shared middleware, in `libs/JSystem/` | `J3D*` (3D), `J2D*` (2D), `JKR*` (heaps), `JUT*` (utility), `JPA*` (particles), `JAI/JA*` (audio) |
| `c_` | common helpers | `cXyz`, `cM_`, `cLib_` |

Function prefixes compress the same idea: `fopAcM_` = f_op **Ac**tor **M**anager,
`fopKy_` = f_op **Ky**(ankyo), `fpcDw_` = f_pc **Dr**a**w**, `dComIfGp_` =
d_com_inf **G**ame **p**rocess (the `dComIfG*` family is the global-access
façade: `Gp_` ~13.7k call sites, `Gs_` save data, `Gd_` draw lists).

**`Ky` is `kankyo` is 環境 is "environment".** That single abbreviation carries
most of what this project cares about: `dKy_` (core), `dKyw_` (wether/weather),
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

All six are in the environment system this project drives. They are not
obscure corners: `kumo` and `kasumi` are the skybox colour fields that
`DusklightAtmosphere.md` reads, and `housi` was one of the particle systems in
the ~1000-draws-a-frame problem.

---

## 3. Romanization is inconsistent — this is the grep trap

There is no single romanization. The tree mixes **kunrei-shiki** (`si`, `tu`,
`ti`, `sya`, `zi`) with **Hepburn** (`shi`, `tsu`, `chi`, `sha`, `ji`) — and
does so **for the same word**:

| Word | In C code | In effect/asset IDs |
| :-- | :-- | :-- |
| 飛沫 spray | `dKyr_drawSibuki`, `setSibukiEffect`, `mSibukiAlpha` | `ZI_S_canoe_shibuki_a`, `ZI_S_lk_takishibuki_a`, **+67 more** — 69 of the 3201 effect names |
| 胞子 spore | `dKyr_housi_init`, `dKyw_drawHousi`, `mHousiCount` | `ZI_J_houshiTest`, `ZI_S_pz_BodyHoushi_a` |

So the two halves of one feature are spelled differently, and **either spelling
alone finds half the system.** Verified in this tree, not recalled.

Other kunrei spellings you will meet: `suisya` (水車 water wheel), `tatigi`
(立ち木 standing tree), `msima` / `zant_sima` (島 shima, island), `ihasi`
(橋 hashi, bridge), `tubo` (壺 tsubo, pot — hence `ootubo`/`kotubo`, big/small
pot). Hepburn shows up in the same directory: `katatsumuri` (snail),
`kamakiri` (mantis), `d_meter_hakusha` (拍車 spur).

**Practical rule: when a search comes back empty, try the other romanization
before concluding the thing does not exist.** For s/t/c sounds that is
`si↔shi`, `tu↔tsu`, `ti↔chi`, `sya↔sha`, `zi↔ji`.

---

## 4. Misspelled English is a second naming system

Independently of the Japanese, the original names contain English spelled by
ear. These are **load-bearing identifiers**, not defects:

| In the tree | Intended | Where |
| :-- | :-- | :-- |
| `wether` | weather | `dKyw_wether_move`, `S_wether_table`, the whole `d_kankyo_wether.cpp` file |
| `Schejule` | schedule | `dKyd_lightSchejule` — the time-of-day light table |
| `Sord` | sword | `dKy_SordFlush_set` |
| `Blure` | blur | `SetBlureActor`, `ResetBlure` (the bloom path) |
| `parcent` | percent | `dKy_get_parcent` |
| `vectle` | vector | `dKyr_get_vectle_calc` |
| `resorce` | resource | `d_resorce.cpp` |
| `tresure` | treasure | `d_tresure`, `mp_tresure` |
| `dalkmist` | dark mist | `DALKMIST_INFLUENCE`, `dKy_dalkmist_inf_set`, `dalkmist_influence` |

> #### `dalkmist` — and why it must not be piped to Remix as a light
>
> The **dark-mist volumes**: up to ten spheres (`d_kankyo.h:258`), each a
> position and a radius, that **hold the Palace of Twilight's fog off**. The
> reading is not a guess — the stage object name the game registers the actor
> that reads them under is **`Drkmst`** (`d_stage.cpp:954`, beside
> `fpcNm_KYTAG12_e`). That the spelling is *dark* heard as *dalk* is our gloss;
> the abbreviation is the game's. *(The fog is the Palace of Twilight's: the
> sound effects are `Z2SE_OBJ_L8_B_*` and the bloom panel names row 11
> 「LV8闇の宮殿」, LV8 Palace of Darkness, `d_kankyo.cpp:5083`. That the level
> the SE prefix names is the same one as the stage string `"D_MN08"` is
> inference from the shared 8 — nothing in the tree states it.)*
>
> Verified 2026-08-11, all of it:
>
> - **One reader, `d_a_kytag12.cpp`** — the same ten-slot loop, copied into each
>   of its three execute variants: `daKytag12_Execute_standard` (`:280`),
>   `_arrival` (`:588`) and `_R00` (`:891`). A fog particle inside any sphere gets
>   `mStatus = 2` and starts `Z2SE_OBJ_L8_B_FOG_FLY`. That state excludes it
>   from the player-proximity test at `:337`, which is what feeds
>   `onForceWolfChange()` at `:396`. So a sphere is a **hole in the fog that
>   forces wolf form** — which is what the SE name says it is: *fog fly*.
> - **It is not a light, and the struct says so.** `DALKMIST_INFLUENCE`
>   (`d_kankyo.h:58-63`) is position + radius + slot index. **No colour, no
>   intensity** — unlike `LIGHT_INFLUENCE` and `DUNGEON_LIGHT` declared beside
>   it. Nothing in the game reads a dalkmist sphere for illumination.
> - **None of the three registering actors registers a light.** `dKy_plight_set`
>   does not appear in `d_a_tag_lightball.cpp`, `d_a_obj_swLight.cpp` or
>   `d_a_obj_carry.cpp` — grep over the three files returns nothing.
> - **They are puzzle state, not scene state.** The tag actor gates its sphere
>   on `fopAcM_isSwitch` (`d_a_tag_lightball.cpp:30-45`, `:58-81`) and draws
>   nothing. *(Precisely: only that one gates on a switch directly. The lit
>   candlestick registers when its own radius becomes non-zero
>   — `d_a_obj_swLight.cpp:236-243`, driven by its switch-on/switch-off modes —
>   and the carryable light ball registers unconditionally at creation,
>   `d_a_obj_carry.cpp:1237-1247`.)*
>
> **So treating these as light sources would be inventing intent.** The name
> contains "light" in two of the three registering actors — `tag_lightball`,
> `obj_swLight` — and that is exactly the trap: what those actors hand the
> environment system is a *fog exclusion radius*, and the game never gives it a
> colour to be wrong about.

**Do not correct them.** Renaming breaks the match with `zeldaret/tp`, which
`docs/code-conventions.md` asks us to upstream fixes to, and silently breaks
anyone's grep. If you need a readable name, add a comment — not a rename.

---

## 5. Short codes are Japanese initials

`d_a_e_*` (enemies), `d_a_b_*` (bosses) and many `d_a_obj_*` names are
two-or-three-letter codes: `d_a_e_rd`, `d_a_b_gnd`, `d_a_obj_so`. Some decode
from Japanese names, some from English ones, and **many are simply not known**.

Do not reverse-engineer one from its letters and then build on the guess. Decode
it from what the file *does* (§7) and, if the meaning still is not established,
write "unknown".

---

## 6. The debug builds are labelled in Japanese, and that is a dictionary

> ### ⚠ Set the locale first, or you will find none of it
>
> ```sh
> export LC_ALL=C.UTF-8      # before any grep -P on Japanese
> ```
>
> The default locale in a session container is `POSIX`. Under it, `grep -P` with a
> kana/kanji character class matches **nothing** — no error, no warning, exit 1,
> zero results. The same command with the locale set returns 419 files from
> `src/d` alone. This was found by accident on 2026-08-10, and it is the most
> likely reason the game's own labels went unread for this project's whole
> history: every search for them came back empty and was read as "not there".
>
> An empty grep for Japanese is not evidence of absence until the locale is set.
> This is the §3 rule again, one level lower down.

**496 files under `src/` and `include/` contain literal kana/kanji** — not
romanized, actual Japanese. It is in the HIO debug sliders, the developer
overlay, the CSV exporters and a great many `OS_REPORT` strings:

```
d_kankyo.cpp:7809      mctx->genSlider("最大数",  &housi_max_number, 0, 1000);
                                        ^ "maximum count"
f_op_actor_mng.cpp     "fopAcM_entrySolidHeap 開始 [%s] 見積もりサイズ=%08x\n"
                                                 ^ "start"  ^ "estimated size"
```

This is the best translation source in the tree, because it is the original
authors labelling their own fields. **When a name is ambiguous, find where the
debug menu prints it.**

> ### Worked example: which `kasumi` is which
>
> `vrbox_kasumi_inner_col` and `vrbox_kasumi_outer_col` are two horizon haze
> bands. Nothing about "inner" and "outer" says which is nearer, and guessing
> from the English is how this got recorded wrongly once (§10). The game answers
> it in three independent places:
>
> 1. **The palette CSV exporter** (`d_kankyo.cpp:6582`) writes a Japanese header
>    row and then the fields in the same order. Aligning them column by column:
>    `空色`→`sky_col`, `上雲色`(upper cloud)→`kumo_top`, `下雲色`(lower
>    cloud)→`kumo_bottom`, `下雲影色`(lower cloud shadow)→`kumo_shadow`,
>    then **`霞手前色` (*temae*, near/in-front) → `kasumi_outer`** and
>    **`霞奥色` (*oku*, far/behind) → `kasumi_inner`**.
> 2. **The debug view** (`d_kankyo_debug.cpp:301,306`) prints the same fields as
>    **`kasumiF`** and **`kasumiB`** — Front and Back. Its neighbours use the
>    same one-letter scheme (`CloudU`/`CloudD` for `kumo_top`/`kumo_bottom`), so
>    the letters are positional, not incidental.
> 3. **The dome actors**: `d_a_vrbox.cpp:121` paints `kasumi_inner` onto
>    `vrbox_sora.bmd` (*sora* = 空, sky), and `d_a_vrbox2.cpp:358` paints
>    `kasumi_outer` onto the second dome. Two shells, front and back.
>
> So **`outer` is the near band and `inner` is the far one** — the opposite of
> what the English words suggest. No code path anywhere references sun position.
> The same exporter also settles a smaller question: the `下雲α` column and the
> debug view's `Cloud A` both print `kumo_top_col.a`, so that alpha is the cloud
> *layer's*, not the top band's alone.

## 7. Decoding a name you have not seen

In rough order of reliability:

1. **Find where a debug build prints it** (§6). The game labelling its own field
   beats every other method.
2. **Read the header.** `include/d/actor/d_a_*.h` carries the class name and the
   struct layout; a field list usually settles what the actor is faster than the
   name does.
3. **Find the profile / actor name.** Actors are registered with a profile whose
   name string often survives in the data — grep the identifier in
   `src/d/d_stage.cpp` and the profile tables.
4. **Look at the effect IDs it uses.** These are long and descriptive
   (`ZI_S_lk_takishibuki_a`), and — usefully — they are frequently romanized the
   *other* way from the C function, so they double as a translation hint.
5. **Try both romanizations** (§3), then try it as misspelled English (§4).
6. **Check the reference decomp.** `zeldaret/tp` has years of naming work and
   issue discussion that this port inherits but does not restate.

**One caution when reading logs.** Because the game's own diagnostic strings are
Japanese, a game-side `OS_REPORT` or assertion can put Japanese in a log the
owner sends us. That is the game talking, not a corrupted file.

---

## 8. Working glossary

Terms this project's own documents and code paths actually touch. **Symbols are
mechanically checked to exist; the readings are our gloss.**

### The environment system (what the Remix work drives)

| Romanji | Japanese | Meaning | A symbol in this tree |
| :-- | :-- | :-- | :-- |
| kankyo | 環境 | environment | `d_kankyo.cpp`, `fopKy_IsKankyo` |
| kumo | 雲 | cloud | `vrbox_kumo_top_col`, `dKyw_drawVrkumo` |
| kasumi | 霞 | haze (horizon band) | `vrbox_kasumi_inner_col` |
| moya | 靄 | mist / haze | `mMoyaCount`, `mMoyaMode` |
| housi | 胞子 | spore (drifting motes) | `dKyr_housi_init` |
| sibuki | 飛沫 | spray / splash | `dKyr_drawSibuki` |
| itudemo | いつでも | "any time", always-on | `dKy_itudemo_se` |
| wether | *(English, by ear)* | weather | `dKyw_wether_move` |
| vrbox | *(English, "VR box")* | the skybox dome | `vrbox_class`, `d_a_vrbox.cpp` |
| kytag | 環境 + tag | per-area environment override actor | `kytag01_class` |

### The six canonical time lights — the game names *and* pins all six

`l_time_attribute` (`d_kankyo_data.cpp:212`) blends the environment between
**six** time-of-day palette slots. The game names each one in Japanese in four
independent places, and the debug time-fix menus additionally pin each to one
exact `daytime` — a value chosen so the schedule lands on that slot with **no
blend**, which makes them the game's own answer to "show me slot N".

| Slot | Romanji | Japanese | Meaning | Pinned `daytime` | Clock |
| :-- | :-- | :-- | :-- | :-- | :-- |
| 0 | asa 0 | 朝０ | morning 0 | 90 | 06:00 |
| 1 | asa 1 | 朝１ | morning 1 | 105 | 07:00 |
| 2 | hiru | 昼 | **midday** | 165 | 11:00 |
| 3 | yuu 0 | 夕０ | evening 0 | 255 | 17:00 |
| 4 | yuu 1 | 夕１ | evening 1 | 285 | 19:00 |
| 5 | yoru | 夜 | night | 345 | 23:00 |

`daytime` runs 0–360 over 24 hours, so 15 units is an hour. The four surfaces,
all verified 2026-08-11: the two time-fix combo boxes (`d_kankyo.cpp:6785-6795`
and `:8064-8069`, whose values feed the switch in
`dScnKy_env_light_c::setDaytime` at `:1641-1658` — that switch *is* the pin),
plus the bloom panel naming its entries 4–9 after the same six slots
(`:5076-5081` and `:6836-6856`, フィールド基準（朝０）… "field standard (morning 0)").

**昼 *hiru* is the one that keeps being mistranslated.** It is **midday**, not
"afternoon" and not "noon": its pure window in `l_time_attribute` is 135–240,
i.e. **09:00–16:00**, and its pin is 11:00. Corrected 2026-08-11 in three
places — `d_kankyo.cpp`'s time-fix combo (whose six comments also had 朝０ as
"midnight" and 夕０ as "noon", off by six and five hours), `d_s_menu.cpp`'s
「ひる固定」 ("Fixed Afternoon"), and `kankyo-remix.md` ("2 afternoon").
**One is still open**, deliberately left because it sits outside that edit's
scope: the bloom-panel gloss at `d_kankyo.cpp:6843` renders フィールド基準（昼）
as "Field standard (noon)". Same word, same fix, one line.

### The bloom is 飽和加算 and the mono colour is 彩度減算

Our documents call this system "the bloom" and "the mono colour". The game has
its own names for both, and — as with `kasan` in the water work — the names
carry the mechanism.

| Romanji | Japanese | Meaning | Where the game says it |
| :-- | :-- | :-- | :-- |
| houwa-kasan | 飽和加算 | **saturating add** — the bloom system as a whole | the HIO node is 飽和加算設定 "saturating-add settings" (`d_kankyo.cpp:8182`); its CSV export filter is 飽和加算ファイル (`:6720`); its buffer allocation reports 飽和加算用にＲＡＭを確保しました (`:8350`) |
| saido-gensan | 彩度減算 | **saturation subtraction** — what our docs call the *mono colour* | the four sliders labelled 彩度減算 R/G/B/A (`d_kankyo.cpp:7089-7092`) |
| kukkiri | くっきり | **crisp** — `BLOOM_CLEAR`, type 0 | `d_kankyo.cpp:7079` |
| yawaraka | やわらか | **soft** — `BLOOM_SOFT`, type 1 | `d_kankyo.cpp:7080` |
| aki | 空き | **vacant** — an unused table row | `d_kankyo.cpp:6946-7074` labels 31–39 and 46–63 this way |

`飽和` (*houwa*, saturation-as-in-clipping) and `彩度` (*saido*,
saturation-as-in-colourfulness) are **different words for different things**,
and English collapses them into one. That is the whole trap in this struct, and
it is spelled out below.

#### The authors' own sliders settle every field

`dKankyo_bloomHIO_c::genMessage` builds one slider per member of
`dkydata_bloomInfo_info_class`, in declaration order, at
**`d_kankyo.cpp:7078-7092`**. This is a primary source in the §6 sense: the
original team labelling their own fields.

| Slider | Reading | Member | What it drives (verified `d_kankyo.cpp:2574-2658`, `m_Do/m_Do_graphic.cpp:1456-1708`) |
| :-- | :-- | :-- | :-- |
| タイプ ／ くっきり(0)・やわらか(1) | type / crisp・soft | `mType` | non-zero on any of the four blended rows picks the soft composite — `GX_BL_INVDSTCLR` instead of `GX_BL_ONE` (`:2648-2658` → `m_Do_graphic.cpp:1703`) |
| しきい値 | *shikiichi*, threshold | `mThreshold` | `setPoint`; at 0xFF the bloom is switched off entirely (`:2642`) |
| ぼやけ幅 | *boyake haba*, blur **width** | `mBlurAmount` | `setBlureSize` |
| ぼやけ濃さ | *boyake kosa*, blur **density** | `mDensity` | `setBlureRatio` |
| 濃さ R／G／B | density R/G/B | `mColorR`, `mColorG`, `mColorB` | the bloom tint — `setBlendColor()` RGB |
| 元濃さ | *moto no kosa*, the **original's** density | `mOrigDensity` | `setBlendColor()` **alpha** |
| 彩度減算 R／G／B／A | saturation subtraction | `mSaturateSubtractR`…`A` | `setMonoColor`; A is both the strength and the on/off gate (`m_Do_graphic.cpp:1566`) |

**元濃さ answers the `// ?` the decomp left on `mOrigDensity`**
(`include/d/d_kankyo_data.h`). *Moto* is "the original/base", so the label reads
"the original's density" — and the code agrees without needing the label: the
field is written into the **alpha** of the bloom blend colour (`:2614-2618`),
and the composite passes that alpha as the **destination** factor of
`GXSetBlendMode(GX_BM_BLEND, …, GX_BL_SRCALPHA, …)` (`m_Do_graphic.cpp:1696-1704`).
The destination is the frame already drawn, so the number is how much of the
un-bloomed image survives the composite: 0xFF keeps it whole, and Twilight's
0xD2 dims the whole scene to 82 %. **The member name is upstream's and stays**
(§4, rule 1); only the comment beside it gained the answer.

That takes `mOrigDensity` off §8b's list of members carrying an unchecked
semantic claim. **The list itself is deliberately not edited here** — the
unmerged `claude/kasumi-naming-correction-w3e204` rewrites that same sentence,
and resolving two edits to one line is exactly the merge trap `CLAUDE.md`
describes. Strike it when the two land.

#### The trap: `m_saturationPattern` is not one of the `mSaturateSubtract*`

In English these read as one family. They have nothing to do with each other,
and they are not even in the same struct:

- **`m_saturationPattern`** (`dKankyo_bloomHIO_c`, `d_kankyo.cpp:5071`) is a
  **row number** — the panel's ■飽和パターン combo, i.e. *which of the 64
  entries of `l_kydata_BloomInf_tbl` is in force*. Its 飽和 is the bloom
  system's own name. Normally the game writes the current palette's
  `bloom_tbl_id` into it as a readout, and only overrides from it when the
  HOSTIO setting flag is on (`:2533-2538`, DEBUG only).
- **`mSaturateSubtractR/G/B/A`** are the 彩度減算 amounts — a colour and a
  strength for the full-screen desaturating overlay, stored **inside** one row.

One selects a grade; the other is a number within a grade. Nothing converts
between them, and "saturation" is a different Japanese word in each.

#### One negative result, recorded so nobody re-derives it

The panel at `d_kankyo.cpp:7477-7485` ("トワイライト センス専用飽和実験",
*a saturation experiment for wolf senses*) offers four bloom rows — 32, 33, 34,
35 — that look like author-made grades. **They are unreachable, and this was
checked rather than assumed.** The DEBUG assignment that would select them
(`:2540-2542`) is gated on `checkNowWolfPowerUp()`; four lines later, **outside
the `#endif`**, `:2545-2547` runs under that same condition and overwrites all
four table ids with `3`. `checkNowWolfPowerUp` is a pure getter
(`d_a_player.h:1187` → `d_a_alink.h:3543`), so it cannot differ between the two
calls, and nothing between them reads the ids. The override is dead on arrival
in every build. The panel's own per-row labels agree: rows 31–39 are all 空き,
*vacant* (`:6944-6978`) — 32–35 are not a distinguishable set, just four of a
run of scratch slots that still hold leftover numbers. A comment on `:2540` now
says so in place.

**Nothing is proposed off the back of this**, and specifically not a way to
force a bloom-table row from our side: Dusk already overrides the bloom at the
*output* end (the ImGui bloom window writes the blended result straight into
the bloom object each frame), which is strictly more direct than picking a row.
The value here is the negative — the panel exists, it looks usable, and it is
not.

### Frequently met elsewhere

| Romanji | Japanese | Meaning | A symbol in this tree |
| :-- | :-- | :-- | :-- |
| hahen | 破片 | fragment, debris | `ep_hahen_s` |
| tubo | 壺 | pot, jar | `d_a_obj_oiltubo`, `bg_damage_proc_ootubo` |
| ita | 板 | board, plank | `d_a_obj_ita` |
| mato | 的 | target | `d_a_obj_mato`, `d_a_obj_itamato` |
| saku | 柵 | fence | `d_a_obj_h_saku`, `d_a_obj_sakuita` |
| hasi / hashi | 橋 | bridge | `d_a_obj_ihasi`, `d_a_obj_thashi` |
| kanban | 看板 | signboard | `d_a_obj_kanban2`, `d_msg_scrn_kanban` |
| kago | 籠 | basket, cage | `d_a_obj_kago` |
| kage | 影 | shadow | `d_a_obj_kage` |
| taru | 樽 | barrel | `d_a_obj_gpTaru`, `d_a_obj_onsenTaru` |
| onsen | 温泉 | hot spring | `d_a_obj_onsen` |
| suisya | 水車 | water wheel | `d_a_obj_suisya` |
| maki | 薪 | firewood | `d_a_obj_maki` |
| ki | 木 | tree | `d_a_obj_ki` |
| tatigi | 立ち木 | standing tree | `d_a_obj_tatigi` |
| hasu | 蓮 | lotus | `d_a_obj_hasu2` |
| kaisou | 海草 | seaweed | `d_a_obj_kaisou` |
| ikada | 筏 | raft | `d_a_obj_ikada` |
| turara | 氷柱 | icicle | `d_a_obj_Turara` |
| sekizo | 石像 | stone statue | `d_a_obj_sekizo` |
| saidan | 祭壇 | altar | `d_a_obj_saidan` |
| koya | 小屋 | hut, shed | `d_a_obj_hbombkoya` |
| hakai | 破壊 | destruction | `d_a_obj_hakai_brl` |
| izumi | 泉 | spring, fountain | `d_a_izumi_gate` |
| kekkai | 結界 | barrier, ward | `d_a_obj_lv8KekkaiTrap` |
| tenbin | 天秤 | balance scale | `d_a_obj_lv6Tenbin` |
| furiko | 振り子 | pendulum | `d_a_obj_lv6FurikoTrap` |
| toge | 棘 | spike | `d_a_obj_lv6TogeTrap` |
| taihou | 大砲 | cannon | `d_a_obj_Y_taihou` |
| yami | 闇 | darkness | `d_a_tag_yami` |
| seirei | 精霊 | spirit | `d_a_npc_seirei` |
| yousei | 妖精 | fairy | `d_a_obj_yousei` |
| kakashi | 案山子 | scarecrow | `d_a_npc_kakashi` |
| saru | 猿 | monkey | `d_a_npc_saru` |
| inko | インコ | parakeet | `d_a_npc_inko` |
| sumou | 相撲 | sumo wrestling | `d_a_alink_sumou.inc` |
| jimaku | 字幕 | subtitles | `d_msg_scrn_jimaku.cpp` |
| nagaisu | 長椅子 | bench ("long chair") | `d_a_obj_nagaisu` |
| kantera | カンテラ | lantern *(itself a loanword)* | `d_a_obj_kantera` |
| takara | 宝 | treasure | `d_a_obj_takaraDai` |

Add an entry when a session spends time working one out — that is the whole
point of keeping the list, and the invariants check will hold the symbol honest.

---

## 8b. A header name is not an authored name

The single most useful rule to come out of applying this document to our own work, and it
is not about Japanese at all.

> **Function and global-data symbols are the original team's. Struct member names are
> not.** The decompilation *reconstructs* member names, so a member name is a **hypothesis
> until an authored string agrees with it** — an HIO slider label (§6), a `dDbVw_Report`
> format, a CSV column header, an `OS_REPORT`.

The worked example is the one that started the audit. `vrbox_kasumi_inner_col` /
`_outer_col` were described backwards for months, and **that was never a misread Japanese
word** — it was a header name trusted like a function symbol, when the header name is the
one artifact in the chain no Japanese developer wrote. The game's own labels
(前 / 奥, `kasumiF` / `kasumiB`) settled it in minutes once anyone looked.

Names still carrying an unchecked semantic claim: `mFogDensity` (the label says 雲影の濃さ,
cloud-shadow density), `mOrigDensity`, `kumo_top_col` / `kumo_bottom_col`, and
`dungeonlight_col` — whose palette source is spelled `plight_col`.

**Apply this opportunistically**, when you are already in a field. Not as a sweep.

Corollary, from the same review: a `field_0x…` member is **unaudited data, not absent
data**. 51 of the 233 declared members of `dScnKy_env_light_c` have no name, and a search
driven by names is structurally blind to every one of them — including two live light
systems. Enumerate by offset when completeness matters.

---

## 9. Rules

1. **Never rename a game symbol** to make it read as English. §4.
2. **Never assert a meaning you have not established.** "Unknown" is a finding;
   a plausible-looking guess is a liability the next session inherits. This is
   project rule 3 applied to names.
3. **Gloss on first use in a document**, then use the bare name — the way
   `kankyo-remix.md` opens with *(kankyo = environment)*. A reader who does not
   know the word cannot look it up, because it is not English.
4. **Our own port code is different.** `src/dusk/`, aurora's `lib/dx9/` and the
   fork's `rtx_dusklight_*` are ours and use ordinary English `camelCase`. The
   romanji convention applies to the game code we *read*, not to the code we
   *write*. Do not romanize anything new.

---

## 10. What this changes about the Remix effort

### It has already found one wrong claim

Applying §6 to the first ambiguous pair in the atmosphere data turned up a
description in the fork that the game contradicts. Recorded because it is the
evidence that this lens pays, and because the correction is small:

`rtx_dusklight_env.h` described `kasumiInner` as "the horizon haze colour **on
the sun's side**" and `kasumiOuter` as "**away from the sun**". **No code path
in the game relates either field to sun position**, and three independent
game-authored labels say the split is front/back instead — the CSV exporter's
`霞手前`/`霞奥`, the debug view's `kasumiF`/`kasumiB`, and the two dome actors
painting one band each (§6). The descriptions were corrected to what the game
says; `RtxOptions.md` is generated, so it carries the old wording until someone
regenerates it on Windows.

**Corrected to "front/near" and "back/far", not to a claim about what they look
like.** Whether the near band is *also* the one that visibly carries sunrise —
which is what the old description was probably reaching for — is a question
about palette content, and nobody has checked it. It stays open rather than
being restated in nicer words.

### And it suggests three things nobody has done

Stated as what they are: a lens, and **untested** unless they say otherwise.

- **Coverage searches have been running in one alphabet.** Any past sweep of the
  form "find every place the game does X" is only as complete as its spelling.
  §3 gives one verified case where a single spelling finds half a feature; how
  many past sweeps that affected is **not established**, and the cheap move is
  to re-run a sweep both ways when its completeness actually matters.
- **Named-by-meaning grouping is available and mostly unused.** The game tells
  us what a thing *is* in its symbol — `kumo`, `moya`, `housi`, `sibuki` are
  cloud, haze, spore, spray. That is exactly the per-draw intent that rule 1,
  "translate, don't tag", wants to hand to Remix, and it is more reliable than
  any texture hash. Whether it is worth wiring is an open design question, not a
  plan.
- **Effect IDs are a translation dictionary we already ship.** They are
  descriptive, they cover the whole game, and they are often romanized opposite
  to the C code (§3). No session has mined them.

---

## See also

- [`japanese-naming-audit.md`](japanese-naming-audit.md) — **what applying this lens to
  the work already on `Fixed-Function-dev` found.** Defects, game data we never picked
  up, and an explicit list of things that are fine and should be left alone.
- [`japanese-naming-worklist.md`](japanese-naming-worklist.md) — ready-to-paste session
  prompts for the work that audit produced
- [`kankyo-tuning-surface.md`](kankyo-tuning-surface.md) — **§6 applied to the
  environment system.** Every one of the 340 labelled bindings in the kankyo debug panel,
  extracted mechanically: the label, the field, the range, and whether it reaches Remix.
  Three labels that contradict the field name they are bound to, and the honest limits —
  the panel is compiled out of every build, and only 62 of the bindings touch live state.
- `docs/code-conventions.md` — how to mark Dusk changes inside game code
- `docs/kankyo-remix.md` — the environment system this vocabulary describes
- [`zeldaret/tp`](https://github.com/zeldaret/tp) — the reference decompilation
