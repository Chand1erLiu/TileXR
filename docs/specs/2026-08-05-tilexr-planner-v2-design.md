# TileXR Planner V2 Design

## Status

Implemented by PR #96. This document records the Planner V2 architecture,
public contract, synchronization protocol, source ownership, and validation
boundary represented by that change.

## Decisions

1. Planner V2 is an A5/Ascend950 direct-launch Planner implemented in TileXR.
   It does not depend on active sources under `reference/`.
2. The public API separates local algorithm scratch from caller-owned metadata
   scratch so their sizes and lifetimes are explicit.
3. Cross-rank Planner metadata uses the communicator's existing
   `CommArgs::peerMems[]` windows. Planner V2 does not register or transfer
   metadata through UDMA.
4. Every rank runs the same deterministic planning algorithm after publishing
   inputs and verifying a common call header.
5. Reusable magic-tagged barriers provide bounded data, status, and ready
   phases. Shared flag memory is not reset between calls.
6. Device-kernel implementation remains under `kernels/`; Runtime V2 launch
   and compiler-generated launch stubs are Host responsibilities under
   `host/`.
7. The MoonEP compatibility API remains available and adapts its single
   workspace contract to the explicit Planner V2 workspaces.

## Evidence and Target

- Repository baseline: C++14, CANN 9.1.0, and driver 25.5.0 or later.
- Planner kernel target: Ascend910A5/Ascend950 vector cores, selected through
  `TILEXR_MOONEP_PLANNER_SOC_TYPE`.
- Planner build is rejected for non-A5 SoCs.
- Planner behavior is checked against an independent CPU reference and focused
  Host, layout, algorithm, ABI, source, and multi-rank tests.
- The standard trusted NPU gate currently runs on 910B3. It validates general
  TileXR hardware behavior but does not prove the A5-only Planner data path.
- Cross-node peer-memory behavior remains unvalidated until it runs on the
  supported multi-node A5/Ascend950 hardware described below.

## Scope

### Public Planner API

- `TileXRMoeEpPlanV2GetWorkspaceSize` validates static dimensions and returns
  local and metadata workspace sizes.
- `TileXRMoeEpPlanV2` validates the communicator, buffers, Plan identity,
  workspace capacity, peer windows, and stream before launching asynchronously.
- `TileXRMoonEpPlannerGetWorkspaceSizeV2` and `TileXRMoonEpPlannerV2` preserve
  the existing MoonEP-facing compatibility contract.

### Planning Algorithm

- Count routed tokens per expert and rank.
- Construct deterministic rank affinity from global rank ids.
- Allocate same-server moves before cross-server moves.
- Enforce prefetch-slot, destination-capacity, and optional per-rank-pair token
  limits.
- Append remaining local token segments and construct `dst`, `cuSeqlens`,
  `expertsToCopy`, `remoteStats`, and status outputs.
- Preserve the affinity order in metadata for reuse only after an epoch commits
  successfully with the same topology.

### Cross-Rank Protocol

- Publish one mailbox row per source rank into every target rank's peer window.
- Exchange the call header, tokens-per-expert row, global rank id, and status.
- Use three bounded collective phases: data, status, and ready.
- Reduce rank-local status deterministically before committing the epoch.

### Compatibility Layer

- Derive the historical MoonEP configuration from communicator rank count and
  `S/K/E`.
- Carve the optimized local workspace, metadata workspace, generated global
  rank ids, duplicate buffers, and status from one caller-owned workspace.
- Copy compatibility outputs asynchronously on the caller's stream.

## Non-Goals

- Running Planner V2 on 910B or other non-A5 products.
- Using HCCL for Planner metadata or barriers.
- Registering Planner metadata with UDMA or adding a UDMA fallback.
- Resetting shared peer flag memory between calls.
- Increasing the active communicator ABI beyond
  `TileXR::TILEXR_MAX_RANK_SIZE`.
- Claiming cross-node correctness from Host, source-only, simulator, or
  single-node tests.
- Owning stream synchronization or device-runtime initialization inside the
  Planner API.

## Component Ownership

```text
src/include/tilexr_ep_plan.h
  Public configuration, Plan descriptor, status, and V2 C ABI

src/moonep/planner_v2/common/
  Shared POD types, workspace/mailbox layout, and Host/device algorithm

src/moonep/planner_v2/reference/
  Independent CPU oracle used by tests

src/moonep/planner_v2/host/
  Validation, API adaptation, Runtime V2 argument packing, and kernel launch

src/moonep/planner_v2/kernels/
  AscendC device kernel, peer publication, barriers, and algorithm execution
```

`host/planner_kernel_launch.cpp` is compiled by Bisheng together with the
device kernel so CANN 9.1 can keep using its compiler-generated registration
stub. Its source ownership is nevertheless Host-side: the device-kernel file
contains no `rtKernelLaunchWithFlagV2` call and no launch wrapper.

## Public Contract

Notation:

