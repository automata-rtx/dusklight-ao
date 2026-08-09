#include "dusk/remix_skeleton.hpp"

#if TARGET_PC

#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include <dolphin/gx/GXAurora.h>

#include "JSystem/J3DGraphAnimator/J3DJoint.h"
#include "JSystem/J3DGraphAnimator/J3DJointTree.h"
#include "JSystem/J3DGraphAnimator/J3DModel.h"
#include "JSystem/J3DGraphAnimator/J3DModelData.h"
#include "JSystem/J3DGraphAnimator/J3DMtxBuffer.h"
#include "JSystem/J3DGraphBase/J3DPacket.h"
#include "JSystem/J3DGraphBase/J3DShape.h"
#include "JSystem/J3DGraphBase/J3DShapeMtx.h"
#include "JSystem/J3DGraphBase/J3DSys.h"
#include "JSystem/J3DGraphBase/J3DTransform.h"
#include "JSystem/JMath/JMath.h"
#include "JSystem/JUtility/JUTNameTab.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace dusk::remix_skeleton {
namespace {

constexpr u16 kNoJoint = 0xFFFF;

using PFN_dusklight_DeclareSkeleton = u32 (*)(u64 modelKey, u32 jointCount, const s32* parents,
                                              const f32* bindTransforms, const char* packedNames);

// Resolved once, including the not-found answer. The export only exists in the Dusklight fork of
// Remix; stock Remix and Microsoft's own d3d9.dll do not have it, and that is the ordinary case
// rather than an error.
PFN_dusklight_DeclareSkeleton declare_fn() {
    static bool resolved = false;
    static PFN_dusklight_DeclareSkeleton fn = nullptr;
    if (!resolved) {
        resolved = true;
#ifdef _WIN32
        if (HMODULE d3d9 = GetModuleHandleW(L"d3d9.dll")) {
            fn = reinterpret_cast<PFN_dusklight_DeclareSkeleton>(
                reinterpret_cast<void*>(GetProcAddress(d3d9, "dusklight_DeclareSkeleton")));
        }
#endif
    }
    return fn;
}

struct Declared {
    u64 modelKey = 0;
    bool accepted = false;
    // Cheap fingerprint of the model this entry was built for. The cache is keyed by pointer,
    // and model data is freed and reallocated across a stage change, so a later model can land on
    // an address a previous one used - and would then inherit its identity, quietly merging two
    // different characters. Re-checking two counts on every hit closes that without paying for the
    // full key, which walks every joint name.
    u16 jointNum = 0;
    u32 vtxNum = 0;
};

std::unordered_map<const J3DModelData*, Declared> s_declared;

// A key that is the same from one run to the next, which a pointer is not. Built from the model's
// shape - joint and vertex counts plus every joint name - so two different characters cannot
// collide and the same character keeps its identity across launches. That stability is the whole
// point: it is what makes the merged mesh's hash in a capture reproducible.
u64 model_key(J3DModelData* modelData) {
    J3DJointTree& tree = modelData->getJointTree();
    const u16 jointNum = tree.getJointNum();

    u64 hash = 1469598103934665603ull; // FNV-1a offset basis
    const auto mix = [&hash](const void* data, size_t size) {
        const auto* bytes = static_cast<const u8*>(data);
        for (size_t i = 0; i < size; ++i) {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
    };

    const u32 vtxNum = modelData->getVtxNum();
    const u16 drawMtxNum = modelData->getDrawMtxNum();
    mix(&jointNum, sizeof(jointNum));
    mix(&vtxNum, sizeof(vtxNum));
    mix(&drawMtxNum, sizeof(drawMtxNum));

    JUTNameTab* names = tree.getJointName();
    for (u16 i = 0; i < jointNum; ++i) {
        const char* name = names != nullptr ? names->getName(i) : nullptr;
        if (name != nullptr) {
            mix(name, std::strlen(name));
        }
        mix(&i, sizeof(i));
    }

    // Never hand back zero; the fork reads it as "no identity".
    return hash == 0 ? 1ull : hash;
}

// A joint's own transform, in its parent's space: translate-rotate, then a column scale. This is
// the composition J3DMtxCalcCalcTransformBasic and ...Maya perform on the same fields, so the
// tree built from it is the model's rest pose as J3D itself would compute it with no animation
// bound.
void local_transform(const J3DTransformInfo& info, Mtx out) {
    J3DGetTranslateRotateMtx(info, out);
    JMAMTXApplyScale(out, out, info.mScale.x, info.mScale.y, info.mScale.z);
}

void store_3x4(const Mtx& src, f32* out12) {
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 4; ++col) {
            out12[row * 4 + col] = src[row][col];
        }
    }
}

