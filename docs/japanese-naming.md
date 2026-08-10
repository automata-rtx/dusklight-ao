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
| 飛沫 spray | `dKyr_drawSibuki`, `setSibukiEffect`, `mSibukiAlpha` | `ZI_S_canoe_shibuki_a`, `ZI_S_lk_takishibuki_a`, +27 more |
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

- `docs/code-conventions.md` — how to mark Dusk changes inside game code
- `docs/kankyo-remix.md` — the environment system this vocabulary describes
- [`zeldaret/tp`](https://github.com/zeldaret/tp) — the reference decompilation
