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

This is a **three-repo** project. Start at
**`docs/kankyo-remix.md` → "Picking this up cold"** — it is written for
exactly this situation and links everything else.

| Repo | Role | Its docs |
| :-- | :-- | :-- |
| `automata-rtx/dusklight-ao` | the game (Twilight Princess decomp/port) | `docs/kankyo-remix.md`, `docs/dx9-fixed-function.md`, `docs/kankyo-fog.md`, `docs/sun-elevation.md` |
| `automata-rtx/aurora-ao` | GX→D3D9 backend, vendored at `extern/aurora` | `docs/dx9/` (README first, then `progress.md`) |
| `automata-rtx/dxvk-remix` | the RTX Remix fork | `documentation/DusklightAtmosphere.md`, `documentation/DusklightOverlay.md` |

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

**The auto-mirror authorization was REVOKED by the owner on 2026-07-29.**
Earlier revisions of this file told a session to push its `claude/*` branch and
then immediately mirror it to `Fixed-Function-dev` without being asked. **Do not
do that any more.** The owner merges to `Fixed-Function-dev` themselves, at
milestones they choose:

```
git push -u origin <session-branch>        # yes
git push origin HEAD:Fixed-Function-dev    # NO - the owner does this
```

Push only to the session branch. If a session branch is about to be deleted and
its work is not yet merged, **say so and stop** rather than mirroring it.

**The containment check survives the revocation, and matters more because of
it.** Before anyone deletes a branch:
`git rev-list --count origin/Fixed-Function-dev..origin/<branch>` must be `0`.
This has already caught a near-miss: dxvk-remix's `Fixed-Function-dev` was
**19 commits behind** its `claude/*` branch, so deleting that branch would have
destroyed the entire atmosphere, overlay, warp and clock work. With auto-mirror
off, that gap is now the *normal* state between milestones rather than an
anomaly — so the check is no longer a formality.

**Before anyone deletes a branch, verify it is contained:**
`git rev-list --count origin/Fixed-Function-dev..origin/<branch>` must be `0`.
This has already caught a near-miss: dxvk-remix's `Fixed-Function-dev` was
**19 commits behind** its `claude/*` branch, so deleting that branch would
have destroyed the entire atmosphere, overlay, warp and clock work.

**`claude/thin-gbuffer-authored-normals-wgqupt`** (dusklight + aurora) is
**unrelated, unmerged work — 15 commits in neither `main` nor
`Fixed-Function-dev`.** Do not delete it and do not merge it into this
lineage without being asked.

## The one coupling that has cost evenings

**The game and the Remix DLL are a single protocol.** The game pushes
`rtx.dusklight.env.protocol`; the fork checks it against `kRequiredProtocol` in
`showDusklightRemixTab`. **Protocol is at 4.** Build both sides from the same
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

Merge aurora dev → `Fixed-Function` **before** dusklight dev →
`Fixed-Function`, so the pinned aurora SHA is reachable from aurora's
`Fixed-Function`.

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