void load_3x4(const f32* in12, Mtx out) {
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 4; ++col) {
            out[row][col] = in12[row * 4 + col];
        }
    }
}

// Walk the joint tree from the root, filling in each joint's parent and its model-space transform
// in the bind pose - what USD calls skel:bindTransforms and what Blender draws as the rest
// armature.
//
// The obvious source for the bind pose is J3DJointTree::getInvJointMtx, and revision 1 of this
// file used it. That was a crash: J3DModelLoader::readEnvelop is the ONLY place mInvJointMtx is
// ever assigned (J3DModelLoader.cpp:581), and it runs only for models carrying an EVP1 envelope
// block - J3DJointTree's constructor leaves the pointer NULL for every other model
// (J3DJointTree.cpp:21). Reading it faulted at address 0 on the first rigid model drawn, which is
// before any scene is visible. J3DTransformInfo is loaded for every joint of every model, so
// composing the tree from it has no such gap.
//
// Iterative rather than recursive: a malformed tree would otherwise take the stack with it, and
// this runs on data loaded from disc. The visit cap is the same guard for a cycle.
void build_bind_pose(J3DJointTree& tree, u16 jointNum, std::vector<s32>& parents,
                     std::vector<f32>& bindTransforms) {
    parents.assign(jointNum, -1);
    bindTransforms.assign(static_cast<size_t>(jointNum) * 12, 0.f);

    // bindTransforms doubles as the working store for the walk: a joint's model-space matrix IS
    // its bind transform, and the parent's is always already written when a child reads it.
    for (u16 i = 0; i < jointNum; ++i) {
        Mtx identity;
        MTXIdentity(identity);
        store_3x4(identity, bindTransforms.data() + static_cast<size_t>(i) * 12);
    }

    // Depth-first, so a parent is always processed before its children and its world matrix is
    // ready when they need it.
    std::vector<J3DJoint*> stack;
    if (J3DJoint* root = tree.getRootNode()) {
        stack.push_back(root);
    }
    u32 visited = 0;
    while (!stack.empty() && visited <= jointNum) {
        J3DJoint* joint = stack.back();
        stack.pop_back();
        ++visited;

        const u16 jntNo = joint->getJntNo();
        if (jntNo < jointNum) {
            Mtx local;
            local_transform(joint->getTransformInfo(), local);
            Mtx world;
            const s32 parent = parents[jntNo];
            if (parent >= 0 && static_cast<u16>(parent) < jointNum) {
                Mtx parentWorld;
                load_3x4(bindTransforms.data() + static_cast<size_t>(parent) * 12, parentWorld);
                MTXConcat(parentWorld, local, world);
            } else {
                MTXCopy(local, world);
            }
            store_3x4(world, bindTransforms.data() + static_cast<size_t>(jntNo) * 12);
        }

        for (J3DJoint* child = joint->getChild(); child != nullptr; child = child->getYounger()) {
            const u16 childNo = child->getJntNo();
            if (childNo < jointNum) {
                parents[childNo] = static_cast<s32>(jntNo);
            }
            stack.push_back(child);
        }
    }
}

bool declare(J3DModelData* modelData, u64& modelKeyOut) {
    const u16 jointNumNow = modelData->getJointTree().getJointNum();
    const u32 vtxNumNow = modelData->getVtxNum();

    const auto existing = s_declared.find(modelData);
    if (existing != s_declared.end() && existing->second.jointNum == jointNumNow &&
        existing->second.vtxNum == vtxNumNow) {
        modelKeyOut = existing->second.modelKey;
        return existing->second.accepted;
    }

    Declared record;
    record.jointNum = jointNumNow;
    record.vtxNum = vtxNumNow;
    record.modelKey = model_key(modelData);
    modelKeyOut = record.modelKey;

    const PFN_dusklight_DeclareSkeleton fn = declare_fn();
    J3DJointTree& tree = modelData->getJointTree();
    const u16 jointNum = tree.getJointNum();
    if (fn == nullptr || jointNum == 0) {
        // Cache the refusal too. Without that, a model would rebuild its whole joint tree every
        // matrix group of every frame on a run with no Remix attached.
        s_declared[modelData] = record;
        return false;
    }

    std::vector<s32> parents;
    std::vector<f32> bindTransforms;
    build_bind_pose(tree, jointNum, parents, bindTransforms);

    std::string packedNames;
    JUTNameTab* names = tree.getJointName();
    for (u16 i = 0; i < jointNum; ++i) {
        const char* name = names != nullptr ? names->getName(i) : nullptr;
        packedNames.append(name != nullptr && name[0] != '\0' ? name : "joint");
        packedNames.push_back('\0');
    }

    record.accepted = fn(record.modelKey, jointNum, parents.data(), bindTransforms.data(),
                         packedNames.c_str()) != 0;
    s_declared[modelData] = record;
    return record.accepted;
}

