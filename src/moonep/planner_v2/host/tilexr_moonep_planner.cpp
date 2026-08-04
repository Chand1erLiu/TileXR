#include "tilexr_ep_plan.h"
#include "tilexr_moonep_planner.h"

#include <limits>
#include <vector>

#include "acl/acl_rt.h"

#include "tilexr_types.h"

#include "ep_plan_host.h"
#include "ep_plan_layout.h"
#include "planner_launch.h"

namespace TileXREp {
namespace Plan {

int PreparePlanLaunchContext(
    TileXRCommPtr comm, const PlanHostArguments &arguments, PlanHostContext *context)
{
    if (comm == nullptr || context == nullptr) {
        return TileXR::TILEXR_ERROR_PARA_CHECK_FAIL;
    }
    *context = PlanHostContext {};

    TileXR::CommArgs *hostCommArgs = nullptr;
    int ret = TileXRGetCommArgsHost(comm, hostCommArgs);
    if (ret != TileXR::TILEXR_SUCCESS) {
        return ret;
    }
    if (hostCommArgs == nullptr) {
        return TileXR::TILEXR_ERROR_NOT_INITIALIZED;
    }

    GM_ADDR deviceCommArgs = nullptr;
    ret = TileXRGetCommArgsDev(comm, deviceCommArgs);
    if (ret != TileXR::TILEXR_SUCCESS) {
        return ret;
    }
    if (deviceCommArgs == nullptr) {
        return TileXR::TILEXR_ERROR_NOT_INITIALIZED;
    }

    const PlanRuntimeMetadata runtime {
        hostCommArgs,
        deviceCommArgs,
    };
    return ValidatePlanHostArguments(arguments, runtime, context);
}

} // namespace Plan
} // namespace TileXREp

namespace {

constexpr uint64_t kDefaultPlanWaitIterations = 1ULL << 24;

int RunOptimizedPlan(const int32_t *topkExperts, const int32_t *tokensPerExpert,
    const int32_t *globalRankIds, TileXRCommPtr comm, int64_t s, int64_t topK, int64_t expertNum,
    const TileXRMoonEPPlanConfig *config, TileXRMoonEPPlanDesc *plan, void *localWorkspace,
    uint64_t localWorkspaceBytes, void *metaWorkspace, uint64_t metaBytes,
    uint64_t waitIterations, aclrtStream stream)
{
    const TileXREp::Plan::PlanHostArguments arguments {
        topkExperts, tokensPerExpert, globalRankIds, s, topK, expertNum, config, plan,
        localWorkspace, localWorkspaceBytes, metaWorkspace, metaBytes, waitIterations, stream,
    };
    TileXREp::Plan::PlanHostContext context {};
    const int ret = TileXREp::Plan::PreparePlanLaunchContext(comm, arguments, &context);
    if (ret != TileXR::TILEXR_SUCCESS) return ret;
    return TileXREp::Plan::LaunchPlanKernel(comm, arguments, context);
}

} // namespace

int TileXRMoeEpPlanV2(const int32_t *topkExperts, const int32_t *tokensPerExpert,
    const int32_t *globalRankIds, TileXRCommPtr comm, int64_t s, int64_t topK, int64_t expertNum,
    const TileXRMoonEPPlanConfig *config, TileXRMoonEPPlanDesc *plan, void *localWorkspace,
    uint64_t localWorkspaceBytes, void *registeredMetaWorkspace, uint64_t registeredMetaBytes,
    aclrtStream stream)
{
    return RunOptimizedPlan(topkExperts, tokensPerExpert, globalRankIds, comm, s, topK, expertNum,
        config, plan, localWorkspace, localWorkspaceBytes, registeredMetaWorkspace,
        registeredMetaBytes, kDefaultPlanWaitIterations, stream);
}

