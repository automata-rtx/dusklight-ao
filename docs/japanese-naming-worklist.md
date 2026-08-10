# Worklist: prompts to run from the naming audit

Ready-to-paste prompts for separate Claude Code sessions, produced by
[`japanese-naming-audit.md`](japanese-naming-audit.md).

**Each fenced block is one session.** Copy the block, paste it, let it run. Do not
combine two of them into one session — several are deliberately split because they have
different regression signatures, and a change that alters two things at once cannot be
judged from one test.

---

## How these are written, and why it matters

Every prompt carries the same nine parts, in the same order:

1. **Read first** — `CLAUDE.md` loads automatically; `docs/japanese-naming.md` does not.
2. **The claim, as something to re-verify** — never as an instruction to implement.
   File and line are given so checking is cheap.
3. **A stop clause** — *"if it does not reproduce, stop and report; do not repair the
   plan."* This is the most important line in every prompt. Without it a session will
   find some way to do the work it was told to do.
4. **In scope** — the exact files that may change.
5. **Out of scope** — named explicitly, including the tempting adjacent work.
6. **Regression signature** — what it looks like if the change is wrong.
7. **Protocol** — whether both sides must bump in the same commit.
8. **Done means** — a checkable end state.
9. **An escape hatch** — *"acceptable outcome: you conclude this is not worth doing."*
   Named as a success, not a failure. This is the main defence against the audit
   generating work for its own sake.

Tags: **[DOC ONLY]** no code changes · **[SAFE]** contained change, no protocol bump ·
**[PROTOCOL]** touches the game↔DLL wire, both sides bump together ·
**[RESEARCH FIRST]** investigate and report, not authorised to change behaviour.

### Two standing warnings that belong in every session

```sh
export LC_ALL=C.UTF-8      # or grep -P finds none of the game's Japanese, silently
```

**Protocol is at 7 on `Fixed-Function-dev` and 11 on the effect-lights branch.** Any
prompt tagged [PROTOCOL] must check the other live `claude/*` branches before taking a
number. Two branches claiming one number is a trap this project has already hit, and the
merge conflict resolves *cleanly* into a wrong answer.

---

# Tier 0 — the two time-critical items, neither of which is a naming finding

## P17 · ⚠ Two branches want the same two material channels — [RESEARCH FIRST, urgent]

**Do this before merging either branch.** It is the only item in this file with a
deadline, and the failure mode is a clean merge that silently breaks a tested feature.

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

ALSO ESTABLISH, because it changes what anyone can plan: how many D3DMATERIAL9 channels
are genuinely free on Fixed-Function-dev today, and which unmerged branches have claimed
each of the remainder. The audit's reading is that BOTH remaining channels are already
spoken for by different in-flight branches. If that is right, several proposals elsewhere
in this worklist that assume a spare channel are planning on space that is gone - say so
explicitly.

IF IT DOES NOT REPRODUCE: say so plainly. A false alarm here is a good outcome.

IN SCOPE:
 - A written channel-allocation status: for each D3DMATERIAL9 field, who uses it on
   Fixed-Function-dev, and who claims it on each unmerged branch.
 - Update aurora-ao/docs/dx9/remix-material-interface.md §2 to record that the remaining
   channels are spoken for by in-flight branches, since that table is what the next
   feature will read. Aurora's check_invariants.py already validates §2 against
   set_remix_material once merged - it cannot see an unmerged branch, which is why this
   needs a human.
 - A recommended resolution for the water branch: rebase it onto Fixed-Function-dev and
   re-derive its channel assignment against the current set_remix_material signature,
   rather than resolving the conflict by hand.

OUT OF SCOPE: performing the rebase, merging anything, changing either feature.

DONE MEANS: the allocation table is written and cited; §2 says what is actually free; and
the water branch has a stated, safe merge procedure. Aurora's invariants pass.

Push only your session branch.
```

## P0 · Merge the effect-lights branch — [PROTOCOL]

## P0 · Merge the effect-lights branch — [PROTOCOL]

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

# Tier 1 — free, do first, cannot break anything

These are documentation corrections. They lead not because they matter most, but because
they cost nothing, cannot regress anything, and they remove the wrong information that
produced the defects. None needs a test window.

## P1 · kytag01 is the Lost Woods, not Lake Hylia — [DOC ONLY]

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

## P2 · The kasumi correction never reached the shader — [SAFE + a gated look change]

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
```

## P3 · Three shipped statements about bloom say the opposite of the code — [DOC ONLY]

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
retuning. This session changes words, not pixels.

