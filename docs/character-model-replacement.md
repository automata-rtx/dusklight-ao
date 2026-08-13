# Replacing a character model — how Dusklight's skeletons actually work

Research notes for the "new Link model with extra bones" idea: physics-driven tail
bones, eye bones that track his gaze, and rigged fingers that reproduce the static
hand poses the game swaps in.

Everything below is read out of this tree and `aurora-ao`, with file:line citations.
Nothing here has been built or prototyped yet; the last two sections are design
options and the order I'd de-risk them in.

> Names in `src/d/`, `src/f_op/`, `src/f_pc/`, `src/m_Do/` and `libs/JSystem/` are the
> original Japanese team's, preserved 1:1 by the decompilation — see
> `docs/japanese-naming.md`. Japanese terms are glossed on first use here.

---

## Table of contents

1. [The one-paragraph answer](#1-the-one-paragraph-answer)
2. [How J3D represents a skeleton](#2-how-j3d-represents-a-skeleton)
3. [Link is not one model — he is six](#3-link-is-not-one-model--he-is-six)
4. [How animation binds to bones (the decisive fact)](#4-how-animation-binds-to-bones-the-decisive-fact)
5. [How submeshes are replaced on the fly](#5-how-submeshes-are-replaced-on-the-fly)
6. [How the eyes work](#6-how-the-eyes-work)
7. [Procedural bone control: the two idioms already in the tree](#7-procedural-bone-control-the-two-idioms-already-in-the-tree)
8. [What aurora does with all this](#8-what-aurora-does-with-all-this)
9. [What the mod API can and cannot reach](#9-what-the-mod-api-can-and-cannot-reach)
10. [Hard limits, in one place](#10-hard-limits-in-one-place)
11. [Design options for the end goal](#11-design-options-for-the-end-goal)
12. [Open questions and what to prototype first](#12-open-questions-and-what-to-prototype-first)

---

## 1. The one-paragraph answer

Joint animation binds to bones **purely by joint index**, so a replacement model that
keeps vanilla's joints at the same indices in the same order plays every vanilla
animation byte-identically. Extra bones appended after them are *not* free, though:
the matrix calc asks the animation for every joint the **model** has, so an unfenced
extra bone reads past the end of the animation's track table. And Link's actor code
carries three hard-coded joint counts plus one JSystem sentinel keyed on
`getJointNum() - 1` that breaks his animation cross-fades the moment the body's joint
count changes at all. **But** the engine is already a multi-model rig — the hat, hair,
face and hands are separate models pinned to body joints each frame, two of them
already running hand-written physics chains — so all three target features map onto
existing satellite models without touching the body skeleton's joint count.

---

## 2. How J3D represents a skeleton

### Storage

A skeleton lives in `J3DModelData`, which is **shared by every instance** of a model
and handed out by `dComIfG_getObjectRes`. It is stored three ways at once:

| Representation | Where | Purpose |
| :-- | :-- | :-- |
| `J3DModelHierarchy` opcode stream | `J3DJointTree.h:17-20` | on-disk form, read from the model file's INF1 block |
| first-child / next-sibling pointer tree | `J3DJoint::mChild`, `mYounger` (`J3DJoint.h:114-115`) | per-frame traversal |
| flat `J3DJoint**` index array | `J3DJointTree::mJointNodePointer` (`J3DJointTree.h:90`) | random access by index |

`J3DJointTree::makeHierarchy` (`J3DJointTree.cpp:24-70`) walks the opcode stream —
`kTypeBeginChild=0x01`, `kTypeEndChild=0x02`, `kTypeJoint=0x10`, `kTypeMaterial=0x11`,
`kTypeShape=0x12` — and the same walk binds materials and shapes onto the joint they
hang off. There is no parent pointer on a joint.

### Identification

A joint's identity at runtime is its `u16 mJntNo` (`J3DJoint.h:116`). **Names survive
the load**: `J3DModelData::getJointName()` returns a `JUTNameTab` wrapping the JNT1
block's `ResNTAB` in place, and `JUTNameTab::getIndex(const char*)` gives name → index
(`JUTNameTab.h:31`, keycode + `strcmp` linear scan, `-1` on miss). So a mod *can* look
a bone up by name — but nothing in the game does; every consumer uses raw indices.

### The per-frame calc

`J3DModel::calc` → `calcAnmMtx` → `J3DJointTree::calc` seeds the globals
`J3DSys::mCurrentMtx / mCurrentS / mParentS` from the model's base matrix, then calls
`root->recursiveCalc()` (`J3DJointTree.cpp:101-112`), which is depth-first: child, then
younger sibling.

```c
// libs/JSystem/src/J3DGraphAnimator/J3DJoint.cpp:194-243  (abridged)
void J3DJoint::recursiveCalc() {
    ...
    if (getMtxCalc() != NULL) { ...; setCurrentMtxCalc(getMtxCalc()); ...; piVar2->calc(); }
    else if (getCurrentMtxCalc() != NULL) { ...; uVar6->calc(); }

    J3DJointCallBack jointCallback = getCallBack();
    if (jointCallback != NULL) (*jointCallback)(this, 0);      // <-- BEFORE children

    if (getChild()  != NULL) getChild()->recursiveCalc();
    MTXCopy(prevCurrentMtx, J3DSys::mCurrentMtx);              // restore parent state
    ...
    if (jointCallback != NULL) (*jointCallback)(this, 1);      // <-- AFTER children

    if (getYounger() != NULL) getYounger()->recursiveCalc();
}
```

Three things to take from this:

- **`J3DJoint::mMtxCalc` is a per-joint strategy override that applies to the whole
  subtree.** `mCurrentMtxCalc` is a class-wide static; a joint carrying its own
  `mMtxCalc` swaps it for the duration of its descendants and restores it afterwards.
  This is how Link runs one animation blend on his lower body and another on his upper
  body. `setMtxCalc(NULL)` is *not* a fence — NULL means "inherit the parent's".
- **`J3DJointCallBack` fires twice per joint per frame**, at timing `0` after this
  joint's matrix is finished but *before* its children, and at timing `1` after the
  subtree and after the parent state has been restored. Timing 0 is the seam for
  procedural bone control; timing 1 cannot influence children. The `int` return value
  is discarded at both call sites — a callback cannot skip recursion.
- **`getAnmMtx(jntNo)` holds a world/model-space matrix, not a local one.** All three
  transform policies (Basic / Softimage / Maya, picked from `mFlags & 0xf`) end with
  `MTXConcat(J3DSys::mCurrentMtx, anmMtx, J3DSys::mCurrentMtx); MTXCopy(..., anmMtx);`.

The callback is set on `J3DJoint`, which lives in the **shared** `J3DModelData`. Every
callback in the tree therefore recovers its instance through
`j3dSys.getModel()->getUserArea()`. Link's model already has its user area set
(`d_a_alink_wolf.inc`, `mpLinkModel->setUserArea((uintptr_t)this)`).

Roughly sixty actors use `setCallBack`; the canonical shape is:

```c
// src/d/actor/d_a_npc_ash.cpp:396-450  (abridged) — the pattern to copy
BOOL daNpcAsh_c::ctrlJointCallBack(J3DJoint* i_joint, int param_1) {
    if (param_1 == 0) {
        J3DModel* model = j3dSys.getModel();
        daNpcAsh_c* _this = (daNpcAsh_c*)model->getUserArea();
        if (_this != NULL) _this->ctrlJoint(i_joint, model);
    }
    return true;
}

bool daNpcAsh_c::ctrlJoint(J3DJoint* i_joint, J3DModel* i_model) {
    int jointNo = i_joint->getJntNo();
    mDoMtx_stack_c::copy(i_model->getAnmMtx(jointNo));       // read world mtx
    switch (jointNo) { case 1: case 3: case 4: setLookatMtx(...); break; }
    i_model->setAnmMtx(jointNo, mDoMtx_stack_c::get());      // write it back
    cMtx_copy(mDoMtx_stack_c::get(), J3DSys::mCurrentMtx);   // <-- children inherit
    ...
}
```

That last line is the whole trick: writing `J3DSys::mCurrentMtx` at timing 0 propagates
your change down the subtree.

`J3DModel::mCalcCallBack` (the model-level hook) is **not** usable for this — it fires
after `calcWeightEnvelopeMtx()` and after `mSkinDeform->deform()`
(`J3DModel.cpp:460-468`), one full pass too late for skinned geometry.

---

## 3. Link is not one model — he is six

`daAlink_c` (a `daPy_py_c` subclass) owns a set of bare `J3DModel*` pointers built in
`daAlink_c::changeLink()` (`src/d/actor/d_a_alink_wolf.inc:311-420`). For default
hero's-clothes human Link:

| Pointer | File | Archive | Joints | Parented by |
| :-- | :-- | :-- | :-- | :-- |
| `mpLinkModel` | `al.bmd` | `Kmdl` | **35** | its own base TR matrix |
| `mpLinkHatModel` | `al_head.bmd` | `Kmdl` | 10 (3 hat + 5 hair) | `setBaseTRMtx(getAnmMtx(4))` |
| `mpLinkFaceModel` | `al_face.bmd` | `Kmdl` | 5 | `setBaseTRMtx(getAnmMtx(4))` |
| `mpLinkHandModel` | `al_hands.bmd` | `Kmdl` | 3 | `setAnmMtx(1/2, getAnmMtx(9/0xE))` |
| `mpLinkBootModels[2]` | `al_bootsH.bmd` | current | — | matrix injection |
| `mpKanteraModel` | `al_kantera.bmd` | current | — | matrix injection |

`kantera` (カンテラ) is a lantern. The wardrobe archives are `Kmdl` (hero's clothes),
`Bmdl` (casual/Ordon), `Zmdl` (Zora armour), `Mmdl` (magic armour), `Wmdl` (wolf), plus
`alSumou` for the sumo bout — chosen by `daAlink_c::setArcName`.

**All five human wardrobes ship the identical 35-joint skeleton with identical joint
names**, which is why one shared animation archive (`AlAnm`) drives them all.

### The 35 joints

Authoritatively named by the generated `enum AL_JNT` in
`assets/<region>/res/Object/Kmdl.h:45-80`:

```
0x00 CENTER      0x09 HANDL       0x12 LEGL1       0x1B FSKIRTL1
0x01 BACKBONE1   0x0A WEAPONL     0x13 LEGL2       0x1C FSKIRTL2
0x02 BACKBONE2   0x0B SHOULDERR   0x14 FOOTL       0x1D FSKIRTR1
0x03 NECK        0x0C ARMR1       0x15 TOEL        0x1E FSKIRTR2
0x04 HEAD        0x0D ARMR2       0x16 CLOTCHR     0x1F RSKIRTL1
0x05 POD         0x0E HANDR       0x17 LEGR1       0x20 RSKIRTL2
0x06 SHOULDERL   0x0F WEAPONR     0x18 LEGR2       0x21 RSKIRTR1
0x07 ARML1       0x10 WAIST       0x19 FOOTR       0x22 RSKIRTR2
0x08 ARML2       0x11 CLOTCHL     0x1A TOER
```

`POD` (0x05) is the scabbard/back mount; `CLOTCH` is the hip; `FSKIRT`/`RSKIRT` are the
front and rear tunic flaps.

### Joint indices that game code hard-codes

A replacement body model must keep every one of these meaning what it means today:

| Index | Used for | Site |
| :-- | :-- | :-- |
| 0, 1, 16 | the three `mDoExt_MtxCalcAnmBlendTblOld` blend roots | `d_a_alink_swindow.inc:166-168` |
| 4 (`HEAD`) | face and hat models parented here | `d_a_alink.cpp:5968`, `:5974` |
| 9 / 14 | `mLeftHandJntNo` / `mRightHandJntNo`, injected into the hand model | `d_a_alink.cpp:19013-19014` |
| 7, 8 / 12, 13 | arm IK chains — **with hard-coded segment lengths** `{29.0f}` and `{26.5f}` | `d_a_alink.cpp:3560-3562` |
| 18-21 / 23-26 | leg IK chains — lengths `{30.0f}`, `{39.3635f}`, `{14.18f}` | `d_a_alink.cpp:3626-3629` |
| 0,1,2,4,5,12,13,16,27,29 | `jointControll` quaternion-composed extra rotation | `d_a_alink.cpp` |

The arm and leg segment lengths being compile-time constants means the new model's
limb proportions must match vanilla's or the hands and feet detach from the IK targets.

---

## 4. How animation binds to bones (the decisive fact)

**Joint animations (`.bck`) bind purely by index. There is no name table in an ANK1
block at all.** (Contrast the *material* animations — BPK/BRK/BTK/BTP — which do carry
a `JUTNameTab` and resolve by name with a `0xffff` not-found sentinel,
`J3DAnimation.cpp:1024-1032`.)

The lookup is `mAnmTable[jointNo * 3 + {0,1,2}]` for the X/Y/Z tracks
(`J3DAnimation.cpp:651-654`), and the joint number comes straight off the joint:

```c
// libs/JSystem/include/JSystem/J3DGraphAnimator/J3DJoint.h:170-181
if (pMtxCalc->getAnmTransform() != NULL) {
    pMtxCalc->getAnmTransform()->getTransform(J3DMtxCalc::getJoint()->getJntNo(), &transform);
    transform_p = &transform;
} else {
    transform_p = &J3DMtxCalc::getJoint()->getTransformInfo();   // bind pose fallback
}
```

### The two consequences

**Good:** any joint whose index is unchanged reads byte-identical tracks from a vanilla
`.bck`. Keeping vanilla's 35 joints at indices 0–34 in the same order is sufficient for
every vanilla animation to play correctly. Names are irrelevant to this — **index order
is the contract**, not naming.

**Bad:** the driving loop is `J3DJoint::recursiveCalc`, which walks the **model's**
joint tree. Nothing anywhere bounds the walk by the animation's track count. The
animation's count lands in `J3DAnmTransform::field_0x1e` and is referenced *only* inside
three `J3D_ASSERT_RANGE` macros (`J3DAnimation.cpp:218, 308, 648`) — which compile to
`(void)0` outside `DEBUG` (`JUTAssert.h:77-88`; `CMakeLists.txt:420-423` forces `NDEBUG`
on all game sources).

So an appended joint that inherits an animating `J3DMtxCalc` calls
`getTransform(jntNo)` with `jntNo >= trackCount`, indexes `mAnmTable[jntNo*3]` out of
bounds, reinterprets whatever follows as big-endian `{mMaxFrame, mOffset, mType}`, and
uses `mOffset` as an unbounded index into the value arrays. **Garbage transforms or a
segfault — not a no-op, and not a rest pose.**

The fence is a per-joint `setMtxCalc` on the appended subtree's root, which replaces the
inherited calc for that whole subtree. `J3DMtxCalcNoAnm<A,B>` (`J3DJoint.h:140-150`) is
the ready-made "stay at bind pose" implementation, and it is a header-only template on
the mod ABI include path.

Also note: **an animation supplies the complete local SRT, it does not layer onto the
bind pose.** An absent track yields scale 1, rotation 0, *translation 0*
(`J3DAnimation.cpp:656-762`), which collapses a bone onto its parent. For every animated
joint the effective bone length comes from the `.bck`, not from the model's JNT1 rest
transform.

### The sentinel that breaks everything (highest-priority finding)

Link does **not** use `mDoExt_McaMorf` (whose arrays self-size from
`modelData->getJointNum()`). He uses `mDoExt_MtxCalcAnmBlendTblOld`, whose cross-fade
bookkeeping is keyed on the model's joint count:

```c
// src/m_Do/m_Do_ext.cpp:1195-1196
} else if (jntNo == modelData->getJointNum() - 1) {
    mOldFrame->onOldFrameFlg();
}
// and again at :1206
if (jntNo == modelData->getJointNum() - 1) { mOldFrame->decOldFrameMorfCounter(); }
```

`onOldFrameFlg()` is called from **exactly one place in the entire tree** — that
sentinel (verified: `grep -rn "onOldFrameFlg()" src/ include/` returns the call site and
the inline definition, nothing else). It is the only thing that ever enables the
cross-fade branch.

Therefore, if the body model's highest joint index is an appended bone that has been
fenced with its own `MtxCalc`, the blend calc never sees that index, the flag is never
set, and **every animation transition in the game becomes a hard pop.** Leave the extra
bones unfenced instead and they read out of bounds from every vanilla `.bck`.

There is no configuration of a larger Link **body** model that is correct without either
patching this sentinel or padding every animation. This is the single most important
constraint in the whole investigation, and it is the main reason §11 recommends leaving
the body's joint count alone.

---

## 5. How submeshes are replaced on the fly

The user's premise is correct, and the mechanism is more specific than "swapping
meshes": it is a **per-shape visibility flag toggled across two model datas**.

The body model `al.bmd` carries two static default hand shapes (materials 11 and 12,
cached as `field_0x06d8` / `field_0x06dc`). A separate model, `al_hands.bmd`, holds
**11 more static hand shapes, one per pose**. Every frame:

```c
// src/d/actor/d_a_alink.cpp:18928 — daAlink_c::setDrawHand(), abridged
void daAlink_c::setDrawHand() {
    field_0x06d0->hide();                    // hide what we showed last frame
    field_0x06d4->hide();
    ... resolve var_r30 (left index) and var_r29 (right index) ...

    mpLinkHandModel->setBaseTRMtx(mpLinkModel->getBaseTRMtx());
    mpLinkHandModel->calc();
    mpLinkHandModel->setAnmMtx(1, mpLinkModel->getAnmMtx(9));      // HANDL
    mpLinkHandModel->setAnmMtx(2, mpLinkModel->getAnmMtx(0xE));    // HANDR

    field_0x06d0 = mpLinkHandModel->getModelData()->getMaterialNodePointer(var_r30)->getShape();
    field_0x06d0->show();
    ...
}
```

The flag is `J3DShpFlag_Visible = 0x0001` and **its name is inverted relative to its
meaning**: `J3DShape::hide()` *sets* it, `show()` *clears* it
(`J3DShape.h:109, 173-174`). It is consumed in `J3DJoint::entryIn()`
(`J3DJoint.cpp:165`), where a set flag makes the traversal skip the mesh entirely — it
never reaches a draw buffer.

### Where the pose index comes from

A four-level priority chain, resolved in `setDrawHand`:

1. **Per-animation default** — `daAlink_c::m_anmDataTable`, 414 rows, fields
   `m_handIndexL` / `m_handIndexR`, latched by `daAlink_c::setHandIndex`
   (`d_a_alink.cpp:6172-6175`).
2. **Item-level override** — `field_0x2f94` / `field_0x2f95`, set by `setSwordModel`,
   `setBowModel`, `setCopyRodModel`, …
3. **Transient action override** — `field_0x2f96` / `field_0x2f97`.
4. **Sentinels** — `0xFE` falls back to the body model's own default hand shape;
   **`0xFB` is the real escape hatch.**

### `0xFB` is already a fully-skinned animated hand model

```c
// src/d/actor/d_a_alink.cpp:19022-19031
if (var_r30 == 0xFB) {
    mpDemoHLTmpModel->setBaseTRMtx(mpLinkModel->getAnmMtx(9));   // parent BEFORE calc
    if (mpDemoHLTmpBck != NULL) mpDemoHLTmpBck->entry(mpDemoHLTmpModel->getModelData());
    mpDemoHLTmpModel->calc();
    field_0x06d0->hide();
}
```

Cutscenes already swap in `demo00_Link_cut00_HL_tmp.bmd` / `..._HR_tmp.bmd`, parent them
by `setBaseTRMtx` **before** `calc()`, and run a `.bck` on them. That is a shipping,
in-engine precedent for exactly what "rigged hands" needs.

The distinction matters enormously. The normal path injects the wrist matrix with
`setAnmMtx` **after** `J3DModel::calc()` — which is where `calcAnmMtx()` and
`calcWeightEnvelopeMtx()` run (`J3DModel.cpp:460-461`). So only rigid, full-weight draw
matrices pick up the injected wrist; **any child joints or envelope skinning inside
`al_hands.bmd` would be computed against the pre-patch pose and silently not follow the
wrist.** The `0xFB` path avoids this by parenting before `calc()`.

The same "separate model pinned by matrix injection" pattern drives Link's boots, and
`d_a_midna.cpp` implements the identical hand swap under clearer decompiler-chosen names
(`setLeftHandShape`, `mpHandsBmd`, `JNT_HAND_L`).

Tunic variants are **not** shape swaps — `J3DModel` has no `setModelData`, so
`changeLink()` frees the archive heap and rebuilds every model from scratch.

---

## 6. How the eyes work

**Link has no eye geometry and no eye bones.** His eyes are flat quads whose *texture
matrix translation* is scrolled in UV to fake a pupil moving in the socket.

The chain:

1. `mpLinkFaceModel` (`al_face.bmd`) is parented to body joint 4 (`HEAD`) every frame:
   `setBaseTRMtx(mpLinkModel->getAnmMtx(4))` (`d_a_alink.cpp:5968`).
2. Material nodes 2 and 3 of that model each get a `daAlink_matAnm_c*` attached as their
   `J3DMaterialAnm` (`d_a_alink_wolf.inc:500-502`).
3. `daAlink_matAnm_c::calc` runs the normal BTK-driven texture SRT animation and then,
   when the static flag `m_eye_move_flg` is set, **overwrites** the texture matrix
   translation:

```c
// src/d/actor/d_a_alink.cpp:2036-2040
} else if (m_eye_move_flg) {
    srt->mTranslationX = mNowOffsetX;
    srt->mTranslationY = mNowOffsetY;
}
```

4. Those offsets are computed once per frame by
   `daAlink_c::setEyeMove(cXyz* aimPos, s16 eyeX, s16 eyeY)` (`d_a_alink.cpp:3283`),
   called only from `daAlink_c::setNeckAngle()` (`:3421`), called from
   `daAlink_c::execute()` (`:18531`).

**Gaze is continuous, not quantised** — better than assumed. `getNeckAimAngle()` computes
the full aim angle to the look target, gives the neck as much as the HIO clamps allow,
and hands the **residual** to the eyes as `o_eyeX` (pitch) / `o_eyeY` (yaw).
`setEyeMove` scales those by `0.00012207031f` (= 1/8192, so ±1.0 ≡ ±45°), clamps to ±1,
and maps to UV translations of at most ±0.25 horizontally and +0.2 / −0.1 vertically.

**So the signal a mod needs already exists as a pair of continuous angles**, and there
are two clean places to read it: hook `setEyeMove` and take `param_1` / `param_2`
directly (s16 binary angles), or read back `mNowOffsetX/Y` and multiply by 8192.

### Expression is a separate axis

A `daAlink_FTANM` id (163 values, e.g. `FTANM_ODOROKU` 驚く *odoroku* "surprised",
`FTANM_UNAZUKU` 頷く *unazuku* "nod") indexes `m_faceTexDataTable`
(`d_a_alink.cpp:849`) to a **BTP** (texture pattern — which eye/brow/mouth texture), a
**BTK** (texture SRT — authored base UV), and a parallel **BCK** joint animation played
on the face model's own five joints. Default expression follows the body animation via
`m_anmDataTable[anm].m_faceTexID`; gameplay, cutscene and dialogue code override it with
"Pri" (priority) variants.

Blinking is autonomous: when the loaded BTP is one of the まばたき *mabataki* (blink)
patterns, `FLG1_UNK_2000` is set and `playFaceTextureAnime()` advances a private counter
with per-frame probability 0.012 instead of following the body animation frame.

Worth knowing: **no humanoid in this tree rotates an eye bone.** 105 files reference
`getEyeball*MaterialNo()`; the only eye-bone rotation anywhere is the Beamos statue. Eye
bones would be new behaviour, not a re-implementation of something existing.

The face model already carries eye textures named `al_eyeball`, `highlight02` and
`eye_kage01` (影 *kage* = shadow), and the port already patches their `maxLOD` at load
(`d_a_alink_wolf.inc:380-396`) — a precedent for post-load model-data surgery.

---

## 7. Procedural bone control: the two idioms already in the tree

There is **no** general physics or cloth library — no `dCloth`, no `dRope`, no shared
spring solver. Two hand-written idioms are re-implemented per actor, and both already
run on Link.

**Idiom A — "angle chain."** Each segment keeps an s16 angle plus an s16 angular
velocity; the parent's per-frame angular delta is subtracted into the child, the angle
springs back toward 0 with `cLib_addCalcAngleS`, velocity is added, the result is
hard-clamped, and the new velocity becomes `0.5 ×` this frame's delta, which drives the
next segment. This is `daAlink_c::setWolfTailAngle` (wolf tail, 3 segments) and
`daHorse_c::setTailAngle` — near-identical code. **This is the closest existing
analogue to the requested tail.**

**Idiom B — "position chain."** Each segment holds a world position, a previous position
and a velocity; gravity and wind are added to the vector from the parent, the vector is
normalised and re-scaled to a fixed bone length (a hard distance constraint), velocity
becomes `(vel + (prevPos - newPos)) * damping`, and world positions are converted back
to local Euler angles through the inverse of the parent matrix. That is
`daObjOnCloth_c` (Ordon banner, 3 bones, per-joint `rotationLimit[3]`, player-proximity
impulse through a delay ring buffer), `daObjLdy_c` (laundry), and `daMidna_c::setHairAngle`
(5 hair bones, 60° cone limit, sinusoidal idle wobble).

**Link's hat and hair are a third instance** — `mpLinkHatModel`, 3 hat segments and 5
hair segments, on a separate model parented to body joint 4, driven through
`daAlink_c::headModelCallBack`.

Results reach the skeleton in exactly two ways: a `setCallBack` node callback that
post-multiplies onto the computed joint matrix and writes it back with `setAnmMtx`, or
`mDoExt_McaMorfCallBack1_c::execute(jointNo, J3DTransformInfo*)`, which hands you the SRT
*before* the matrix is built.

Useful helpers: `cLib_addCalcAngleS(s16* v, s16 target, s16 scale, s16 maxStep, s16 minStep)`,
`cLib_addCalcAngleS2`, `cLib_chaseAngleS` (`include/SSystem/SComponent/c_lib.h:22-30`).

### Timestep

The port ticks game logic at a **fixed 1/30 s** (`dusk::game_clock::sim_pace()`), caps
catch-up at `kMaxSimTicksPerFrame = 2`, and runs **zero** ticks after a >250 ms gap. A
spring integrator must tolerate skipped updates but never a variable `dt`.

Presentation frames in between are produced by `dusk::frame_interp`, which the port
inserted *inside* J3D: `J3DModel::calc` calls `record_final_mtx(getAnmMtx(i))` for every
joint (`J3DModel.cpp:473-477`) and registers `J3DModel::interp_callback`
(`:508-509`). **So extra bones are interpolated to display rate automatically, with no
extra work**, provided their matrices go through `getAnmMtx`. The API is
`src/dusk/frame_interpolation.h` (`record_final_mtx`, `add_interpolation_callback`,
`is_sim_frame`).

There is no save-state, replay or determinism system in the port, so physics-bone
determinism is not a correctness requirement.

Collision is queryable per frame through `dBgS` (`GroundCross`, `LineCross`, `SphChk`),
each looping all 256 registered background objects. `dCcS` is register-and-batch-resolve,
run once per frame — not a query API.

---

## 8. What aurora does with all this

Aurora is a source-level GX compatibility layer over WebGPU/Dawn, and it hosts more than
graphics — the DVD, GX and card shims all live there (`aurora-ao/lib/dolphin/`).

**There is no skinning in aurora.** GX hardware applies exactly **one** 3×4 position
matrix and one 3×3 normal matrix per vertex, selected by the 1-byte `GX_VA_PNMTXIDX`
attribute. Weighted skinning happens on the **CPU, game-side**:
`J3DMtxBuffer::calcWeightEnvelopeMtx()` (`J3DMtxBuffer.cpp:246-410`) collapses each
unique weight-set ("envelope", the EVP1 block) into a single matrix once per frame, and
the vertex then references that blended matrix exactly like a rigid bone.

So the file carries two kinds of draw matrix — full-weight/rigid (indexes a joint) and
envelope (indexes a pre-blended matrix) — distinguished by
`J3DDrawMtxData::mDrawMtxFlag[i]`, with rigid entries stored first.

**Arbitrarily many joints may influence a vertex** (up to 255 per envelope, `u8`
`mWEvlpMixMtxNum`), because they are collapsed before drawing. What is capped is how
many matrices one *draw packet* may reference:

- `J3DShapeMtxMulti::load()` walks `mUseMtxIndexTable[0..mUseMtxNum)` and writes matrix
  *i* into GX slot `i*3`. **The loop counter is the GX slot.**
- `GX_PNMTX0..GX_PNMTX9` = 0,3,…,27 — **ten slots** (`GXEnum.h:262-271`).
- Aurora agrees exactly: `constexpr u32 MaxPnMtx = (GX_PNMTX9 / 3) + 1;`
  (`aurora-ao/lib/gx/gx.hpp:67`).

Slot 10 computes XF address `12*10 = 120 = 0x78`, which aurora routes to `texMtxs[0]` —
i.e. **exceeding 10 silently corrupts a texture matrix** rather than failing. Nothing
validates `mUseMtxNum` at load or draw time; it is an **exporter** constraint.

Cost profile for a heavier model: every draw re-uploads all 10 position + 10 texture +
10 normal matrices (1440 bytes) into its own per-draw uniform block
(`shader_info.cpp:203-205, 409-421`), against a hard `MaxUniformSize = 3840` that
`abort()`s on exceed. Consecutive draws merge into one `DrawIndexed` only when nothing
changed, and a matrix load sets `stateDirty`, breaking the merge. So **packet count is
the real cost**, and it is paid at least twice per frame because
`dDlst_shadowReal_c::imageDraw` re-issues every matrix packet for the shadow pass
(`d_drawlist.cpp:1078-1116`; Link registers up to ten models into one shadow).

This fork enlarged the vertex/index/storage streaming buffers (5→16 / 1→4 / 8→16 MB) but
**not** the uniform arena (24 MB). `aurora_get_stats()` exposes `lastUniformSize`,
`drawCallCount`, `mergedDrawCallCount` for measurement.

**Verdict: aurora needs no changes for a higher bone count.** It is agnostic; it just
faithfully reproduces the 10-slot GX limit.

---

## 9. What the mod API can and cannot reach

A `.dusk` bundle is a zip with `mod.json`, an optional native module, and optional
`res/`, `overlay/`, `textures/` trees. Eleven services exist (camera, config, game, gfx,
hook, host, log, overlay, resource, texture, ui). **None of them mentions models,
joints, skeletons, bones or animation** — a case-insensitive search of `sdk/include`
for those words returns nothing.

### What works today

**Model file substitution: yes, with no code at all.** Files under `overlay/` override
game files at the corresponding disc path. The implementation registers them into
**aurora's DVD FST** (`src/dusk/mods/svc/overlay.cpp:181-207` →
`aurora-ao/lib/dolphin/dvd/`), so an overlaid path is served through
`DVDOpen`/`DVDFastOpen` exactly like a real disc file. It is file-class agnostic:
`/res/Object/Kmdl.arc` is substitutable. `OverlayService` also allows runtime
registration.

**Reaching game internals: yes, but not through a service.** The real power is
`add_mod(... FEATURES game)` (`cmake/GameABIConfig.cmake:13-20, 45-60`), which puts
`include/`, `libs/JSystem/include/`, `assets/GZ2E01/` and the aurora headers on the mod's
include path and links the mod against the game binary. `mods/shadow_mod` already calls
`dComIfGd_drawOpaList*`, `j3dSys.setViewMtx` and raw `GX*` from a render-stage callback.
`J3DJoint::setCallBack` is a public header inline, and the statics a custom `MtxCalc`
needs (`J3DSys::mCurrentMtx`, `J3DMtxCalc::mJoint/mMtxBuffer`, `J3DJoint::mCurrentMtxCalc`)
are `DUSK_GAME_DATA`-annotated, i.e. dllimport-reachable on Windows.

**Hooking: by compile-time-declared target only.** `DEFINE_HOOK(&Class::method)` (an
address relocation) or `DEFINE_HOOK_SYMBOL("name", sig)` (through an embedded symbol
manifest). The host records declared addresses at load and rejects any install on an
address it did not see declared (`hook.cpp:72-90, 250-252`). Detours are funchook-based
with priority-ordered pre/post and a single conflict-managed replace.

### What does not work

- `ResourceService` is strictly mod-local (`res/<path>` inside the bundle; `..` and
  absolute paths rejected). It cannot touch game assets.
- **Overlay granularity is a whole disc file.** There is no hook for a file *inside* a
  RARC, so shipping a new Link means rebuilding and shipping the entire `Kmdl.arc` — and
  `Bmdl`/`Zmdl`/`Mmdl`/`alSumou` for the other outfits, each selected independently.
- `mod_initialize` runs after `OSInit()`, `dComIfG_ct()` and `aurora_dvd_open()` but
  **before** `main01()` (`m_Do_main.cpp:816-892`) — no scene, no actors, no Link model.
- `mod_update` runs at the *top* of `fapGm_Execute`, before actors run
  (`f_ap_game.cpp:836-841`). **Per-frame joint writes belong in a hook, not
  `mod_update`.**
- Game logic is single-threaded, so joint data can be written from anywhere on the game
  thread with no locking. Only `GfxDrawFn`/`GfxComputeFn` run on the render worker.

### The stomping problem

`daAlink_c::changeModelDataDirect` re-installs `daAlink_modelCallBack` over joints 0–34
and re-installs the three MtxCalcs on joints 0/1/16 on **every wardrobe change and every
wolf transformation**, and NULLs them for the status-window paper-doll draw
(`d_a_alink_swindow.inc:158-208`). Any mod-installed callback on those joints is
transient and must be re-applied there. Joints ≥ 35 are untouched by both loops.

---

## 10. Hard limits, in one place

| Limit | Value | Where | Enforced? |
| :-- | :-- | :-- | :-- |
| Joints per model | 65535 (`u16`) | `J3DJoint.h:116`, `J3DJointTree.h:91` | type |
| Influences per envelope | 255 (`u8`) | `J3DJointTree.h:93` | type |
| **Draw matrices per SHP1 matrix group** | **10** | `GXEnum.h:262-271`, `J3DShapeMtx.cpp:437-446`, `aurora .../gx.hpp:67` | **no — silently corrupts texmtx 0** |
| `sMtxLoadCache` | 10 | `J3DShape.h:84` | no |
| Link old-frame arrays | **40** | `d_a_alink.cpp:4255` | no — OOB write |
| Link callback install loop | **35** | `d_a_alink_swindow.inc:171, 195` | n/a |
| `field_0x30c6` (bounds public `getModelJointMtx`) | 35 human / 40 wolf | `d_a_alink_wolf.inc:544` | returns base mtx in release |
| `initOldFrameMorf` end joint | 35 human / 40 wolf | ~10 call sites | n/a |
| Cross-fade sentinel | `getJointNum() - 1` | `m_Do_ext.cpp:1195, 1206` | **breaks silently** |
| Per-draw uniform block | 3840 B (1440 B of matrices) | `aurora .../gx.hpp:69` | `abort()` |
| Models per shadow | 38 | `include/d/d_drawlist.h:284` | no |
| Shape vtx-desc pool | 64 KiB (`u16` offset) | `J3DShapeFactory.h:19` | no |

**Release builds validate nothing on this path.** Every `J3D_ASSERT` / `JUT_ASSERT`
compiles to `(void)0` outside `DEBUG`, and `CMakeLists.txt:420-423` forces `NDEBUG` on
all game sources while only JSystem gets `DEBUG=1` in a Debug config (`:427`). A
malformed replacement model produces silent memory corruption, not a diagnostic.
**Bring-up must happen on a Debug configuration.**

---

## 11. Design options for the end goal

### The key realisation

Link is **already a multi-model rig**, and two of his satellites already run
hand-written physics chains. Each of the three target features maps onto an existing
satellite seam — which sidesteps the BCK track-count hazard, the 35/40 literals, the
`getJointNum()-1` sentinel and the callback stomping **all at once**, because none of
them apply to a model the vanilla body animations were never bound to.

| Feature | Satellite | Precedent already in the tree |
| :-- | :-- | :-- |
| **Physics tail** | new model parented to `CENTER` (0x00) or `WAIST` (0x10) | `mpLinkHatModel` — 3 hat + 5 hair segments, parented to joint 4, driven by `headModelCallBack` |
| **Eye bones** | extend `al_face.bmd` (5 joints today, parented to joint 4) | its own `mFaceBck` already animates those joints |
| **Rigged fingers** | replace `al_hands.bmd`, drive via the `0xFB` idiom | `demo00_Link_cut00_HL_tmp.bmd` — a skinned, BCK-animated hand model that already ships |

**Option A — satellite-only. The body model keeps exactly 35 joints and is not
replaced at all** (or is replaced with matching topology purely for looks). Everything
new lives in satellites.

- Nothing in §4 applies: no vanilla `.bck` is ever bound to the new bones.
- No game-code change strictly required for the tail; the eye and finger work needs
  small hooks to *drive* the bones, but not to make the model load.
- Fingers get a real caveat: the normal hand path injects the wrist with `setAnmMtx`
  *after* `calc()`, so skinned child joints would not follow the wrist. The fix is the
  `0xFB` idiom — `setBaseTRMtx(getAnmMtx(9))` *before* `calc()` — which means either
  forcing the pose index to `0xFB` and supplying the models, or a small patch to
  `setDrawHand` to parent the hand model that way unconditionally.
- Cost: the new bones are in a different coordinate parent than the body's, so a tail
  that must deform continuously with the tunic mesh cannot be done this way — a
  satellite is a separate mesh with its own skin.

**Option B — extend the body skeleton (35 + N), and fix the four blockers.**
Needed if the new bones must deform the *body* mesh (e.g. a tail that grows out of the
tunic with shared skinning, or extra deform bones for silhouette).

Four things must move together:

1. **The sentinel.** Change `jntNo == modelData->getJointNum() - 1` in
   `mDoExt_MtxCalcAnmBlendTblOld::calc` to a stored "last animated joint", or ensure the
   highest index stays inside the blend calc's reach.
2. **The 40.** `int sp38 = 40;` (`d_a_alink.cpp:4255`) sizes both old-frame arrays,
   indexed unconditionally by raw joint number. Raise it, or guarantee no visited joint
   exceeds 39.
3. **The 35s.** The callback install/teardown loops and `field_0x30c6`.
4. **Track coverage.** Either fence every appended subtree with `setMtxCalc` (and
   accept #1's consequence), or **pad every `.bck`** so its track count ≥ the new joint
   count. Padding is a well-defined offline binary transform and is arguably the
   cleanest — but it must cover the per-cutscene demo archives too, not just `AlAnm`,
   because `setDemoBodyBck` (`d_a_alink_demo.inc:1218-1235`) loads body animations from
   `dStage_roomControl_c::getDemoArcName()` into the same slots. That is an unbounded
   set of overlays, which argues for the fence-plus-sentinel-patch combination instead.

**Recommendation: start with Option A**, and only move to B for whatever genuinely
cannot be expressed as a satellite. A is reachable almost entirely through the mod API;
B is a port patch by construction.

### Driving the bones, once they exist

All three ride the same seam — a `J3DJointCallBack` at timing 0, using the
`ctrlJointCallBack` / `getUserArea()` pattern from §2:

- **Tail** — port `setWolfTailAngle`'s angle-chain (idiom A) or `daMidna_c::setHairAngle`'s
  position-chain (idiom B). Both already exist as working reference code, both use
  `cLib_addCalcAngleS`, both run on the fixed 1/30 s tick, and `frame_interp` smooths the
  output to display rate for free. Add a "a cutscene is authoring this bone, stand down"
  check — `headModelCallBack` already does exactly that at `d_a_alink.cpp:2461`.
- **Eyes** — read the residual gaze angles. Cleanest signal: hook
  `daAlink_c::setEyeMove(cXyz*, s16 eyeX, s16 eyeY)` and take the two s16 binary angles
  directly (±8192 ≡ ±45°). Map to eye-bone yaw/pitch, then optionally zero
  `m_eye_move_flg` (or leave the UV shift in place if the new eye textures are neutral).
  Expression is a separate axis — `daAlink_FTANM` via `m_faceTexDataTable` — and can be
  read the same way if brows/lids need bone-driven shapes.
- **Fingers** — the pose index resolved in `setDrawHand` (`var_r30`/`var_r29`, 0–10 plus
  sentinels `0xFE`/`0xFB`) is the trigger signal. Map each index to a stored finger pose
  and blend toward it with `cLib_addCalcAngleS` so poses ease rather than snap — vanilla
  snaps because it is swapping meshes, but nothing requires the replacement to.

### Authoring constraints for whoever builds the model

- **Joint index order 0–34 must match `AL_JNT` exactly.** Names are not the contract.
- **Limb segment lengths must match** the hard-coded arm/leg IK constants (§3).
- **Rigid and envelope geometry have opposite authoring requirements.** Rigid
  (`drawMtxFlag == 0`) applies **no** inverse bind — those vertices live in *joint-local*
  space. Envelope vertices live in *bind/model* space and are transformed by
  `anmMtx * invBind`. Rigging currently-rigid geometry (which is exactly what "rigged
  fingers" means — the hand meshes are rigid today) moves vertices from the first
  convention to the second.
- **≤ 10 draw matrices per SHP1 matrix group**, enforced by nothing. Rigging fingers is
  precisely the case that pushes a small mesh region past 10 influences.
- **Materials: ≤ 3 TEV stages, ≤ 3 texgens, texture slot 3 unused.** Link's models are
  `BMWR`-tagged, so the resource loader appends a warp texture/texgen/TEV stage to every
  material and writes slot 3 / stage 3. A material that already used them silently loses
  them, in release, with no message.
- **Big-endian, BMD not BDL.** The resource path `dRes_info_c::loaderBasicBmd`
  (`d_resorce.cpp:227-238` — note the misspelling in the filename) calls
  `J3DModelLoaderDataBase::load`, which accepts only `J3D2` + `bmd2`/`bmd3`
  (`J3DModelLoader.cpp:52-58`). BDL has a separate entry point,
  `loadBinaryDisplayList`, that this path never calls, so a `.bdl` in a character
  archive will not load. The loader byte-swaps records **in place** in the loaded
  buffer, so an `add_buffer`-backed archive must not be read by the mod afterwards, and
  the same bytes must never be handed to a second loader.
- **JNT1 name hashing** must use the engine's keycode (`k = k*3 + c`, `u16`-truncated) or
  `JUTNameTab::getIndex()` silently fails.
- **No LOD relief.** `J3DMdlFlag_EnableLOD` is never set on Link and there is no
  low-poly variant; the full-detail model is drawn in the main, shadow, mirror and
  status-window passes alike.

---

## 12. Open questions and what to prototype first

**Biggest practical gap: there is no authoring toolchain in either repo.** `tools/` is
three unrelated Python scripts; `assets/` holds only generated resource-ID headers.
Nothing here can produce or validate a BMD or repack a RARC. Every option above assumes
a valid replacement file exists, and producing one is out-of-tree work (a third-party
BMD exporter plus a RARC packer) that must satisfy engine invariants nobody in-tree
checks.

**Suspected latent bug worth confirming before it bites.** `J3DModelLoader.cpp:588`
computes `drawMtxData->mEntryNum = i_block->mMtxNum - mpModelData->getWEvlpMtxNum();`
and then `:598` allocates `mWEvlpImportantMtxIdx` with that count, while
`J3DJointTree::findImportantMtxIndex` (`:81-97`) writes
`wEvlpImportantMtxIdx[i + getDrawFullWgtMtxNum()]` for `i` in `[0, wEvlpMtxNum)`. If
DRW1's count includes both rigid and envelope entries, that overruns by the envelope
count — and the overrun **grows with the number of distinct weight sets**, which a more
heavily-rigged model increases. This needs checking against a real asset; it may be a
decompilation artefact rather than a live bug.

**Unverified: the symbol manifest.** `cmake/SymbolManifest.cmake` downloads a pinned
`symgen` release, so whether file-local statics like `daAlink_modelCallBack` end up
hookable by name is not knowable from source. Prefer `DEFINE_HOOK(&Class::method)` on
public non-virtual members (address relocations, no manifest needed) and verify by
building and dumping before committing to any by-name hook.

### Suggested order of de-risking

1. **Prove the overlay path end to end with a trivial change.** Repack `Kmdl.arc`
   unmodified, ship it as an `overlay/`, confirm the game is byte-identical. Then make
   one visible vertex change. This validates the toolchain before any skeleton work.
2. **Prove joint-index compatibility.** Replace `al.bmd` with a model that has the same
   35 joints and different geometry. If vanilla animations play correctly, §4's central
   claim is confirmed on real data.
3. **Prove the callback seam.** Ship a mod that installs a `J3DJointCallBack` on a body
   joint and rotates it visibly, using the `getUserArea()` pattern — and re-installs
   after a wardrobe change to confirm the stomping behaviour.
4. **Then the tail**, as a satellite model with the hat/hair chain as the template. It
   is the feature with the closest existing precedent and the fewest unknowns.
5. **Then the eyes** (a `setEyeMove` hook is small and self-contained), **then the
   fingers** (the most authoring-heavy, and the one needing the `0xFB`-style parenting
   change).

Do all of this on a **Debug** build — the asserts are the only validation that exists.