// The model's whole joint palette, rebuilt whenever the model being drawn changes.
//
// Each entry is a straight copy of the draw matrix the game would have loaded into a GX
// position-matrix slot for that joint - J3DShapeMtx::load reads exactly these - so handing them
// over indexed by joint changes the addressing and nothing else. That equivalence is the reason
// this is safe: WORLDMATRIX(joint) ends up holding precisely what WORLDMATRIX(compactedSlot) held.
//
// Only rigid draw matrices contribute. A weighted envelope is a blend of several joints and has no
// single joint to sit at; those entries stay identity, the original geometry never indexes them
// because it reaches them through its own slots, and a replacement that does index one gets a
// bone that does not move rather than one that moves wrongly.
//
// THE BUFFER IS PER MODEL AND ITS ADDRESS NEVER MOVES. That is not tidiness, it is required:
// GXSetModelIdentity writes the palette's *address* into the GX FIFO, and the FIFO is not drained
// until aurora::end_frame (lib/gx/fifo.cpp drain(), called from aurora.cpp end_frame). Revision 1
// used one shared std::vector rebuilt per shape packet, so by the time the command processor read
// any of those addresses the vector had been reassigned - to a different model's matrices, and
// after a size change to freed memory. A per-model buffer that is allocated once and only ever
// overwritten in place is read-correct whenever it is drained.
//
// Contents are still refreshed per model per frame rather than cached across frames: these are
// animated draw matrices. Overwriting in place is safe against the deferred drain because the
// draw matrices are computed once per frame in the calc phase and do not change again during the
// draw phase, so a command queued earlier in the same frame reads the values it was queued with.
struct Palette {
    std::vector<f32> values;
    u16 jointNum = 0;
};

std::unordered_map<const J3DModel*, Palette> s_palettes;
const J3DModel* s_paletteModel = nullptr;

const f32* build_joint_palette(J3DModel* model, J3DModelData* modelData, u16 jointNum) {
    J3DMtxBuffer* mtxBuffer = model->getMtxBuffer();
    if (mtxBuffer == NULL) {
        return NULL;
    }
    // A model with no draw matrices of its own reports them through a single shared "not in use"
    // matrix (J3DMtxBuffer::setNoUseDrawMtx, taken for J3DMdlDataFlag_NoAnimation and for
    // ConcatView loads). getDrawMtx(i) would index off the end of that one matrix, so there is
    // nothing here to publish.
    Mtx** drawMtxArr = mtxBuffer->getDrawMtxPtrPtr();
    if (drawMtxArr == NULL || drawMtxArr == &J3DMtxBuffer::sNoUseDrawMtxPtr) {
        return NULL;
    }
    Mtx* drawMtxBase = mtxBuffer->getDrawMtxPtr();
    if (drawMtxBase == NULL || drawMtxBase == J3DMtxBuffer::sNoUseDrawMtxPtr) {
        return NULL;
    }

    Palette& palette = s_palettes[model];
    const size_t wanted = static_cast<size_t>(jointNum) * 12;
    const bool sameModelThisPacket = (s_paletteModel == model) && palette.jointNum == jointNum &&
                                     palette.values.size() == wanted;
    if (sameModelThisPacket) {
        return palette.values.data();
    }

    if (palette.values.size() != wanted) {
        // The only resize this buffer ever takes. Anything already queued in the FIFO against the
        // old address belongs to a model that no longer exists at this pointer.
        palette.values.assign(wanted, 0.f);
        palette.jointNum = jointNum;
    }
    for (u16 j = 0; j < jointNum; ++j) {
        f32* out = palette.values.data() + static_cast<size_t>(j) * 12;
        for (size_t k = 0; k < 12; ++k) {
            out[k] = 0.f;
        }
        out[0] = 1.f;
        out[5] = 1.f;
        out[10] = 1.f;
    }

    const u16 drawMtxNum = modelData->getDrawMtxNum();
    for (u16 i = 0; i < drawMtxNum; ++i) {
        if (modelData->getDrawMtxFlag(i) != 0) {
            continue; // weighted envelope - no single joint to place it at
        }
        const u16 joint = modelData->getDrawMtxIndex(i);
        if (joint >= jointNum) {
            continue;
        }
        const Mtx& drawMtx = drawMtxBase[i];
        f32* out = palette.values.data() + static_cast<size_t>(joint) * 12;
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 4; ++col) {
                out[row * 4 + col] = drawMtx[row][col];
            }
        }
    }

    s_paletteModel = model;
    return palette.values.data();
}

} // namespace