namespace {

struct MoonEpCompatLayout {
    TileXREp::Plan::PlanWorkspaceLayout optimized {};
    uint64_t localWorkspaceOffset = 0;
    uint64_t registeredMetaOffset = 0;
    uint64_t globalRankIdsOffset = 0;
    uint64_t dupGroupsOffset = 0;
    uint64_t dupLoffsOffset = 0;
    uint64_t dupCountsOffset = 0;
    uint64_t statusOffset = 0;
    uint64_t totalBytes = 0;
};

bool CompatCheckedAdd(uint64_t lhs, uint64_t rhs, uint64_t *out)
{
    if (out == nullptr || rhs > std::numeric_limits<uint64_t>::max() - lhs) return false;
    *out = lhs + rhs;
    return true;
}

bool CompatCheckedMul(uint64_t lhs, uint64_t rhs, uint64_t *out)
{
    if (out == nullptr || (lhs != 0 && rhs > std::numeric_limits<uint64_t>::max() / lhs)) return false;
    *out = lhs * rhs;
    return true;
}

bool CompatAlign(uint64_t value, uint64_t *out)
{
    const uint64_t alignment = TileXREp::Plan::kPlanWorkspaceAlignment;
    const uint64_t remainder = value % alignment;
    return remainder == 0 ? ((*out = value), true) : CompatCheckedAdd(value, alignment - remainder, out);
}

bool CompatAddRegion(uint64_t bytes, uint64_t *cursor, uint64_t *offset)
{
    uint64_t aligned = 0;
    uint64_t end = 0;
    if (cursor == nullptr || offset == nullptr || !CompatAlign(*cursor, &aligned) ||
        !CompatCheckedAdd(aligned, bytes, &end)) return false;
    *offset = aligned;
    *cursor = end;
    return true;
}

TileXRMoonEPPlanConfig MakeMoonEpCompatConfig(int64_t rankSize, int64_t s, int64_t k,
    int64_t expertCount)
{
    TileXRMoonEPPlanConfig config {};
    config.prefetchSlots = expertCount / rankSize;
    config.rankTokenCapacity = s * k;
    config.nvS = config.rankTokenCapacity;
    config.tokenPadding = 1;
    config.tokenRouteLimitPerPair = 0;
    config.cardsPerServer = TileXREp::Plan::kPlanCardsPerServer;
    config.cardsPerCabinet = TileXREp::Plan::kPlanCardsPerCabinet;
    config.crossCandidateCount = TileXREp::Plan::kPlanCrossCandidateCount;
    return config;
}

int BuildMoonEpCompatLayout(int64_t rankSize, int64_t s, int64_t k, int64_t expertCount,
    MoonEpCompatLayout *out)
{
    if (out == nullptr || rankSize <= 0 || s <= 0 || k <= 0 || expertCount <= 0 ||
        expertCount % rankSize != 0 || s > std::numeric_limits<int64_t>::max() / k) {
        return TileXR::TILEXR_ERROR_PARA_CHECK_FAIL;
    }
    const TileXRMoonEPPlanConfig config = MakeMoonEpCompatConfig(rankSize, s, k, expertCount);
    MoonEpCompatLayout layout {};
    int ret = TileXREp::Plan::BuildPlanWorkspaceLayout(
        rankSize, s, k, expertCount, config, &layout.optimized);
    if (ret != TileXR::TILEXR_SUCCESS) return ret;

    uint64_t cursor = layout.optimized.local.totalBytes;
    layout.localWorkspaceOffset = 0;
    if (!CompatAddRegion(layout.optimized.registeredMeta.totalBytes, &cursor,
            &layout.registeredMetaOffset)) return TileXR::TILEXR_ERROR_PARA_CHECK_FAIL;

    uint64_t bytes = 0;
    uint64_t dupGroupElements = 0;
    const uint64_t rankSizeU = static_cast<uint64_t>(rankSize);
    const uint64_t expertCountU = static_cast<uint64_t>(expertCount);
    const uint64_t cap = static_cast<uint64_t>(config.rankTokenCapacity);
    if (!CompatCheckedMul(rankSizeU, sizeof(int32_t), &bytes) ||
        !CompatAddRegion(bytes, &cursor, &layout.globalRankIdsOffset) ||
        !CompatCheckedMul(cap, 3U, &dupGroupElements) ||
        !CompatCheckedMul(dupGroupElements, sizeof(int32_t), &bytes) ||
        !CompatAddRegion(bytes, &cursor, &layout.dupGroupsOffset) ||
        !CompatCheckedMul(cap, sizeof(int32_t), &bytes) ||
        !CompatAddRegion(bytes, &cursor, &layout.dupLoffsOffset) ||
        !CompatCheckedMul(2U, sizeof(int32_t), &bytes) ||
        !CompatAddRegion(bytes, &cursor, &layout.dupCountsOffset) ||
        !CompatCheckedMul(TileXREp::Plan::kPlanStatusWords, sizeof(int32_t), &bytes) ||
        !CompatAddRegion(bytes, &cursor, &layout.statusOffset) ||
        !CompatAlign(cursor, &layout.totalBytes) ||
        layout.totalBytes > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        return TileXR::TILEXR_ERROR_PARA_CHECK_FAIL;
    }
    *out = layout;
    return TileXR::TILEXR_SUCCESS;
}

int GetCompatCommArgs(TileXRCommPtr comm, TileXR::CommArgs **args)
{
    if (comm == nullptr || args == nullptr) return TileXR::TILEXR_ERROR_PARA_CHECK_FAIL;
    *args = nullptr;
    int ret = TileXRGetCommArgsHost(comm, *args);
    if (ret != TileXR::TILEXR_SUCCESS) return ret;
    if (*args == nullptr || (*args)->rankSize <= 0 || (*args)->rank < 0 ||
        (*args)->rank >= (*args)->rankSize) return TileXR::TILEXR_ERROR_NOT_INITIALIZED;
    return TileXR::TILEXR_SUCCESS;
}


} // namespace