DONE MEANS: the three sites agree with the code; RtxOptions.md staleness is in its header
note, not in edited rows; both invariants scripts pass.

Push only your session branch.
```

## P4 · Effect lights: guard the word lists, do not chase them — [SAFE, small]

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
```

## P5 · Two planned features are aimed at mechanisms that do not exist — [DOC ONLY]

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

OUT OF SCOPE: removing the guard, building heterogeneous fog, retuning anything.

DONE MEANS: for claim 1, section 8.2 and row C6 either state that no work is owed or
state precisely what is; for claim 2, the constant's provenance is written down, the log
line exists, and the entry says UNKNOWN if it is.

Acceptable outcome: both claims refuted and the documents left alone. That is a real
result - write it down so nobody re-opens it.

Push only your session branch.
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
 - Consume ONLY the two kasumi alphas, and only in place of the luminance proxy at
   dusklight_sky.comp.slang:79-80.
 - Push kumoAlpha and display it; consume nothing. Clouds are a later phase.

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
```

---

# Tier 3 — strategic; changes what this project can offer a remaster artist

## P8 · Turn the material-identity hook back on — [SAFE, high value]

This is the difference between a remaster artist identifying a surface by texture hash
and identifying it by the name the game's own artists gave it.

```
Read docs/japanese-naming.md first. export LC_ALL=C.UTF-8 before any Japanese grep.

CLAIM TO VERIFY FIRST.

Three documents record that `grp=` in the material report does not work, and that a
correct implementation "labels at draw-buffer execution, carrying the label from
registration. Nobody has built it."
  aurora-ao/docs/dx9/remix-material-interface.md  section 9 "Identification"
  aurora-ao/docs/dx9/progress.md
  aurora-ao/CLAUDE.md

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
 - Decouple the push from `#if DEBUG`. Prefer a RUNTIME toggle over a compile-time one,
   following the settings pattern already used in src/d/d_drawlist.cpp
   (dusk::getSettings().game.*), because the owner tests from a release CI artifact and
   cannot rebuild. TARGET_PC stays in the condition.
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
```

## P9 · Harvest the original developers' tuning panel — [RESEARCH FIRST]

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

OUT OF SCOPE: adding any option, changing the overlay, the bridge, or any game code.
This session produces a document and a shortlist.

DONE MEANS: docs/kankyo-tuning-surface.md exists; every row is mechanically derived and
cited to file:line; the three flag categories are called out; it ends with a ranked
shortlist of at most 15 candidates for the Dusklight overlay with a sentence each on why.
Routed to from docs/japanese-naming.md and docs/kankyo-remix.md.

Acceptable outcome: you conclude the rendering-relevant subset is small and mostly
already covered. Say so with the numbers.

Push only your session branch.
```

## P10 · Give the bloom table a vocabulary, and record the four unreachable presets — [DOC ONLY]

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

TASK 2: bloom presets 32-35 are labelled "vacant" by the panel that describes the table
and NAMED as four twilight/senses looks by a different HIO panel. They are four
author-made alternative grades - complete with mono amounts and, for 33, the same base
dimming twilight uses - that ship in the game's data and cannot currently be reached.
Verify, then document 32-35 and their author names in kankyo-remix.md I.1, marked as a
DEBUG-only experiment rather than shipped behaviour.
EXPLICITLY DO NOT wire them up in this session. If someone wants them live the cheap
route is a rtx.dusklight.game.* bloom-table-id override read back by the bridge, NOT a
change to the game's palette data - and that is a later session's proposal.

TASK 3: add `dalkmist` = "dark mist" to the misspelled-English list in japanese-naming.md
section 4, with one line on what it is (a Palace-of-Twilight fog-repulsion volume read
only by d_a_kytag12). Note explicitly that it is NOT a light and should not be piped to
Remix - none of the registering actors calls dKy_plight_set and the volumes gate on
puzzle switch state, so treating them as light sources would be inventing intent.

OUT OF SCOPE: any code change at all.

DONE MEANS: the glossary has bloom vocabulary; the `// ?` is answered in prose; 32-35 are
documented as unreachable; dalkmist is listed; invariants pass.
```

---

## P12 · Batch the Twilight fog — the densest particle field in the game — [SAFE]

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

dKyr_evil_draw2 adds up to another ~1000 on top. That is roughly THREE TIMES the rain
case the owner reported as unusable, in an area the player spends a long stretch in.

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
```

