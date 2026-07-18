#include "dusk/gpu_skinning.h"

#if TARGET_PC

#include <unordered_map>
#include <vector>

#include <dolphin/gx/GXAurora.h>

#include "JSystem/J3DGraphAnimator/J3DJointTree.h"
#include "JSystem/J3DGraphAnimator/J3DModel.h"
#include "JSystem/J3DGraphAnimator/J3DModelData.h"
#include "JSystem/J3DGraphAnimator/J3DMtxBuffer.h"
#include "JSystem/J3DGraphAnimator/J3DSkinDeform.h"
#include "JSystem/J3DGraphBase/J3DPacket.h"

namespace dusk::gpu_skin {
namespace {

constexpr u32 kMaxInfluences = GX_AURORA_MAX_SKIN_INFLUENCES; // 4

// One record per (bone, weight); byte layout mirrors the aurora skinning shader.
struct InfluenceRec {
    u32 bone;
    f32 weight;
};

// Forward per-position influence table, keyed by position index and padded to kMaxInfluences.
// Derived from J3DModelData (shared by all instances) and constant, so it is built once.
struct Table {
    std::vector<InfluenceRec> records; // vtxNum * kMaxInfluences
    bool supported = false;
};

std::unordered_map<const J3DModelData*, Table> s_tables;

// Per-frame descriptor for a live GPU-skinned model instance.
struct Descriptor {
    const Mtx* palette; // mPosMtx: anm * invBind per joint
    const InfluenceRec* influences;
    u32 jointCount;
    u32 vtxCount;
};

std::unordered_map<const J3DModel*, Descriptor> s_active;

// Invert J3DSkinDeform's per-joint skin lists into a per-position influence table. This mirrors the
// exact linear-blend the CPU fast path performs (same joints, same weights), so the GPU result is
// identical - independent of the skinning coordinate conventions. Falls back (supported = false)
// when any vertex has more than kMaxInfluences influences, so those models keep the CPU path.
const Table& build_table(J3DSkinDeform* skinDeform, J3DModelData* modelData) {
    auto existing = s_tables.find(modelData);
    if (existing != s_tables.end()) {
        return existing->second;
    }

    Table table;
    const u32 vtxNum = modelData->getVtxNum();
    const u16 jointNum = modelData->getJointNum();
    table.records.assign(static_cast<size_t>(vtxNum) * kMaxInfluences, InfluenceRec{0u, 0.0f});
    std::vector<u32> counts(vtxNum, 0);

    const J3DSkinNList* lists = skinDeform->getSkinNList();
    bool ok = lists != nullptr;
    for (u16 j = 0; ok && j < jointNum; ++j) {
        const J3DSkinNList& list = lists[j];
        const u16 num = list.field_0x10; // position influences assigned to this joint
        for (u16 k = 0; k < num; ++k) {
            const u32 vert = list.field_0x0[k];
            if (vert >= vtxNum) {
                continue;
            }
            u32& count = counts[vert];
            if (count >= kMaxInfluences) {
                ok = false; // more than kMaxInfluences influences: use the CPU path instead
                break;
            }
            InfluenceRec& rec = table.records[static_cast<size_t>(vert) * kMaxInfluences + count];
            rec.bone = j;
            rec.weight = list.field_0x8[k];
            ++count;
        }
    }

    table.supported = ok;
    if (!ok) {
        table.records.clear();
    }
    return s_tables.emplace(modelData, std::move(table)).first->second;
}

} // namespace

bool try_deform(J3DSkinDeform* skinDeform, J3DModel* model) {
    J3DMtxBuffer* mtxBuffer = model->getMtxBuffer();
    if (mtxBuffer == NULL) {
        return false;
    }
    J3DJointTree* jointTree = mtxBuffer->getJointTree();
    // Fast-skin (F32) envelope models only; everything else keeps the stock CPU path.
    if (jointTree == NULL || !jointTree->checkFlag(0x100) || jointTree->getWEvlpMtxNum() == 0) {
        s_active.erase(model);
        return false;
    }

    J3DModelData* modelData = model->getModelData();
    const Table& table = build_table(skinDeform, modelData);
    if (!table.supported) {
        s_active.erase(model);
        return false;
    }

    // Compute the per-joint palette (anm * invBind), exactly as the skipped CPU path would.
    skinDeform->calcAnmInvJointMtx(mtxBuffer);

    Descriptor desc;
    desc.palette = skinDeform->getPosMtx();
    desc.influences = table.records.data();
    desc.jointCount = jointTree->getJointNum();
    desc.vtxCount = modelData->getVtxNum();
    s_active[model] = desc;
    return true; // skip the CPU vertex deform; the shader blends from the rest pose
}

bool begin_shape(J3DShapePacket* packet) {
    auto it = s_active.find(packet->getModel());
    if (it == s_active.end()) {
        return false;
    }
    const Descriptor& desc = it->second;
    GXSetSkinning(desc.palette, desc.jointCount, desc.influences, desc.vtxCount, kMaxInfluences,
                  *packet->getBaseMtxPtr());
    return true;
}

void end_shape() { GXClearSkinning(); }

} // namespace dusk::gpu_skin

#endif
