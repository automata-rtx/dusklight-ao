# Claude session notes — dusklight-ao (`main`)

This fork's `main` is **the mod platform**: pristine upstream Dusklight plus the
release-publishing job and the `extern/aurora` repoint to our enlarged-buffer
aurora fork. `dusklight-mods` pins a commit of it as `DUSKLIGHT_VERSION` and
compiles against the SDK fetched from here.

**Keep it pristine apart from those deltas.** Documentation and read-only tooling
(`docs/`, `tools/`) are fine; changes to `src/`, `libs/`, the build system or
`build.yml`'s existing jobs are a re-platforming decision, not a side effect of
another task.

## The game's code is named in Japanese

**Every identifier in `src/d/`, `src/f_op/`, `src/f_pc/`, `src/m_Do/` and
`libs/JSystem/` is the original Japanese team's name, preserved 1:1 by the
decompilation.** They are romaji — Japanese words in Latin letters — mixed with
abbreviated Japanese and English spelled by ear. Read as English they produce
confident, wrong answers, and this has already cost real time on this project.

`kankyo` (環境) is *environment*. `dKyr_drawSibuki` draws 飛沫 *shibuki*, spray.
`dKyw_wether_move` is the **weather** system and `wether` is not a typo to fix.
`d_a_ep` does not stand for anything anybody here has established.

**`docs/japanese-naming.md` is the full reference**, and every game symbol it names
is machine-checked by `tools/check_japanese_naming.py`. Five things to internalise
now:

- **Search in both romanizations.** The tree mixes kunrei-shiki (`si`, `tu`, `ti`,
  `sya`) with Hepburn (`shi`, `tsu`, `chi`, `sha`) **for the same word** — spray is
  `Sibuki` in the C functions (21 hits) and `shibuki` in the effect-ID table (69
  hits, and zero `sibuki`). Either spelling alone finds none of the other half. An
  empty search is not evidence of absence until you have tried the other spelling.
- **Never rename a game symbol**, and never "correct" one of the misspellings
  (`wether`, `Schejule`, `Sord`, `Blure`, `parcent`, `vectle`, `resorce`,
  `tresure`). They are load-bearing across the tree and across `zeldaret/tp`, which
  `docs/code-conventions.md` asks us to upstream fixes to.
- **Search Japanese with ripgrep, not `grep -P`.** 496 files under `src/` and
  `include/` contain literal kana/kanji — the original team's own debug-panel
  labels, which are the most authoritative documentation in this tree. This
  container's locale is `POSIX`, and under it `grep -P '\p{Han}'` silently matches
  **nothing** while a raw-character class silently matches **too much** (507 files,
  11 of them false positives). `rg` — and the Claude Code `Grep` tool, which is
  ripgrep — is correct under either locale. If you must use `grep -P`, `export
  LC_ALL=C.UTF-8` first. Full table in `docs/japanese-naming.md` §6.
- **A header field name is not an authored name.** Function and global-data symbols
  are the original team's; struct *member* names were reconstructed by the
  decompilation. A member name is a hypothesis until an authored string agrees with
  it. The live example: `g_env_light.mFogDensity` is labelled 雲影の濃さ,
  **cloud-shadow** density, and both its consumers are cloud-shadow paths.
- **Gloss a name the first time a document uses it**, then use it bare. A reader who
  does not know the word cannot look it up, because it is not English.

Our own code — `src/dusk/`, aurora's `lib/`, everything in `dusklight-mods` — is
ordinary English. The convention applies to the code we *read*, not the code we
*write*; do not romanize anything new.

## Docs

| File | Contents |
| :-- | :-- |
| `docs/japanese-naming.md` | **how to read the game's symbol names**, the locale rule, and the working glossary |
| `docs/code-conventions.md` | how to mark Dusk changes inside game code |
| `docs/building.md` | building the port |
| `docs/modding.md` | the mod API |

The companion documents live in the other two repos and cover only what each does
with the finding: `dusklight-mods/docs/japanese-naming.md` (the mods, and the
corrections it has already produced) and `aurora-ao/docs/japanese-naming.md` (what
survives into GX, and why aurora is where a baked TEV meaning becomes readable).

## Checks

```sh
python3 tools/check_japanese_naming.py
```

Verifies that every game symbol the glossary names still exists in this tree, and
re-measures the locale claim rather than trusting it. Run it after editing the
document; a glossary nobody re-reads is only worth something if something enforces
it.

## Branches

Other branches of this fork carry a separate RTX Remix / fixed-function
experiment (`Fixed-Function-dev` and most `claude/*` branches). **`main` is the
baseline for mod-related work.** `docs/japanese-naming.md` was brought over from
`Fixed-Function-dev` and re-verified against this tree; nothing else from that
lineage applies here.
