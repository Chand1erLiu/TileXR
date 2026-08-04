#ifndef TILEXR_EP_PLAN_H
#define TILEXR_EP_PLAN_H

#ifdef __cplusplus

#include <cstdint>

#include "acl/acl_base.h"
#include "tilexr_api.h"

struct TileXRMoonEPPlanConfig {
    int64_t prefetchSlots;
    int64_t rankTokenCapacity;
    int64_t nvS;
    int64_t tokenPadding;
    int64_t tokenRouteLimitPerPair;
    int32_t cardsPerServer;
    int32_t cardsPerCabinet;
    int32_t crossCandidateCount;
    int32_t reserved;
};

struct TileXRMoonEPPlanDesc {
    int32_t *dst;
    int32_t *cuSeqlens;
    int32_t *expertsToCopy;
    int32_t *remoteStats;
    int32_t *dupGroups;
    int32_t *dupLoffs;
    int32_t *dupCounts;
    int32_t *status;
    int64_t s;
    int64_t k;
    int64_t r;
    int64_t e;
    int64_t b;
    int64_t cap;
    int64_t nvS;
    int64_t tokenPadding;
    uint64_t epoch;
};

enum TileXRMoonEPPlanStatus : int32_t {
    PLAN_OK = 0,
    PLAN_PARTIAL_NO_FEASIBLE_PAIR = 1,
    PLAN_PARTIAL_PREFETCH_SLOT_EXHAUSTED = 2,
    PLAN_PARTIAL_TOKEN_SUPPLY = 3,
    PLAN_ERROR_CONFIG_MISMATCH = 4,
    PLAN_ERROR_TPE_MISMATCH = 5,
    PLAN_ERROR_LAYOUT_EXCEEDS_NVS = 6,
    PLAN_ERROR_MOVE_RECORD_OVERFLOW = 7,
    PLAN_ERROR_INTERNAL_INVARIANT = 8,
};

extern "C" {

int TileXRMoeEpPlanV2GetWorkspaceSize(int64_t rankSize, int64_t s, int64_t topK, int64_t expertNum,
    const TileXRMoonEPPlanConfig *config, uint64_t *localWorkspaceBytes, uint64_t *registeredMetaBytes);

int TileXRMoeEpPlanV2(const int32_t *topkExperts, const int32_t *tokensPerExpert,
    const int32_t *globalRankIds, TileXRCommPtr comm, int64_t s, int64_t topK, int64_t expertNum,
    const TileXRMoonEPPlanConfig *config, TileXRMoonEPPlanDesc *plan, void *localWorkspace,
    uint64_t localWorkspaceBytes, void *registeredMetaWorkspace, uint64_t registeredMetaBytes,
    aclrtStream stream);

} // extern "C"
#endif // __cplusplus
#endif // TILEXR_EP_PLAN_H