## P13 · The same kasumi mistake, one file away — [DOC ONLY, + one measurement]

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
```

## P14 · `hideSkyBillboards` deletes the star field — [RESEARCH FIRST]

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
```

## P15 · Six time-slot comments in the game tree are wrong — [DOC ONLY + SAFE]

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
```

## P16 · Let a pack author see which texture a file is — [SAFE]

Directly serves the stated end state: *most remastering work is creating art assets*.

```
Read docs/japanese-naming.md first.

THE PROBLEM. The HD texture pack feature works and is content-keyed end to end (tested
2026-08-06) - nothing is wrong with it. But an artist making a pack today matches hex
filenames by eye: nothing in the shipping build tells them that
tex1_128x128_<hex>_9.dds is the cliff texture from a particular stage.

Both halves of the join already exist in our own code.

IN SCOPE - one bounded addition that changes NO key and NO hash, so texture tagging and
hash stability are untouched by construction:
  A game-side pass over loaded J3DModelData that emits, once per distinct texture:
      <dolphin filename>   <->   <bti name>   (<archive/model>)
  gated behind an existing game.* setting, and bounded/capped with a truncation notice
  like the other reports (CLAUDE.md's logging rule).

It needs two small pieces:
 (a) a public wrapper mirroring the shape of the existing
     aurora::texture::has_replacement(const GXTexObj*, const GXTlutObj*)
     (include/aurora/texture.hpp:122) that forwards to build_texture_replacement_name;
 (b) a TARGET_PC accessor for J3DTexture::mpTexObj[i], which is private today
     (J3DTexture.h:22-26). That block is our port's own addition, so adding an accessor
     is in-house rather than an upstream change.

VERIFY BOTH EXIST AND HAVE THOSE SHAPES before writing anything. If the key derivation
differs from what you expect, stop - a tool that emits the wrong filename is worse than
no tool.

OUT OF SCOPE: changing how textures are hashed or keyed; changing the pack format;
rtx.conf categories; anything about material names (that is P8).

REGRESSION SIGNATURE: none in the image. If the emitted filename does not match what a
real pack directory contains, the key derivation differs and the TOOL is wrong.

DONE MEANS: the report exists behind a setting, is capped, and one run's output is pasted
into aurora-ao/docs/dx9/texture-replacements.md as a worked example.

Acceptable outcome: you find the join cannot be made without changing the key. Say so -
that is a real answer and it closes the question.
```

---

## P18 · Grass is also flowers, and blobShadows drops far more than we say — [DOC ONLY]

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
  FIX - DOCUMENTATION ONLY. Do NOT silently widen perBladeGrass to cover flowers: its
  description promises grass and its cost profile differs. State in
  docs/remix-open-issues.md issue 7, and in the option text in
  dxvk-remix/src/dxvk/rtx_render/rtx_dusklight_game.h, that the switch covers
  dGrass_packet_c only and that the flower packet still batches. A sibling switch is a
  small mechanical port IF flowers turn out to matter - but that decision comes after the
  grass switch has been tested in game even once, which it has not been.

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
must NOT plan on a spare D3DMATERIAL9 channel, see P17).

DONE MEANS: issue 7, issue 11, the two option descriptions and the shadow prose all say
what the code does; invariants and CI green. No behaviour changed.
```

---

# Tier 4 — only with the owner watching

## P11 · The kasumi blend itself

This is step 2 of **P2**, and it is listed separately because it is a look judgement, not
a correctness fix. Ship it behind an option defaulting to today's behaviour, then decide
by A/B in one session. Do not flip the default without the owner seeing both.

---

## Sequencing summary

| When | Run | Needs a play-test? |
| :-- | :-- | :-- |
| **Before merging any branch** | **P17** (material channel collision) | no |
| First — pure documentation, cannot regress anything | P1, P3, P5, P13, P15, P18 | no |
| Then — small guarded changes | P4, P2 step 1, P14 | no |
| Then — the strategic reads | P8 (material identity), P9 (tuning panel), P10, P16 | no |
| Measure, then act | P12 (Twilight fog draw count) | one log, then a change |
| Together in one window | P6, P7 | yes, one window |
| When ready for lights | P0 (the merge) | yes |
| Last | P11 | yes, A/B |

**P0 can be run at any time and depends on none of the others.** It is placed late in the
table only because it is the one with a merge to resolve — for the goal of getting light
creation into Remix it is the most important item in this file.

**P12 is the one item where a single log settles whether there is anything to do at all.**
Ask for a `dx9.draws` sample standing in the Palace of Twilight before spending a session
on it.
