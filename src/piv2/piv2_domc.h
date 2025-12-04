// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef HU_HU_DOMC_H
#define HU_HU_DOMC_H

#include "amount.h"
#include "primitives/transaction.h"
#include "serialize.h"
#include "uint256.h"

#include <vector>

// Forward declarations
struct HuGlobalState;
namespace Consensus { struct Params; }

/**
 * DOMC (Decentralized Open Monetary Committee)
 *
 * Phase 6.2: Masternode governance for R% (annual yield rate)
 *
 * ARCHITECTURE:
 * - Commit-reveal voting every 172800 blocks (4 months)
 * - Votes stored in CKHUDomcDB (NOT in HuGlobalState)
 * - Result: median(R) clamped to R_MAX_dynamic
 * - No minimum quorum (v1): ≥1 vote → apply median, 0 votes → R unchanged
 *
 * CYCLE PHASES (R% ACTIVE FOR FULL 4 MONTHS):
 * 1. R% active only: 0 → 132480 blocks (~3 months)
 * 2. VOTE phase: 132480 → 152640 blocks (commits + reveals, ~2 weeks)
 * 3. REVEAL INSTANT: Block 152640 → calculate median, R_next visible
 * 4. ADAPTATION phase: 152640 → 172800 blocks (everyone adapts, ~2 weeks)
 * 5. ACTIVATION: Block 172800 → R_next becomes R_annual, new cycle starts
 *
 * TIMELINE:
 * 0────────────132480────────152640────────172800
 * │              │              │              │
 * │              │    VOTE      │  ADAPTATION  │
 * │              │  (2 weeks)   │  (2 weeks)   │
 * │              │  commits +   │              │
 * │              │  reveals     │  REVEAL      │
 * │              │              │  instant     │
 * │              │              │  ↓           │
 * │              │              │  R_next      │
 * │              │              │  visible     │
 * │                                            │
 * ├────────────────────────────────────────────┤
 * │     R% ACTIVE FOR FULL CYCLE (4 months)    │
 * └────────────────────────────────────────────┴──► New R% activated
 */

