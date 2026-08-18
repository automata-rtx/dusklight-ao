# Claude session notes — dusklight-ao

**Interactive approval prompts are broken for the owner** — they always resolve
as "no approval given". Read other public repos via `raw.githubusercontent.com`
rather than `add_repo`, use a background watcher rather than `send_later`, and
ask questions in plain chat.

## Read this first if you have no context

A **three-repo** project. This repo is the game (Twilight Princess decomp/port);
`automata-rtx/aurora-ao` is the GX→D3D9 backend, vendored at `extern/aurora`
(`docs/dx9/README.md`); `automata-rtx/dxvk-remix` is the RTX Remix fork
(`documentation/DusklightAtmosphere.md`, `DusklightOverlay.md`).

Start at **`docs/kankyo-remix.md`** — its "Start here" table routes to the rest.

| File | Holds | Changes when |
| :-- | :-- | :-- |
| `docs/kankyo-remix.md` | the design: how kankyo works and what we drive with it | the design changes |
| `docs/kankyo-fog.md` | the game's fog model, and exactly what the bridge pushes | rarely |
| `docs/effect-lights.md` | how fire, lava and glow get real lights, placed at the JPA emitter's origin | the design changes |
| `docs/remix-open-issues.md` | what is broken, and **the project's single "not verified in game" ledger** | every session |
| `docs/remix-test-playbook.md` | what to run next, in order | a test is added |
| `docs/dx9-fixed-function.md` | how to set the game up under Remix, and the `rtx.conf` | settings change |
| `docs/japanese-naming-remix.md` | **how to read the game's symbol names**, which are romanized Japanese | a session works a name out |

A status claim belongs in `remix-open-issues.md` and nowhere else. Four parallel
verification ledgers is why three of them went stale.

## The game's code is named in Japanese

**Every identifier in `src/d/`, `src/f_op/`, `src/f_pc/`, `src/m_Do/` and
`libs/JSystem/` is the original Japanese team's name, preserved 1:1 by the
decompilation.** Read as English they produce confident, wrong answers.
`kankyo` (環境) is *environment*; `dKyw_wether_move` is the **weather** system and
`wether` is not a typo to fix. Three rules, full reference in
`docs/japanese-naming-remix.md`:

- **Search both romanizations.** The tree mixes kunrei-shiki (`si`, `tu`, `ti`)
  with Hepburn (`shi`, `tsu`, `chi`) *for the same word* — spray is `Sibuki` in
  the C functions and `shibuki` in 69 effect IDs. An empty grep is not evidence
  of absence until you have tried the other spelling.
- **Never rename a game symbol** and never "correct" a misspelling (`wether`,
  `Schejule`, `Sord`, `Blure`, `parcent`, `vectle`, `resorce`, `tresure`). They
  are load-bearing across the tree and across `zeldaret/tp`.
- **`export LC_ALL=C.UTF-8` before grepping for Japanese.** Hundreds of files
  carry the original team's kana/kanji debug labels — the most authoritative
  documentation in the tree — and under the default `POSIX` locale `grep -P` on
  a kana class silently matches **nothing**.

Our own code (`src/dusk/`, aurora's `lib/dx9/`, the fork's `rtx_dusklight_*`) is
ordinary English `camelCase`. Do not romanize anything new.

## Branches — all three repos use the same structure

- **`Fixed-Function-dev` is the working branch.** All development commits land
  there, in every repo.
- `Fixed-Function` is integration, advancing only at checkpoints that are both
  CI-green and tested in game. It is deliberately well behind; that is not drift
  to fix. (dxvk-remix has no such branch and needs none.)

**Push only your session branch** — the owner merges to `Fixed-Function-dev`
themselves. If a session branch is about to be deleted and its work is not
merged, **say so and stop** rather than mirroring it. Before any deletion,
`git rev-list --count origin/Fixed-Function-dev..origin/<branch>` must be `0`; a
non-zero count is the normal state between milestones, which is why this is not a
formality. **`claude/thin-gbuffer-authored-normals-wgqupt`** (here and in aurora)
is unrelated, unmerged work — do not delete it, do not merge it into this
lineage without being asked.

## What the D3D9 renderer is for

**The raw fixed-function D3D9 image is never shown to a player.** It exists so
Remix's DX9→Vulkan translation picks the scene up for free. **Remix's renderer is
the product; D3D9 is the feed** — so where D3D9 cannot carry something
faithfully, implement it in Remix rather than contorting D3D9. All three repos
are ours, and "raw D3D9 stays correct" is not a design goal.

