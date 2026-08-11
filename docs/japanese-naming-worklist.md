# Worklist: prompts to run from the naming audit

Ready-to-paste prompts for separate Claude Code sessions, produced by
[`japanese-naming-audit.md`](japanese-naming-audit.md).

**Each fenced block is one session.** Copy the block, paste it, let it run. Do not
combine two of them into one session — several are deliberately split because they have
different regression signatures, and a change that alters two things at once cannot be
judged from one test.

---

## How these are written, and why it matters

> **Changed 2026-08-11, by the owner's instruction.** These prompts used to forbid code
> changes almost everywhere, on the theory that verification and implementation are
> separate jobs and mixing them produces changes nobody can review. That theory is not
> wrong, but the practice was: it made the owner the integration step for every finding,
> and they do not have the time or the context to be one. Findings piled up as prose.
>
> **Every prompt now implements its finding.** Verification still comes first and the
> stop clause is still absolute — but a session that verifies a finding and then leaves
> the code alone has done half a job.
>
> §4.2 of the audit is what this rule is made of: a session corrected the `kasumi`
> descriptions, reported "description strings only, no behavioural change", and never
> checked whether a consumer had implemented the wrong premise. One had. The shader is
> still wrong today, months of documents later, and every document says the correction
> landed.

Every prompt carries the same nine parts, in the same order:

1. **Read first** — `CLAUDE.md` loads automatically; `docs/japanese-naming.md` does not.
2. **The claim, as something to re-verify** — file and line are given so checking is
   cheap. Verify it against the source before you build on it. **Then build it.**
3. **A stop clause** — *"if it does not reproduce, stop and report; do not repair the
   plan."* Still the most important line in every prompt, and authorising the code change
   makes it **more** important rather than less: it is now the only thing standing between
   a wrong finding and a wrong commit. A session that cannot reproduce a claim must not
   implement it anyway on the grounds that it was told to.
4. **In scope** — the code *and* the documents that change.
5. **Out of scope** — named explicitly, including the tempting adjacent work. Scope limits
   here are about **blast radius**, never about avoiding code.
6. **Regression signature** — what it looks like if the change is wrong.
7. **Protocol** — whether both sides must bump in the same commit.
8. **Done means** — a checkable end state.
9. **An escape hatch** — *"acceptable outcome: you conclude this is not worth doing."*
   Named as a success, not a failure. This is the main defence against the audit
   generating work for its own sake, and it survives the change above: **authorised to
   change code is not obliged to change code.**

**And a tenth part that closes every session, which is not optional:**

> ### 10. What this changes for you
>
> Three short paragraphs in plain English at the end of the session's reply — no jargon,
> no file paths, no option names:
>
> - **What was wrong before** — in terms of what the game or the renderer actually did.
> - **What is better now** — what the owner should expect to see or be able to do.
> - **What is still owed** — anything untested, deferred, or waiting on a play session.
>
> If the session concluded the finding was **not** worth implementing, this section says
> that instead, in the same three parts, with the reason in the same plain language.

The owner is the one person on this project who cannot be replaced by a log, and their
time is the scarce resource. A session that hands back a diff they cannot evaluate has
moved the bottleneck rather than removed it — and "I'll read the code" is not available
to them.

### The tags

| Tag | What the session is authorised to do |
| :-- | :-- |
| **[CODE]** | Verify, then change the code. No wire bump. **The default.** |
| **[CODE, GATED]** | Same, but the new behaviour ships behind an option **defaulting to today's behaviour**, so it can be compared rather than argued about. Use where the change is a matter of look rather than correctness. |
| **[PROTOCOL]** | Code change that crosses the game↔DLL wire. Both sides bump in the same commit. |
| **[MEASURE FIRST]** | One log decides whether there is anything to build at all. Ask for it, then act on what it says — including acting on "nothing to do here". |
| **[RECORD ONLY]** | **The code is correct and a document is wrong about it.** There is genuinely nothing to build. Rare, and named explicitly so it cannot be used as cover. |

**[RECORD ONLY] is the one tag that forbids a code change, and it is a description rather
than a preference.** If a session tagged [RECORD ONLY] finds something the *code* gets
wrong, the tag was mis-assigned: say so, retag it, and fix the code.

### Two standing warnings that belong in every session

```sh
export LC_ALL=C.UTF-8      # or grep -P finds none of the game's Japanese, silently
```

**Protocol is at 11.** It was 7 on `Fixed-Function-dev` and 11 on the effect-lights
branch until those merged on 2026-08-11; 11 is the merged number. Any prompt tagged
[PROTOCOL] must check the other live `claude/*` branches before taking a number. Two
branches claiming one number is a trap this project has already hit, and the merge
conflict resolves *cleanly* into a wrong answer — **it hit again here**: the effect
lights and the HD texture pack readouts both landed at 7, independently, so a build
reporting 7 may carry either or both. `DusklightOverlay.md`'s protocol ladder says so.

---

## ⚠ Read this before scheduling anything: the triage

A completeness critic reviewed all twelve audits and returned a verdict worth quoting:

> *"About six things are worth doing, and about thirty-five are sentences that are wrong
> in documents. Treating the whole set as a backlog would turn a working system into a
> project."*

That is the right call, and this file is organised by tier rather than by priority, so use
the table below instead of working top to bottom.

### ✅ DONE — with what actually landed in code

Audited 2026-08-11 against `Fixed-Function-dev` at `0402654`. **The point of this table is
the last column**, which is the question nobody was asking: *did the finding reach the
code, or only the documents?*

| | Landed | Code integrated? |
| :-- | :-- | :-- |
| **P0** | PR #10, effect-lights merged | **Yes.** `src/dusk/effect_lights.{cpp,hpp}`, protocol 7 → 11. |
| **P17** | PR #11, water channels | **Yes.** `water_materials.hpp`, `remix_bridge`, `settings.{h,cpp}`, aurora's `Power` packing, and bidirectional side-channel checks in both invariants scripts. |
| **P1** | PR #12, Lost Woods fog tag | **No — and correctly so.** There is no fork code that implements the `kytag01` blend; the game computes the fog range and pushes it. What is owed is a **measurement**, not a commit. See the item. |

### DO — schedule these

| | Why it earns a session |
| :-- | :-- |
| **P19** | The largest piece of game data we drop — and cheaper than it looked (see below). Instrument first. |
| **P12** | The densest particle field in the game is unbatched. **Measure before changing anything.** |
| **P8** | Turns on per-draw material identity, which is the door to name-keyed art assets. |
| **P2** | **The one open case of a correction that reached the docs and not the code.** Now authorised to reach the code. |

### FIX IN PASSING — cheap, but no longer free

**P3, P5, P6, P7, P9, P10, P13, P14, P15, P16, P18.**

The old note here said these were "wrong sentences in documents" and that a wrong sentence
only bites when someone reads it. **That was true when the prompts were forbidden to touch
code, and it stopped being true when they were not.** Re-read against the new tags, this
set splits:

- **Genuinely record-only** — P3, P10, P13, P18 and the documentation halves of P5 and
  P15. The code is right; a document is wrong about it. Correct in passing.
- **Carry a real code change** — P6, P7, P14, P16, P20, the shader half of P2, the overlay
  half of P15, and the log line in P5. These were parked as "documentation" because the
  prompt forbade the change, not because the change was not worth making. **P16 is one
  line that has been sitting unmade since it was verified.**

Correcting the first group opportunistically is still right. Treating the second group as
prose is how the backlog got here.

### DROPPED — with reasons

- **P4, item 1** (the dead-keyword guard). Dropped on merit and it stays dropped: the live
  words already cover every effect the dead ones would have, and at default settings the
  class cannot move a pixel. **It was never built** — confirmed 2026-08-11, there is no
  keyword check in `scripts/check_invariants.py`. Items 2 and 3 of P4 — the
  single-romanization trap in the `Excluded` guidance, and the corrected description of
  what `Class` does — remain worth doing in passing.
- **`dKy_get_schbit`, the seasons index, the calendar.** All three are inert or
  actionless. They are recorded in the audit's §6 so nobody re-investigates them; there is
  nothing to build.

**Nothing in twelve audits justifies changing a shipping behaviour that is currently
tested good** *without a way to compare it* — which is what [CODE, GATED] is for. A change
that is worth making and cannot be judged from one test window should ship behind an
option defaulting to today's behaviour, not be deferred until someone has time to argue
about it.

---

# Tier 0 — the two time-critical items, neither of which is a naming finding

**Both are now done, and both reached the code.** Kept for the record because the shape of
each failure recurs.

## P17 · ✅ DONE 2026-08-11 — Two branches wanted the same two material channels

> **Reproduced, then resolved.** Both collisions were real. The audit is
> `extern/aurora/docs/dx9/in-flight-allocation.md`; the fix was to **rebase** the
> water branch onto `Fixed-Function-dev` rather than merge it, and re-derive its
> channel assignment.
>
> **Outcome:**
> - Water's three facts now share `D3DMATERIAL9::Power`, packed as
>   `tag * 100 + layer * 10 + role`. HD texture packs keep `Ambient.g`/`.b`.
> - **`Ambient.a` is the only free channel left**, and
>   `claude/dusklight-remix-transparency-e7l766` has an unmerged claim on it. Any
>   item in this file that assumes a spare channel should assume it has to pack,
>   or move to a different transport.
> - GX FIFO `0x0053` is water's; `0x0054`–`0x0057` are reserved in a registry
>   comment in `extern/aurora/include/dolphin/gx/GXAurora.h`.
> - Both invariants scripts now run the side-channel map in **both** directions
>   and cover `Power`; a third check requires every allocated channel to be in
>   `computeIdentityHash`; duplicate and unregistered subcommands fail. Each was
>   verified by breaking the tree deliberately.
> - One correction to the framing below: git **does** conflict, in two files —
>   the hunk is a block, not a single line. That made it worse rather than
>   better, because the conflict's obvious resolution is the wrong one and the
>   field map merges clean beside it.
>
> The original prompt is kept for the record.

**Superseded. Do not run this.** It is left because the shape of the failure
recurs and this is the only statement of it.

```
Read CLAUDE.md ("Merges that succeed and are still wrong") and
aurora-ao/docs/dx9/remix-material-interface.md §2 before starting.

CLAIM TO VERIFY FIRST. Investigation and documentation only in this session - you are
NOT authorised to change either branch's behaviour.

The HD texture pack feature (tested good in game, 2026-08-06) carries its pack index and
stage in two D3DMATERIAL9 channels. The unmerged branch
claude/water-rendering-investigation-7baezw is reported to reclaim those same two
channels for two boolean water flags.

Verify by reading `set_remix_material` in aurora-ao/lib/dx9/dx9_internal.hpp on BOTH
Fixed-Function-dev and that branch, and the fork's reads in
dxvk-remix/src/dxvk/rtx_render/rtx_dusklight_texrep.{h,cpp} and
rtx_dusklight_emissive.h.

WHY THIS IS THE DANGEROUS SHAPE. The water branch does not contain the texrep commit, so
the conflicting hunk is a SINGLE LINE whose obvious resolution - take the newer side - is
the wrong one. Git will report a clean or trivially-resolvable merge and every draw will
then present a texRepIndex of 0 or 1. That is precisely the failure CLAUDE.md documents.

SECOND COLLISION, SAME SHAPE, FOUND SEPARATELY: **four live branches each define GX FIFO
opcode 0x0053**, and the dispatch is an else-if chain. Two of them merging produces NO
CONFLICT at the place that matters - the first arm wins and the others become unreachable,
silently. Verify which four, and recommend an allocation (a shared registry, or an
opcode-per-branch assignment) rather than resolving it arm by arm.

ALSO ESTABLISH, because it changes what anyone can plan: how many D3DMATERIAL9 channels
are genuinely free on Fixed-Function-dev today, and which unmerged branches have claimed
each of the remainder. The sweep reports THREE parties overlapping, not two. The audit's reading is that BOTH remaining channels are already
spoken for by different in-flight branches. If that is right, several proposals elsewhere
in this worklist that assume a spare channel are planning on space that is gone - say so
explicitly.

IF IT DOES NOT REPRODUCE: say so plainly. A false alarm here is a good outcome.

IN SCOPE:
 - A written channel-allocation status: for each D3DMATERIAL9 field, who uses it on
   Fixed-Function-dev, and who claims it on each unmerged branch.
 - Update aurora-ao/docs/dx9/remix-material-interface.md §2 to record that the remaining
   channels are spoken for by in-flight branches, since that table is what the next
   feature will read.

 *** DO NOT ASSUME THE INVARIANTS SCRIPT COVERS THIS. It does not. ***
 aurora's scripts/check_invariants.py:109-155 walks the fields set_remix_material WRITES
 and requires a §2 row for each - it does not check the reverse. The water branch drops
 the texrep rows from BOTH the code and §2, so a merge that takes the water side wholesale
 is SELF-CONSISTENT and the script passes green with HD texture packs silently broken. It
 also only inspects Ambient/Diffuse/Specular/Emissive (:126), so mat.Power escapes the
 check entirely. Nothing automated catches this. If you can cheaply make the check
 bidirectional and cover Power, do - and say so - but the human review is the actual
 safeguard here.
 - A recommended resolution for the water branch: rebase it onto Fixed-Function-dev and
   re-derive its channel assignment against the current set_remix_material signature,
   rather than resolving the conflict by hand.

OUT OF SCOPE: performing the rebase, merging anything, changing either feature.

DONE MEANS: the allocation table is written and cited; §2 says what is actually free; and
the water branch has a stated, safe merge procedure. Aurora's invariants pass.

Push only your session branch.
```

