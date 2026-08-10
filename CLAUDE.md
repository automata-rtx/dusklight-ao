# Claude session notes — dusklight-ao

## Owner's environment: interactive approval prompts are BROKEN

Any tool call that pops an interactive approval/authorization prompt for the
owner is bugged across ALL of their Claude Code sessions — the prompt always
resolves as "no approval given" (e.g. MCP calls returning
`MCP error -32003: MCP tool call requires approval`, or the `add_repo`
authorization flow looping back to "there was no approval").

**Never rely on a tool that requires interactive approval.** Route around it:

- Reading other public repos (e.g. dxvk-remix for RTX Remix research): fetch
  files via `raw.githubusercontent.com` instead of the `add_repo` flow.
- Scheduling / reminders (`send_later` etc.): use a background `Monitor` /
  background Bash watcher instead.
- Questions for the owner: ask in plain chat text, not interactive pickers.

## Read this first if you have no context

This is a **three-repo** project. Start at **`docs/kankyo-remix.md`** — its
"Start here" table routes to everything else.

| Repo | Role | Its docs |
| :-- | :-- | :-- |
| `automata-rtx/dusklight-ao` | the game (Twilight Princess decomp/port) | see the table below |
| `automata-rtx/aurora-ao` | GX→D3D9 backend, vendored at `extern/aurora` | `docs/dx9/` (README first; `remix-material-interface.md` for anything about materials or colour) |
| `automata-rtx/dxvk-remix` | the RTX Remix fork | `documentation/DusklightAtmosphere.md`, `documentation/DusklightOverlay.md` |

This repo's Remix documents are split by volatility, so a session loads only
what it needs:

| File | Contents | Changes when |
| :-- | :-- | :-- |
| `docs/kankyo-remix.md` | the design: how kankyo works and what we drive with it | the design changes |
| `docs/remix-open-issues.md` | what is broken, what is untested | every session |
| `docs/remix-test-playbook.md` | how to run a test session | a test is added |
| `docs/remix-history.md` | **unmaintained archive.** Last resort only — stale status, superseded plans, one wrong diagnosis | never |
| `docs/kankyo-fog.md` | fog, game side | rarely |
| `docs/dx9-fixed-function.md` | how to set the game up under Remix, and the `rtx.conf` | settings change |
| `docs/sun-elevation.md` | the sun/moon orbit | rarely |
| `docs/japanese-naming.md` | **how to read the game's symbol names**, which are romanized Japanese | a session works a name out |

## The game's code is named in Japanese

**Every identifier in `src/d/`, `src/f_op/`, `src/f_pc/`, `src/m_Do/` and
`libs/JSystem/` is the original Japanese team's name, preserved 1:1 by the
decompilation.** They are romaji — Japanese words in Latin letters — mixed with
abbreviated Japanese and English spelled by ear. Read as English they produce
confident, wrong answers, and this has cost real time.

`kankyo` (環境) is *environment*. `dKyr_drawSibuki` draws 飛沫 *shibuki*, spray.
`dKyw_wether_move` is the **weather** system and `wether` is not a typo to fix.
`d_a_ep` does not stand for anything anybody here has established.

Three things to internalise now; `docs/japanese-naming.md` is the full
reference, including a glossary whose every symbol is checked by
`scripts/check_invariants.py`:

- **Search in both romanizations.** The tree mixes kunrei-shiki (`si`, `tu`,
  `ti`, `sya`) with Hepburn (`shi`, `tsu`, `chi`, `sha`) **for the same word** —
  spray is `Sibuki` in the C functions and `shibuki` in 69 of the effect IDs.
  Either spelling alone finds half the feature. An empty grep is not evidence of
  absence until you have tried the other spelling.
- **Never rename a game symbol**, and never "correct" one of the misspellings
  (`wether`, `Schejule`, `Sord`, `Blure`, `parcent`, `vectle`, `resorce`,
  `tresure`). They are load-bearing across the tree and across `zeldaret/tp`,
  which `docs/code-conventions.md` asks us to upstream fixes to.
- **Gloss a name the first time a document uses it**, then use it bare. A reader
  who does not know the word cannot look it up, because it is not English.
- **`export LC_ALL=C.UTF-8` before grepping for Japanese.** 496 files under
  `src/` and `include/` contain literal kana/kanji — the original team's own
  debug-panel labels, which are the most authoritative documentation in this
  tree. Under the container's default `POSIX` locale, `grep -P` on a kana/kanji
  class silently matches **nothing**. That one missing variable is the most
  likely reason those labels went unread for this project's whole history.

Our own code — `src/dusk/`, aurora's `lib/dx9/`, the fork's `rtx_dusklight_*` —
is ordinary English `camelCase`. The convention applies to the code we *read*,
not the code we *write*; do not romanize anything new.

## Branches — ALL THREE repos use the same structure

- **`Fixed-Function-dev` — the working branch. ALL development commits land
  here, in every one of the three repos.**
