// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "piv2/piv2_domc.h"
#include "piv2/piv2_domcdb.h"
#include "piv2/piv2_state.h"
#include "piv2/piv2_statedb.h"
#include "piv2/piv2_validation.h"
#include "chainparams.h"
#include "consensus/params.h"
#include "hash.h"
#include "logging.h"

#include <algorithm>

namespace khu_domc {

// ============================================================================
// Network-aware parameter getters (uses Consensus::Params for per-network values)
// ============================================================================

uint32_t GetDomcCycleLength()
{
    return Params().GetConsensus().nDOMCCycleBlocks;
}

uint32_t GetDomcVoteOffset()
{
    return Params().GetConsensus().nDOMCCommitOffset;
}

uint32_t GetDomcRevealHeight()
{
    return Params().GetConsensus().nDOMCRevealOffset;
}

uint32_t GetDomcVoteDuration()
{
    return Params().GetConsensus().nDOMCPhaseDuration;
}

uint32_t GetDomcAdaptationDuration()
{
    return Params().GetConsensus().nDOMCPhaseDuration;
}

// ============================================================================
// DomcCommit implementation
// ============================================================================

uint256 DomcCommit::GetHash() const
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << hashCommit;
    ss << mnOutpoint;
    ss << nCycleId;
    ss << nCommitHeight;
    return ss.GetHash();
}

// ============================================================================
// DomcReveal implementation
// ============================================================================

uint256 DomcReveal::GetHash() const
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << nRProposal;
    ss << salt;
    ss << mnOutpoint;
    ss << nCycleId;
    ss << nRevealHeight;
    return ss.GetHash();
}

uint256 DomcReveal::GetCommitHash() const
{
    // Commit hash = Hash(R_proposal || salt)
    CHashWriter ss(SER_GETHASH, 0);
    ss << nRProposal;
    ss << salt;
    return ss.GetHash();
}

// ============================================================================
// Cycle management functions
// ============================================================================

uint32_t GetCurrentCycleId(uint32_t nHeight, uint32_t nActivationHeight)
{
    if (nHeight < nActivationHeight) {
        return 0; // Before activation
    }

    uint32_t cycleLen = GetDomcCycleLength();
    uint32_t blocks_since_activation = nHeight - nActivationHeight;
    uint32_t cycle_number = blocks_since_activation / cycleLen;
    return nActivationHeight + (cycle_number * cycleLen);
}

bool IsDomcCycleBoundary(uint32_t nHeight, uint32_t nActivationHeight)
{
    // Before activation: not a boundary
    if (nHeight < nActivationHeight) {
        return false;
    }

    // First cycle starts at activation height
    if (nHeight == nActivationHeight) {
        return true;
    }

    // Subsequent cycles: every DOMC_CYCLE_LENGTH blocks (network-dependent)
    uint32_t blocks_since_activation = nHeight - nActivationHeight;
    return (blocks_since_activation % GetDomcCycleLength()) == 0;
}

bool IsDomcActivationBlock(uint32_t nHeight, uint32_t nActivationHeight)
{
    // ═══════════════════════════════════════════════════════════════════════════
    // UNIFIED ACTIVATION BLOCK: R% activation + DAO payouts at same height
    // ═══════════════════════════════════════════════════════════════════════════
    // The activation block is the LAST block of the DOMC cycle (cycleLength - 1)
    // This coincides with the last block of the 3rd DAO cycle (DAO payout height)
    //
    // For regtest (DOMC=90, DAO=30):
    //   - Block 89: DOMC activation + DAO3 payout (cycle 1)
    //   - Block 179: DOMC activation + DAO6 payout (cycle 2)
    //   - etc.
    // ═══════════════════════════════════════════════════════════════════════════

    // Before activation: not an activation block
    if (nHeight < nActivationHeight) {
        return false;
    }

    // First cycle has no previous R_next to activate (V6 activation initializes R)
    // So first activation is at end of first DOMC cycle
    uint32_t cycleLen = GetDomcCycleLength();
    uint32_t blocks_since_activation = nHeight - nActivationHeight;

    // Activation at cycleLength - 1 (last block of cycle)
    // cycleLength - 1, 2×cycleLength - 1, 3×cycleLength - 1, etc.
    // Formula: (offset + 1) % cycleLength == 0
    return ((blocks_since_activation + 1) % cycleLen) == 0;
}

