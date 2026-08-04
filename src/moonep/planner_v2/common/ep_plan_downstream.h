#ifndef TILEXR_EP_PLANNER_COMMON_EP_PLAN_DOWNSTREAM_H
#define TILEXR_EP_PLANNER_COMMON_EP_PLAN_DOWNSTREAM_H

#include <cstdint>

#include "tilexr_ep_plan.h"

namespace TileXREp {
namespace Plan {

struct MoonEPRouteTarget {
    int32_t rawDst;
    int32_t dstRank;
    int32_t recvSlot;
    int32_t sendHidden;
    int32_t writeRouteWeight;
};

struct MoonEPReceivedRoute {
    int32_t srcRank;
    int32_t tokenId;
    int32_t topKId;
    int32_t recvSlot;
    int32_t isPrimary;
};

TileXRMoonEPPlanStatus DecodeMoonEPDst(
    int32_t encoded, int64_t nvS, int64_t rankSize, MoonEPRouteTarget *target);

TileXRMoonEPPlanStatus BuildMoonEPDuplicateMetadata(const MoonEPReceivedRoute *records,
    int64_t recordCount, int64_t rankSize, int64_t s, int64_t topK, int64_t nvS,
    int32_t *dupGroups, int32_t *dupLoffs, int32_t *dupCounts);

} // namespace Plan
} // namespace TileXREp

#endif // TILEXR_EP_PLANNER_COMMON_EP_PLAN_DOWNSTREAM_H