```text
S      tokens on each rank
K      experts selected per token
R      ranks in the Planner communicator
E      global experts; E must be divisible by R
B      prefetch slots per rank
Cap    token capacity per rank; required to equal S * K
NvS    encoded destination stride; NvS >= Cap
```

Inputs are caller-owned device buffers:

```text
topkExperts       int32 [S, K]
tokensPerExpert   int32 [E]
globalRankIds     int32 [R]
```

Primary outputs are caller-owned device buffers:

```text
dst               int32 [S, K]
cuSeqlens         int32 [E + B]
expertsToCopy     int32 [R, B]
remoteStats       int32 [2]
status            int32 [8]
```

The Plan descriptor also carries duplicate-group buffers used by the public
and compatibility ABI. All Plan pointers must be non-null and int32-aligned.
The caller owns every input, output, workspace, Plan object, communicator, and
stream until asynchronous work has completed.

The Plan identity must match the invocation:

```text
plan.s             == S
plan.k             == K
plan.r             == R
plan.e             == E
plan.b             == config.prefetchSlots
plan.cap           == config.rankTokenCapacity
plan.nvS           == config.nvS
plan.tokenPadding  == config.tokenPadding
plan.epoch         != 0
```

## Configuration and Limits

The optimized API requires:

- positive `R/S/K/E/B/Cap/NvS/tokenPadding`;
- `K <= 32`;
- `E % R == 0`;
- `Cap == S * K`;
- `NvS >= Cap`;
- `0 <= tokenRouteLimitPerPair <= Cap`;
- `cardsPerServer == 8`;
- `cardsPerCabinet == 64`;
- `crossCandidateCount == 3`;
- `R * NvS <= INT32_MAX` for destination encoding.

Workspace arithmetic is checked in uint64 before conversion to addressable
sizes. Layout-only tests may exercise up to 512 logical ranks, while an active
TileXR communicator remains limited to `TILEXR_MAX_RANK_SIZE` (currently 128).

## Workspace Layout

Both workspaces are aligned to 64 bytes.

Local workspace contains:

```text
expertCount
rankLoad
remainingTpe
alloc
remoteExpertSet
srcExpertCursor
dstExpertCursor
expertPhysicalBase
localExpertOrdinal
tokenSegments
routedPairTokens        optional
scratchStatus
```

Metadata workspace contains:

```text
planCallHeaders
tokensPerExpert matrix
globalRankIds
epochState
affinityOrder
localStatusByRank
barrierFlags
```

The `registeredMetaWorkspace` API name describes the ABI region. Planner V2
itself does not call `TileXRUDMARegister` and does not treat this buffer as a
UDMA transfer target.

## Peer Mailbox Layout

Each source rank owns one mailbox row in the data portion of every peer window:

```text
peerMems[target] + IPC_DATA_OFFSET + sourceRank * rowBytes

row:
  PlanCallHeader       128-byte stride
  tokensPerExpert      E * sizeof(int32_t)
  globalRankId         sizeof(int32_t)
  status               64-byte stride
  end padding          rowBytes aligned to 512 bytes
```

The Host validates that all `peerMems[0..R)` entries are non-null and that one
publication row fits the supported IPC data window. The kernel first writes the
local row, performs cache maintenance, and copies it to every other target with
512-byte MTE2/MTE3 transfers.

No Planner path reads `udmaInfoPtr`, `udmaRegistryPtr`, or a registered remote
memory handle.

## Barrier Protocol

Planner V2 reserves three phase families in the IPC flag region:

```text
data    publish and validate call inputs
status  publish and reduce rank-local algorithm status
ready   keep peer windows and the completed Plan mutually ordered
```

For each phase and source rank, the slot address is derived from event id 4096,
the phase, rank count, and a 512-byte stride. A static assertion keeps the
maximum slot range below `IPC_DATA_OFFSET`.

Each call obtains a fresh magic through `TileXRCommNextMagic`. The published
64-bit value combines magic and phase. Publication uses scalar-to-MTE3 ordering;
polling uses MTE2-to-scalar ordering and explicit cache maintenance. Every wait
is bounded by `waitIterations`.

On timeout, status word 0 is `1000 + timedOutPeer`. Remaining status words may
contain the phase, peer, observed value, and address evidence needed for device
debugging. A timeout prevents epoch commit.

## Call Consistency and Epoch Commit

Every rank publishes a `PlanCallHeader` containing ABI version, dimensions,
configuration, epoch, and topology hash. Rank 0's header is the comparison
baseline. Any mismatch returns `PLAN_ERROR_CONFIG_MISMATCH` through the
device-visible status protocol.

The topology hash is an FNV-style hash of `globalRankIds`. A cached affinity
order is reusable only when:

- requested and committed epochs agree;
- the committed and requested headers match;
- the affinity-valid bit is set; and
- the saved topology hash matches.

The kernel writes `committedEpoch = epoch` only after all three phases complete
and final status is `PLAN_OK`.

## Algorithm Stages

The shared implementation is compiled for both Host tests and the AscendC
kernel through address/function macros. Its deterministic stages are:

1. Validate dimensions, ids, token counts, capacities, and workspace bounds.
2. Build expert counts, rank loads, and rank affinity.
3. Move feasible expert-token segments within each eight-card server.
4. Move remaining feasible segments across server groups using the configured
   candidate count.
5. Append tokens that remain on their home ranks.
6. Build remote expert sets, cumulative sequence lengths, and encoded
   destinations.
7. Fill status and statistics, including partial-plan reasons where constraints
   prevent a complete move set.

The CPU reference is intentionally separate and checks output invariants and
exact results for representative balanced, biased, duplicate, capacity, and
partial-plan cases.

## Host Invocation Path

```text
TileXRMoeEpPlanV2
  -> PreparePlanLaunchContext
     -> TileXRGetCommArgsHost / TileXRGetCommArgsDev
     -> ValidatePlanHostArguments
  -> LaunchPlanKernel
     -> TileXRCommNextMagic
     -> launch_tilexr_ep_plan_kernel
        -> compiler-generated CANN launch stub or Runtime V2 branch
        -> tilexr_ep_plan_kernel
```

The public API returns validation and launch-enqueue errors synchronously.
Algorithm, mismatch, partial-plan, and timeout results are asynchronous and are
observed in `plan.status` after the caller synchronizes its stream or waits on
an appropriate event.

Planner V2 does not call `aclInit`, `aclFinalize`, `aclrtSetDevice`,
`aclrtResetDevice`, or `aclrtSynchronizeStream` behind the caller.

## Build and Installation

- Enable through `TILEXR_BUILD_MOONEP_PLANNER`.
- Bisheng compiles the kernel and Host launch translation units as GNU++17
  because CANN 9.1 AscendC headers require newer language features.
- The normal Host shared library remains C++14.
- `libtilexr-moonep-planner.so` links the generated
  `libtilexr_moonep_planner_kernel.so`, `tile-comm`, `ascendcl`, and `runtime`.
- Both libraries and the public Planner headers are installed through CMake.
- Installed runtime RPATH is `$ORIGIN`; the toolkit `devlib` directory is used
  only at link time and is not placed in RPATH/RUNPATH.

## Failure Model

Synchronous API failures cover:

- null or misaligned pointers;
- invalid dimensions/configuration/Plan identity;
- undersized workspaces;
- unavailable Host or device `CommArgs`;
- missing peer windows;
- failure to obtain a new magic; and
- kernel enqueue failure.

Asynchronous status covers:

- cross-rank call-header mismatch;
- tokens-per-expert mismatch;
- encoded layout overflow;
- move-record overflow;
- internal algorithm invariant failure;
- constrained partial plans; and
- bounded peer timeout.

There is no automatic retry, UDMA fallback, or silent downgrade. All ranks must
use the same dimensions, configuration, epoch, call order, and stream ordering.

## Verification

### Source and Host

- Public ABI compilation and compatibility wrapper tests.
- Checked workspace layout, alignment, and overflow tests.
- Host validation with missing pointers, invalid Plan identity, insufficient
  workspaces, missing peer windows, and invalid runtime metadata.
- Shared algorithm tests and independent CPU-reference comparisons.
- Source guards for build ownership, no UDMA calls, Runtime V2 launch placement,
  barrier ordering, epoch commit, and status reduction.
- Multi-rank harness source checks for 2, 8, and 32 ranks.

### A5 Single Node

- Configure and build with CANN 9.1 for Ascend910A5/Ascend950.
- Verify kernel and Host launch translation units link into the generated
  kernel library.
- Run 1/2/8-rank balanced, biased, duplicate, partial, repeated-epoch, and
  timeout cases against the CPU reference.
- Repeat calls with new magic values and verify affinity-cache reuse only after
  successful commit.

### A5 Cross Node

- Use at least two supported nodes and one TileXR communicator.
- Verify every remote `peerMems` entry and mailbox row before full planning.
- Compare all primary outputs and status against the CPU reference.
- Exercise call-header mismatch, skipped peer, and bounded timeout.
- Confirm there are no UDMA registrations or transfers in the Planner path.

Cross-node Planner behavior remains unvalidated until this matrix completes on
actual supported hardware.

## Acceptance Criteria

1. Public and compatibility APIs validate inputs and enqueue Planner work on the
   caller's stream without hidden synchronization.
2. Host launch code is outside the device-kernel source while retaining the
   CANN 9.1 compiler-generated registration path.
3. Same inputs and topology produce deterministic outputs matching the CPU
   reference.
4. All ranks agree on call headers and final status before epoch commit.
5. Peer publication and barriers use only `peerMems[]`, bounded magic-tagged
   waits, and explicit ordering.
6. Missing peers, mismatched calls, capacity errors, and timeouts are reported
   explicitly rather than hanging or falling back.
7. Build/install preserves C++14 Host compatibility, A5-only kernel targeting,
   `$ORIGIN` runtime lookup, and no active dependency on `reference/`.
8. Validation claims remain scoped to the Host and hardware actually exercised.