namespace khu_domc {

// =============================================================================
// DOMC cycle parameters (network-dependent)
// =============================================================================
//
// MAINNET/TESTNET (production):
//   - DOMC_CYCLE_LENGTH = 172800 blocks (~4 months)
//   - DOMC_VOTE_OFFSET = 132480 blocks (~3 months into cycle)
//   - DOMC_REVEAL_HEIGHT = 152640 blocks (~3.5 months into cycle)
//
// REGTEST (fast testing):
//   - DOMC_CYCLE_LENGTH = 1008 blocks (~1 week at 10 blocks/min for regtest)
//   - DOMC_VOTE_OFFSET = 720 blocks (~5 days into cycle)
//   - DOMC_REVEAL_HEIGHT = 864 blocks (~6 days into cycle)
//
// Use GetDomcCycleLength(), GetDomcVoteOffset(), GetDomcRevealHeight() functions
// which return the appropriate values based on the active network.
// =============================================================================

// Production values (mainnet/testnet)
static const uint32_t DOMC_CYCLE_LENGTH = 172800;      // 4 months (172800 blocks)
static const uint32_t DOMC_VOTE_OFFSET = 132480;       // Start VOTE phase (commits+reveals) at 132480
static const uint32_t DOMC_REVEAL_HEIGHT = 152640;     // REVEAL instant at 152640 (R_next calculated)
static const uint32_t DOMC_VOTE_DURATION = 20160;      // VOTE window: 20160 blocks (~2 weeks)
static const uint32_t DOMC_ADAPTATION_DURATION = 20160; // ADAPTATION window: 20160 blocks (~2 weeks)

// RegTest values (fast testing - ~1 week cycle)
static const uint32_t DOMC_CYCLE_LENGTH_REGTEST = 1008;     // ~1 week (1008 blocks)
static const uint32_t DOMC_VOTE_OFFSET_REGTEST = 720;       // Start VOTE at ~5 days
static const uint32_t DOMC_REVEAL_HEIGHT_REGTEST = 864;     // REVEAL at ~6 days
static const uint32_t DOMC_VOTE_DURATION_REGTEST = 144;     // VOTE window: 144 blocks (~1 day)
static const uint32_t DOMC_ADAPTATION_DURATION_REGTEST = 144; // ADAPTATION: 144 blocks (~1 day)

// Network-aware getter functions (defined in khu_domc.cpp)
uint32_t GetDomcCycleLength();
uint32_t GetDomcVoteOffset();
uint32_t GetDomcRevealHeight();
uint32_t GetDomcVoteDuration();
uint32_t GetDomcAdaptationDuration();

// Convenience aliases
static const uint32_t DOMC_COMMIT_OFFSET = DOMC_VOTE_OFFSET;
static const uint32_t DOMC_REVEAL_OFFSET = DOMC_REVEAL_HEIGHT;

// R% limits (basis points: 4000 = 40.00%)
static const uint16_t R_MIN = 700;     // Minimum R%: 7.00% (floor after 33 years)
static const uint16_t R_MAX = 4000;    // Absolute maximum R%: 40.00% (genesis cap)
static const uint16_t R_DEFAULT = 4000; // Default R% at genesis (block 0): 40.00%

// R_MAX_dynamic formula: max(700, 4000 - year × 100)
// Year 0:  4000 bp (40%)
// Year 33: 700 bp (7%) - minimum guaranteed
static const uint16_t R_MAX_DYNAMIC_INITIAL = 4000;  // 40% at genesis (block 0)
static const uint16_t R_MAX_DYNAMIC_MIN = 700;       // 7% minimum (floor)
static const uint16_t R_MAX_DYNAMIC_DECAY = 100;     // -1% per year

// DAO Treasury (T) - Phase 6.3
// T accumulates based on U and R%: T_daily = U × R% / T_DIVISOR / 365
// At R=40%: T = 40%/8 = 5% annual, at R=7%: T = 7%/8 = 0.875% annual
static const uint16_t T_DIVISOR = 8;           // Treasury = 1/8 of yield rate (~5% at R=40%)
static const int64_t T_GENESIS_INITIAL = 500000LL * 100000000LL; // 500,000 PIVHU initial Treasury

/**
 * DomcCommit - Masternode commit for R% vote
 *
 * Phase 1 (commit): Masternode publishes hash(R_proposal || salt)
 * Prevents front-running and collusion.
 */
struct DomcCommit
{
    uint256 hashCommit;          // Hash of (R_proposal || salt)
    COutPoint mnOutpoint;        // Masternode collateral outpoint (identity)
    uint32_t nCycleId;           // Cycle ID (cycle_start height)
    uint32_t nCommitHeight;      // Block height of commit
    std::vector<unsigned char> vchSig; // Masternode signature

    DomcCommit()
    {
        SetNull();
    }

    void SetNull()
    {
        hashCommit.SetNull();
        mnOutpoint.SetNull();
        nCycleId = 0;
        nCommitHeight = 0;
        vchSig.clear();
    }

    bool IsNull() const
    {
        return hashCommit.IsNull();
    }

    /**
     * GetHash - Unique identifier for this commit
     * Used as key in CKHUDomcDB
     */
    uint256 GetHash() const;

    SERIALIZE_METHODS(DomcCommit, obj)
    {
        READWRITE(obj.hashCommit);
        READWRITE(obj.mnOutpoint);
        READWRITE(obj.nCycleId);
        READWRITE(obj.nCommitHeight);
        READWRITE(obj.vchSig);
    }
};

/**
 * DomcReveal - Masternode reveal for R% vote
 *
 * Phase 2 (reveal): Masternode reveals R_proposal and salt
 * Must match previously committed hash(R_proposal || salt)
 */
struct DomcReveal
{
    uint16_t nRProposal;         // Proposed R% (basis points: 1500 = 15.00%)
    uint256 salt;                // Random salt (for commit hash)
    COutPoint mnOutpoint;        // Masternode collateral outpoint (must match commit)
    uint32_t nCycleId;           // Cycle ID (must match commit)
    uint32_t nRevealHeight;      // Block height of reveal
    std::vector<unsigned char> vchSig; // Masternode signature

    DomcReveal()
    {
        SetNull();
    }

    void SetNull()
    {
        nRProposal = 0;
        salt.SetNull();
        mnOutpoint.SetNull();
        nCycleId = 0;
        nRevealHeight = 0;
        vchSig.clear();
    }

    bool IsNull() const
    {
        return salt.IsNull();
    }

    /**
     * GetHash - Unique identifier for this reveal
     * Used as key in CKHUDomcDB
     */
    uint256 GetHash() const;

