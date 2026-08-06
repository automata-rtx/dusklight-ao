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
| `docs/effect-lights.md` | the effect-light system: how fire and glow get real lights in the path tracer, and why the placement no longer comes from the game's own light registry | the design changes |
| `docs/kankyo-fog.md` | fog, game side | rarely |
| `docs/dx9-fixed-function.md` | how to set the game up under Remix, and the `rtx.conf` | settings change |
| `docs/sun-elevation.md` | the sun/moon orbit | rarely |

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
- Verify D3D9 code with the MinGW syntax harness described in
  `extern/aurora/docs/dx9/progress.md` §"How to resume"; full builds happen on
  the owner's Windows machine and in CI.
- **Verify Remix-facing game code with `tools/syntax-check-remix.sh`** before
  pushing. It cross-compiles `remix_bridge.cpp`, `effect_lights.cpp` and
  `d_particle.cpp` with MinGW. **A native Linux `g++` is worse than useless
  here:** the bridge is inside `#if defined(_WIN32)`, so Linux preprocesses the
  entire thing away and then reports success. On 2026-08-06 four compile errors
  reached CI that way. Needs `g++-mingw-w64-x86-64` and `libfmt-dev`.
  It also runs `tools/check-remix-protocol.py`, which cross-checks every
  `rtx.dusklight.*` name the game reads or pushes against the fork's
  `RTX_OPTION` declarations. **No compiler can see that class of mistake** — a
  mistyped name silently falls back to `config.json` forever, and a readout
  nothing pushes silently reads as its default.
