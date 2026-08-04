#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {
int g_failures = 0;
#ifdef TILEXR_SOURCE_ROOT
const char *kSourceRoot = TILEXR_SOURCE_ROOT;
#else
const char *kSourceRoot = ".";
#endif

std::string JoinPath(const std::string &base, const std::string &path)
{
    return base.empty() || base[base.size() - 1] == '/' ? base + path : base + "/" + path;
}

bool ReadFile(const std::string &path, std::string *contents)
{
    std::ifstream stream(JoinPath(kSourceRoot, path).c_str(), std::ios::binary);
    if (!stream.is_open()) {
        std::cerr << "missing file: " << path << std::endl;
        ++g_failures;
        return false;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    *contents = buffer.str();
    return true;
}

void CheckContains(const std::string &path, const std::string &contents, const std::string &needle)
{
    if (contents.find(needle) == std::string::npos) {
        std::cerr << path << " missing: " << needle << std::endl;
        ++g_failures;
    }
}

void CheckNotContains(const std::string &path, const std::string &contents, const std::string &needle)
{
    if (contents.find(needle) != std::string::npos) {
        std::cerr << path << " unexpectedly contains: " << needle << std::endl;
        ++g_failures;
    }
}

void TestPublicPlanApi()
{
    std::string header;
    if (!ReadFile("src/include/tilexr_ep_plan.h", &header)) {
        return;
    }
    CheckContains("src/include/tilexr_ep_plan.h", header, "struct TileXRMoonEPPlanConfig");
    CheckContains("src/include/tilexr_ep_plan.h", header, "struct TileXRMoonEPPlanDesc");
    CheckContains("src/include/tilexr_ep_plan.h", header, "int TileXRMoeEpPlanV2GetWorkspaceSize(");
    CheckContains("src/include/tilexr_ep_plan.h", header, "int TileXRMoeEpPlanV2(");

    std::string compatibilityHeader;
    if (ReadFile("src/include/tilexr_moonep_planner.h", &compatibilityHeader)) {
        CheckContains("src/include/tilexr_moonep_planner.h", compatibilityHeader,
            "int TileXRMoonEpPlannerGetWorkspaceSizeV2(");
        CheckContains("src/include/tilexr_moonep_planner.h", compatibilityHeader,
            "int TileXRMoonEpPlannerV2(");
    }
    CheckContains("src/include/tilexr_ep_plan.h", header, "uint64_t epoch");
    CheckContains("src/include/tilexr_ep_plan.h", header, "int32_t *dupGroups");
    CheckContains("src/include/tilexr_ep_plan.h", header, "int32_t *status");
    CheckNotContains("src/include/tilexr_ep_plan.h", header, "zeroFillRanges");

}

void TestSharedPlanAbi()
{
    std::string types;
    if (!ReadFile("src/moonep/planner_v2/common/ep_plan_types.h", &types)) {
        return;
    }
    CheckContains("src/moonep/planner_v2/common/ep_plan_types.h", types, "struct PlanCallHeader");
    CheckContains("src/moonep/planner_v2/common/ep_plan_types.h", types, "struct TokenSegmentMove");
    CheckContains("src/moonep/planner_v2/common/ep_plan_types.h", types, "kPlanCardsPerServer = 8");
    CheckContains("src/moonep/planner_v2/common/ep_plan_types.h", types, "kPlanCardsPerCabinet = 64");
    CheckContains("src/moonep/planner_v2/common/ep_plan_types.h", types, "kPlanCrossCandidateCount = 3");
    CheckContains("src/moonep/planner_v2/common/ep_plan_types.h", types, "kPlanAffinityCacheValid");
}

void TestSharedPlanAlgorithm()
{
    std::string algorithmHeader;
    ReadFile("src/moonep/planner_v2/common/ep_plan_algorithm.h", &algorithmHeader);
    CheckContains("src/moonep/planner_v2/common/ep_plan_algorithm.h", algorithmHeader, "TILEXR_PLAN_ADDR");
    CheckContains("src/moonep/planner_v2/common/ep_plan_algorithm.h", algorithmHeader, "TILEXR_PLAN_FN");

    std::string algorithmImpl;
    ReadFile("src/moonep/planner_v2/common/ep_plan_algorithm_impl.h", &algorithmImpl);
    CheckContains("src/moonep/planner_v2/common/ep_plan_algorithm_impl.h", algorithmImpl,
        "RunPlanAlgorithm");
    CheckContains("src/moonep/planner_v2/common/ep_plan_algorithm_impl.h", algorithmImpl,
        "const int64_t groups");
    CheckContains("src/moonep/planner_v2/common/ep_plan_algorithm_impl.h", algorithmImpl,
        "const int64_t remoteSlotCount");
    CheckContains("src/moonep/planner_v2/common/ep_plan_algorithm_impl.h", algorithmImpl,
        "s.localSegments >= s.ws.tokenSegmentCapacity");
    CheckContains("src/moonep/planner_v2/common/ep_plan_algorithm_impl.h", algorithmImpl,
        "PLAN_ERROR_MOVE_RECORD_OVERFLOW");
    CheckNotContains("src/moonep/planner_v2/common/ep_plan_algorithm_impl.h", algorithmImpl,
        "BetterLoad(");
    CheckContains("src/moonep/planner_v2/common/ep_plan_algorithm_impl.h", algorithmImpl,
        "const TILEXR_PLAN_ADDR int32_t *rankLoad");
    CheckContains("src/moonep/planner_v2/common/ep_plan_algorithm_impl.h", algorithmImpl,
        "const TILEXR_PLAN_ADDR int32_t *globalRankIds");
    CheckContains("src/moonep/planner_v2/common/ep_plan_algorithm_impl.h", algorithmImpl,
        "value < 0 || value >= rankSize");
    CheckContains("src/moonep/planner_v2/common/ep_plan_algorithm_impl.h", algorithmImpl,
        "rhs < 0 || rhs >= rankSize");

    std::string algorithmSource;
    ReadFile("src/moonep/planner_v2/common/ep_plan_algorithm.cpp", &algorithmSource);
    CheckContains("src/moonep/planner_v2/common/ep_plan_algorithm.cpp", algorithmSource,
        "#include \"ep_plan_algorithm_impl.h\"");

    std::string referenceSource;
    ReadFile("src/moonep/planner_v2/reference/ep_plan_reference.cpp", &referenceSource);
    CheckContains("src/moonep/planner_v2/reference/ep_plan_reference.cpp", referenceSource,
        "const int64_t groupCount");
    CheckNotContains("src/moonep/planner_v2/reference/ep_plan_reference.cpp", referenceSource,
        "zeroFillRanges");

    std::string kernel;
    ReadFile("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", &kernel);
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "#include \"ep_plan_algorithm_impl.h\"");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "CollectiveBarrier");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "peerMems");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "IPC_DATA_OFFSET");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "PublishInputs");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "GatherInputs");
    CheckNotContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "UDMAPut");
    CheckNotContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "UDMAQuiet");
    CheckNotContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "TileXRUDMA");
    CheckNotContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "zeroFillRanges");
}

