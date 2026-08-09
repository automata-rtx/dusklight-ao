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

// J3DJoint stores its first child and next sibling but not its parent, so the tree is walked from
// the root to derive them. Iterative rather than recursive because a malformed tree would
// otherwise take the stack with it, and this runs on data loaded from disc.
void derive_parents(J3DJointTree& tree, u16 jointNum, std::vector<s32>& parents) {
    parents.assign(jointNum, -1);
    std::vector<J3DJoint*> stack;
    if (J3DJoint* root = tree.getRootNode()) {
        stack.push_back(root);
    }
    u32 visited = 0;
    while (!stack.empty() && visited <= jointNum) {
        J3DJoint* joint = stack.back();
        stack.pop_back();
        ++visited;
        for (J3DJoint* child = joint->getChild(); child != nullptr; child = child->getYounger()) {
            const u16 childNo = child->getJntNo();
            if (childNo < jointNum) {
                parents[childNo] = static_cast<s32>(joint->getJntNo());
            }
            stack.push_back(child);
        }
    }
}

// Invert the 3x4 inverse-bind matrix J3D stores, giving the joint's model-space transform in the
// bind pose - which is what USD calls skel:bindTransforms and what Blender draws as the rest
// armature.
void bind_transform(const Mtx& invBind, f32* out12) {
    Mtx bind;
    MTXInverse(const_cast<MtxPtr>(invBind), bind);
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 4; ++col) {
            out12[row * 4 + col] = bind[row][col];
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
    derive_parents(tree, jointNum, parents);

    std::vector<f32> bindTransforms(static_cast<size_t>(jointNum) * 12, 0.f);
    std::string packedNames;
    JUTNameTab* names = tree.getJointName();
    for (u16 i = 0; i < jointNum; ++i) {
        Mtx invBind;
        tree.getInvJointMtx(i).to_host(invBind);
        bind_transform(invBind, bindTransforms.data() + static_cast<size_t>(i) * 12);

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
// Invalidated in end_shape, so it is rebuilt once per shape packet rather than once per matrix
// group. It CANNOT be cached across frames: these are animated draw matrices and they change every
// frame, so a cache keyed on the model alone would pin a character in whatever pose it held when
// it was first seen.
//
// The pointer handed to aurora stays valid because the D3D9 backend consumes it synchronously -
// it copies the matrices straight into D3D9 world matrix state inside the same call - so nothing
// downstream holds it past the draw.
const J3DModel* s_paletteModel = nullptr;
std::vector<f32> s_jointPalette;

const f32* build_joint_palette(J3DModel* model, J3DModelData* modelData, u16 jointNum) {
    if (s_paletteModel == model && s_jointPalette.size() == static_cast<size_t>(jointNum) * 12) {
        return s_jointPalette.data();
    }

    J3DMtxBuffer* mtxBuffer = model->getMtxBuffer();
    if (mtxBuffer == NULL) {
        return NULL;
    }

    s_jointPalette.assign(static_cast<size_t>(jointNum) * 12, 0.f);
    for (u16 j = 0; j < jointNum; ++j) {
        f32* out = s_jointPalette.data() + static_cast<size_t>(j) * 12;
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
        const Mtx* drawMtx = mtxBuffer->getDrawMtx(i);
        if (drawMtx == NULL) {
            continue;
        }
        f32* out = s_jointPalette.data() + static_cast<size_t>(joint) * 12;
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 4; ++col) {
                out[row * 4 + col] = (*drawMtx)[row][col];
            }
        }
    }

    s_paletteModel = model;
    return s_jointPalette.data();
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

void reset() {
    s_declared.clear();
    s_paletteModel = nullptr;
    s_jointPalette.clear();
}

} // namespace dusk::remix_skeleton

#else

namespace dusk::remix_skeleton {
void set_matrix_group(const J3DShapeMtx*) {}
void end_shape() {}
void reset() {}
} // namespace dusk::remix_skeleton

#endif