**Two exceptions must still rasterize:** the **HUD** (Remix rasterizes UI draws)
and **alpha** (Remix reads the stage's alpha for opacity and the alpha test).
Full statement: `aurora-ao/docs/dx9/remix-material-interface.md` §0.

## Five rules, each learned expensively

1. **Translate, don't tag.** A texture tag is wrong somewhere by construction; a
   per-draw translation of the game's own state is right everywhere.
2. **A question we would have to ask the owner is a defect in the logging.** They
   play, they send a log, we know. Bounded, capped, self-describing.
3. **Do not write inference as finding.** "Unknown" beats confident and wrong.
4. **A fix that cannot be observed is a guess.** Instrument first; state the
   regression signature.
5. **Say what was verified.** "Compiles", "CI green" and "tested in game" are
   three different claims.

## Protocol — the one coupling that has cost evenings

The game and the Remix DLL are a single protocol. The game pushes
`rtx.dusklight.env.protocol`; the fork checks it against `kRequiredProtocol`, a
`constexpr` at **file scope** in `dxvk-remix/src/dxvk/imgui/dxvk_imgui.cpp:2613`,
read by the overlay's status strip rather than by any one tab.
**Protocol is at 17.** Build both sides from the same commit point, and bump
both in the same commit — skew in either direction has cost an evening twice.
The status strip says which side is old; read it before debugging anything else.

**12 is skipped and is not free** — it belongs to the unmerged
`claude/kasumi-naming-correction-w3e204`, and 13–17 were taken beside it. The
next number is **18**. Nothing checks this, because no script can see an unmerged
branch, so check the live branches yourself:
`git show origin/claude/<name>:src/dusk/remix_bridge.cpp | grep env.protocol`.

## Submodule discipline

Aurora is `extern/aurora`. After pushing aurora commits:

```
git -C extern/aurora fetch origin && git -C extern/aurora checkout <sha>
git add extern/aurora && git submodule status     # verify before committing
```

**Aurora is always merged first.** Whenever a merge carries a submodule bump,
merge aurora into the target branch before dusklight, so the pinned SHA is
reachable from that branch rather than only from a session branch that may later
be deleted.

## Verification

```
python3 scripts/check_invariants.py        # after any merge, before any push
tools/syntax-check-remix.sh                # before pushing Remix-facing game code
```

`check_invariants.py` checks the protocol literal against every document stating
it, the declare/initialise/register triple in `settings.{h,cpp}` **including
order** — C++20 requires designated initialisers to follow declaration order, so
resolving that (very common) merge conflict into a different order in each file
is an MSVC compile error — that the `extern/aurora` pin names a commit that
exists, and conflict markers. The `Invariants` workflow runs it on every push
with no path filter, and runs aurora's own script against the pinned submodule
since that repo has no CI.

`syntax-check-remix.sh` cross-compiles `remix_bridge.cpp`, `effect_lights.cpp`
and `d_particle.cpp` with MinGW. **A native Linux `g++` is worse than useless
here:** the bridge is inside `#if defined(_WIN32)`, so Linux preprocesses the
whole thing away and reports success — four compile errors reached CI that way.
It also runs `tools/check-remix-protocol.py`, which cross-checks every
`rtx.dusklight.*` name the game reads or pushes against the fork's `RTX_OPTION`
declarations, plus the protocol number across both repos' docs. **No compiler can
see that class of mistake**: a mistyped name silently falls back to `config.json`
forever. Its `doc_paths()` reads files **by literal path and silently skips a
missing one**, so a new fork document must be added there or it passes while
checking nothing.

**A clean `git merge` is not a correct merge.** No script can see whether a
"tested in game" claim survived the change under it, whether prose still
describes reality, or whether two in-flight branches are about to claim the same
protocol number, side channel or GX subcommand. Two shared registries, both
argued at their allocation site rather than here: a new per-draw fact is a new
**bit**, not a new `D3DMATERIAL9` channel
(`dxvk-remix/src/dxvk/rtx_render/rtx_dusklight_drawmeta.h:27-50`), and GX FIFO
subcommand numbers are taken from the registry comment at
`extern/aurora/include/dolphin/gx/GXAurora.h:210-232`.

## Other standing facts

- `mods/shadow_mod` and `mods/ao_mod` are third-party demonstration mods —
  **ignore them entirely.**
- **HD texture packs go to Remix, never through D3D9** (`remix_bridge.cpp`
  `updateTextureReplacements`, tested good 2026-08-06). The D3D9 texture is what
  Remix hashes, so uploading a pack would silently re-key every texture tag,
  `rtx.conf` category and USD binding. If you find yourself making aurora upload
  replacement pixels, that is the trap.
- **The game's own UI is never drawn** in fixed-function D3D9 mode, so anything
  that must be reachable while running has to be hosted in the Remix overlay
  (`rtx.dusklight.game.*`) — `dxvk-remix/documentation/DusklightOverlay.md`.
- The owner tests the GitHub Actions **"Build Windows (MSVC x86_64)"** artifact,
  the only build job this line runs — the other platforms are switched off with
  `if: ${{ false }}` rather than deleted. `build.yml` has path filters, so a
  docs-only commit correctly produces no run; that is not a failure. It also has
  `workflow_dispatch`, for when GitHub drops a push event.
- **Effect lights deliberately derive no radiance from the authored JPA data** —
  nothing the artists wrote is photometric. `effect_lights.cpp:1517-1522` states
  the prohibition and the reason, at the point of temptation.