bool IsDomcVotePhase(uint32_t nHeight, uint32_t cycleStart)
{
    if (nHeight < cycleStart) {
        return false;
    }

    uint32_t offset = nHeight - cycleStart;
    // VOTE phase: [VOTE_OFFSET, REVEAL_HEIGHT) - MN submit commits AND reveals
    return (offset >= GetDomcVoteOffset() && offset < GetDomcRevealHeight());
}

bool IsDomcAdaptationPhase(uint32_t nHeight, uint32_t cycleStart)
{
    if (nHeight < cycleStart) {
        return false;
    }

    uint32_t offset = nHeight - cycleStart;
    // ADAPTATION phase: [REVEAL_HEIGHT, CYCLE_LENGTH) - R_next visible, everyone adapts
    return (offset >= GetDomcRevealHeight() && offset < GetDomcCycleLength());
}

bool IsRevealHeight(uint32_t nHeight, uint32_t cycleStart)
{
    if (nHeight < cycleStart) {
        return false;
    }

    uint32_t offset = nHeight - cycleStart;
    // REVEAL instant at REVEAL_HEIGHT (network-dependent)
    return (offset == GetDomcRevealHeight());
}

// ============================================================================
// Median calculation (consensus-critical)
// ============================================================================

uint16_t CalculateDomcMedian(
    uint32_t cycleId,
    uint16_t currentR,
    uint16_t R_MAX_dynamic
)
{
    // Get DOMC database
    CKHUDomcDB* domcDB = GetKHUDomcDB();
    if (!domcDB) {
        LogPrintf("ERROR: CalculateDomcMedian: DOMC DB not initialized\n");
        return currentR; // Fallback: keep current R
    }

    // Collect all valid reveals for this cycle
    std::vector<DomcReveal> reveals;
    if (!domcDB->GetRevealsForCycle(cycleId, reveals)) {
        LogPrint(BCLog::HU, "CalculateDomcMedian: No reveals found for cycle %u\n", cycleId);
        return currentR; // No reveals → keep current R
    }

    // Extract R proposals from valid reveals
    std::vector<uint16_t> proposals;
    proposals.reserve(reveals.size());

    for (const auto& reveal : reveals) {
        // Verify reveal matches a commit
        DomcCommit commit;
        if (!domcDB->ReadCommit(reveal.mnOutpoint, cycleId, commit)) {
            LogPrint(BCLog::HU, "CalculateDomcMedian: No commit found for reveal (MN=%s)\n",
                     reveal.mnOutpoint.ToString());
            continue; // Skip invalid reveal
        }

        // Verify commit hash matches reveal
        if (commit.hashCommit != reveal.GetCommitHash()) {
            LogPrint(BCLog::HU, "CalculateDomcMedian: Commit hash mismatch (MN=%s)\n",
                     reveal.mnOutpoint.ToString());
            continue; // Skip invalid reveal
        }

        // Valid reveal → add to proposals
        proposals.push_back(reveal.nRProposal);
    }

    // V1 RULE: No minimum quorum
    if (proposals.empty()) {
        LogPrint(BCLog::HU, "CalculateDomcMedian: No valid proposals for cycle %u (keeping R=%u)\n",
                 cycleId, currentR);
        return currentR; // 0 valid votes → keep current R
    }

    // Calculate median (floor index)
    std::sort(proposals.begin(), proposals.end());
    uint16_t median = proposals[proposals.size() / 2];

    // Clamp to R_MAX_dynamic (governance safety limit)
    if (median > R_MAX_dynamic) {
        LogPrint(BCLog::HU, "CalculateDomcMedian: Clamping median %u to R_MAX %u\n",
                 median, R_MAX_dynamic);
        median = R_MAX_dynamic;
    }

    LogPrint(BCLog::HU, "CalculateDomcMedian: Cycle %u → %zu valid votes, median R=%u (clamped to %u)\n",
             cycleId, proposals.size(), median, R_MAX_dynamic);

    return median;
}