- `Fixed-Function` — integration. Advances only by merging `Fixed-Function-dev`
  at checkpoints that are **both CI-green and tested in game by the owner**.
  It is deliberately well behind the dev branch; that is not drift to
  "fix". (dxvk-remix has no `Fixed-Function` branch and does not need one.)
- Base lineage: dusklight/aurora `Fixed-Function*` share fork-point ancestry
  with `ao` (this fork's active line) and `main` (upstream tracker). Backport
  by merging/cherry-picking **into** `Fixed-Function-dev`. Neither is otherwise
  related to the FF DX9 work.

**Push only to your session branch.** The owner merges to `Fixed-Function-dev`
themselves, at milestones they choose. (An auto-mirror rule existed until
2026-07-29 and was revoked.)

```
git push -u origin <session-branch>        # yes
git push origin HEAD:Fixed-Function-dev    # NO - the owner does this
```

If a session branch is about to be deleted and its work is not yet merged,
**say so and stop** rather than mirroring it.

**Before anyone deletes a branch, verify it is contained:**
`git rev-list --count origin/Fixed-Function-dev..origin/<branch>` must be `0`.
This has already caught a near-miss: dxvk-remix's `Fixed-Function-dev` was
**19 commits behind** its `claude/*` branch, so deleting it would have destroyed
the entire atmosphere, overlay, warp and clock work. A non-zero count is the
*normal* state between milestones, so this is not a formality.

**`claude/thin-gbuffer-authored-normals-wgqupt`** (dusklight + aurora) is
**unrelated, unmerged work — 15 commits in neither `main` nor
`Fixed-Function-dev`.** Do not delete it and do not merge it into this
lineage without being asked.

## What the D3D9 renderer is for — read this before proposing a fix

**The raw fixed-function D3D9 image is never shown to a player.** It exists so
Remix's DX9→Vulkan translation picks the scene up automatically — geometry,
transforms, textures, most of a frame, for free. **Remix's renderer is the
product; D3D9 is the feed.**

So:

- **Fixed-function limits are not the ceiling.** Where the D3D9 stream cannot
  carry something faithfully enough to reach Remix, implement it **in Remix** —
  Remix API or a fork change — rather than contorting D3D9 to approximate it.
  All three repos are ours.
- **"Raw D3D9 stays correct" is not a design goal.** It is occasionally a handy
  safety property, never a reason to reject an approach. Documents written
  before 2026-08-04 sometimes treat it as a requirement; they are wrong and are
  being corrected as they are touched.

**Two exceptions still have to rasterize correctly:** the **HUD** (Remix
rasterizes UI draws rather than path-tracing them) and **alpha** (Remix reads
the stage's alpha to build opacity and the alpha test).

Full statement: `aurora-ao/docs/dx9/remix-material-interface.md` §0.

## How this project works — read before proposing a fix

Five rules. They exist because each was learned the expensive way, and following
them is worth more than any individual fix.

### 1. Translate, don't tag

Every Remix project the world over works by hashing textures and hand-authoring
replacements, because the game is a closed box. **All three of our repos are
ours.** We can read the game's intent at the source and hand it to the renderer
directly.

So the default answer to "how do we make Remix understand X" is *translate the
game state*, not *tag the asset*. Tagging gives one answer per texture; this
game reuses textures across contexts constantly, so a tag is wrong somewhere
almost by construction. Translation is per-draw and is right everywhere.

### 2. A question we would have to ask the owner is a defect in the logging

The owner should not be the diagnostic instrument. Asking them to describe a
colour, count an artifact, or judge whether something looks "too dark" produces
answers that are honest and unusable — and it wastes a scarce test window.

**The target loop is: they play, they send a log, we know.** If a question
cannot be answered from a log, the correct response is to add the log line, not
to ask the question. Design instrumentation before designing the fix.

Corollary: **logs must be bounded and self-describing.** One line per distinct
thing, capped, with a truncation notice when the cap is hit, and enum names
spelled out so a reader without the source can follow. A log nobody can read is
the same as no log; a log that fills a disk is worse.

### 3. Do not write inference as finding

This project has three times recorded a plausible mechanism as a verified cause.
One of those shipped and turned out to be a no-op, and the documents kept saying
"FIXED" for a week.

State what you read, cite where, and mark inference as inference. A document
that says "unknown" is more valuable than one that says something confident and
wrong, because the second one stops the next person looking.

### 4. A fix that cannot be observed is a guess

Before shipping a change to a system with no instrumentation, add the
instrumentation. A change that alters behaviour *and* reports on itself is
fine — bundling saves a test window — but a change that alters behaviour and
stays silent cannot be evaluated except by looking at pixels, which is how this
project lost three rounds.

Say plainly what the regression signature of a change is, so it can be
recognised rather than discovered.

### 5. Say what was verified and what was not

"Compiles" and "is correct" are different claims. So are "CI green" and "tested
in game". Every doc entry and every hand-off should make clear which one it is.
There is no penalty here for saying a thing is untested; there is a real cost to
implying it was tested.

## The one coupling that has cost evenings

**The game and the Remix DLL are a single protocol.** The game pushes
`rtx.dusklight.env.protocol`; the fork checks it against `kRequiredProtocol` in
`showDusklightRemixTab`. **Protocol is at 7.** Build both sides from the same
commit point, and when you bump one, bump the other in the same commit. Skew in
either direction has already cost an evening twice — the Dusklight tab reports
which side is old, so read it before debugging anything else.

## Submodule discipline

Aurora is the `extern/aurora` submodule. After pushing aurora commits:

```
git -C extern/aurora fetch origin && git -C extern/aurora checkout <sha>
git add extern/aurora          # from the dusklight root
git submodule status           # verify before committing
```

**Aurora is always merged first.** Whenever a merge carries a submodule bump,
merge aurora into the target branch **before** dusklight, so the pinned aurora
SHA is reachable from that branch rather than only from a session branch that
may later be deleted. This applies to `Fixed-Function-dev` and `Fixed-Function`
alike — a dusklight branch pinning a SHA that lives only on a `claude/*` branch
still builds today and breaks the moment that branch is cleaned up.

## Merges that succeed and are still wrong

**A clean `git merge` is not a correct merge.** Several features are developed
on parallel branches that touch the same files, and git only compares *lines* —
it cannot see that two branches have made the same sentence false, or that a
conflict's obvious resolution is the wrong one.

Three instances, all real:

- **The protocol double-bump.** Two branches independently took protocol 6 → 7.
  Git *did* conflict — and that made it worse: both sides said `7`, so keeping
  either looks right and ships two features claiming one version. The conflict
  was flagged; the **resolution** was the trap.
- **`settings.h` / `settings.cpp`.** Every branch that adds a setting collides
  here. Resolving by keeping one side drops a setting silently; resolving into
  a different order in each file is a **compile error on MSVC**, because C++20
  requires designated initialisers to follow declaration order.
- **A submodule pin written by hand.** `extern/aurora` was pinned to a SHA typed
  from a 7-character prefix. It pointed at nothing, and every CI job then failed
  at *checkout* — which reads as a broken runner, not a bad pin.

**So, after any merge — and before pushing one:**

```
python3 scripts/check_invariants.py
```

It checks the protocol literal against every document that states it, the
declare/initialise/register triple in `settings.{h,cpp}` including order, that
the `extern/aurora` pin names a commit that exists, and leftover conflict
markers. The `Invariants` GitHub workflow runs it on **every** push — with no
path filter, unlike `build.yml`, because a docs-only commit is the most likely
way to introduce exactly this drift. It also runs aurora's own script against
the pinned submodule, since that repo has no CI.

**What it cannot check, and therefore what a human still has to:**

- whether a "tested in game" claim survived the change underneath it
- whether a document's *prose* still describes reality, as opposed to its
  numbers agreeing with the code
- whether two in-flight branches are about to claim the same protocol number —
  nothing can see a branch that has not merged yet, so **check the other live
  branches before bumping** (`git log origin/claude/... -- src/dusk/remix_bridge.cpp`)

**When auditing documentation after a merge, re-derive the file list from the
diff, not from memory.** On the merge that prompted all of this, every gap found
on the thorough pass was in a document nobody had edited — precisely the set
recall does not surface.

## Other standing facts

- `mods/shadow_mod` and `mods/ao_mod` are third-party demonstration mods —
  **ignore them entirely.**
- The owner tests via the GitHub Actions **"Build Windows (MSVC x86_64)"**
  artifact. Keep CI green on the dev branch. Dusklight's workflow has path
  filters, so a docs-only commit correctly produces no run — that is not a
  failure.
- The game's own UI is **never drawn** in the fixed-function D3D9 mode. Any
  setting that needs to be reachable while running has to be hosted in the
  Remix overlay (`rtx.dusklight.game.*`) — see `documentation/DusklightOverlay.md`
  in the fork.
- **HD texture packs go to Remix, never through D3D9** (`remix_bridge.cpp`
  `updateTextureReplacements`, tested good 2026-08-06). The game hands each
  `.dds` to `remixapi_CreateMaterial` and aurora tags each draw with its index.
  The reason is load-bearing and easy to undo by accident: the D3D9 texture is
  what Remix hashes, so uploading a pack would silently re-key **every** texture
  tag, `rtx.conf` category and USD binding. If you ever find yourself making
  aurora upload replacement pixels, that is the trap.
  `extern/aurora/docs/dx9/texture-replacements.md`.
- Verify D3D9 code with the MinGW syntax harness described in
  `extern/aurora/docs/dx9/progress.md` §"How to resume"; full builds happen on
  the owner's Windows machine and in CI.
