#include "ep_plan_downstream.h"

#include <climits>
#include <cstddef>

namespace TileXREp {
namespace Plan {

TileXRMoonEPPlanStatus DecodeMoonEPDst(
    int32_t encoded, int64_t nvS, int64_t rankSize, MoonEPRouteTarget *target)
{
    if (target == nullptr || nvS <= 0 || nvS > INT32_MAX || rankSize <= 0 || rankSize > 128 ||
        rankSize * nvS > static_cast<int64_t>(INT32_MAX) + 1) {
        return PLAN_ERROR_CONFIG_MISMATCH;
    }
    const int32_t rawDst = encoded >= 0 ? encoded : ~encoded;
    const int64_t dstRank = static_cast<int64_t>(rawDst) / nvS;
    const int64_t recvSlot = static_cast<int64_t>(rawDst) % nvS;
    if (dstRank < 0 || dstRank >= rankSize || recvSlot < 0 || recvSlot >= nvS) {
        return PLAN_ERROR_CONFIG_MISMATCH;
    }
    target->rawDst = rawDst;
    target->dstRank = static_cast<int32_t>(dstRank);
    target->recvSlot = static_cast<int32_t>(recvSlot);
    target->sendHidden = encoded >= 0 ? 1 : 0;
    target->writeRouteWeight = 1;
    return PLAN_OK;
}

namespace {

bool RecordMatches(const MoonEPReceivedRoute &record, int32_t srcRank, int32_t tokenId)
{
    return record.srcRank == srcRank && record.tokenId == tokenId;
}

} // namespace

TileXRMoonEPPlanStatus BuildMoonEPDuplicateMetadata(const MoonEPReceivedRoute *records,
    int64_t recordCount, int64_t rankSize, int64_t s, int64_t topK, int64_t nvS,
    int32_t *dupGroups, int32_t *dupLoffs, int32_t *dupCounts)
{
    if ((records == nullptr && recordCount != 0) || recordCount < 0 || rankSize <= 0 ||
        rankSize > 128 || s <= 0 || topK <= 0 || topK > 32 || nvS <= 0 ||
        nvS > INT32_MAX || dupGroups == nullptr ||
        dupLoffs == nullptr || dupCounts == nullptr || s > INT64_MAX / topK ||
        rankSize > INT64_MAX / (s * topK) || recordCount > rankSize * s * topK) {
        return PLAN_ERROR_CONFIG_MISMATCH;
    }

    for (int64_t i = 0; i < nvS * 3; ++i) dupGroups[i] = -1;
    for (int64_t i = 0; i < nvS; ++i) dupLoffs[i] = -1;
    dupCounts[0] = 0;
    dupCounts[1] = 0;

    for (int64_t i = 0; i < recordCount; ++i) {
        const MoonEPReceivedRoute &record = records[i];
        if (record.srcRank < 0 || record.srcRank >= rankSize || record.tokenId < 0 ||
            record.tokenId >= s || record.topKId < 0 || record.topKId >= topK ||
            record.recvSlot < 0 || record.recvSlot >= nvS ||
            (record.isPrimary != 0 && record.isPrimary != 1)) {
            return PLAN_ERROR_CONFIG_MISMATCH;
        }
        for (int64_t prior = 0; prior < i; ++prior) {
            if (records[prior].srcRank == record.srcRank && records[prior].tokenId == record.tokenId &&
                records[prior].topKId == record.topKId) {
                return PLAN_ERROR_INTERNAL_INVARIANT;
            }
        }
    }

    int32_t groupCount = 0;
    int32_t duplicateCount = 0;
    for (int32_t srcRank = 0; srcRank < rankSize; ++srcRank) {
        for (int32_t tokenId = 0; tokenId < s; ++tokenId) {
            int32_t routeCount = 0;
            int32_t primaryCount = 0;
            int32_t primarySlot = -1;
            for (int64_t i = 0; i < recordCount; ++i) {
                if (!RecordMatches(records[i], srcRank, tokenId)) continue;
                ++routeCount;
                if (records[i].isPrimary != 0) {
                    ++primaryCount;
                    primarySlot = records[i].recvSlot;
                }
            }
            if (routeCount == 0) continue;
            if (primaryCount != 1) return PLAN_ERROR_INTERNAL_INVARIANT;
            if (routeCount == 1) continue;

            const int32_t localDuplicateCount = routeCount - 1;
            if (groupCount >= nvS || duplicateCount > nvS - localDuplicateCount) {
                return PLAN_ERROR_MOVE_RECORD_OVERFLOW;
            }
            const int32_t groupBase = groupCount * 3;
            dupGroups[groupBase] = primarySlot;
            dupGroups[groupBase + 1] = duplicateCount;
            dupGroups[groupBase + 2] = localDuplicateCount;

            for (int32_t topKId = 0; topKId < topK; ++topKId) {
                for (int64_t i = 0; i < recordCount; ++i) {
                    if (RecordMatches(records[i], srcRank, tokenId) && records[i].topKId == topKId &&
                        records[i].isPrimary == 0) {
                        dupLoffs[duplicateCount++] = records[i].recvSlot;
                    }
                }
            }
            ++groupCount;
        }
    }
    dupCounts[0] = groupCount;
    dupCounts[1] = duplicateCount;
    return PLAN_OK;
}

} // namespace Plan
} // namespace TileXREp