// ============================================================================
// Cycle initialization/finalization
// ============================================================================

void InitializeDomcCycle(
    HuGlobalState& state,
    uint32_t nHeight,
    bool isFirstCycle
)
{
    state.domc_cycle_start = nHeight;
    state.domc_cycle_length = GetDomcCycleLength();
    state.domc_commit_phase_start = nHeight + GetDomcVoteOffset();
    state.domc_reveal_deadline = nHeight + GetDomcRevealHeight();

    // CRITICAL: At V6 activation (first cycle), initialize R_annual to R_DEFAULT (40%)
    // This is the ONLY place where R_annual is set to 40% for the first time.
    // SetNull() initializes R_annual = 0, so we MUST set it here.
    if (isFirstCycle) {
        state.R_annual = R_DEFAULT;  // 4000 basis points = 40%
        state.R_MAX_dynamic = R_MAX_DYNAMIC_INITIAL;  // 4000 basis points = 40%
        LogPrint(BCLog::HU, "InitializeDomcCycle: FIRST CYCLE at V6 activation - R_annual initialized to %u (%.2f%%)\n",
                 state.R_annual, state.R_annual / 100.0);
    }

    LogPrint(BCLog::HU, "InitializeDomcCycle: New cycle at height %u\n", nHeight);
    LogPrint(BCLog::HU, "  VOTE phase: %u - %u\n",
             state.domc_commit_phase_start, state.domc_reveal_deadline - 1);
    LogPrint(BCLog::HU, "  REVEAL instant: %u\n", state.domc_reveal_deadline);
    LogPrint(BCLog::HU, "  ADAPTATION phase: %u - %u\n",
             state.domc_reveal_deadline, nHeight + GetDomcCycleLength() - 1);
}

bool ProcessRevealInstant(
    HuGlobalState& state,
    uint32_t nHeight,
    const Consensus::Params& consensusParams
)
{
    uint32_t V6_activation = consensusParams.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight;

    // Must be at REVEAL height within current cycle
    uint32_t cycleStart = GetCurrentCycleId(nHeight, V6_activation);
    if (!IsRevealHeight(nHeight, cycleStart)) {
        LogPrintf("ERROR: ProcessRevealInstant called at non-reveal height %u (cycle %u)\n",
                  nHeight, cycleStart);
        return false;
    }

    LogPrint(BCLog::HU, "ProcessRevealInstant: Processing REVEAL at height %u (cycle %u)\n",
             nHeight, cycleStart);

    // Calculate median R% from valid reveals for THIS cycle
    uint16_t R_next_value = CalculateDomcMedian(cycleStart, state.R_annual, state.R_MAX_dynamic);

    // Store in R_next (visible during ADAPTATION phase)
    state.R_next = R_next_value;

    LogPrint(BCLog::HU, "ProcessRevealInstant: R_next set to %u (%.2f%%) - visible during ADAPTATION\n",
             R_next_value, R_next_value / 100.0);
    LogPrint(BCLog::HU, "ProcessRevealInstant: Current R_annual remains %u (%.2f%%) until cycle end\n",
             state.R_annual, state.R_annual / 100.0);

    return true;
}