void TestBuildWiring()
{
    std::string moonepCmake;
    if (ReadFile("src/moonep/CMakeLists.txt", &moonepCmake)) {
        CheckContains("src/moonep/CMakeLists.txt", moonepCmake,
            "add_subdirectory(planner_v2)");
    }

    std::string plannerManifest;
    if (ReadFile("src/moonep/planner_v2/CMakeLists.txt", &plannerManifest)) {
        CheckContains("src/moonep/planner_v2/CMakeLists.txt", plannerManifest,
            "kernels/tilexr_moonep_planner_kernel.cpp");
        CheckContains("src/moonep/planner_v2/CMakeLists.txt", plannerManifest,
            "host/tilexr_moonep_planner.cpp");
        CheckContains("src/moonep/planner_v2/CMakeLists.txt", plannerManifest,
            "INSTALL_RPATH \"$ORIGIN\"");
        CheckContains("src/moonep/planner_v2/CMakeLists.txt", plannerManifest,
            "src/include/tilexr_ep_plan.h");

        const std::string planOutput =
            "OUTPUT \"${TILEXR_MOONEP_PLANNER_KERNEL_SO}\"";
        const std::string planTarget =
            "add_custom_target(tilexr_moonep_planner_kernel";
        const std::string::size_type begin = plannerManifest.find(planOutput);
        const std::string::size_type end = plannerManifest.find(planTarget, begin);
        if (begin == std::string::npos || end == std::string::npos) {
            std::cerr << "src/moonep/planner_v2/CMakeLists.txt missing isolated Plan kernel build block"
                      << std::endl;
            ++g_failures;
        } else {
            const std::string planBuild = plannerManifest.substr(begin, end - begin);
            // CANN 9.1 tikcpp headers use C++17 language features; only the vendor
            // kernel translation unit is compiled as GNU++17. TileXR Host remains C++14.
            CheckContains("src/moonep/planner_v2/CMakeLists.txt Plan kernel block",
                planBuild, "-std=gnu++17");
        }
    }

    std::string rootCmake;
    if (ReadFile("CMakeLists.txt", &rootCmake)) {
        CheckContains("CMakeLists.txt", rootCmake, "set(CMAKE_CXX_STANDARD 14)");
        CheckContains("CMakeLists.txt", rootCmake, "TILEXR_BUILD_MOONEP_PLANNER");
    }
}

