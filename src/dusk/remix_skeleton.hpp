#ifndef DUSK_REMIX_SKELETON_H
#define DUSK_REMIX_SKELETON_H

// Publishes character identity and joint trees to RTX Remix.
//
// Why this exists. Remix keys everything on the draw call, and J3D draws a character as one draw
// per matrix group, so a capture contains dozens of unrelated pieces - each with its own geometry
// hash and its own invented skeleton (Remix names the joints "root/joint1..." and places them at
// vertex centroids). Pulling a body out for remastering, or replacing one, is impractical in that
// form.
//
// Remix cannot fix it alone, and neither can the backend, because the two missing facts live here:
//
//   1. Which joint the game loaded into which GX position-matrix slot. Slots are reused between
//      matrix groups - slot 3 is a different joint in the next one - so a blend index means
//      nothing without this.
//   2. The joint tree itself: names, parents and bind transforms, from J3DJointTree.
//
// So the game publishes (1) through aurora, which is the only side that knows how it compacted the
// palette into D3D9 blend indices, and (2) straight to the fork, which only needs it once per
// model. The fork then merges a character's draws into one mesh carrying its real armature.
//
// Nothing here changes what is drawn on any backend. If Remix is not present the declaration
// export is simply absent and every call becomes a no-op.
//
// Fork side: dxvk-remix/src/dxvk/rtx_render/rtx_dusklight_skeleton.{h,cpp}
// Aurora side: extern/aurora/lib/dx9/dx9_backend.cpp (publish_draw_skeleton)
// Design: docs/remix-open-issues.md issue 15.

class J3DModel;
class J3DModelData;
class J3DShapeMtx;

namespace dusk::remix_skeleton {

// Called from J3DShape::drawFast, once per matrix group, after the group's matrices are loaded and
// before its draw is issued. Declares the model's skeleton the first time it sees it, then tells
// aurora which joint sits in each GX slot for this group.
void set_matrix_group(const J3DShapeMtx* shapeMtx);

// Called after a shape packet's draws. Drops the identity so an unrelated draw is not attributed
// to this character - which would merge it into the body.
void end_shape();

// Called once per frame, immediately after aurora_begin_frame. Must be a point where aurora's GX
// FIFO has been drained: this is where per-model state is released, and a joint palette's address
// travels through the FIFO, so freeing one mid-frame would hand the command processor a dangling
// pointer at drain time.
void begin_frame();

// Drops every cached declaration. Model data is freed and reallocated across a stage change, and a
// stale pointer key could otherwise match a different model.
void reset();

} // namespace dusk::remix_skeleton

#endif