bool FinalizeDomcCycle(
    HuGlobalState& state,
    uint32_t nHeight,
    const Consensus::Params& consensusParams
)
{
    uint32_t V6_activation = consensusParams.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight;

    // First cycle boundary at activation height - nothing to finalize
    if (nHeight == V6_activation) {
        LogPrint(BCLog::HU, "FinalizeDomcCycle: First cycle at V6 activation, nothing to finalize\n");
        return true;
    }

    // Calculate previous cycle ID
    uint32_t prevCycleId = nHeight - GetDomcCycleLength();

    if (prevCycleId < V6_activation) {
        // First cycle after activation → no previous cycle to finalize
        LogPrint(BCLog::HU, "FinalizeDomcCycle: First cycle, no previous cycle to finalize\n");
        return true;
    }

    LogPrint(BCLog::HU, "FinalizeDomcCycle: Finalizing cycle %u at height %u\n",
             prevCycleId, nHeight);

    // ACTIVATION: R_next → R_annual
    // R_next was set at REVEAL instant (block 152640 of previous cycle)
    uint16_t old_R = state.R_annual;
    uint16_t new_R = state.R_next;

    // Fallback: if R_next is 0 (no REVEAL processed), keep current R_annual
    if (new_R == 0) {
        LogPrint(BCLog::HU, "FinalizeDomcCycle: R_next is 0 (no REVEAL), keeping R_annual=%u (%.2f%%)\n",
                 old_R, old_R / 100.0);
        new_R = old_R;
    }

    if (new_R != old_R) {
        LogPrint(BCLog::HU, "FinalizeDomcCycle: R_annual ACTIVATED: %u → %u (%.2f%% → %.2f%%)\n",
                 old_R, new_R, old_R / 100.0, new_R / 100.0);
        state.R_annual = new_R;
    } else {
        LogPrint(BCLog::HU, "FinalizeDomcCycle: R_annual unchanged: %u (%.2f%%)\n",
                 old_R, old_R / 100.0);
    }

    // Reset R_next for the new cycle
    state.R_next = 0;

    return true;
}

bool UndoFinalizeDomcCycle(
    HuGlobalState& state,
    uint32_t nHeight,
    const Consensus::Params& consensusParams
)
{
    // Calculate previous cycle ID (the cycle that was finalized at nHeight)
    uint32_t prevCycleId = nHeight - GetDomcCycleLength();

    uint32_t V6_activation = consensusParams.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight;

    if (prevCycleId < V6_activation) {
        // First cycle boundary after activation
        // FinalizeDomcCycle returned early without changing state
        // Nothing to undo
        LogPrint(BCLog::HU, "UndoFinalizeDomcCycle: First cycle boundary, no state changes to undo\n");
        return true;
    }

    // To undo FinalizeDomcCycle, we need to restore ALL DOMC state fields to what they
    // were BEFORE FinalizeDomcCycle was called. The correct way to do this is to read
    // the state from the previous cycle boundary (nHeight - cycleLength).

    // Calculate the height of the previous cycle boundary
    uint32_t prevCycleBoundary = nHeight - GetDomcCycleLength();

    // Read state from previous cycle boundary
    CKHUStateDB* db = GetKHUStateDB();
    if (!db) {
        LogPrintf("ERROR: UndoFinalizeDomcCycle: State DB not initialized\n");
        return false;
    }

    HuGlobalState prevState;
    if (!db->ReadKHUState(prevCycleBoundary, prevState)) {
        // Edge case: If previous state doesn't exist (shouldn't happen in valid chain),
        // fall back to defaults
        LogPrint(BCLog::HU, "UndoFinalizeDomcCycle: Cannot read state at height %u, falling back to defaults\n",
                 prevCycleBoundary);
        state.R_annual = R_DEFAULT;
        state.R_MAX_dynamic = R_MAX;
        // domc_cycle_* fields will be reinitialized by InitializeDomcCycle
        return true;
    }

    // Restore ALL DOMC-related state fields from previous state
    uint16_t old_R = state.R_annual;
    uint16_t restored_R = prevState.R_annual;
    uint16_t old_R_MAX = state.R_MAX_dynamic;
    uint16_t restored_R_MAX = prevState.R_MAX_dynamic;

    // Restore R_annual
    if (restored_R != old_R) {
        LogPrint(BCLog::HU, "UndoFinalizeDomcCycle: Restoring R_annual: %u → %u (%.2f%% → %.2f%%)\n",
                 old_R, restored_R, old_R / 100.0, restored_R / 100.0);
        state.R_annual = restored_R;
    }

    // Restore R_next (was reset to 0 in FinalizeDomcCycle)
    uint16_t old_R_next = state.R_next;
    uint16_t restored_R_next = prevState.R_next;
    if (restored_R_next != old_R_next) {
        LogPrint(BCLog::HU, "UndoFinalizeDomcCycle: Restoring R_next: %u → %u (%.2f%% → %.2f%%)\n",
                 old_R_next, restored_R_next, old_R_next / 100.0, restored_R_next / 100.0);
        state.R_next = restored_R_next;
    }

    // Restore R_MAX_dynamic
    if (restored_R_MAX != old_R_MAX) {
        LogPrint(BCLog::HU, "UndoFinalizeDomcCycle: Restoring R_MAX_dynamic: %u → %u (%.2f%% → %.2f%%)\n",
                 old_R_MAX, restored_R_MAX, old_R_MAX / 100.0, restored_R_MAX / 100.0);
        state.R_MAX_dynamic = restored_R_MAX;
    }

    // Restore DOMC cycle tracking fields
    // Note: These will be re-initialized by InitializeDomcCycle, but we restore them
    // here for consistency and to maintain exact state reversibility
    state.domc_cycle_start = prevState.domc_cycle_start;
    state.domc_commit_phase_start = prevState.domc_commit_phase_start;
    state.domc_reveal_deadline = prevState.domc_reveal_deadline;

    LogPrint(BCLog::HU, "UndoFinalizeDomcCycle: Restored DOMC cycle fields (start=%u, commit_start=%u, reveal_deadline=%u)\n",
             state.domc_cycle_start, state.domc_commit_phase_start, state.domc_reveal_deadline);

    // Clean up commits and reveals from the cycle being undone
    // The cycle being undone is the one that started at nHeight - cycleLength
    CKHUDomcDB* domcDB = GetKHUDomcDB();
    if (domcDB) {
        // Clear all commits/reveals for this cycle from DB
        // This prevents stale data from affecting future cycles after reorg
        if (!domcDB->EraseCycleData(prevCycleId)) {
            LogPrint(BCLog::HU, "UndoFinalizeDomcCycle: Warning - failed to erase cycle data for cycle %u\n",
                     prevCycleId);
            // Non-critical: continue even if cleanup fails
        } else {
            LogPrint(BCLog::HU, "UndoFinalizeDomcCycle: Cleaned up commits/reveals for cycle %u\n",
                     prevCycleId);
        }
    } else {
        LogPrint(BCLog::HU, "UndoFinalizeDomcCycle: Warning - DOMC DB not initialized, skipping cleanup\n");
    }

    return true;
}