void set_matrix_group(const J3DShapeMtx* shapeMtx) {
    if (shapeMtx == nullptr) {
        return;
    }
    J3DShapePacket* packet = j3dSys.getShapePacket();
    if (packet == nullptr) {
        return;
    }
    J3DModel* model = packet->getModel();
    if (model == nullptr) {
        return;
    }
    J3DModelData* modelData = model->getModelData();
    if (modelData == nullptr) {
        return;
    }

    u64 modelKey = 0;
    if (!declare(modelData, modelKey)) {
        return;
    }

    J3DJointTree& tree = modelData->getJointTree();
    const u16 jointNum = tree.getJointNum();

    // Which joint sits in each GX position-matrix slot for this group. A slot holding a weighted
    // envelope is reported as kNoJoint rather than as its dominant joint: an envelope is a blend
    // of several joints, and calling it one of them would silently rebind part of a body to the
    // wrong bone. Those draws simply stay unmerged, which is honest.
    u16 slotToJoint[GX_AURORA_MAX_PN_MTX];
    for (u32 i = 0; i < GX_AURORA_MAX_PN_MTX; ++i) {
        slotToJoint[i] = kNoJoint;
    }

    const u16 useMtxNum = shapeMtx->getUseMtxNum();
    const u32 slotCount = useMtxNum < GX_AURORA_MAX_PN_MTX ? useMtxNum : GX_AURORA_MAX_PN_MTX;
    for (u32 slot = 0; slot < slotCount; ++slot) {
        const u16 drawMtxIndex = shapeMtx->getUseMtxIndex(static_cast<u16>(slot));
        if (drawMtxIndex >= modelData->getDrawMtxNum()) {
            continue;
        }
        // Flag 0 is a rigid draw matrix whose index is a joint; 1 is a weighted envelope whose
        // index is an envelope, and there is no single joint to name.
        if (modelData->getDrawMtxFlag(drawMtxIndex) != 0) {
            continue;
        }
        const u16 joint = modelData->getDrawMtxIndex(drawMtxIndex);
        if (joint < jointNum) {
            slotToJoint[slot] = joint;
        }
    }

    // The palette is what lets aurora address world matrices by joint instead of by this draw's
    // compacted slots, so every draw of the character agrees on what bone 7 means - which is what
    // a replacement body authored against the model's skeleton needs.
    const f32* jointPalette = build_joint_palette(model, modelData, jointNum);

    GXSetModelIdentity(modelKey, reinterpret_cast<u64>(model), jointNum, slotCount, slotToJoint,
                       jointPalette);
}

void end_shape() {
    GXClearModelIdentity();
    // Force the next packet to rebuild. See build_joint_palette: these matrices animate, and a
    // palette held across frames would freeze the character.
    s_paletteModel = nullptr;
}

void begin_frame() {
    s_paletteModel = nullptr;

    // Both maps are keyed by pointers to objects the game frees - a model instance dies with its
    // actor, model data with its archive - so entries accumulate over a session. Dropping them
    // wholesale is only safe where no queued GX command can still name a palette address, which
    // is exactly here: aurora drained the FIFO in the previous end_frame. Doing this from
    // anywhere inside a frame would hand the command processor a freed pointer.
    //
    // The cost of a drop is one rebuilt palette and one re-declaration per live model, and the
    // fork's declare is idempotent on the model key.
    constexpr size_t kMaxTracked = 512;
    if (s_palettes.size() > kMaxTracked) {
        s_palettes.clear();
    }
    if (s_declared.size() > kMaxTracked) {
        s_declared.clear();
    }
}

void reset() {
    s_declared.clear();
    s_paletteModel = nullptr;
    s_palettes.clear();
}

} // namespace dusk::remix_skeleton

#else

namespace dusk::remix_skeleton {
void set_matrix_group(const J3DShapeMtx*) {}
void end_shape() {}
void begin_frame() {}
void reset() {}
} // namespace dusk::remix_skeleton

#endif