void TestMultiRankValidationHarness()
{
    std::string testSource;
    ReadFile("tests/ep/integration/test_tilexr_ep_plan_multirank.cpp", &testSource);
    CheckContains("tests/ep/integration/test_tilexr_ep_plan_multirank.cpp", testSource,
        "TileXRCommInitRankLocal");
    CheckContains("tests/ep/integration/test_tilexr_ep_plan_multirank.cpp", testSource,
        "TileXRGetCommArgsHost");
    CheckContains("tests/ep/integration/test_tilexr_ep_plan_multirank.cpp", testSource,
        "peerMems");
    CheckContains("tests/ep/integration/test_tilexr_ep_plan_multirank.cpp", testSource,
        "TileXRMoeEpPlanV2(");
    CheckContains("tests/ep/integration/test_tilexr_ep_plan_multirank.cpp", testSource,
        "BuildReferencePlan");
    CheckContains("tests/ep/integration/test_tilexr_ep_plan_multirank.cpp", testSource,
        "requestedEpoch");
    CheckContains("tests/ep/integration/test_tilexr_ep_plan_multirank.cpp", testSource,
        "committedEpoch");
    CheckContains("tests/ep/integration/test_tilexr_ep_plan_multirank.cpp", testSource,
        "ALL_RANKS_PASS");
    CheckNotContains("tests/ep/integration/test_tilexr_ep_plan_multirank.cpp", testSource,
        "TileXRUDMARegister");
    CheckNotContains("tests/ep/integration/test_tilexr_ep_plan_multirank.cpp", testSource,
        "TileXRGetUDMARegistryHost");
    CheckNotContains("tests/ep/integration/test_tilexr_ep_plan_multirank.cpp", testSource,
        "DumpUDMAQueueDiagnostics");
    CheckNotContains("tests/ep/integration/test_tilexr_ep_plan_multirank.cpp", testSource,
        "UDMA_QUEUE_DIAG");
    CheckNotContains("tests/ep/integration/test_tilexr_ep_plan_multirank.cpp", testSource,
        "zeroFillRanges");
    CheckContains("tests/ep/integration/test_tilexr_ep_plan_multirank.cpp", testSource,
        "rankSize != 2 && rankSize != 8 && rankSize != 32");
    CheckContains("tests/ep/integration/test_tilexr_ep_plan_multirank.cpp", testSource,
        "input.globalRankIds = {0, 8};");
    CheckContains("tests/ep/integration/test_tilexr_ep_plan_multirank.cpp", testSource,
        "<2|8|32> <rank> <device 0..7>");

    std::string cmake;
    ReadFile("tests/ep/CMakeLists.txt", &cmake);
    CheckContains("tests/ep/CMakeLists.txt", cmake,
        "BUILD_TILEXR_EP_PLAN_MULTIRANK_TEST");
    CheckContains("tests/ep/CMakeLists.txt", cmake,
        "test_tilexr_ep_plan_multirank");

    std::string orchestrator;
    ReadFile("tests/ep/integration/run_tilexr_ep_plan_multinode.ps1", &orchestrator);
    CheckContains("tests/ep/integration/run_tilexr_ep_plan_multinode.ps1", orchestrator,
        "mutagen sync flush");
    CheckContains("tests/ep/integration/run_tilexr_ep_plan_multinode.ps1", orchestrator,
        "8 Rank validation failed; 32 Rank phase is blocked");
    CheckContains("tests/ep/integration/run_tilexr_ep_plan_multinode.ps1", orchestrator,
        R"($RunId = "$(Get-Date -Format 'yyyyMMdd-HHmmss-fff')-$PID")");
    CheckContains("tests/ep/integration/run_tilexr_ep_plan_multinode.ps1", orchestrator,
        R"($phaseRun = "$Phase-$RunId")");
    CheckContains("tests/ep/integration/run_tilexr_ep_plan_multinode.ps1", orchestrator,
        "function Wait-RemoteLog");
    CheckContains("tests/ep/integration/run_tilexr_ep_plan_multinode.ps1", orchestrator,
        "Waiting for rank 0 listener");
    CheckContains("tests/ep/integration/run_tilexr_ep_plan_multinode.ps1", orchestrator,
        "skip unverified planner pid");
    CheckContains("tests/ep/integration/run_tilexr_ep_plan_multinode.ps1", orchestrator,
        R"(tr '\0' ' ' <"/proc/`$pid/cmdline")");
    CheckNotContains("tests/ep/integration/run_tilexr_ep_plan_multinode.ps1", orchestrator,
        "find '$RemoteEvidence' -maxdepth 1 -type f -name '$Phase-rank-*' -delete");
    CheckNotContains("tests/ep/integration/run_tilexr_ep_plan_multinode.ps1", orchestrator, "scp ");
    CheckNotContains("tests/ep/integration/run_tilexr_ep_plan_multinode.ps1", orchestrator, "rsync ");

    std::string rankRunner;
    ReadFile("tests/ep/integration/run_tilexr_ep_plan_rank.sh", &rankRunner);
    CheckNotContains("tests/ep/integration/run_tilexr_ep_plan_rank.sh", rankRunner, "\r");
    CheckNotContains("tests/ep/integration/run_tilexr_ep_plan_rank.sh", rankRunner, "TILEXR_UDMA");
}

void TestPlanLaunchAndCommitProtocol()
{
    std::string launchHeader;
    ReadFile("src/moonep/planner_v2/host/planner_launch.h", &launchHeader);
    CheckContains("src/moonep/planner_v2/host/planner_launch.h", launchHeader, "LaunchPlanKernel");

    std::string launchSource;
    ReadFile("src/moonep/planner_v2/host/planner_launch.cpp", &launchSource);
    CheckContains("src/moonep/planner_v2/host/planner_launch.cpp", launchSource,
        "launch_tilexr_ep_plan_kernel");
    CheckContains("src/moonep/planner_v2/host/planner_launch.cpp", launchSource, "TileXRCommNextMagic");
    CheckContains("src/moonep/planner_v2/host/planner_launch.cpp", launchSource, "waitIterations");

    std::string apiSource;
    ReadFile("src/moonep/planner_v2/host/tilexr_moonep_planner.cpp", &apiSource);
    CheckContains("src/moonep/planner_v2/host/tilexr_moonep_planner.cpp", apiSource, "LaunchPlanKernel");
    CheckContains("src/moonep/planner_v2/host/tilexr_moonep_planner.cpp", apiSource, "waitIterations");

    std::string kernel;
    ReadFile("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", &kernel);
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel, "PublishInputs");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel, "GatherInputs");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel, "PublishStatus");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel, "GatherStatuses");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "PushPeerMailboxRowMte");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "PeerMailboxRow(peerMems[rank]");
    CheckNotContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "GM_ADDR base = peerMems[peer] + TileXR::IPC_DATA_OFFSET;");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel, "CollectiveBarrier");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel, "WaitPhase");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel, "BarrierEventId");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "PublishBarrierFlags");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "peerMems[rank]");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "kPlannerBarrierStrideBytes = 512");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "kPlannerBarrierWords = kPlannerBarrierStrideBytes / sizeof(uint64_t)");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "(phase - 1) * rankSize + sourceRank");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "AscendC::HardEvent::S_MTE3");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "AscendC::HardEvent::MTE3_S");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "AscendC::HardEvent::MTE2_S");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "AscendC::DataCopy(remoteBarrierGlobal, barrierLocal, kPlannerBarrierWords)");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "AscendC::DataCopy(barrierLocal, barrierGlobal, kPlannerBarrierWords)");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "RefreshCacheLines(reinterpret_cast<GM_ADDR>(barrier), kPlannerBarrierStrideBytes)");
    CheckNotContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "barrierSlot[0] = barrierValue;");
    CheckNotContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "sync.SetSyncFlag(magic, phase, eventId, targetRank);");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "TileXR::IPC_DATA_OFFSET");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "ReduceGlobalPlanStatus");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "BuildLocalOffsets");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "BuildTopologyHash");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "RunPlanAlgorithm");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "status[0] == PLAN_OK");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel, "rtArgsEx_t");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "rtTaskCfgInfo_t");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "rtKernelLaunchWithFlagV2");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "tilexr_ep_plan_kernel<<<blockDim, nullptr, stream>>>");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "waitIterations");

    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "epochState->requestedEpoch = epoch");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "epochState->committedEpoch = epoch");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "workspace.affinityOrderValid = affinityOrderValid;");
    CheckContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "epochState->reserved = TileXREp::Plan::kPlanAffinityCacheValid;");
    CheckNotContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "epochState->reserved = 0;");

    CheckNotContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel, "UDMAPut");
    CheckNotContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel, "UDMAQuiet");
    CheckNotContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel, "TileXRUDMA");
    CheckNotContains("src/moonep/planner_v2/kernels/tilexr_moonep_planner_kernel.cpp", kernel,
        "zeroFillRanges");
}

} // namespace

int main()
{
    TestPublicPlanApi();
    TestSharedPlanAbi();
    TestSharedPlanAlgorithm();
    TestBuildWiring();
    TestMultiRankValidationHarness();
    TestPlanLaunchAndCommitProtocol();
    if (g_failures != 0) {
        std::cerr << g_failures << " plan source checks failed" << std::endl;
        return 1;
    }
    std::cout << "Plan API/source checks passed" << std::endl;
    return 0;
}