## P0 · ✅ DONE — Merge the effect-lights branch — [PROTOCOL]

> **Landed as PR #10**, merge commit `41835d8`, before `Fixed-Function-dev` reached
> `0402654`. `origin/claude/remix-sphere-lights-system-0j781o` is fully contained
> (`git rev-list --count` returns `0`).
>
> **Code integrated: yes.** `src/dusk/effect_lights.{cpp,hpp}` and `docs/effect-lights.md`
> are on `Fixed-Function-dev`, and the protocol went **7 → 11** — which is where the
> current number comes from. The three merge traps the prompt named (settings ordering,
> the protocol double-bump, the backwards aurora pin) were all live and all resolved.
>
> **Nothing is owed here.** An earlier version of this entry claimed the merge left
> `sphere.shaping_hasvalue = 0` in place and that forwarded lights therefore discard cone
> shape. **Checked 2026-08-11: that is wrong.** Neither forwarded light type carries a cone
> — `LIGHT_INFLUENCE` has no angle fields, and the effect-light path reads `BOSS_LIGHT` for
> colour only, deliberately and under a comment saying so. Hardcoding shaping off is the
> correct encoding of "this light has no cone". The cone question belongs entirely to P19;
> see the note in the sequencing summary for the trap it contains.
>
> This entry was **stale for the whole of 2026-08-11**: the body below still described the
> branch as unmerged, "27 commits ahead / 15 behind, protocol 11 vs 7", while the merge had
> already happened. A worklist that does not mark its own items done sends the next session
> to redo them. *Original prompt kept for the record.*

**Superseded. Do not run this.**

The system that turns the game's own fire and glow into Remix sphere lights is finished,
reviewed, tested in game, and sitting unmerged behind three merge traps. Light creation
was named as critical game data that must reach Remix; this is the thing that does it.

Measured state: 27 commits ahead of `Fixed-Function-dev`, 15 behind. Protocol 11 vs 7.
Aurora pin `e195164` vs `bdc9b20` — **the branch's pin is older**. The branch predates
`scripts/check_invariants.py` entirely. Files changed on both sides include
`settings.h`, `settings.cpp` and `remix_bridge.cpp` — exactly the set `CLAUDE.md` warns
about by name.

```
Read CLAUDE.md and docs/japanese-naming.md before starting.

TASK: bring origin/claude/remix-sphere-lights-system-0j781o up to date with
Fixed-Function-dev and prepare it to merge. Do NOT merge to Fixed-Function-dev yourself
- the owner does that. Work on your own session branch, in dusklight-ao and aurora-ao.

The branch is 27 ahead / 15 behind. Merge Fixed-Function-dev INTO it and hand back a
branch that is CI-green and whose invariants pass.

THREE TRAPS, ALL DOCUMENTED IN CLAUDE.md, ALL LIVE ON THIS MERGE:

1. settings.h / settings.cpp. Both sides added settings. Keeping one side drops a
   setting silently. Resolving into a different order in the two files is a COMPILE
   ERROR on MSVC - C++20 requires designated initialisers to follow declaration order.
   After resolving, run scripts/check_invariants.py, which checks the
   declare/initialise/register triple including order. NOTE: this branch predates that
   script; bring it over from Fixed-Function-dev as part of the merge.

2. Protocol. Branch 11, Fixed-Function-dev 7. The branch's 8/9/10/11 are its own
   features so 11 is correct to keep - but VERIFY no other live claude/* branch has
   taken 8-11, and that kRequiredProtocol in the fork's showDusklightRemixTab matches on
   the fork side in the same commit.
   AND FIX THE HANDSHAKE WHILE YOU ARE HERE: the audit found that the protocol check
   detects only ONE of the two skew directions, and the untested direction is exactly the
   one this merge creates (a protocol-11 game meeting a protocol-7 DLL). Confirm that,
   then make the check report both directions before you rely on it to diagnose this
   merge.

3. The aurora submodule. The branch pins an OLDER aurora than Fixed-Function-dev.
   Aurora is always merged FIRST: merge aurora's Fixed-Function-dev into the aurora
   side, push it, then set the dusklight pin with `git -C extern/aurora rev-parse HEAD`
   - never by typing a hash. A hand-typed pin that points at nothing fails every CI job
   at CHECKOUT, which reads as a broken runner rather than a bad pin.

ALSO: the naming work (docs/japanese-naming.md, the CLAUDE.md sections, the locale
warning) landed on Fixed-Function-dev after this branch diverged, so it arrives in this
merge. Keep it; it does not conflict with anything the branch does.

DO NOT change effect-lights behaviour during this merge. If you find a defect, write it
down and leave it - a merge that also fixes things cannot be reviewed.

LOCAL LIGHTS: leave rtx.dusklight.game.localLights exactly as the branch has it, present
and defaulting OFF, as the comparison path (docs/effect-lights.md section 8). Retiring it
is a separate change, and doing it inside this merge would destroy the only way to A/B
the new light placement. Verify the overlay's "both enabled" warning survived.

DONE MEANS: the branch contains Fixed-Function-dev; check_invariants.py passes in
dusklight-ao and aurora-ao; kRequiredProtocol matches; the aurora pin resolves; CI green
on all three Windows configs; and a short note saying what the merge resolved and what it
deliberately left alone.

Acceptable outcome: you find the merge is not safe to do mechanically, and report why.

Push only your session branch.
```

---

# Tier 1 — cheap, do first, low blast radius

These lead not because they matter most, but because they are contained. Most are
[RECORD ONLY] — the code is right and a document is wrong about it. **The two that are
not** (P2's shader, P5's log line) are marked, and they are the ones worth a session.

## P1 · ~~kytag01 is the Lost Woods, not Lake Hylia~~ — **DONE 2026-08-11** [RECORD ONLY]

**Landed.** The claim reproduced in full, including the blend direction, and **twelve**
passages were corrected rather than five — `kankyo-fog.md` §3.3/§5/§6/§7 and
`DusklightAtmosphere.md` §3/§5.1/§8.1/§8.4/§10/§13(×2)/§14.7. Both invariant scripts pass.
Two things the prompt did not anticipate: "Lost Woods" and "Sacred Grove" are the same
stage (`F_SP117`), so the header comment and the `OS_REPORT` never disagreed; and only
the fog *range* varies with position — the colpat blend, moya count and audio are uniform
across the room. Whether Lake Hylia also carries a kytag01 remains **unprovable from
source** (placement is `.dzs` data). Findings in `japanese-naming-audit.md` §4.1.