    /**
     * GetCommitHash - Calculate hash(R_proposal || salt)
     * Must match DomcCommit.hashCommit for validation
     */
    uint256 GetCommitHash() const;

    SERIALIZE_METHODS(DomcReveal, obj)
    {
        READWRITE(obj.nRProposal);
        READWRITE(obj.salt);
        READWRITE(obj.mnOutpoint);
        READWRITE(obj.nCycleId);
        READWRITE(obj.nRevealHeight);
        READWRITE(obj.vchSig);
    }
};

/**
 * GetCurrentCycleId - Calculate cycle ID for given height
 *
 * Cycle ID = domc_cycle_start (height of cycle start)
 *
 * @param nHeight Current block height
 * @param nActivationHeight KHU V6.0 activation height
 * @return Cycle start height (cycle ID)
 */
uint32_t GetCurrentCycleId(uint32_t nHeight, uint32_t nActivationHeight);

/**
 * IsDomcCycleBoundary - Check if current height is DOMC cycle boundary
 *
 * Cycle boundary = height where cycle ends and new cycle starts
 * At boundary: finalize previous cycle, calculate median(R), start new cycle
 *
 * @param nHeight Current block height
 * @param nActivationHeight KHU V6.0 activation height
 * @return true if (nHeight - activation) % 172800 == 0
 */
bool IsDomcCycleBoundary(uint32_t nHeight, uint32_t nActivationHeight);

/**
 * IsDomcActivationBlock - Check if current height is UNIFIED ACTIVATION block
 *
 * Activation block = LAST block of DOMC cycle (cycleLength - 1)
 * At this block: R_next → R_annual AND DAO payouts execute
 *
 * Regtest (DOMC=90, DAO=30): Block 89, 179, 269, ...
 * Mainnet (DOMC=129600, DAO=43200): Block 129599, 259199, ...
 *
 * @param nHeight Current block height
 * @param nActivationHeight KHU V6.0 activation height
 * @return true if (nHeight - activation + 1) % cycleLength == 0
 */
bool IsDomcActivationBlock(uint32_t nHeight, uint32_t nActivationHeight);

/**
 * IsDomcVotePhase - Check if current height is in VOTE phase
 *
 * VOTE phase: [cycle_start + 132480, cycle_start + 152640)
 * During this phase: MN submit commits AND reveals
 * Duration: 20160 blocks (~2 weeks)
 *
 * @param nHeight Current block height
 * @param cycleStart Cycle start height
 * @return true if in VOTE phase
 */
bool IsDomcVotePhase(uint32_t nHeight, uint32_t cycleStart);

/**
 * IsDomcAdaptationPhase - Check if current height is in ADAPTATION phase
 *
 * ADAPTATION phase: [cycle_start + 152640, cycle_start + 172800)
 * During this phase: R_next is visible, everyone adapts
 * Duration: 20160 blocks (~2 weeks)
 *
 * @param nHeight Current block height
 * @param cycleStart Cycle start height
 * @return true if in ADAPTATION phase
 */
bool IsDomcAdaptationPhase(uint32_t nHeight, uint32_t cycleStart);

/**
 * IsRevealHeight - Check if current height is the REVEAL instant
 *
 * At REVEAL height (152640): calculate median R_next from votes
 *
 * @param nHeight Current block height
 * @param cycleStart Cycle start height
 * @return true if this is the reveal height
 */
bool IsRevealHeight(uint32_t nHeight, uint32_t cycleStart);

// Convenience aliases
inline bool IsDomcCommitPhase(uint32_t nHeight, uint32_t cycleStart) {
    return IsDomcVotePhase(nHeight, cycleStart);
}
inline bool IsDomcRevealPhase(uint32_t nHeight, uint32_t cycleStart) {
    return IsDomcAdaptationPhase(nHeight, cycleStart);
}

/**
 * CalculateDomcMedian - Calculate median R% from valid reveals
 *
 * V1 RULE (no minimum quorum):
 * - If 0 valid reveals → return current R_annual (no change)
 * - If ≥1 valid reveals → return clamped median(R)
 *
 * Clamping: median ≤ R_MAX_dynamic (governance safety limit)
 *
 * @param cycleId Cycle ID to finalize
 * @param currentR Current R_annual (fallback if 0 votes)
 * @param R_MAX_dynamic Maximum allowed R% (clamp limit)
 * @return New R_annual (basis points)
 */
uint16_t CalculateDomcMedian(
    uint32_t cycleId,
    uint16_t currentR,
    uint16_t R_MAX_dynamic
);

/**
 * InitializeDomcCycle - Initialize new DOMC cycle in state
 *
 * Called at cycle boundary in ConnectBlock.
 * Updates domc_cycle_start, domc_commit_phase_start, domc_reveal_deadline.
 *
 * CRITICAL: At V6 activation (first cycle), this also initializes:
 *   - R_annual = R_DEFAULT (4000 bp = 40%)
 *   - R_MAX_dynamic = R_MAX_DYNAMIC_INITIAL (4000 bp = 40%)
 *
 * @param state [IN/OUT] Global state to update
 * @param nHeight Current block height (cycle start)
 * @param isFirstCycle true if this is V6 activation (first cycle ever)
 */
void InitializeDomcCycle(
    HuGlobalState& state,
    uint32_t nHeight,
    bool isFirstCycle = false
);

/**
 * ProcessRevealInstant - Process REVEAL instant at block 152640
 *
 * Called at REVEAL height in ConnectBlock.
 * 1. Collect valid reveals from current cycle
 * 2. Calculate median(R) with clamping
 * 3. Set state.R_next (visible during ADAPTATION phase)
 *
 * @param state [IN/OUT] Global state to update (R_next)
 * @param nHeight Current block height (REVEAL height)
 * @param consensusParams Consensus parameters (for activation height)
 * @return true on success, false on error
 */
bool ProcessRevealInstant(
    HuGlobalState& state,
    uint32_t nHeight,
    const Consensus::Params& consensusParams
);

/**
 * FinalizeDomcCycle - Finalize cycle and activate R_next as R_annual
 *
 * Called at cycle boundary in ConnectBlock (BEFORE InitializeDomcCycle).
 * 1. R_annual = R_next (activate the voted rate)
 * 2. R_next = 0 (reset for next cycle)
 *
 * @param state [IN/OUT] Global state to update (R_annual)
 * @param nHeight Current block height (cycle boundary)
 * @param consensusParams Consensus parameters (for activation height)
 * @return true on success, false on error
 */
bool FinalizeDomcCycle(
    HuGlobalState& state,
    uint32_t nHeight,
    const Consensus::Params& consensusParams
);

/**
 * UndoFinalizeDomcCycle - Undo DOMC cycle finalization during reorg
 *
 * Called in DisconnectBlock when a cycle boundary block is disconnected.
 * Restores R_annual to its value before FinalizeDomcCycle was called.
 *
 * CRITICAL for reorg safety: Without this, R_annual changes are irreversible
 * and cause state divergence during chain reorganizations.
 *
 * @param state [IN/OUT] Global state to restore (R_annual)
 * @param nHeight Current block height (cycle boundary being disconnected)
 * @param consensusParams Consensus parameters (for activation height)
 * @return true on success, false on error
 */
bool UndoFinalizeDomcCycle(
    HuGlobalState& state,
    uint32_t nHeight,
    const Consensus::Params& consensusParams
);

/**
 * CalculateRMaxDynamic - Calculate R_MAX_dynamic based on year since activation
 *
 * FORMULA CONSENSUS:
 *   R_MAX_dynamic = max(700, 4000 - year × 100)
 *
 * Timeline:
 *   Year 0:  4000 bp (40%)
 *   Year 1:  3900 bp (39%)
 *   ...
 *   Year 33: 700 bp (7%) - minimum
 *   Year 34+: 700 bp (7%) - floor
 *
 * @param nHeight Current block height
 * @param nActivationHeight V6.0 activation height
 * @return R_MAX_dynamic in basis points
 */
uint16_t CalculateRMaxDynamic(uint32_t nHeight, uint32_t nActivationHeight);

/**
 * UpdateRMaxDynamic - Update R_MAX_dynamic in state if at year boundary
 *
 * Called during ConnectBlock to update R_MAX_dynamic when entering a new year.
 * R_MAX_dynamic decreases by 100 basis points (1%) each year.
 *
 * @param state [IN/OUT] Global state to update
 * @param nHeight Current block height
 * @param nActivationHeight V6.0 activation height
 */
void UpdateRMaxDynamic(
    HuGlobalState& state,
    uint32_t nHeight,
    uint32_t nActivationHeight
);

} // namespace khu_domc

#endif // HU_HU_DOMC_H