int TileXRMoonEpPlannerGetWorkspaceSizeV2(TileXRCommPtr comm, int64_t s, int64_t k,
    int64_t expertCount, uint64_t *workspaceBytes, int64_t *dispatchedCapacity)
{
    if (workspaceBytes == nullptr || dispatchedCapacity == nullptr) {
        return TileXR::TILEXR_ERROR_PARA_CHECK_FAIL;
    }
    *workspaceBytes = 0;
    *dispatchedCapacity = 0;
    TileXR::CommArgs *args = nullptr;
    int ret = GetCompatCommArgs(comm, &args);
    if (ret != TileXR::TILEXR_SUCCESS) return ret;
    MoonEpCompatLayout layout {};
    ret = BuildMoonEpCompatLayout(args->rankSize, s, k, expertCount, &layout);
    if (ret != TileXR::TILEXR_SUCCESS) return ret;
    *workspaceBytes = layout.totalBytes;
    *dispatchedCapacity = s * k;
    return TileXR::TILEXR_SUCCESS;
}

int TileXRMoonEpPlannerV2(const int32_t *topkExpertIds, const int32_t *tokensPerExpert,
    TileXRCommPtr comm, int64_t s, int64_t k, int64_t expertCount,
    void *workspace, uint64_t workspaceBytes, int32_t *dst, int32_t *cuSeqlens,
    int32_t *expertsToCopy, int32_t *remoteStats, int32_t *plannerStatus,
    uint64_t waitIterations, aclrtStream stream)
{
    if (topkExpertIds == nullptr || tokensPerExpert == nullptr || workspace == nullptr ||
        dst == nullptr || cuSeqlens == nullptr || expertsToCopy == nullptr || remoteStats == nullptr ||
        plannerStatus == nullptr || waitIterations == 0 || stream == nullptr ||
        reinterpret_cast<uintptr_t>(workspace) % TileXREp::Plan::kPlanWorkspaceAlignment != 0) {
        return TileXR::TILEXR_ERROR_PARA_CHECK_FAIL;
    }
    TileXR::CommArgs *args = nullptr;
    int ret = GetCompatCommArgs(comm, &args);
    if (ret != TileXR::TILEXR_SUCCESS) return ret;
    MoonEpCompatLayout layout {};
    ret = BuildMoonEpCompatLayout(args->rankSize, s, k, expertCount, &layout);
    if (ret != TileXR::TILEXR_SUCCESS || workspaceBytes < layout.totalBytes) {
        return TileXR::TILEXR_ERROR_PARA_CHECK_FAIL;
    }

    uint8_t *base = static_cast<uint8_t *>(workspace);
    GM_ADDR registeredMeta = base + layout.registeredMetaOffset;
    if (args->localRankSize <= 0 || args->rankSize % args->localRankSize != 0) {
        return TileXR::TILEXR_ERROR_PARA_CHECK_FAIL;
    }
    std::vector<int32_t> rankIds(static_cast<size_t>(args->rankSize));
    for (int32_t rank = 0; rank < args->rankSize; ++rank) {
        rankIds[rank] = (rank / args->localRankSize) * TileXREp::Plan::kPlanCardsPerServer +
            (rank % args->localRankSize);
    }
    int32_t *rankIdsDev = reinterpret_cast<int32_t *>(base + layout.globalRankIdsOffset);
    if (aclrtMemcpyAsync(rankIdsDev, static_cast<size_t>(args->rankSize) * sizeof(int32_t),
            rankIds.data(), static_cast<size_t>(args->rankSize) * sizeof(int32_t),
            ACL_MEMCPY_HOST_TO_DEVICE, stream) != 0) return TileXR::TILEXR_ERROR_MKIRT;

    int64_t epochMagic = 0;
    ret = TileXRCommNextMagic(comm, &epochMagic);
    if (ret != TileXR::TILEXR_SUCCESS) return ret;

    const TileXRMoonEPPlanConfig config = MakeMoonEpCompatConfig(args->rankSize, s, k, expertCount);
    TileXRMoonEPPlanDesc plan {};
    plan.dst = dst;
    plan.cuSeqlens = cuSeqlens;
    plan.expertsToCopy = expertsToCopy;
    plan.remoteStats = remoteStats;
    plan.dupGroups = reinterpret_cast<int32_t *>(base + layout.dupGroupsOffset);
    plan.dupLoffs = reinterpret_cast<int32_t *>(base + layout.dupLoffsOffset);
    plan.dupCounts = reinterpret_cast<int32_t *>(base + layout.dupCountsOffset);
    plan.status = reinterpret_cast<int32_t *>(base + layout.statusOffset);
    plan.s = s;
    plan.k = k;
    plan.r = args->rankSize;
    plan.e = expertCount;
    plan.b = config.prefetchSlots;
    plan.cap = config.rankTokenCapacity;
    plan.nvS = config.nvS;
    plan.tokenPadding = config.tokenPadding;
    plan.epoch = static_cast<uint64_t>(epochMagic);

    ret = RunOptimizedPlan(topkExpertIds, tokensPerExpert, rankIdsDev, comm, s, k,
        expertCount, &config, &plan, base + layout.localWorkspaceOffset,
        layout.optimized.local.totalBytes, registeredMeta, layout.optimized.registeredMeta.totalBytes,
        waitIterations, stream);
    if (ret != TileXR::TILEXR_SUCCESS) return ret;

    const TileXREp::Plan::PlanRegion &remoteSet = layout.optimized.local.remoteExpertSet;
    if (aclrtMemcpyAsync(expertsToCopy, remoteSet.bytes, base + remoteSet.offset, remoteSet.bytes,
            ACL_MEMCPY_DEVICE_TO_DEVICE, stream) != 0 ||
        aclrtMemcpyAsync(plannerStatus, sizeof(int32_t), plan.status, sizeof(int32_t),
            ACL_MEMCPY_DEVICE_TO_DEVICE, stream) != 0) {
        return TileXR::TILEXR_ERROR_MKIRT;
    }
    return TileXR::TILEXR_SUCCESS;
}