// ============================================================================
// R_MAX_dynamic calculation
// ============================================================================

uint16_t CalculateRMaxDynamic(uint32_t nHeight, uint32_t nActivationHeight)
{
    // Before activation: return initial value
    if (nHeight < nActivationHeight) {
        return R_MAX_DYNAMIC_INITIAL;
    }

    // Calculate year since activation
    // year = (nHeight - activation) / BLOCKS_PER_YEAR
    uint32_t blocks_since_activation = nHeight - nActivationHeight;
    uint32_t year = blocks_since_activation / Consensus::Params::BLOCKS_PER_YEAR;

    // FORMULA CONSENSUS: R_MAX_dynamic = max(700, 4000 - year × 100)
    int32_t calculated = R_MAX_DYNAMIC_INITIAL - (year * R_MAX_DYNAMIC_DECAY);

    // Clamp to minimum (floor at 7%)
    if (calculated < R_MAX_DYNAMIC_MIN) {
        return R_MAX_DYNAMIC_MIN;
    }

    return static_cast<uint16_t>(calculated);
}

void UpdateRMaxDynamic(
    HuGlobalState& state,
    uint32_t nHeight,
    uint32_t nActivationHeight
)
{
    uint16_t newRMax = CalculateRMaxDynamic(nHeight, nActivationHeight);

    if (newRMax != state.R_MAX_dynamic) {
        LogPrint(BCLog::HU, "UpdateRMaxDynamic: R_MAX_dynamic updated %u → %u (%.2f%% → %.2f%%) at height %u\n",
                 state.R_MAX_dynamic, newRMax,
                 state.R_MAX_dynamic / 100.0, newRMax / 100.0,
                 nHeight);
        state.R_MAX_dynamic = newRMax;
    }
}

} // namespace khu_domc