> **Code integrated: no, and that is the right answer.** Checked 2026-08-11 — the merge
> (PR #12) touched three files, all documentation. There is no fork code implementing the
> `kytag01` blend to correct: the game computes the fog range itself and pushes the result,
> so the finding lands on *where the owner stands*, not on a line of code.
>
> **What is still owed is a measurement, and it has not been taken.** `zHalfMin` and
> `froxelRangeScale` are still carrying guessed values —
> `DusklightAtmosphere.md:1428` reads *"`froxelRangeScale` (0.6) remains unchallenged
> rather than validated"* — because the dense-fog regime has never been visited. The
> correction says where to go. Nobody has gone.
>
> **One thing that *is* a code gap, and it is small:** the whole `kytag01` layer is
> switch-gated (`d_a_kytag01.cpp:71`, `:124-144`), and nothing logs whether the gate is
> open. So if the owner walks the Lost Woods and sees no fog, they cannot tell "the tag is
> off" from "the fork is ignoring it" — which is exactly the ambiguity project rule 2
> exists to delete. **A session going to the Lost Woods for any reason should add that log
> line first**, or the trip produces an unusable answer.

*Original prompt kept below for provenance.*

The dense-fog worked example is attributed to the wrong area in five places across two
repos — including the instruction the owner follows during a test session. The game says
so twice, and one of them is in English.

```
Read /home/user/dusklight-ao/docs/japanese-naming.md first.
export LC_ALL=C.UTF-8 before grepping Japanese.

CLAIM TO VERIFY FIRST.

Our documents attribute the dense-fog kytag01 case to Lake Hylia. The game disagrees:

  src/d/actor/d_a_kytag01.cpp:1-4     /** d_a_kytag01.cpp   Sacred Grove Mist Tag */
  src/d/actor/d_a_kytag01.cpp:202     OS_REPORT("\n迷いの森　霧タグの...")
                                      迷いの森 = the Lost Woods, 霧タグ = fog tag

The five sites to correct:
  dusklight-ao/docs/kankyo-fog.md:103   (the section 3.3 heading)
  dusklight-ao/docs/kankyo-fog.md:226   (the measurement instruction to the owner)
  dusklight-ao/docs/kankyo-fog.md:239   (the moya mode table)
  dxvk-remix/documentation/DusklightAtmosphere.md:289, :421, :1315

ALSO VERIFY AND STATE THE BLEND DIRECTION, which the docs have backwards. Read
d_a_kytag01.cpp:53-71 and :81-92. The distance term is 0 inside mNamiInnerRange and 1
beyond mNamiOuterRange, and it is the WEIGHT of the -2000/200 whiteout override passed
to dKy_fog_startendz_set at :94 - so the tag marks a CLEAR CENTRE and the fog is
strongest away from it. The view-angle term runs the same way: fog is weakest when you
face the tag. Confirm both yourself before writing them down.

TWO NARROWINGS TO CARRY, so you do not overstate it:
 - `var_f31 *= i_this->field_0x594` at d_a_kytag01.cpp:72, and field_0x594 is the
   switch-gated fade from :124-144. With the gating switches off the fog is zero
   everywhere regardless of distance, so "whiteout away from the tag" is CONDITIONAL on
   the tag being switched on. Say so.
 - kankyo-fog.md:129's "strengthens as you look into the fog bank" is NOT a sign error.
   If "the fog bank" means the away-from-tag region, it is correct. The defect is
   ambiguity about what the bank is. Fix the ambiguity, do not flip the sentence.

WHERE THE MIX-UP PROBABLY CAME FROM, and it is worth recording: d_kankyo.cpp:7458 is an
HIO combo item 「２：ハイリア湖専用」 - "2: Lake Hylia only" - in the wolf-SENSE pattern panel.
That is a different index space from colpat, and it is the same confusion behind the
kPalaceOfTwilightColpat finding in P5. One Japanese label in the wrong panel appears to
have produced two separate wrong attributions.

WHY THIS MATTERS BEYOND TIDINESS: kankyo-fog.md:226 is what the owner follows during a
test session, and DusklightAtmosphere.md ledger row C0 records that zHalfMin and
froxelRangeScale are still uncalibrated because the dense-fog regime was never visited.
The test plan has been sending them to the wrong place.

IF IT DOES NOT REPRODUCE: stop and report.

IN SCOPE: documentation only, in the two repos named.
OUT OF SCOPE: any code change; retuning zHalfMin or froxelRangeScale; the other 17 kytags.

DONE MEANS: all five sites name the Sacred Grove / Lost Woods; the blend direction is
stated correctly and cited; the measurement instruction tells the owner to stand at the
tag and walk out. Run python3 scripts/check_invariants.py in dusklight-ao and
python3 scripts/check_dusklight_invariants.py in dxvk-remix before pushing.

Acceptable outcome: you find Lake Hylia does also carry a kytag01 and the docs were
right. Say what you checked - note that actor placement lives in .dzs stage data, so
"Lake Hylia has no kytag01" may be unprovable from source. If so, say so.

Push only your session branch.
```

## P2 · The kasumi correction never reached the shader — [CODE, GATED]

A correction landed in the option descriptions and the documents, and not in the code
that consumes the values. The shader still implements the premise the game contradicts,
and its own comment states that premise as fact.

```
Read /home/user/dusklight-ao/docs/japanese-naming.md, especially section 6.
export LC_ALL=C.UTF-8 before grepping Japanese.

CLAIM TO VERIFY FIRST.

kasumiInner/kasumiOuter were described as the horizon haze "on the sun's side" and "away
from the sun". The game has no such split - it is front/back, and confusingly `outer` is
the NEAR band. Four game-authored labels say so, none mentioning the sun:

  dusklight-ao/src/d/d_kankyo.cpp:6369  genLabel("● 前かすみ") over the kasumi_OUTER
                                        sliders (前 mae = front/near)
  dusklight-ao/src/d/d_kankyo.cpp:6392  genLabel("● 奥かすみ") over the kasumi_INNER
                                        sliders (奥 oku = back/far)
  dusklight-ao/src/d/d_kankyo.cpp:6582  CSV header 霞手前色 / 霞奥色, column-aligned with
                                        the kasumi_outer then kasumi_inner writes
  dusklight-ao/src/d/d_kankyo_debug.cpp:301,306   prints them as kasumiFR / kasumiBR
  (and the dome actors paint one band each: d_a_vrbox.cpp:121, d_a_vrbox2.cpp:358)

That correction reached the option descriptions and the .md files ONLY. The code that
consumes both bands still implements the old premise:

  src/dxvk/shaders/rtx/pass/dusklight/dusklight_sky.comp.slang:179-186
    comment: "The game keeps two horizon colours, one for the sun's side and one for
              away from it, and this is what chooses between them."
    code:    horizonColor = lerp(cb.kasumiOuter, cb.kasumiInner, sunProximity)
  src/dxvk/shaders/rtx/pass/dusklight/dusklight_atmosphere.h:53,60
    "// Horizon haze on the sun's side." / "// Horizon haze away from the sun."

IF IT DOES NOT REPRODUCE: stop and report. Do not repair the plan.

STEP 1 (required, no behaviour change):
 - Correct the three stale comments to state the front/back split, noting `outer` is the
   near band.
 - Add the shader to the list of sites the correction touched, in
   documentation/DusklightAtmosphere.md and dusklight-ao/docs/japanese-naming.md
   section 10, so the next reader does not conclude the correction is complete.
 - Add the HIO 前かすみ/奥かすみ labels as a fourth confirmation in japanese-naming.md
   section 6, which currently cites three.
 - Also correct japanese-naming.md:332-338, which implies the correction is finished.

 *** ONE TRAP IN THE EVIDENCE. Directly above genLabel("● 奥かすみ") the decomp carries an
 English comment reading "● Inner kasumi". That is a later translator's gloss and it
 REPRODUCES THE VERY MISREADING at issue. Cite the Japanese 奥 / 前, never the English
 above it. ***

 Also note, when writing the impact: "the sky light and the fog tint inherit the rotation"
 rests on the shader's own header comment (dusklight_sky.comp.slang:39-42) saying this one
 image is the visible sky, the light it casts and the fade colour. Cite it as that, not as
 a separately traced path.

STEP 2 (the behaviour question - DO NOT silently change the look):
 The lerp places the FAR band at the sun and rotates the horizon with the sun's compass
 bearing even when the palette is static. Because that image is also the dome light and
 the fog target colour, the sky light and fog tint inherit the rotation. The game draws
 the near band over the far band at all azimuths, so the direct translation is a fixed
 composite.
 Implement the corrected blend BEHIND A NEW OPTION THAT DEFAULTS TO TODAY'S BEHAVIOUR,
 so the owner can A/B it in the Dusklight tab and decide by looking. Do not flip the
 default in this commit.

OUT OF SCOPE: the physical sky model, the fog range split, tone mapping, clouds, and the
kasumi alphas (P5).

REGRESSION SIGNATURE: with the option enabled and the blend wrong, the horizon loses its
warm side at sunrise/sunset and reads flat - sunsets are where it shows first. Step 1
cannot regress anything.

PROTOCOL: not touched.

DONE MEANS: comments and docs state the front/back split; the option exists, defaults to
current behaviour, is visible in the Dusklight tab; CI green.

Acceptable outcome: you conclude step 2 is not worth doing. Say why, plainly.

Push only your session branch. No drive-by cleanups.

FINALLY - DO NOT SKIP THIS. End your reply with a section headed "What this changes for
you": three short paragraphs in plain English - no jargon, no file paths, no option names.
  1. What was wrong before, in terms of what the game or the renderer actually did.
  2. What is better now - what the owner should expect to see, or be able to do.
  3. What is still owed - anything untested, deferred, or waiting on a play session.
The owner does not read code, and this section is how they decide what happens next. If
you concluded the change was NOT worth making, write those same three parts about that
instead - that is a real result, not a failure.
```

## P3 · Three shipped statements about bloom say the opposite of the code — [RECORD ONLY]

One of these would send a session to reimplement a feature that already exists; another
aims a scarce test window at something that cannot show a result.

```
Read docs/japanese-naming.md first. export LC_ALL=C.UTF-8 before Japanese greps.

THREE CLAIMS TO VERIFY - documentation disagreeing with shipped code. Verify each
against the code before changing a word. If one does not reproduce, correct the other
two and report the third.

CLAIM 1 - the bloom threshold's description is the inverse of the shader.
  rtx_bloom.h:152, :162, :167-170 describe rtx.bloom.dusklightThreshold in a way the
  shader at :35-42 contradicts: the key is blue-weighted and display-referred, so a
  tuner following the tooltip expects a saturated colour to bloom once any channel
  clears the threshold, when pure red at 1.0 has a key of 0.25 and never clears the
  default 0.5. dx9-fixed-function.md:431-434 repeats the wrong account while :439-450
  already has the right one. This matters because dx9-fixed-function.md calls this
  option "the one knob that needs tuning per setup".
  FIX: text only. Do NOT touch the shader - the render is correct. RtxOptions.md is
  generated on Windows; record the staleness in its header note rather than editing rows.

CLAIM 2 - kankyo-remix.md:214-215 says the mono pass and base-image weight are unported.
  Both shipped, and the same document says so ~180 lines later. Check
  bloom_dusklight_prepass.comp.slang and bloom_composite.comp.slang:74.
  FIX: state that both are ported, cited, marked CI-green / untested-in-game.
  WHY IT MATTERS: section I.5 is the statement of what exists. A session reading it
  concludes the twilight desaturation and the 0.82 dimming still need building.

CLAIM 3 - wolf senses exercises neither the mono overlay nor the base weight. The senses
  look is a low threshold with a blue-cyan bloom gain; both values allegedly under test
  sit at neutral defaults while senses is active. rtx_bloom.h:208 and
  DusklightOverlay.md:474-478 assume otherwise.
  FIX: drop "and wolf senses" from the option text; amend the overlay doc to say fixing
  the senses screen overlay is not on the critical path for that test.
  DO NOT extend this to the Palace of Twilight claim at kankyo-fog.md:149-151 - which
  bloom entry D_MN08 selects lives in stage archive data and is unverifiable from this
  tree. Leave it, or mark it unverified.

OUT OF SCOPE: the bloom shaders, the threshold value, the pyramid, tone mapping, any
retuning. This one is genuinely [RECORD ONLY] - the render is CORRECT and three documents
are wrong about it, which is the rarer direction and why the tag exists.

BUT THE TAG IS A DESCRIPTION, NOT A RESTRICTION. If you find something the CODE gets
wrong here, the tag was mis-assigned: say so plainly and fix the code, rather than filing
it as another wrong sentence. That is exactly the mistake this file is being corrected for.

DONE MEANS: the three sites agree with the code; RtxOptions.md staleness is in its header
note, not in edited rows; both invariants scripts pass.

Push only your session branch.

FINALLY - DO NOT SKIP THIS. End your reply with a section headed "What this changes for
you": three short paragraphs in plain English - no jargon, no file paths, no option names.
  1. What was wrong before, in terms of what the game or the renderer actually did.
  2. What is better now - what the owner should expect to see, or be able to do.
  3. What is still owed - anything untested, deferred, or waiting on a play session.
The owner does not read code, and this section is how they decide what happens next. If
you concluded the change was NOT worth making, write those same three parts about that
instead - that is a real result, not a failure.
```

## P4 · Effect lights: guard the word lists, do not chase them — [CODE]

Read the framing paragraph carefully — it is what keeps this session small.

```
Read docs/japanese-naming.md first, especially section 3 and the locale warning in
section 6. export LC_ALL=C.UTF-8 before grepping Japanese.
Work on origin/claude/remix-sphere-lights-system-0j781o.

READ THIS FRAMING BEFORE YOU START.

The effect-lights classifier decides WHETHER a light exists from blend mode and colour,
NOT from the effect's name. The name only picks a Class, and Class only feeds a vertical
offset, a merge tie-break, and two gates (Excluded, Burst). At stock settings
effectLightGlowOffset is 0.0, identical to Other's offset - so most name-classification
questions in this system currently change NOTHING VISIBLE. Do not spend a session
perfecting word lists that cannot move a pixel.

What is worth doing, in order:

1. A MECHANICAL GUARD. Ten of the classifier's thirty keywords match zero of the game's
   3205 effect names; the source comment and docs/effect-lights.md say three. The dead
   ones: lava, magma, youdo, bakuha, honoo, hono, taimatsu, kagarib, pika, shine.
   Verify that count yourself by replaying the lists over src/d/d_particle_name.cpp.
   Then add a check to scripts/check_invariants.py that parses the word lists out of
   classifyByName and fails on any keyword matching zero names. This branch predates
   that script - bring it over from Fixed-Function-dev.
   IMPORTANT NEGATIVE TO PRESERVE IN THE COMMENT: these are NOT romanization misses.
   taimatsu/taimatu, kagaribi/kagari, honoo/honou/homura match zero in BOTH spellings -
   the game used English (fire, torch) or different Japanese (maki 薪, kantera カンテラ,
   kaen 火炎). Adding spellings would not help. Say so, so nobody "fixes" it later.

2. A TRAP IN THE DOCUMENT. docs/effect-lights.md section 4 sizes candidates for widening
   the Excluded list - the ONE list where a name can put a light out - and quotes them in
   a single romanization. Droplet is spelled both ways in that same table: shizuku 29
   names, sizuku 26, neither a substring of the other. A session following that guidance
   literally would exclude 29 and leave 26 still lighting the room. Add both counts
   wherever the section sizes a word, and link japanese-naming.md section 3.

3. CORRECT THE RECORD on what Class does. effect_lights.hpp:20-22 and
   docs/effect-lights.md:358-360 claim Class selects "the fallback radius/reach". It does
   not - that keys on whether the vanilla light was derived. State what the code does,
   and note that Glow and Other coincide while glowOffset is 0.

EXPLICITLY DO NOT: add `burn` to the Fire list (24 names, worth a 15-unit offset, and it
would also catch a smoke effect and a scorch decal); widen the Excluded list; retune any
threshold; change the drawn/additive/glow rule.

REGRESSION SIGNATURE: none expected - this is a check and two documents. If you find
yourself changing classifier behaviour, you have exceeded the brief.

DONE MEANS: the check exists and passes; the two documents are corrected; no
classification behaviour changed.

Push only your session branch.

FINALLY - DO NOT SKIP THIS. End your reply with a section headed "What this changes for
you": three short paragraphs in plain English - no jargon, no file paths, no option names.
  1. What was wrong before, in terms of what the game or the renderer actually did.
  2. What is better now - what the owner should expect to see, or be able to do.
  3. What is still owed - anything untested, deferred, or waiting on a play session.
The owner does not read code, and this section is how they decide what happens next. If
you concluded the change was NOT worth making, write those same three parts about that
instead - that is a real result, not a failure.
```

## P5 · Two planned features are aimed at mechanisms that do not exist — [RECORD ONLY + one log line]

```
Read japanese-naming.md first. export LC_ALL=C.UTF-8 for Japanese greps.

TWO CLAIMS TO VERIFY, both saying a planned feature has no target.

CLAIM 1 - fog_avoid_tag does not touch fog.
  DusklightAtmosphere.md:432-440 says "g_env_light.fog_avoid_tag tracks a moving position
  that pushes fog away from the player. A homogeneous medium cannot express 'clear
  here'"; ledger row C6 (:510) records a permanent compromise plus a planned
  heterogeneous-fog feature to carry it. kankyo-fog.md:67 lists it as fog-modifier layer 6.
  The audit's reading: it aims a projected texture matrix at a background material -
  ordinary drawn geometry that already reaches Remix - and touches no fog state.
  CONFIRM OR REFUTE THAT YOURSELF before changing a word.

CLAIM 2 - kPalaceOfTwilightColpat = 9 is sourced from the wrong index space.
  rtx_dusklight_atmosphere.cpp:259-263 drives physicalWeight to 0, bypassing the entire
  Hillaire sky, when the pushed colpat equals 9. The constant is said to come from
  dKy_sense_pat_get - the wolf-SENSE vision pattern, a different index space from colpat.
  Verify where the 9 came from and whether any stage actually runs colpat 9.
  BE HONEST ABOUT WHAT IS UNKNOWABLE: colpat placement lives in .dzs stage data, so
  "does D_MN08 use colpat 9" may not be answerable from this checkout. If so, say
  UNKNOWN - do not guess. The guard is probably inert because skyHidden() is checked four
  lines earlier, but "probably inert" is the reasoning that produced three no-op fixes on
  this project, and the live risk runs the other way: an outdoor stage whose ENVR happens
  to use pselect slot 9 loses its physical sky for no reason.

IF EITHER DOES NOT REPRODUCE: stop on that one and report.

IN SCOPE: documentation, plus - for claim 2 only - a log line reporting the pushed colpat
and whether the 9-guard fired, so one play session settles it. That is project rule 4:
instrument before deciding.

THEN ACT ON WHAT THE LOG SAYS - do not file the result and stop. If one play session shows
the 9-guard firing on an outdoor stage, REMOVE IT in the same follow-up; that is a stage
losing its physical sky for no reason. If it shows the guard never fires, delete it as dead
code and say so. Either way it stops being a question.

OUT OF SCOPE until that log exists: removing the guard on reasoning alone, building
heterogeneous fog, retuning anything. "Probably inert" is the reasoning that produced three
no-op fixes on this project - the log is what converts it into a decision.

DONE MEANS: for claim 1, section 8.2 and row C6 either state that no work is owed or
state precisely what is; for claim 2, the constant's provenance is written down, the log
line exists, and the entry says UNKNOWN if it is.

Acceptable outcome: both claims refuted and the documents left alone. That is a real
result - write it down so nobody re-opens it.

Push only your session branch.

FINALLY - DO NOT SKIP THIS. End your reply with a section headed "What this changes for
you": three short paragraphs in plain English - no jargon, no file paths, no option names.
  1. What was wrong before, in terms of what the game or the renderer actually did.
  2. What is better now - what the owner should expect to see, or be able to do.
  3. What is still owed - anything untested, deferred, or waiting on a play session.
The owner does not read code, and this section is how they decide what happens next. If
you concluded the change was NOT worth making, write those same three parts about that
instead - that is a real result, not a failure.
```

---

# Tier 2 — retained game data, contained, needs a protocol bump

One test window covers both of these together.

## P6 · Push the three vrbox alphas the bridge throws away — [PROTOCOL]

The sky shader currently *guesses* a number the game states explicitly.

```
Read docs/japanese-naming.md first. export LC_ALL=C.UTF-8 before grepping Japanese.

CLAIM TO VERIFY FIRST.

The game authors an alpha on three of its sky palette colours and blends them every
frame exactly like the RGBs. Our bridge drops all three:

  dusklight-ao/src/dusk/remix_bridge.cpp:242-251
      formatColorS10() formats only r, g, b. The alpha is never read.
  dxvk-remix/src/dxvk/rtx_render/rtx_dusklight_env.h:116-136
      all six sky/fog colour options are Vector3, so there is no channel for it.

The three, and where the game blends them per frame:
  vrbox_kasumi_inner_col.a   d_kankyo.cpp:2847-2850  -> d_a_vrbox.cpp:124
  vrbox_kasumi_outer_col.a   d_kankyo.cpp:2827-2830  -> d_a_vrbox2.cpp:361
  vrbox_kumo_top_col.a       d_kankyo.cpp:2775-2778  -> d_a_vrbox2.cpp:324,330,342,348

The original team exposed all three as sliders, which is how we know they are deliberate:
  d_kankyo.cpp:6376  "α" under "● 前かすみ"     (front/near haze)
  d_kankyo.cpp:6399  "α" under "● 奥かすみ"     (back/far haze)
  d_kankyo.cpp:6353  "α" under "● 下雲影カラー"  (lower cloud shadow)

WHY IT MATTERS: dusklight_sky.comp.slang:79-80 derives haze thickness as
dot(kasumiInner + kasumiOuter, luma) * 0.5f and calls it "a good proxy for how thick the
air reads in a given palette". The game states that number. Two palettes wanting the same
hue at different densities currently arrive identical.

ONE CONFUSING DETAIL TO WRITE DOWN: kumo_top_col.a is the CLOUD LAYER's alpha, not the
top band's - its palette source is kumo_shadow_col.a, the CSV column is 下雲α, and the
debug view prints it as "Cloud A" not "CloudU A". Name the option for the layer.

IF IT DOES NOT REPRODUCE: stop and report.

IN SCOPE:
 - Add three floats: rtx.dusklight.env.kasumiInnerAlpha, kasumiOuterAlpha, kumoAlpha.
   Separate options - do NOT widen the existing Vector3s, which would disturb every
   existing consumer for no reason.
 - Push them from the bridge.
 - Push all three and DISPLAY them in the Dusklight tab. CONSUME NOTHING in this session.

 *** NARROWED ON REVIEW, and this is the important part. What is READ in source is only
 that these three alphas are authored per palette entry, blended every frame, and handed
 to J3D TEV colour registers. That they are opacity - still less that they are "the haze
 and cloud amounts" the shader's luminance proxy stands in for - is INFERENCE. What a
 TevColor/TevKColor alpha does depends on the TEV alpha stages inside vrbox_sora.bmd /
 vrbox_kasumiM.bmd / vrbox_kumo.bmd, and no .bmd exists in any of the three checkouts.
 So: push them, watch them across palettes and weather for one session, and only then
 decide whether either kasumi alpha belongs in hazeLevel. Wiring them on this session's
 evidence would be writing inference as finding. ***

OUT OF SCOPE: the kasumi near/far blend (P2), the cloud layer, Phase D, retuning the
physical sky constants.

PROTOCOL: wire change. Bump rtx.dusklight.env.protocol on the game side AND
kRequiredProtocol in the fork's showDusklightRemixTab IN THE SAME COMMIT on each side.
Currently 7 on Fixed-Function-dev - CHECK THE OTHER LIVE claude/* BRANCHES FIRST; the
sphere-lights branch is already at 11.

REGRESSION SIGNATURE: haze reads uniformly too thin or too thick across all weather
rather than varying with the palette; or overall sky density jumps when a palette
changes. If an alpha ever arrives as 0 where the old proxy was non-zero the horizon haze
disappears - guard for that explicitly.

DONE MEANS: three options exist and are pushed; the shader reads the two kasumi alphas
instead of guessing; the Dusklight tab shows all three; invariants and CI green. Record
in DusklightAtmosphere.md that the proxy was replaced by the game's own value, marked
UNTESTED IN GAME.

Acceptable outcome: you establish the alphas are inert in the .bmd materials - the models
are in none of the three checkouts, so this cannot be settled from source - and recommend
against wiring them. Say what you checked.

Push only your session branch.

FINALLY - DO NOT SKIP THIS. End your reply with a section headed "What this changes for
you": three short paragraphs in plain English - no jargon, no file paths, no option names.
  1. What was wrong before, in terms of what the game or the renderer actually did.
  2. What is better now - what the owner should expect to see, or be able to do.
  3. What is still owed - anything untested, deferred, or waiting on a play session.
The owner does not read code, and this section is how they decide what happens next. If
you concluded the change was NOT worth making, write those same three parts about that
instead - that is a real result, not a failure.
```

## P7 · The colpat blend arrives one third complete — [PROTOCOL]

```
Read japanese-naming.md first. export LC_ALL=C.UTF-8 before grepping Japanese.

CLAIM TO VERIFY FIRST.

Two documents describe the "gather" colpat fields as a SECOND, independent palette blend:
  dusklight-ao/docs/kankyo-fog.md:66     "a second, independent palette blend"
  dxvk-remix/documentation/DusklightAtmosphere.md:232  "the second gather colpat blend"

The game says they are the INPUT to the one blend. d_kankyo.cpp:4788-4828 (exeKankyo)
copies them into the primary fields and clears them to sentinels:
    wether_pat0 = mColpatPrevGather;  wether_pat1 = mColpatCurrGather;
    pat_ratio   = mColPatBlendGather;
and dKy_change_colpat (d_kankyo.cpp:9523-9528) writes only the gather pair. There is
exactly one pat_ratio in the blend (d_kankyo.cpp:2409).

The consequence that sentence hides: the bridge pushes ONE THIRD of that blend.
remix_bridge.cpp:1550-1551 pushes wether_pat1 alone as rtx.dusklight.env.colpat. Nothing
pushes wether_pat0 or pat_ratio. And the fork cuts hard on the single index at
rtx_dusklight_atmosphere.cpp:286. So physicalWeight steps discontinuously at the first
frame of a weather transition and stays stepped for its duration, while every colour it
blends against moves continuously - and dKy_change_colpat sets the blend to 0.0f, so at
the frame colpat changes on the wire the palette is still 100% the OLD pattern.

IF IT DOES NOT REPRODUCE: stop and report.

PART A (documentation, do regardless): fold "layer 5" into layer 1 in kankyo-fog.md
section 2 and DusklightAtmosphere.md sections 3/5.2. Say the gather fields are how tags
and events INJECT INTO the one blend, citing d_kankyo.cpp:4788-4828. While there, fix the
stale line numbers in kankyo-remix.md I.2 (setLight_palno_get is at d_kankyo.cpp:1849 not
1793; dKy_change_colpat at :9523 not 9468).

PART B (code): push wether_pat0 and pat_ratio as rtx.dusklight.env.colpatPrev and
colpatBlend (NoSave, quantized like daytime). Make resolvePhysicalWeight lerp its
weatherTerm between the two patterns by colpatBlend instead of cutting on one index.
DEFAULT-SAFE REQUIREMENT: if colpatBlend is absent or stale at 1.0, the result must equal
today's behaviour exactly. Demonstrate that property.

OUT OF SCOPE: the colpat-9 guard (P5), the kytags, the fog range split.

PROTOCOL: wire change - bump both sides in one commit; check other live branches first.

REGRESSION SIGNATURE: the stylised/physical handover becomes gradual where it used to
snap. If it becomes erratic or oscillates during weather changes, colpatBlend is being
read at the wrong point in the frame.

DONE MEANS: docs corrected; two options pushed; the lerp is in; the default-safe property
demonstrated; invariants and CI green; marked UNTESTED.

Acceptable outcome: part A only, if part B's benefit looks unproven. The documentation
correction stands alone.

Push only your session branch.

FINALLY - DO NOT SKIP THIS. End your reply with a section headed "What this changes for
you": three short paragraphs in plain English - no jargon, no file paths, no option names.
  1. What was wrong before, in terms of what the game or the renderer actually did.
  2. What is better now - what the owner should expect to see, or be able to do.
  3. What is still owed - anything untested, deferred, or waiting on a play session.
The owner does not read code, and this section is how they decide what happens next. If
you concluded the change was NOT worth making, write those same three parts about that
instead - that is a real result, not a failure.
```

---

# Tier 3 — strategic; changes what this project can offer a remaster artist

## P8 · Turn the material-identity hook back on — [CODE, high value]

This is the difference between a remaster artist identifying a surface by texture hash
and identifying it by the name the game's own artists gave it.

```
Read docs/japanese-naming.md first. export LC_ALL=C.UTF-8 before any Japanese grep.

CLAIM TO VERIFY FIRST.

The documents record that `grp=` in the material report does not work and that a correct
implementation has not been built:
  aurora-ao/docs/dx9/remix-material-interface.md  section 9 "Identification"
  aurora-ao/docs/dx9/progress.md
  aurora-ao/CLAUDE.md
Read them precisely, because they are not all making the same claim.
remix-material-interface.md:576-580 asks for a label "carrying the label from
registration" - i.e. ACTOR identity - and that genuinely is not built. A MATERIAL-name
label is a different thing, and it exists. Correct the documents to distinguish the two
rather than declaring the whole paragraph wrong.

IT IS ALREADY BUILT. Verified location and text:

  dusklight-ao/libs/JSystem/src/J3DGraphBase/J3DPacket.cpp:214-227

      void J3DMatPacket::draw() {
          ...
          mpMaterial->load();
      #if DEBUG && TARGET_PC
          if (mpMaterial->mMaterialName != nullptr) {
              char buf[64];
              snprintf(buf, sizeof(buf), "Mat: %s", mpMaterial->mMaterialName);
              GXPushDebugGroup(buf);
          }
      #endif
          callDL();                      <-- the draw is ISSUED here
      ...  (matching GXPopDebugGroup at :250)

It sits exactly where the documents say a correct implementation would have to sit, it
pushes the MATERIAL'S OWN AUTHORED NAME, and it is compiled out of every build the owner
tests with, because CI artifacts are not DEBUG.

Confirm that, then note a NEARBY BUT DIFFERENT thing so you do not conflate them:
dusklight-ao/src/d/d_drawlist.cpp:2033-2041 also pushes a debug group, under
DUSK_GFX_DEBUG_GROUPS, with a C++ type name (typeid(*dlst).name() + "::draw()"), not a
material name. That is not what this task is about.

WHY THE NAME MATTERS - verified, not speculation. The game's OWN environment system
dispatches on material names at runtime:
  dusklight-ao/src/d/d_kankyo.cpp:11399   void dKy_bg_MAxx_proc(void* bg_model_p)
  dusklight-ao/src/d/d_kankyo.cpp:11508   memcmp(&mat_name[3], "MA00", 4) == 0 || ...
Background materials carry an MAnn semantic code (MA00..MA17 all appear) plus a
descriptive romaji suffix. Real examples in the tree:
    MA00_Gake              崖 gake   = cliff
    MA00_Kusa              草 kusa   = grass
    MA00_Enkei_Tree_Color  遠景 enkei = distant scenery
So the game classifies its own surfaces by name every frame and Remix receives none of
it - cliff, grass, water and distant scenery are all just opaque legacy materials with a
TEV-derived albedo. The only alternative route today is hash-tagging textures, which is
what rule 1 exists to avoid and which cannot distinguish two uses of one texture.

IF IT DOES NOT REPRODUCE - no such push, or it pushes something else - STOP and report.
Do not build a replacement in this session.

IN SCOPE:
 - Decouple the push from `#if DEBUG`.

 THE WORK IS SMALLER THAN IT LOOKS. DUSK_GFX_DEBUG_GROUPS is ALREADY a CMake option
 (CMakeLists.txt:91, default ON only in Debug at :78-82) and ALREADY drives
 execution-time labelling elsewhere: GXScopedDebugGroup / GX_DEBUG_GROUP in
 include/helpers/gx_helper.h:16-25,73-83, the per-dDlst_base_c type label in
 src/d/d_drawlist.cpp:2013-2043, and three IF_DUSK(GXPushDebugGroup(...)) calls in
 src/d/d_kankyo_rain.cpp:4285,6241,6488. So the J3DMatPacket hook is simply the ONE push
 that uses `#if DEBUG` instead of the project's own DUSK_GFX_DEBUG_GROUPS. Nothing new
 needs inventing. A dusk::getSettings() runtime toggle is a WANT (the owner tests from a
 release artifact and cannot rebuild), not a requirement - do the cheap correct thing
 first and say whether the runtime toggle is worth the extra step. TARGET_PC stays in the
 condition either way.
 - Correct the three documents and the stale comment in aurora's gx.hpp in the SAME
   commit, so the record stops describing this as unbuilt work.

TWO MECHANICAL DETAILS - carry them or you will ship a truncation bug:
 - the push uses char buf[64] but aurora's MaxDebugGroupLabel is 48 (gx.hpp:408), and the
   "Mat: " prefix leaves 42 usable characters. Handle truncation deliberately.
 - each push allocates a std::string per material per frame in aurora's
   command_processor read_string. That is why it must be switchable, not always on.

OUT OF SCOPE, and this is the important part:
 Do NOT design a side channel to carry material class to Remix. Do NOT add water/sky/mist
 classification. Do NOT touch rtx.conf categories. The point of this session is to make
 the LOG answer the question first. Once grp= carries material names, one ordinary matrep
 session tells us how many distinct names exist, which MAnn codes occur, and whether the
 suffix is stable - and THAT is what a transport decision should be made on.

REGRESSION SIGNATURE: with the toggle on, matrep.sum lines carry a grp= that is a
material name rather than "-". Names truncated mid-word means the buffer detail was not
handled. A noticeable frame-rate drop with it on is the per-frame allocation, which is
expected and is why it is a toggle.

DONE MEANS: the toggle exists and is off by default; grp= carries material names when on;
the three documents and the gx.hpp comment no longer call this unbuilt; invariants and CI
green. Write down that the transport question is deliberately still open.

Acceptable outcome: you find the existing push unusable for a stated reason and recommend
against it. Say what you checked.

Push only your session branch.

FINALLY - DO NOT SKIP THIS. End your reply with a section headed "What this changes for
you": three short paragraphs in plain English - no jargon, no file paths, no option names.
  1. What was wrong before, in terms of what the game or the renderer actually did.
  2. What is better now - what the owner should expect to see, or be able to do.
  3. What is still owed - anything untested, deferred, or waiting on a play session.
The owner does not read code, and this section is how they decide what happens next. If
you concluded the change was NOT worth making, write those same three parts about that
instead - that is a real result, not a failure.
```

## P9 · Harvest the original developers' tuning panel — [CODE, after the harvest]

```
Read /home/user/dusklight-ao/docs/japanese-naming.md first, especially section 6.

*** export LC_ALL=C.UTF-8 before any grep -P on Japanese. The default locale here is
POSIX and grep will silently report zero matches. This is why 496 files of kana/kanji
went unnoticed for this project's whole history. ***

THE FACT (verify it, then use it):

The game ships the original team's own debug tuning panels. Counted across src/ and
include/: 3,903 genSlider, 421 genCheckBox, 1,249 genLabel, across 149 files.
src/d/d_kankyo.cpp alone has 291 sliders. Each is a machine-readable binding:

    genSlider("<Japanese label>", &<live game field>, <min>, <max>)

- a label written by the people who authored the game's look, the exact variable, and the
range they considered sane.

YOUR JOB: turn the RENDERING-RELEVANT subset into a reference document,
docs/kankyo-tuning-surface.md. Extract mechanically with a throwaway script - do not
transcribe by hand - then curate.

For each entry: the Japanese label, a translation, the C field, the range, and whether
anything currently pushes it to Remix (grep src/dusk/remix_bridge.cpp).

Start with d_kankyo.cpp, d_kankyo_wether.cpp, d_kankyo_rain.cpp, m_Do_graphic.cpp.
Ignore the gameplay panels - d_meter_HIO.cpp has 1,323 sliders and none of them matter
here; d_a_alink_HIO.inc likewise.

THREE THINGS TO FLAG LOUDLY:
 1. A label that CONTRADICTS the decomp's field name. One is already known:
    d_kankyo.cpp:5058 binds "雲影の濃さ" (cloud shadow density) to a field the decomp
    named g_env_light.mFogDensity. An m-prefixed name is a decompiler's guess; a
    genSlider label is the original team's word, and the label wins.
 2. A field the game exposes that we do not, and that a path tracer would want. Already
    spotted: bg_amb_col[1].a is labelled 水面α (water surface alpha) and bg_amb_col[3].a
    is labelled ウソFog (fake fog) - both meaningless in English, neither exposed.
 3. Anything bound to a still-unnamed field_0x... - the label names it. Note that many
    such bindings have empty label strings, so this yields less than it sounds like;
    report the real number.

BE HONEST ABOUT LIMITS. These are HIO debug panels. Do NOT claim the original host tool
can be driven in the PC port unless you establish it - the value here is that the
bindings are readable statically, which is all a specification needs to be. And do not
propose exposing 3,903 controls; the deliverable is a curated reference, and the
judgement about which few deserve to be in the Dusklight overlay is the point.

THEN ACT ON THE SHORTLIST - do not stop at the document. Take the TOP THREE, no more, and
expose them in the Dusklight tab: one option each, defaulting to the game's own value so
nothing changes until the owner moves a slider. Choose them by "a tuner would reach for
this first", not by what is easiest to wire. Three is the number because a tab full of
controls nobody has a reason to touch is the same as no tab.

CHECK THE PROTOCOL QUESTION EXPLICITLY before you wire them: whether these need a wire
bump depends on which direction they travel. A fork-side control the game reads back is
not the same as a readout the game pushes, and the answer decides whether both sides bump
in one commit. Say which it is in the commit message.

OUT OF SCOPE: the other ~3,900 sliders; consuming anything new in a shader; the grade
pass. Exposing a control is not the same as building a feature behind it - stop at the
control and let one play session say whether it earns more.

DONE MEANS: docs/kankyo-tuning-surface.md exists; every row is mechanically derived and
cited to file:line; the three flag categories are called out; it ends with a ranked
shortlist of at most 15 candidates for the Dusklight overlay with a sentence each on why.
Routed to from docs/japanese-naming.md and docs/kankyo-remix.md.

Acceptable outcome: you conclude the rendering-relevant subset is small and mostly
already covered. Say so with the numbers.

Push only your session branch.

FINALLY - DO NOT SKIP THIS. End your reply with a section headed "What this changes for
you": three short paragraphs in plain English - no jargon, no file paths, no option names.
  1. What was wrong before, in terms of what the game or the renderer actually did.
  2. What is better now - what the owner should expect to see, or be able to do.
  3. What is still owed - anything untested, deferred, or waiting on a play session.
The owner does not read code, and this section is how they decide what happens next. If
you concluded the change was NOT worth making, write those same three parts about that
instead - that is a real result, not a failure.
```

## P10 · Give the bloom table a vocabulary, and record the four unreachable presets — [RECORD ONLY]

```
Read docs/japanese-naming.md first. *** export LC_ALL=C.UTF-8 or you will find none of
the Japanese below. ***

The bloom/mood table (l_kydata_BloomInf_tbl) is the game's per-palette grade, and its
field semantics are settled by the original authors' own HIO slider labels at
d_kankyo.cpp:7073-7087 - including one the decomp left marked `// ?`.

Two names the game has for this system that our docs do not:
    飽和加算  houwa-kasan   "saturating add"         - the bloom itself
    彩度減算  saido-gensan  "saturation subtraction" - what our docs call the mono colour
and the CLEAR/SOFT modes are labelled くっきり kukkiri "crisp" and やわらか yawaraka
"soft" (d_kankyo.cpp:7074-7075).

TASK 1: add a bloom section to docs/japanese-naming.md's glossary with those labels and
the 飽和 vs 彩度 split, citing d_kankyo.cpp:7073-7087. In kankyo-remix.md I.1 cite the
label as the source for mOrigDensity so the decomp's `// ?` is visibly answered.
DO NOT rename anything in d_kankyo_data.h - the comment may gain the answer beside it,
but the member name is upstream's.
Flag the trap while you are there: m_saturationPattern and mSaturateSubtract* look like
one family in English and are not - one is a preset id, one is a desaturation amount.

TASK 2 WAS WITHDRAWN. An earlier draft asked you to document bloom presets 32-35 as four
unreachable author-made grades. That finding was REFUTED on verification: the DEBUG
assignment that would select them (d_kankyo.cpp:2540-2542) is clobbered four lines later
by an unconditional overwrite under the identical gate (:2545-2547, outside the #endif),
so nothing reads those rows even in a DEBUG build. They are scratch slots the authors'
own panel labels 空き, vacant.
The only thing worth doing, and it is optional: leave a one-line comment at :2540 saying
that override is dead on arrival, so nobody uses that panel expecting it to work. Do NOT
document the rows in kankyo-remix.md and do NOT propose a bloom-table-id override.

TASK 3: add `dalkmist` = "dark mist" to the misspelled-English list in japanese-naming.md
section 4, with one line on what it is (a Palace-of-Twilight fog-repulsion volume read
only by d_a_kytag12). Note explicitly that it is NOT a light and should not be piped to
Remix - none of the registering actors calls dKy_plight_set and the volumes gate on
puzzle switch state, so treating them as light sources would be inventing intent.

OUT OF SCOPE: any BEHAVIOUR change. The one-line comment at d_kankyo.cpp:2540 IS in
scope - a comment is record, not behaviour - and so is the `// ?` beside mOrigDensity in
d_kankyo_data.h. Do NOT rename anything in that header; the member names are upstream's
and the never-rename rule applies.

DONE MEANS: the glossary has bloom vocabulary; the `// ?` is answered in prose; 32-35 are
documented as unreachable; dalkmist is listed; invariants pass.

FINALLY - DO NOT SKIP THIS. End your reply with a section headed "What this changes for
you": three short paragraphs in plain English - no jargon, no file paths, no option names.
  1. What was wrong before, in terms of what the game or the renderer actually did.
  2. What is better now - what the owner should expect to see, or be able to do.
  3. What is still owed - anything untested, deferred, or waiting on a play session.
The owner does not read code, and this section is how they decide what happens next. If
you concluded the change was NOT worth making, write those same three parts about that
instead - that is a real result, not a failure.
```

---

## P12 · Batch the Twilight fog — the densest particle field in the game — [MEASURE FIRST]

The 2026-08-07 batching sweep left two systems, recorded as "bounded and situational
rather than weather". One of them is 2,000 particles.

```
Read docs/japanese-naming.md first. export LC_ALL=C.UTF-8 before Japanese greps.

CLAIM TO VERIFY FIRST.

On 2026-08-07 the dense kankyo weather particles were batched because rain at ~1000 draws
a frame was unusable under Remix. Two were deliberately left, described as "bounded and
situational": dKyr_mud_draw and dKyr_evil_draw. Both names read as incidental in English.
They are not the same size:

  dKyr_drawRain   rain                                mRainEff[250]    batched
  dKyr_drawSnow   snow                                mSnowEff[500]    batched
  dKyr_mud_draw   泥 mud in a 沼 numa (bog)           mEffect[100]     NOT batched
  dKyr_evil_draw  闇 yami - the Palace of Twilight    mEffect[2000]    NOT batched
                  fog that forces wolf form

Verify: include/d/d_kankyo_wether.h - EF_EVIL_EFF mEffect[2000] - and
src/d/d_kankyo_rain.cpp:6432 and :6719, which still emit GXBegin(GX_QUADS, GX_VTXFMT0, 4)
per quad while rain at :3264 uses GX_AUTO hoisted above its loop.

dKyr_evil_draw2 adds up to another ~1000 on top.

*** DO NOT REPEAT "three times the rain case" - that figure was withdrawn on review.
2000 and 1000 are ARRAY BOUNDS and loop trip counts, not per-frame draws: each particle
must also pass mStatus != 0, field_0x38 <= 9000, the screen-space reject at :6588-6597
(which runs whenever fovy > 40, i.e. normally) and sp54 > 0.000001f before it draws, and
draw2 skips even indices and has a D_MN08-room-1 i < 1600 cull at :6318. The honest
statement is: the loop is bounded by nothing Remix cares about, and the pre-cull bound is
2000 + 1000. The actual cost is UNMEASURED - which is why the measurement below is not
optional. ***

IF IT DOES NOT REPRODUCE: stop and report.

MEASURE BEFORE YOU CHANGE ANYTHING. Nobody has played the Palace of Twilight under Remix
and reported a frame rate - the count is read from source, the symptom is predicted, not
observed. Aurora already logs `dx9.draws frames=600 mean=... peak=...` every 600 frames.
Ask for one log standing in D_MN08 first. If peak is not in the thousands there, the
diagnosis is wrong and you should say so rather than batching anyway.

IN SCOPE (after the measurement supports it):
 - Batch dKyr_evil_draw and dKyr_evil_draw2 exactly the way rain was: hoist one
   GXBegin(GX_QUADS, GX_VTXFMT0, GX_AUTO) above the loop, with the per-quad GXBegin
   demoted to IF_NOT_DUSK.
 - color_reg0 is already in GX_VA_CLR0. color_reg1 needs the second GX colour channel
   (GX_VA_CLR1 / GX_COLOR1A1), or folding into CLR0 if the TEV expression permits.
   Whichever you choose, say why in the commit.
 - Do dKyr_mud_draw in the SAME change - it is trivial there (the only per-particle state
   is color_reg0.a plus a redundant GXLoadTexObj) - but do NOT prioritise it on its own.
   It is 100 quads in Diababa's boss room.

OUT OF SCOPE: the particle simulation, spawning, pathing; drawVrkumo (that is P13);
anything about how the fog looks.

REGRESSION SIGNATURE: the Twilight fog vanishing, drawing in one flat colour, losing its
per-particle fade, or a `GX_AURORA_DRAW_SIZED` / vertex-count-mismatch assertion in the
log. All three mean the vertex colour is not reaching the TEV stage.

DONE MEANS: both evil draws and mud are batched; a before/after dx9.draws peak from
D_MN08 is recorded; CI green; the entry in docs/remix-open-issues.md issue 13 is updated
to say these are no longer outstanding and why the priority was inverted.

Acceptable outcome: the measurement shows the area is fine and you recommend leaving it.

Push only your session branch.

FINALLY - DO NOT SKIP THIS. End your reply with a section headed "What this changes for
you": three short paragraphs in plain English - no jargon, no file paths, no option names.
  1. What was wrong before, in terms of what the game or the renderer actually did.
  2. What is better now - what the owner should expect to see, or be able to do.
  3. What is still owed - anything untested, deferred, or waiting on a play session.
The owner does not read code, and this section is how they decide what happens next. If
you concluded the change was NOT worth making, write those same three parts about that
instead - that is a real result, not a failure.
```

## P13 · The same kasumi mistake, one file away — [RECORD ONLY + MEASURE FIRST]

```
Read docs/japanese-naming.md first. export LC_ALL=C.UTF-8 before Japanese greps.

CLAIM TO VERIFY FIRST.

The fork describes kumoTop as "the game's lit cloud colour" and kumoBottom as "the game's
shaded cloud underside colour" (rtx_dusklight_env.h:131-136). The game's own labels say
otherwise, in three places:
  d_kankyo.cpp:6302  genLabel("● 上雲カラー")   upper cloud   -> kumo_top sliders
  d_kankyo.cpp:6324  genLabel("● 下雲カラー")   lower cloud   -> kumo_bottom sliders
  d_kankyo.cpp:6582  CSV header 上雲色,下雲色,下雲影色
  d_kankyo_debug.cpp:288,293   "CloudU R" / "CloudD R"
and the ONE site that consumes both lerps them by horizontal distance from the camera
(d_kankyo_rain.cpp:5025-5039) - a zenith-to-horizon gradient across the cloud field, not
a lighting term.

This is the same shape of error as the kasumi pair, in the adjacent fields. Nothing
renders wrong today because none of the three is consumed - the damage is that these
descriptions are the spec a future clouds phase will build from, and "lit vs shaded
underside" leads to a physically-lit cloud model where the game means a distance gradient
it already ships a closed-form recipe for.

IF IT DOES NOT REPRODUCE: stop and report.

IN SCOPE (documentation):
 - Correct the three descriptions to the game's terms: kumoTop = upper/overhead cloud
   band, kumoBottom = lower/horizon cloud band, kumoShadow = the LOWER cloud's shadow
   (下雲影, not a generic cloud shadow).
 - Record that kumo_top_col.a is the whole cloud LAYER's alpha, not the top band's - the
   CSV column is 下雲α and the debug view prints it as "Cloud A".
 - Write down the recipe the clouds phase will want: lerp top->bottom by
   (1 - mDistFalloff), with per-layer factors 0.8 and 0.92 for cloud textures 2 and 3
   (d_kankyo_rain.cpp:5031-5039).
 - Update ledger entry C3 in DusklightAtmosphere.md so Phase D starts from this.
 - DO NOT restate any claim about how they LOOK. That was the trap the first time.

SEPARATE, AND MEASURE FIRST: drawVrkumo - the skybox cloud billboards - was missed by the
batching sweep and costs up to a few hundred unbatched draws every outdoor frame. These
are the ONLY clouds in the image, since the fork consumes none of the cloud colours. Ask
for one dx9.draws peak from an outdoor cloudy scene before deciding whether to batch it.
Do NOT add a hideVrkumo switch - today, removing these billboards removes the clouds.

DONE MEANS: the three descriptions match the game's labels; the recipe and the alpha note
are written down; C3 is updated; the vrkumo measurement is either taken or explicitly
requested. Invariants pass.

FINALLY - DO NOT SKIP THIS. End your reply with a section headed "What this changes for
you": three short paragraphs in plain English - no jargon, no file paths, no option names.
  1. What was wrong before, in terms of what the game or the renderer actually did.
  2. What is better now - what the owner should expect to see, or be able to do.
  3. What is still owed - anything untested, deferred, or waiting on a play session.
The owner does not read code, and this section is how they decide what happens next. If
you concluded the change was NOT worth making, write those same three parts about that
instead - that is a real result, not a failure.
```

## P14 · `hideSkyBillboards` deletes the star field — [CODE]

```
Read docs/japanese-naming.md first.

CLAIM TO VERIFY.

rtx.dusklight.game.hideSkyBillboards is tested in exactly two places
(d_kankyo_wether.cpp:106 the star packet, :176 the sun packet). The recommended rtx.conf
enables it, so under the recommended setup there are no stars at night at all - and the
loss reportedly includes a 13-star 北斗 (Big Dipper) constellation the original team
placed by hand. Verify that the constellation exists before repeating the claim.

The switch was aimed at the MOON. The moon quad is drawn in dKyr_drawSun, not
dKyr_drawStar - a different gate - so the star half has never been shown to be
load-bearing. The tested 2026-07-29 observation was about shadow coverage wandering with
the camera; no test isolated the star packet from the sun packet.

IN SCOPE: split the one setting into two so the star packet can be enabled independently
of the sun/moon packet, and correct the comment at d_kankyo_wether.cpp:88-98 either way -
right now it attributes the moon to the wrong function, so the next person re-derives it.

THEN ONE TEST, which the owner runs: night, outdoors, stars ON and moon OFF. Does shadow
coverage still follow the camera? If it does, say so explicitly in that comment.

OUT OF SCOPE: the generated sky, painting the moon into the dome, anything about the sun.

PROTOCOL: adding a game-side setting - check whether it needs a bump and whether another
live branch has taken the next number.

DONE MEANS: two settings exist, defaults preserve today's behaviour, the comment is
correct, and the test is written into docs/remix-test-playbook.md.

Acceptable outcome: you find the stars do occlude and the single switch was right. Record
it so nobody re-opens it.

FINALLY - DO NOT SKIP THIS. End your reply with a section headed "What this changes for
you": three short paragraphs in plain English - no jargon, no file paths, no option names.
  1. What was wrong before, in terms of what the game or the renderer actually did.
  2. What is better now - what the owner should expect to see, or be able to do.
  3. What is still owed - anything untested, deferred, or waiting on a play session.
The owner does not read code, and this section is how they decide what happens next. If
you concluded the change was NOT worth making, write those same three parts about that
instead - that is a real result, not a failure.
```

## P15 · Six time-slot comments in the game tree are wrong — [CODE]

```
Read docs/japanese-naming.md first. export LC_ALL=C.UTF-8 before Japanese greps.

CLAIM TO VERIFY FIRST.

The game names its six canonical time lights in Japanese in three independent debug
surfaces - 朝0 / 朝1 / 昼 / 夕0 / 夕1 / 夜 - and pins each to an exact daytime value:
90, 105, 165, 255, 285, 345.

Six ENGLISH comments in src/d/d_kankyo.cpp:6779-6790 mistranslate them by five to six
hours, telling a reader slot 0 is midnight and slot 3 is noon when they are 06:00 and
17:00. The same file already carries correct glosses at :6830-6851. One of the six errors
has already propagated into docs/kankyo-remix.md:99, which says "2 afternoon" - it is
昼 hiru, MIDDAY.

These are comments and prose, not symbols, so the never-rename rule does not apply.

IN SCOPE:
 - Correct the six comments to match the correct glosses the same file already has.
 - Correct d_s_menu.cpp:649 「ひる固定」 from "Fixed Afternoon" to "Fixed Midday".
 - Correct docs/kankyo-remix.md:99 to "2 midday (昼 hiru - holds 09:00-16:00)".
 - Add the six slot names and their pinned daytime values to the japanese-naming
   glossary, since the game supplies both.
 - Since the wrong comments came in with the import, offer the same correction upstream
   to zeldaret/tp, per docs/code-conventions.md.

SECOND, SEPARATE ITEM (fork-side, no protocol bump): the overlay's time presets in
showDusklightTimeOfDay (dxvk_imgui.cpp:2811-2814) reach only FOUR of the game's six
palette slots, and the game ships the two missing numbers. Extend kPresets with the
game's own six values and its own names - Morning 0 (90), Morning 1 (105), Midday (165),
Evening 0 (255), Evening 1 (285), Night (345) - and correct the "four the light actually
differs at" comment.
WHY IT MATTERS: every per-slot colour, fog and sky comparison made through that overlay
has been made against four of the six slots the palette data contains. Whether that has
actually skewed the tuning is INFERENCE, not established - say so.

OUT OF SCOPE: pushing startTimeLight/endTimeLight/color_ratio as readouts. That is a
protocol bump and a separate, later item; note it as a follow-up rather than doing it.

DONE MEANS: the six comments and the two docs are correct; the overlay reaches all six
slots; invariants and CI green.

FINALLY - DO NOT SKIP THIS. End your reply with a section headed "What this changes for
you": three short paragraphs in plain English - no jargon, no file paths, no option names.
  1. What was wrong before, in terms of what the game or the renderer actually did.
  2. What is better now - what the owner should expect to see, or be able to do.
  3. What is still owed - anything untested, deferred, or waiting on a play session.
The owner does not read code, and this section is how they decide what happens next. If
you concluded the change was NOT worth making, write those same three parts about that
instead - that is a real result, not a failure.
```

## P16 · Turn on the texture dump that already exists — [CODE, one line]

**Demoted to fix-in-passing 2026-08-11, by the owner, on a fact no audit had.** The
capability this item unlocks **is already available outside the game**, twice over:

- **A GameCube emulator dumping from the same ISO produces the filenames this path
  expects — VERIFIED 2026-08-11 by the owner**, who ran it and confirmed the output was
  exactly as expected. Aurora's `format_replacement_filename` emits Dolphin's convention
  on purpose (`aurora-ao/docs/dx9/texture-replacements.md` opens by saying
  "Dolphin-format replacement packs"; pack compatibility is why the format was chosen).
  So the emulator's own dump, with its browser and preview tooling, already answers
  "which texture is this" for a pack author.
- **Remix scene captures already pull every texture present at the moment of capture** —
  under Remix's hash naming rather than the pack key, so they identify the *image* but not
  the pack filename.

That leaves the in-engine dump a convenience duplicate of a mature external tool. It is
still correct, still one line, and still worth flipping — but **it does not earn a
scheduled session on its own.** Do it when a session is already editing `settings.{h,cpp}`
or `m_Do_main.cpp`.

> **⚠ Verified 2026-08-11, and still not done.** `src/m_Do/m_Do_main.cpp:646` reads
> `config.allowTextureDumps = false;` on `Fixed-Function-dev` today. The verification the
> owner ran confirmed the *external* route works; it did not make this line change itself,
> and demoting the item quietly turned "one line, do it in passing" into "nobody did it".
>
> **This is the smallest example of the problem this whole file was rewritten for**, and
> it should be the first thing folded into the next session that opens either file — which
> P14 and P9 both do.

**The one thing that would revive it:** a texture the emulator route does not cover. The
in-engine dump is keyed the way the runtime keys by construction, so it would settle any
such case. None has been found.

**Earlier correction, kept because it is the audit's best example of verification paying
off.** The first draft of this item asked for a tool to join pack filenames to game
texture names. That was refused on review, correctly: **aurora already writes exactly
that**, and it is off because of one hardcoded line.

```
THE SITUATION, verified.

An artist making an HD pack matches hex filenames by eye. But aurora already contains the
answer:

  aurora-ao/lib/gfx/texture_replacement.cpp:999-1001
      if (aurora::g_config.allowTextureDumps) { dump_editable_texture_dds(key, obj); }
  aurora-ao/lib/gfx/texture_replacement.cpp:949-952
      dumpRoot = <cachePath>/"texture_dumps";
      path     = dumpRoot / format_replacement_filename(key);
      write_rgba8_dds(path, texWidth, texHeight, pixels.data);

That writes the texture's own decoded image to THE EXACT FILENAME a pack file must carry.
A picture named with the key is a better answer to "which texture is this" than a BTI name
would be - BTI names collide (`dummy`, `Zbuffer`).

The empty-registry early-outs are explicitly bypassed when dumps are on
(texture_replacement.cpp:1314, :1325, :1337, :1348), so it works with no pack installed.

It is disabled by ONE HARDCODED LINE:

  dusklight-ao/src/m_Do/m_Do_main.cpp:646
      config.allowTextureDumps = false;

Verify all of the above before changing anything.

IN SCOPE - and keep it this small:
 - Replace the hardcoded false with an existing-style setting
   (dusk::getSettings().game.*), defaulting OFF, so the owner can turn it on from a
   release CI artifact without a rebuild. Follow the pattern of the neighbouring lines,
   which already read from getSettings().
 - Document it in aurora-ao/docs/dx9/texture-replacements.md: what it writes, where, and
   that it works with no pack installed. Paste one run's directory listing as a worked
   example.

OUT OF SCOPE - explicitly, because the earlier draft of this item proposed all of it and
it is not needed: do NOT build a name-join pass over J3DModelData; do NOT add an aurora
wrapper mirroring has_replacement; do NOT add a TARGET_PC accessor for
J3DTexture::mpTexObj; do NOT change how textures are keyed or hashed.

REGRESSION SIGNATURE: with the setting on, a texture_dumps directory fills with .dds
files whose names match pack filenames. Disk use grows while it is on - that is why it
defaults off and why the doc should say so. Nothing in the image changes either way.

DONE MEANS: the setting exists and defaults off; one run's output is documented; nothing
else changed.

Acceptable outcome: you find the dumped filename does NOT match what a pack directory
needs. Then say so - that is the one thing that would justify revisiting the join idea.

FINALLY - DO NOT SKIP THIS. End your reply with a section headed "What this changes for
you": three short paragraphs in plain English - no jargon, no file paths, no option names.
  1. What was wrong before, in terms of what the game or the renderer actually did.
  2. What is better now - what the owner should expect to see, or be able to do.
  3. What is still owed - anything untested, deferred, or waiting on a play session.
The owner does not read code, and this section is how they decide what happens next. If
you concluded the change was NOT worth making, write those same three parts about that
instead - that is a real result, not a failure.
```

## P18 · Grass is also flowers, and blobShadows drops far more than we say — [CODE]

```
Read docs/japanese-naming.md first. export LC_ALL=C.UTF-8 before Japanese greps.

TWO CLAIMS TO VERIFY, both in the same area, both record-only.

CLAIM 1 - perBladeGrass covers grass but not flowers.
  daGrass_c is a grass AND flower actor: kind 0 is 草 kusa, kinds 2 and 3 are 花 hana.
  perBladeGrass exists to stop the batched path churning the asset hash, and it covers
  dGrass_packet_c only. dFlower_packet_c has the identical churning batch and appears in
  NO document in any of the three repos.
  Verify in d_grass.inc and d_flower.inc (or wherever the packets live) and in the
  option's implementation.
  WHY IT MATTERS: the option is named for the English word "grass" and the open issue is
  titled "Grass patches shade wrongly", so nothing signals that half the vegetation the
  same actor spawns is untouched. If flowers show the same symptom, toggling the switch
  will not move it - which reads as the diagnosis being wrong.
  FIX - BOTH HALVES, record then code.
  RECORD: state in docs/remix-open-issues.md issue 7, and in the option text in
  dxvk-remix/src/dxvk/rtx_render/rtx_dusklight_game.h, that the switch covers
  dGrass_packet_c only and that the flower packet still batches.
  CODE: add the sibling switch - perBladeFlowers, DEFAULTING OFF, a mechanical port of the
  grass one over dFlower_packet_c.
  Do NOT widen perBladeGrass itself to cover flowers. Its description promises grass and
  its cost profile differs, so widening it silently changes what an already-shipped option
  means - and the grass half has not been tested in game even once, so there is nothing to
  widen from. A separate switch costs one option and lets the next test session answer
  both questions in one trip rather than two, which is the whole point: the owner's play
  windows are the scarce resource, not the options list.

CLAIM 2 - blobShadows drops far more than four documents say.
  It is described as covering shadows "under rupees, hearts and pots". It reportedly drops
  the simple ground shadow of EVERY actor that registers one - items, objects, insects,
  enemies, NPCs, cutscene actors - across 48 call sites. Verify the call-site count
  yourself.
  The tested-good claim (2026-08-06) is therefore much narrower than what changed. The
  reasoning still holds for anything whose caster geometry reaches Remix, so this is a
  prose defect, not a behaviour defect - but a session reading "rupees, hearts and pots"
  would not predict that an NPC's ground shadow is affected.
  FIX: correct the four descriptions to say the switch drops the game's simple ground
  shadows for every actor that registers one, and leaves the projected system alone.
  dDlst_shadowControl_c::setReal is genuinely untouched - that half is accurate, keep it.
  While there, record that the game calls the projected shadows リアル影 ("real kage") in
  its own debug labels, and that "blob shadow" is OUR coinage - the simple class has no
  Japanese name anywhere in the tree.

THIRD, SMALLER: open issue 11 attributes a black shadow quad to a projected texture
transform, but the item shadows it names are the class that does NOT use one, and the fix
it proposes has already shipped for that class. Rewrite it to state which shadow class it
is about, and note that its requested confirmation now DISCRIMINATES: a matrep.gx line
showing GX_TG_MTX3x4 means the real-shadow class and the diagnosis stands; GX_TG_MTX2x4
from GX_TG_TEX0 means the simple class and it does not. Do not build anything.

OUT OF SCOPE: adding a flower switch; changing blob-shadow behaviour; the four
stage-authored grass types (that is a separate, instrumentation-first item - and note it
must NOT plan on a spare D3DMATERIAL9 channel: as of 2026-08-11 there is exactly one,
`Ambient.a`, and it is already claimed by an unmerged branch. See P17).

DONE MEANS: issue 7, issue 11, the two option descriptions and the shadow prose all say
what the code does; invariants and CI green. No behaviour changed.

FINALLY - DO NOT SKIP THIS. End your reply with a section headed "What this changes for
you": three short paragraphs in plain English - no jargon, no file paths, no option names.
  1. What was wrong before, in terms of what the game or the renderer actually did.
  2. What is better now - what the owner should expect to see, or be able to do.
  3. What is still owed - anything untested, deferred, or waiting on a play session.
The owner does not read code, and this section is how they decide what happens next. If
you concluded the change was NOT worth making, write those same three parts about that
instead - that is a real result, not a failure.
```

## P19 · The room's authored lights are live every frame and nothing reads them — [PROTOCOL, high value]

The largest single piece of game-derived data we are throwing away, and it is the only
light source in the game that carries a **cone**.

```
Read docs/japanese-naming.md and, on the sphere-lights branch, docs/effect-lights.md §0
before starting. export LC_ALL=C.UTF-8 before Japanese greps.

CLAIM TO VERIFY FIRST.

  include/d/d_kankyo.h:259       DUNGEON_LIGHT dungeonlight[8];
  src/d/d_kankyo.cpp:8664-8671   refreshed every frame from the CURRENT room:
                                   mPosition, mRefDistance, mCutoffAngle,
                                   mAngleAttenuation (spot function),
                                   mDistAttenuation, mAngleX, mAngleY

Up to six authored lights per room - several of them spotlights, some switch-gated, with
palette-blended colour - and the only reference anywhere in our port is a stub
constructor. Nothing reads it. Confirm that yourself.

Also confirm: the bridge hardcodes sphere.shaping_hasvalue = 0 (remix_bridge.cpp:1389),
so even the lights it DOES forward discard cone shape - and the room lights are the ones
that actually have cones.

READ THIS BEFORE PROPOSING ANYTHING, because it is the argument against this work:
docs/effect-lights.md §0 argues, correctly, that GameCube point lights were placed where
the SHADING looked best rather than where a light physically is, and that a path tracer
exposes that - which is why the effect-lights system derives placement from the effect
that draws the fire instead. Room lights are authored placements and inherit that
criticism. Your job is to weigh it honestly, not to route around it. What is different
about these: they are a DIFFERENT registry from the one localLights mirrored, they are
the only source with cone data, and interiors today are lit only by what the effect
emitters and the fallback light supply.

IF IT DOES NOT REPRODUCE: stop and report.

IN SCOPE:
 *** THE WORK IS SMALLER THAN IT LOOKS, and this was confirmed on review. DUNGEON_LIGHT
 embeds an mInfluence - a LIGHT_INFLUENCE, the EXACT struct the bridge's forwarding loop
 already speaks. It is populated ONLY inside dungeonlight_init() (d_kankyo.cpp:1157-1162),
 from a table of y = -99999 and colour {0,0,0}, and never re-derived - while the raw
 fields beside it (mPosition, mColor, mCutoffAngle, ...) ARE refreshed every frame at
 :8664-8671. So the task is "re-derive mInfluence from the live fields and let the existing
 loop carry it", not "design a second light path and a side channel".
 It also means ANYONE WHO READS dungeonlight[i].mInfluence TODAY SEES DEAD DATA and will
 conclude, wrongly, that the room lights are not there. Do not let that stop you. ***

 - Forward dungeonlight[0..5] as Remix sphere lights, guarded EXACTLY as the game guards
   them: skip unless dComIfGp_roomControl_getStatusRoomDt(stayRoom)->getLightVecInfo() is
   non-NULL; skip i >= getLightVecInfoNum() (capped at 6); skip slots 0-1 when
   dKy_SunMoon_Light_Check() is TRUE (the celestial light already covers those); skip a
   zero colour. Verify each guard in the game before copying it.
 - Populate remixapi_LightInfoSphereEXT shaping from mCutoffAngle + mAngleX/mAngleY
   whenever mAngleAttenuation != GX_SP_OFF, and leave shaping off otherwise.
 - INSTRUMENT BEFORE SHIPPING, per project rule 4: push a roomLightsFound /
   roomLightsDrawn pair the way localLightsFound/localLightsDrawn already work, so "this
   room has none" and "we dropped them" stay distinguishable in a log.
 - OFF BY DEFAULT. The double-counting question against effect lights must be settled
   from one log, not from an argument.

INSTRUMENT BEFORE ANY TRANSPORT WORK. Log, once per room change, how many of the eight
entries have a non-degenerate mPosition, what their mAngleAttenuation is, and their
blended mColor. One session through two dungeons answers whether this is six lights per
room or two, and whether they duplicate the emitter lights the bridge already sends.

OUT OF SCOPE: touching localLights (being retired); changing effect-lights; the
fallback light; anything about outdoor lighting. DO NOT do this at the same time as
retiring local lights - two light changes at once cannot be judged from one test.

PROTOCOL: wire change - bump both sides in one commit, and check the other live branches
first. The sphere-lights branch is at 11.

REGRESSION SIGNATURE: interiors becoming double-lit (every fire with two lights, one
offset) means it is double-counting with effect lights - that is what the off-by-default
plus counters exists to catch. Shadows appearing to come from the wrong place means the
authored-placement criticism above is real for this registry too, and the honest answer
is then to say so rather than to tune around it.

DONE MEANS: the option exists and defaults off; the counters are pushed and displayed;
one log from an interior shows found/drawn; invariants and CI green; marked UNTESTED.

Acceptable outcome: you conclude the authored placements read wrong under a path tracer
and recommend against it, with the log to back it. That is a real result.

Push only your session branch.

FINALLY - DO NOT SKIP THIS. End your reply with a section headed "What this changes for
you": three short paragraphs in plain English - no jargon, no file paths, no option names.
  1. What was wrong before, in terms of what the game or the renderer actually did.
  2. What is better now - what the owner should expect to see, or be able to do.
  3. What is still owed - anything untested, deferred, or waiting on a play session.
The owner does not read code, and this section is how they decide what happens next. If
you concluded the change was NOT worth making, write those same three parts about that
instead - that is a real result, not a failure.
```

## P20 · Three of four background-ambient layers never cross the wire — [PROTOCOL]

The coverage answer §3 asked for: **of the 30 live environment fields the original team
put a slider on, 11 reach Remix.**

```
Read docs/japanese-naming.md first. export LC_ALL=C.UTF-8 before Japanese greps.

CLAIM TO VERIFY FIRST.

The game maintains FOUR background-ambient layers (bg_amb_col[0..3]) and routes them to
different material classes. The bridge sends ONE, as rtx.dusklight.env.bgAmbient - and
that option's description claims to cover room and terrain geometry generally.

The other three are not spare padding. Their alphas carry named, authored meanings the
original team put sliders on:
    bg_amb_col[1].a   水面α    "water surface alpha"
    bg_amb_col[2].a   補佐α    "auxiliary alpha"
    bg_amb_col[3].a   ウソFog  "fake fog"
(d_kankyo.cpp, the ambient panel - find the exact lines yourself.)

Verify which material classes the game routes each layer to before proposing anything.
That routing is the whole question: if all four land on geometry Remix already relights,
sending them buys nothing.

IF IT DOES NOT REPRODUCE: stop and report.

IN SCOPE:
 - Correct the bgAmbient description first, regardless of what else you do: it should say
   which layer it carries.
 - If, and only if, the routing shows the layers are meaningfully different, push the
   remaining three as separate options and display them. Consume nothing in this session.

ALSO IN SCOPE, and cheap: two rtx.dusklight.* option descriptions were found to be
paraphrases nobody read out of the game. Find them by checking each description against
the game field behind it - the kasumi pair is the worked example of how that goes wrong -
and correct them.

OUT OF SCOPE: consuming the new ambients; the grade pass; anything about the four
material classes themselves.

PROTOCOL: wire change if you add options. Bump both sides in one commit.

DONE MEANS: bgAmbient's description is accurate; the routing is written down; any new
options are pushed and displayed but not consumed; invariants and CI green.

Acceptable outcome: the routing shows one layer is enough. Say so with the citation -
that closes a question §3 opened.

FINALLY - DO NOT SKIP THIS. End your reply with a section headed "What this changes for
you": three short paragraphs in plain English - no jargon, no file paths, no option names.
  1. What was wrong before, in terms of what the game or the renderer actually did.
  2. What is better now - what the owner should expect to see, or be able to do.
  3. What is still owed - anything untested, deferred, or waiting on a play session.
The owner does not read code, and this section is how they decide what happens next. If
you concluded the change was NOT worth making, write those same three parts about that
instead - that is a real result, not a failure.
```

---

# Tier 4 — only with the owner watching

## P11 · The kasumi blend itself

This is step 2 of **P2**, and it is listed separately because it is a look judgement, not
a correctness fix. Ship it behind an option defaulting to today's behaviour, then decide
by A/B in one session. Do not flip the default without the owner seeing both.

---

## Sequencing summary

**✅ Done:** P17 (channel collision), P0 (effect-lights merged, protocol 11), P1 (Lost
Woods fog tag — record corrected; the measurement it points at is still owed).

| When | Run | Needs a play-test? |
| :-- | :-- | :-- |
| **Next** | **P2** — the shader half. The one open case of a correction that reached the docs and not the code | no, it ships gated |
| Then — record corrections, in passing | P3, P10, P13, P18 (record half), P5 (record half) | no |
| Then — contained code | P4, P14, P15, **P16 (one line, overdue)** | no |
| Then — the strategic reads, each ending in a change | P8 (material identity), P9 (tuning panel → 3 controls) | no |
| Measure, then act | P12 (Twilight fog), P13 (vrkumo half) | one log, then a change |
| Then — retained game data | P19 (room lights, off by default), P20 (ambient layers) | yes, one window |
| Together in one window | P6, P7 | yes, one window |
| Last | P11 (= P2 step 2) | yes, A/B |

**Three items are one log away from being decided rather than discussed** — P12 (a
`dx9.draws` peak from the Palace of Twilight), P13's vrkumo half (the same, outdoors and
cloudy), and P5's colpat-9 guard. Ask for those three samples in the *same* play session;
they do not conflict and it collapses three test windows into one.

**P16 is one line and has been verified since 2026-08-11.** Fold it into whichever session
opens `settings.{h,cpp}` or `m_Do_main.cpp` first — P14 and P9 both do.

**P19 walks into a trap, and it is the approach the prompt recommends that walks into it.**
Corrected 2026-08-11 — an earlier version of this paragraph called it "an unpaid debt from
P0" and said every forwarded light discards its cone. **That was wrong**, and the real
shape is worth stating precisely:

- **Nothing is discarding a cone today.** `sphere.shaping_hasvalue = 0` appears at two
  sites — `updateLocalLights` (`remix_bridge.cpp:1455`) and `updateEffectLights` (`:1702`)
  — and it is **correct at both**, because neither path carries cone data to begin with.
  `LIGHT_INFLUENCE` (`d_kankyo.h:17-23`) is position, colour, power, fluctuation, index:
  **no angle fields at all.** The effect-light `Site` (`effect_lights.hpp:41-55`) likewise.
- **The one cone-bearing source the effect lights touch, they read for colour only, on
  purpose.** `gatherVanillaLights` harvests `BOSS_LIGHT` at `effect_lights.cpp:1441` and
  passes `reach = 0, reachKnown = false` under a comment reading "COLOUR ONLY,
  deliberately". `mCutoffAngle`, `mAngleX` and `mAngleY` are never read. The `spot` bool it
  sets reaches the log and nothing else.
- **The cone data exists only in `DUNGEON_LIGHT` and `BOSS_LIGHT`, and neither is
  forwarded.** So P19 is not paying off a debt; it is the first thing that would ever have
  a cone to send.

**Here is the trap.** P19's prompt recommends re-deriving `mInfluence` from
`DUNGEON_LIGHT`'s live fields and letting the existing forwarding loop carry it — which is
good advice for the transport and fatal for the cone. `mInfluence` is a `LIGHT_INFLUENCE`
embedded at offset `0x2C`, and **the cone fields sit outside it**, at `0x18`–`0x24`. So the
recommended route drops the cone *structurally*, before it ever reaches the hardcoded
`shaping_hasvalue = 0`. Both would have to be fixed, and neither failure is visible in a
diff.

Populating it is small — `remixapi_LightInfoLightShaping` is a normalized direction,
`coneAngleDegrees`, `coneSoftness` and `focusExponent` — but mapping GX's
`mAngleAttenuation` spot function onto those last two is an **approximation nobody has
characterised**, not a transcription. Treat it as its own step with its own regression
signature.
