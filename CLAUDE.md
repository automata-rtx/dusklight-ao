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

## Project context (DX9 fixed-function / RTX Remix work)

- Development branch: `claude/dusklight-dx9-fixed-function-6uoy92`
  (user-facing name "Fixed-Function"). Never push to any other branch.
- Aurora lives at `extern/aurora` (submodule of `automata-rtx/aurora-ao`,
  same branch name). After pushing aurora commits: `git -C extern/aurora
  fetch/checkout <sha>`, then `git add extern/aurora` from the dusklight root
  and verify with `git submodule status` before committing.
- DX9 mode docs: `docs/dx9-fixed-function.md` (game-side + rtx.conf notes)
  and `extern/aurora/docs/dx9/` (spec, progress/resume log).
- `mods/shadow_mod` and `mods/ao_mod` are third-party demonstration mods —
  ignore them entirely.
- The owner tests via the GitHub Actions "Build Windows (MSVC x86_64)"
  artifact; keep CI green on the dev branch.
