#ifndef DUSK_GPU_SKINNING_H
#define DUSK_GPU_SKINNING_H

class J3DModel;
class J3DSkinDeform;
class J3DShapePacket;

namespace dusk::gpu_skin {

// Called from J3DSkinDeform::deform on PC. If the model can be skinned on the GPU, computes the
// per-joint palette, ensures the influence table is built, records the model as GPU-skinned for
// this frame, and returns true so the caller skips the CPU vertex deform. Returns false to fall
// back to the (identical) CPU path.
bool try_deform(J3DSkinDeform* skinDeform, J3DModel* model);

// Called from J3DShapePacket::drawFast on PC, before the shape's display list is issued. If the
// packet's model is GPU-skinned, emits GXSetSkinning with the palette, influence table, and the
// shape's model->view base matrix, and returns true (call end_shape after the draw).
bool begin_shape(J3DShapePacket* packet);
void end_shape();

} // namespace dusk::gpu_skin

#endif
