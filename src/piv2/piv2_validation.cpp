// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "piv2/piv2_validation.h"

#include "chain.h"
#include "dao/dao_proposal.h"  // CDAOManager, g_daoManager
#include "consensus/params.h"
#include "key_io.h"  // EncodeDestination
#include "piv2/piv2_commitment.h"
#include "piv2/piv2_commitmentdb.h"
#include "piv2/piv2_dao.h"
#include "piv2/piv2_domc.h"
#include "piv2/piv2_domcdb.h"
#include "piv2/piv2_domc_tx.h"
#include "piv2/piv2_mint.h"
#include "piv2/piv2_redeem.h"
#include "piv2/piv2_lock.h"
#include "piv2/piv2_state.h"
#include "piv2/piv2_statedb.h"
#include "piv2/piv2_unlock.h"
#include "piv2/piv2_yield.h"
#include "piv2/zkpiv2_db.h"
#include "primitives/block.h"
#include "sync.h"
#include "util/system.h"
#include "validation.h"

#include <memory>

// Global KHU state database
static std::unique_ptr<CKHUStateDB> pkhustatedb;

// Global KHU commitment database (Phase 3: Masternode Finality)
static std::unique_ptr<CHUCommitmentDB> pkhucommitmentdb;

// Global ZKHU database (Phase 4/5: Sapling notes, nullifiers, anchors)
static std::unique_ptr<CZKHUTreeDB> pzkhudb;

// KHU state lock (protects state transitions)
static RecursiveMutex cs_khu;

bool InitKHUStateDB(size_t nCacheSize, bool fReindex)
{
    LOCK(cs_khu);

    try {
        pkhustatedb.reset();
        pkhustatedb = std::make_unique<CKHUStateDB>(nCacheSize, false, fReindex);
        return true;
    } catch (const std::exception& e) {
        LogPrintf("ERROR: Failed to initialize KHU state database: %s\n", e.what());
        return false;
    }
}

/**
 * GetKHUStateDB - Get pointer to KHU state database
 *
 * THREAD-SAFETY MODEL:
 * - The database pointer is initialized once at startup (protected by cs_khu)
 * - LevelDB (CDBWrapper) is internally thread-safe for individual read/write ops
 * - All HU state mutation operations (ApplyHU*, UndoKHU*) MUST hold cs_khu
 * - Read operations can be performed without lock (LevelDB handles internally)
 *
 * IMPORTANT: For state mutations that require atomicity across multiple
 * database operations, the caller MUST hold cs_khu. Use AssertLockHeld(cs_khu)
 * in mutation functions to enforce this invariant.
 *
 * @return Pointer to the database (never null after initialization)
 */
CKHUStateDB* GetKHUStateDB()
{
    // Database is initialized once at startup, pointer is stable
    // No lock needed for read access (LevelDB is thread-safe)
    return pkhustatedb.get();
}

bool InitKHUCommitmentDB(size_t nCacheSize, bool fReindex)
{
    LOCK(cs_khu);

    try {
        pkhucommitmentdb.reset();
        pkhucommitmentdb = std::make_unique<CHUCommitmentDB>(nCacheSize, false, fReindex);
        LogPrint(BCLog::HU, "KHU: Initialized commitment database (Phase 3 Finality)\n");
        return true;
    } catch (const std::exception& e) {
        LogPrintf("ERROR: Failed to initialize KHU commitment database: %s\n", e.what());
        return false;
    }
}

CHUCommitmentDB* GetKHUCommitmentDB()
{
    return pkhucommitmentdb.get();
}

bool InitZKHUDB(size_t nCacheSize, bool fReindex)
{
    LOCK(cs_khu);

    try {
        pzkhudb.reset();
        pzkhudb = std::make_unique<CZKHUTreeDB>(nCacheSize, false, fReindex);
        LogPrint(BCLog::HU, "KHU: Initialized ZKHU database (Phase 4/5 Sapling)\n");
        return true;
    } catch (const std::exception& e) {
        LogPrintf("ERROR: Failed to initialize ZKHU database: %s\n", e.what());
        return false;
    }
}

CZKHUTreeDB* GetZKHUDB()
{
    return pzkhudb.get();
}

bool GetCurrentKHUState(HuGlobalState& state)
{
    LOCK(cs_main);

    CBlockIndex* pindex = chainActive.Tip();
    if (!pindex) {
        return false;
    }

    CKHUStateDB* db = GetKHUStateDB();
    if (!db) {
        return false;
    }

    return db->ReadKHUState(pindex->nHeight, state);
}

// Get current DAO Treasury balance (T)
CAmount GetHuTreasuryBalance()
{
    HuGlobalState state;
    if (!GetCurrentKHUState(state)) {
        return 0;
    }
    return state.T;
}

// Deduct amount from DAO Treasury when a proposal is paid
// Returns true if sufficient funds and deduction successful
bool DeductFromHuTreasury(CAmount amount, const uint256& proposalHash)
{
    LOCK(cs_khu);

    HuGlobalState state;
    if (!GetCurrentKHUState(state)) {
        LogPrintf("KHU: DeductFromHuTreasury - failed to get current state\n");
        return false;
    }

    if (state.T < amount) {
        LogPrintf("KHU: DeductFromHuTreasury - insufficient treasury balance: T=%lld, requested=%lld\n",
                  state.T, amount);
        return false;
    }

    // Deduction will be applied during block processing
    // This function is called to validate the proposal payment is possible
    LogPrint(BCLog::HU, "KHU: Treasury deduction validated: amount=%lld, proposal=%s, T_remaining=%lld\n",
             amount, proposalHash.ToString().substr(0, 16), state.T - amount);

    return true;
}

bool ProcessHUBlock(const CBlock& block,
                     CBlockIndex* pindex,
                     CCoinsViewCache& view,
                     CValidationState& validationState,
                     const Consensus::Params& consensusParams,
                     bool fJustCheck)
{
    LOCK(cs_khu);

    const int nHeight = pindex->nHeight;
    // Get hash from block directly - pindex->phashBlock may be nullptr during TestBlockValidity
    const uint256 hashBlock = block.GetHash();

    LogPrint(BCLog::HU, "ProcessHUBlock: height=%d, fJustCheck=%d, block=%s\n",
             nHeight, fJustCheck, hashBlock.ToString().substr(0, 16));

    CKHUStateDB* db = GetKHUStateDB();
    if (!db) {
        return validationState.Error("khu-db-not-initialized");
    }

    // Load previous state (or genesis if first KHU block)
    HuGlobalState prevState;
    if (nHeight > 0) {
        if (!db->ReadKHUState(nHeight - 1, prevState)) {
            // If previous state doesn't exist, initialize with genesis values
            // This happens at first block after genesis (height 1) or at KHU activation
            prevState.SetNull();
            prevState.nHeight = nHeight - 1;
            // Initialize with PIVHU genesis parameters
            prevState.T = khu_domc::T_GENESIS_INITIAL;
            prevState.R_annual = khu_domc::R_DEFAULT;
            prevState.R_MAX_dynamic = khu_domc::R_MAX_DYNAMIC_INITIAL;
            prevState.domc_cycle_start = 0;
            prevState.domc_cycle_length = khu_domc::GetDomcCycleLength();
            prevState.domc_commit_phase_start = khu_domc::GetDomcVoteOffset();
            prevState.domc_reveal_deadline = khu_domc::GetDomcRevealHeight();
            LogPrint(BCLog::HU, "ProcessHUBlock: Initialized genesis state for height %d (T=%lld R=%d)\n",
                     nHeight, (long long)prevState.T, prevState.R_annual);
        } else {
            // ✅ FIX CVE-KHU-2025-002: Vérifier les invariants de l'état chargé
            // CRITICAL: Without this check, a corrupted DB with invalid state (C != U)
            // would be loaded and used as the base for all future blocks, permanently
            // breaking the sacred invariants.
            if (!prevState.CheckInvariants()) {
                return validationState.Error(strprintf(
                    "khu-corrupted-prev-state: Previous state at height %d has invalid invariants (C=%d U=%d Cr=%d Ur=%d)",
                    nHeight - 1, prevState.C, prevState.U, prevState.Cr, prevState.Ur));
            }
        }
    } else {
        // Genesis block - initialize with PIVHU genesis parameters
        // T = 500,000 PIVHU initial DAO Treasury
        // R_annual = 40% (4000 basis points)
        // R_MAX_dynamic = 40% (initial ceiling)
        prevState.SetNull();
        prevState.nHeight = -1;
        prevState.T = khu_domc::T_GENESIS_INITIAL;
        prevState.R_annual = khu_domc::R_DEFAULT;
        prevState.R_MAX_dynamic = khu_domc::R_MAX_DYNAMIC_INITIAL;
        prevState.domc_cycle_start = 0;
        prevState.domc_cycle_length = khu_domc::GetDomcCycleLength();
        prevState.domc_commit_phase_start = khu_domc::GetDomcVoteOffset();
        prevState.domc_reveal_deadline = khu_domc::GetDomcRevealHeight();
    }

    // Create new state (copy from previous)
    HuGlobalState newState = prevState;

    // Update block linkage
    newState.nHeight = nHeight;
    newState.hashBlock = hashBlock;
    newState.hashPrevState = prevState.GetHash();

    LogPrint(BCLog::HU, "ProcessHUBlock: Before processing - C=%d U=%d Cr=%d Ur=%d (height=%d)\n",
             prevState.C, prevState.U, prevState.Cr, prevState.Ur, nHeight);

    // PHASE 6: Canonical order (CONSENSUS CRITICAL)
    // STEP 0: Update R_MAX_dynamic (year-based decay)
    // STEP 1: DOMC cycle boundary (R% activation, reveal instant)
    // STEP 2: DAO Treasury accumulation
    // STEP 3: Daily Yield (per-note Ur_accumulated + global Cr/Ur)
    // STEP 4: KHU Transactions (MINT/REDEEM/LOCK/UNLOCK/DOMC)
    // STEP 5: Block Reward (= 0 post-V6)
    // STEP 6: CheckInvariants()
    // STEP 7: PersistState()

    // STEP 0: Update R_MAX_dynamic based on year since activation
    // Formula: R_MAX_dynamic = max(700, 4000 - year × 100)
    // Decreases by 1% per year from 40% to 7% floor
    khu_domc::UpdateRMaxDynamic(newState, nHeight,
        consensusParams.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight);

    // STEP 1: DOMC cycle boundary and R% activation (Phase 6.2)
    // ═══════════════════════════════════════════════════════════════════════════
    // UNIFIED ACTIVATION: R% activates at END of DOMC cycle (same block as DAO3 payout)
    //
    // Regtest (DOMC=90, DAO=30):
    //   - Block 89: R_next → R_annual (UNIFIED ACTIVATION + DAO3 payout)
    //   - Block 90: Initialize new DOMC cycle
    //   - Block 179: R_next → R_annual (UNIFIED ACTIVATION + DAO6 payout)
    //   - Block 180: Initialize new DOMC cycle
    //   etc.
    //
    // V6 activation is special: R_annual = 40% at the start (no previous R_next)
    // ═══════════════════════════════════════════════════════════════════════════
    uint32_t V6_activation = consensusParams.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight;

    // Special case: V6 activation block initializes R_annual to default 40%
    if (nHeight == V6_activation) {
        khu_domc::InitializeDomcCycle(newState, nHeight, true /* isFirstCycle */);
        LogPrint(BCLog::HU, "ProcessHUBlock: V6 ACTIVATION at height %u, R_annual=%u (%.2f%%), R_MAX=%u (%.2f%%) [FIRST CYCLE]\n",
                 nHeight, newState.R_annual, newState.R_annual / 100.0,
                 newState.R_MAX_dynamic, newState.R_MAX_dynamic / 100.0);
    }

    // UNIFIED ACTIVATION BLOCK: R_next → R_annual at END of DOMC cycle
    // This is the LAST block of DOMC cycle (block cycleLength-1 relative to V6)
    // Coincides with 3rd DAO payout (every DOMC cycle = 3 DAO cycles)
    if (khu_domc::IsDomcActivationBlock(nHeight, V6_activation)) {
        // Finalize cycle: R_next → R_annual (R% ACTIVATION)
        if (!khu_domc::FinalizeDomcCycle(newState, nHeight, consensusParams)) {
            return validationState.Error("domc-finalize-failed");
        }

        LogPrint(BCLog::HU, "ProcessHUBlock: UNIFIED ACTIVATION at height %u - R_next=%u → R_annual=%u (%.2f%%), R_MAX=%u (%.2f%%)\n",
                 nHeight, newState.R_next, newState.R_annual, newState.R_annual / 100.0,
                 newState.R_MAX_dynamic, newState.R_MAX_dynamic / 100.0);
    }

    // CYCLE BOUNDARY: Initialize new DOMC cycle at START of new cycle
    // This happens one block AFTER the unified activation (block 0, 90, 180, ...)
    // Skip V6 activation block (already handled above)
    if (nHeight != V6_activation && khu_domc::IsDomcCycleBoundary(nHeight, V6_activation)) {
        // Initialize new cycle: update cycle_start, commit_phase_start, reveal_deadline
        khu_domc::InitializeDomcCycle(newState, nHeight, false /* isFirstCycle */);

        LogPrint(BCLog::HU, "ProcessHUBlock: DOMC cycle START at height %u, R_annual=%u (%.2f%%), R_MAX=%u (%.2f%%)\n",
                 nHeight, newState.R_annual, newState.R_annual / 100.0,
                 newState.R_MAX_dynamic, newState.R_MAX_dynamic / 100.0);
    }

    // STEP 1b: DOMC REVEAL instant (Phase 6.2)
    // At REVEAL height (block 152640 of cycle): calculate median(R) → R_next
    // R_next is visible during ADAPTATION phase (blocks 152640-172800)
    uint32_t cycleStart = khu_domc::GetCurrentCycleId(nHeight, V6_activation);
    if (khu_domc::IsRevealHeight(nHeight, cycleStart)) {
        if (!khu_domc::ProcessRevealInstant(newState, nHeight, consensusParams)) {
            return validationState.Error("domc-reveal-failed");
        }

        LogPrint(BCLog::HU, "ProcessHUBlock: DOMC REVEAL at height %u, R_next=%u (%.2f%%), R_annual remains %u (%.2f%%)\n",
                 nHeight, newState.R_next, newState.R_next / 100.0,
                 newState.R_annual, newState.R_annual / 100.0);
    }

    // STEP 2: DAO Treasury accumulation (Phase 6.3)
    // Budget calculated on INITIAL state (before yield/transactions)
    // Only apply when !fJustCheck (no DB writes in DAO, but for consistency)
    if (!fJustCheck && !khu_dao::AccumulateDaoTreasuryIfNeeded(newState, nHeight, consensusParams)) {
        return validationState.Error("dao-treasury-failed");
    }

    // STEP 3: Daily Yield distribution (Phase 6.1)
    // Apply daily yield to all mature lockd notes (every 1440 blocks)
    // This updates Cr += total_yield, Ur += total_yield (invariant Cr==Ur preserved)
    // CRITICAL: Only apply when !fJustCheck to avoid double DB writes
    // ApplyDailyYield writes to ZKHU note DB, so must skip during fJustCheck=true
    // Note: V6_activation already defined above (STEP 1)
    if (!fJustCheck && khu_yield::ShouldApplyDailyYield(nHeight, V6_activation, newState.last_yield_update_height)) {
        if (!khu_yield::ApplyDailyYield(newState, nHeight, V6_activation)) {
            return validationState.Error("daily-yield-failed");
        }

        LogPrint(BCLog::HU, "ProcessHUBlock: Applied daily yield at height %u, Cr=%d Ur=%d\n",
                 nHeight, newState.Cr, newState.Ur);
    }

    // STEP 4: Process KHU transactions
    // Note: Basic transaction validation was done by CheckSpecialTx
    // Here we apply transactions to update newState and view.
    // DB writes are conditional on !fJustCheck (handled inside Apply* functions or here)
    int nKHUTxCount = 0;
    for (const auto& tx : block.vtx) {
        if (tx->nType == CTransaction::TxType::KHU_MINT) {
            nKHUTxCount++;
            // ApplyHUMint modifies global KHU UTXO map - only call when !fJustCheck
            // Transaction structure validation was already done by CheckSpecialTx
            if (!fJustCheck) {
                if (!ApplyHUMint(*tx, newState, view, nHeight)) {
                    return validationState.Error(strprintf("Failed to apply KHU MINT at height %d", nHeight));
                }
            }
            LogPrint(BCLog::HU, "ProcessHUBlock: KHU_MINT tx %s (fJustCheck=%d)\n",
                     tx->GetHash().ToString().substr(0, 16), fJustCheck);
        } else if (tx->nType == CTransaction::TxType::KHU_REDEEM) {
            nKHUTxCount++;
            // ApplyHURedeem modifies global KHU UTXO map - only call when !fJustCheck
            // Transaction structure validation was already done by CheckSpecialTx
            if (!fJustCheck) {
                if (!ApplyHURedeem(*tx, newState, view, nHeight)) {
                    return validationState.Error(strprintf("Failed to apply KHU REDEEM at height %d", nHeight));
                }
            }
            LogPrint(BCLog::HU, "ProcessHUBlock: KHU_REDEEM tx %s (fJustCheck=%d)\n",
                     tx->GetHash().ToString().substr(0, 16), fJustCheck);
        } else if (tx->nType == CTransaction::TxType::KHU_LOCK) {
            // Phase 4: KHU_T → ZKHU (state unchanged: C, U, Cr, Ur)
            // ApplyHULock writes to ZKHU DB, so only call when !fJustCheck
            // For fJustCheck=true, we validate structure but skip DB writes
            nKHUTxCount++;
            if (!fJustCheck) {
                if (!ApplyHULock(*tx, view, newState, nHeight)) {
                    return validationState.Error(strprintf("Failed to apply KHU LOCK at height %d", nHeight));
                }
            }
            LogPrint(BCLog::HU, "ProcessHUBlock: KHU_LOCK tx %s (fJustCheck=%d)\n",
                     tx->GetHash().ToString().substr(0, 16), fJustCheck);
        } else if (tx->nType == CTransaction::TxType::KHU_UNLOCK) {
            // Phase 4: ZKHU → KHU_T + bonus (double flux: C+, U+, Cr-, Ur-)
            // ApplyHUUnlock reads from ZKHU DB and modifies state
            // For fJustCheck=true, skip since it needs prior LOCK data
            nKHUTxCount++;
            if (!fJustCheck) {
                if (!ApplyHUUnlock(*tx, view, newState, nHeight)) {
                    return validationState.Error(strprintf("Failed to apply KHU UNLOCK at height %d", nHeight));
                }
            }
            LogPrint(BCLog::HU, "ProcessHUBlock: KHU_UNLOCK tx %s (fJustCheck=%d)\n",
                     tx->GetHash().ToString().substr(0, 16), fJustCheck);
        } else if (tx->nType == CTransaction::TxType::KHU_DOMC_COMMIT) {
            // Phase 6.2: DOMC commit vote (Hash(R || salt))
            // Validation runs in both paths, DB write only when !fJustCheck
            nKHUTxCount++;
            if (!ValidateDomcCommitTx(*tx, validationState, newState, nHeight, consensusParams)) {
                return false; // validationState already set
            }
            if (!fJustCheck) {
                if (!ApplyDomcCommitTx(*tx, nHeight)) {
                    return validationState.Error(strprintf("Failed to apply DOMC COMMIT at height %d", nHeight));
                }
            }
            LogPrint(BCLog::HU, "ProcessHUBlock: KHU_DOMC_COMMIT tx %s (fJustCheck=%d)\n",
                     tx->GetHash().ToString().substr(0, 16), fJustCheck);
        } else if (tx->nType == CTransaction::TxType::KHU_DOMC_REVEAL) {
            // Phase 6.2: DOMC reveal vote (R + salt)
            // Validation runs in both paths, DB write only when !fJustCheck
            nKHUTxCount++;
            if (!ValidateDomcRevealTx(*tx, validationState, newState, nHeight, consensusParams)) {
                return false; // validationState already set
            }
            if (!fJustCheck) {
                if (!ApplyDomcRevealTx(*tx, nHeight)) {
                    return validationState.Error(strprintf("Failed to apply DOMC REVEAL at height %d", nHeight));
                }
            }
            LogPrint(BCLog::HU, "ProcessHUBlock: KHU_DOMC_REVEAL tx %s (fJustCheck=%d)\n",
                     tx->GetHash().ToString().substr(0, 16), fJustCheck);
        }
    }
    LogPrint(BCLog::HU, "ProcessHUBlock: Processed %d KHU transactions at height %d\n", nKHUTxCount, nHeight);

    // STEP 5: DAO Proposal Payouts (Phase 6.4)
    // ═══════════════════════════════════════════════════════════════════════════
    // DAO payouts execute at payout height (last block of each DAO cycle)
    //
    // Regtest timing (DOMC=90, DAO=30):
    //   - Block 29: DAO1 payout
    //   - Block 59: DAO2 payout
    //   - Block 89: DAO3 payout + UNIFIED ACTIVATION (R_next → R_annual)
    //   - Block 119: DAO4 payout
    //   - Block 149: DAO5 payout
    //   - Block 179: DAO6 payout + UNIFIED ACTIVATION
    //   etc.
    //
    // Every 3rd DAO payout coincides with DOMC activation (3 DAO cycles = 1 DOMC cycle)
    // ═══════════════════════════════════════════════════════════════════════════
    if (!fJustCheck && g_daoManager) {
        // Check if we're at payout height for this cycle
        uint32_t nCycleStart = CDAOManager::GetCycleStart(nHeight);
        uint32_t nOffset = nHeight - nCycleStart;
        uint32_t nPayoutHeight = GetDAOPayoutHeight() - 1;  // Last block of cycle

        if (nOffset == nPayoutHeight) {
            // Check if this is a UNIFIED activation block (every 3rd DAO payout)
            bool isUnifiedActivation = khu_domc::IsDomcActivationBlock(nHeight, V6_activation);

            LogPrint(BCLog::HU, "ProcessHUBlock: DAO payout height %d (cycle %d, offset %d)%s\n",
                     nHeight, nCycleStart, nOffset,
                     isUnifiedActivation ? " [UNIFIED ACTIVATION]" : "");

            std::vector<std::pair<CTxDestination, CAmount>> payouts;
            if (g_daoManager->ExecutePayouts(nHeight, newState.T, payouts)) {
                // Deduct each payout from Treasury T
                CAmount nTotalPaid = 0;
                for (const auto& payout : payouts) {
                    if (!khu_dao::DeductBudgetPayment(newState, payout.second)) {
                        return validationState.Error(strprintf(
                            "DAO payout failed: insufficient T for %d satoshis", payout.second));
                    }
                    nTotalPaid += payout.second;
                    LogPrint(BCLog::HU, "ProcessHUBlock: DAO payout %s = %d satoshis\n",
                             EncodeDestination(payout.first), payout.second);
                }
                LogPrint(BCLog::HU, "ProcessHUBlock: Total DAO payouts = %d satoshis, T_after = %d\n",
                         nTotalPaid, newState.T);
            }
        }
    }

    // Verify invariants (CRITICAL)
    if (!newState.CheckInvariants()) {
        LogPrint(BCLog::HU, "ProcessHUBlock: FAIL - Invariants violated at height %d (C=%d U=%d Cr=%d Ur=%d)\n",
                 nHeight, newState.C, newState.U, newState.Cr, newState.Ur);
        return validationState.Error(strprintf("KHU invariants violated at height %d", nHeight));
    }

    LogPrint(BCLog::HU, "ProcessHUBlock: After processing - C=%d U=%d Cr=%d Ur=%d (height=%d, fJustCheck=%d)\n",
             newState.C, newState.U, newState.Cr, newState.Ur, nHeight, fJustCheck);

    // Persist state to database ONLY when not just checking
    if (!fJustCheck) {
        if (!db->WriteKHUState(nHeight, newState)) {
            LogPrint(BCLog::HU, "ProcessHUBlock: FAIL - Write state failed at height %d\n", nHeight);
            return validationState.Error(strprintf("Failed to write KHU state at height %d", nHeight));
        }
        LogPrint(BCLog::HU, "ProcessHUBlock: SUCCESS - Persisted state at height %d\n", nHeight);
    } else {
        LogPrint(BCLog::HU, "ProcessHUBlock: SUCCESS - Validated state at height %d (fJustCheck=true, no persist)\n", nHeight);
    }

    return true;
}

bool DisconnectKHUBlock(const CBlock& block,
                       CBlockIndex* pindex,
                       CValidationState& validationState,
                       CCoinsViewCache& view,
                       HuGlobalState& huState,
                       const Consensus::Params& consensusParams,
                       bool fJustCheck)
{
    LOCK(cs_khu);

    const int nHeight = pindex->nHeight;

    // ═══════════════════════════════════════════════════════════════════════════
    // fJustCheck MODE: Skip all mutations, just return success
    // This is used by VerifyDB which does disconnect/reconnect cycles for validation
    // without actually modifying the KHU database state.
    // ═══════════════════════════════════════════════════════════════════════════
    if (fJustCheck) {
        LogPrint(BCLog::HU, "KHU: DisconnectKHUBlock fJustCheck=true, skipping mutations for block %d\n", nHeight);
        return true;
    }

    CKHUStateDB* db = GetKHUStateDB();
    if (!db) {
        return validationState.Error("khu-db-not-initialized");
    }

    // PHASE 3: Check cryptographic finality via commitments (V6_0+ only)
    // NOTE: DisconnectKHUBlock is only called if NetworkUpgradeActive(V6_0) in validation.cpp
    // but we double-check here for clarity and safety
    CHUCommitmentDB* commitmentDB = GetKHUCommitmentDB();
    if (commitmentDB) {
        uint32_t latestFinalized = commitmentDB->GetLatestFinalizedHeight();

        // Cannot reorg finalized blocks with quorum commitments
        if (nHeight <= latestFinalized) {
            LogPrint(BCLog::HU, "KHU: Rejecting reorg of finalized block %d (latest finalized: %d)\n",
                     nHeight, latestFinalized);
            return validationState.Error(strprintf(
                "khu-reorg-finalized: Cannot reorg block %d (finalized at %d)",
                nHeight, latestFinalized));
        }
    }

    // Validate reorg depth (12 blocks maximum)
    const int KHU_FINALITY_DEPTH = 12;

    CBlockIndex* pindexTip = chainActive.Tip();
    if (pindexTip) {
        int reorgDepth = pindexTip->nHeight - nHeight;
        if (reorgDepth > KHU_FINALITY_DEPTH) {
            LogPrint(BCLog::HU, "KHU: Rejecting reorg depth %d (max %d blocks)\n",
                     reorgDepth, KHU_FINALITY_DEPTH);
            return validationState.Error(strprintf(
                "khu-reorg-too-deep: KHU reorg depth %d exceeds maximum %d blocks",
                reorgDepth, KHU_FINALITY_DEPTH));
        }
    }

    // PHASE 4: Undo KHU transactions in REVERSE order
    // This restores the exact state by reversing all mutations from ProcessHUBlock
    for (int i = block.vtx.size() - 1; i >= 0; i--) {
        const auto& tx = block.vtx[i];

        if (tx->nType == CTransaction::TxType::KHU_UNLOCK) {
            // Reverse double flux: C-, U-, Cr+, Ur+ (opposite of Apply)
            if (!UndoKHUUnlock(*tx, view, huState, nHeight)) {
                return validationState.Invalid(false, REJECT_INVALID, "khu-undo-unlock-failed",
                    strprintf("Failed to undo KHU UNLOCK at height %d (tx %s)",
                              nHeight, tx->GetHash().ToString()));
            }
        } else if (tx->nType == CTransaction::TxType::KHU_LOCK) {
            // Restore KHU_T UTXO, erase ZKHU note (state unchanged: C, U, Cr, Ur)
            if (!UndoKHULock(*tx, view, huState, nHeight)) {
                return validationState.Invalid(false, REJECT_INVALID, "khu-undo-lock-failed",
                    strprintf("Failed to undo KHU LOCK at height %d (tx %s)",
                              nHeight, tx->GetHash().ToString()));
            }
        } else if (tx->nType == CTransaction::TxType::KHU_DOMC_REVEAL) {
            // Phase 6.2: Undo DOMC reveal vote (erase from DB)
            if (!UndoDomcRevealTx(*tx, nHeight)) {
                return validationState.Invalid(false, REJECT_INVALID, "khu-undo-domc-reveal-failed",
                    strprintf("Failed to undo DOMC REVEAL at height %d (tx %s)",
                              nHeight, tx->GetHash().ToString()));
            }
        } else if (tx->nType == CTransaction::TxType::KHU_DOMC_COMMIT) {
            // Phase 6.2: Undo DOMC commit vote (erase from DB)
            if (!UndoDomcCommitTx(*tx, nHeight)) {
                return validationState.Invalid(false, REJECT_INVALID, "khu-undo-domc-commit-failed",
                    strprintf("Failed to undo DOMC COMMIT at height %d (tx %s)",
                              nHeight, tx->GetHash().ToString()));
            }
        } else if (tx->nType == CTransaction::TxType::KHU_MINT) {
            // Phase 2: Undo MINT (C-, U-)
            if (!UndoKHUMint(*tx, huState, view)) {
                return validationState.Invalid(false, REJECT_INVALID, "khu-undo-mint-failed",
                    strprintf("Failed to undo KHU MINT at height %d (tx %s)",
                              nHeight, tx->GetHash().ToString()));
            }
        } else if (tx->nType == CTransaction::TxType::KHU_REDEEM) {
            // Phase 2: Undo REDEEM (C+, U+)
            if (!UndoKHURedeem(*tx, huState, view)) {
                return validationState.Invalid(false, REJECT_INVALID, "khu-undo-redeem-failed",
                    strprintf("Failed to undo KHU REDEEM at height %d (tx %s)",
                              nHeight, tx->GetHash().ToString()));
            }
        }
    }

    // PHASE 6: Undo Daily Yield (Phase 6.1)
    // Must be undone AFTER transactions, BEFORE DOMC/DAO (reverse order of Connect)
    uint32_t V6_activation = consensusParams.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight;
    if (khu_yield::ShouldApplyDailyYield(nHeight, V6_activation, huState.last_yield_update_height)) {
        if (!khu_yield::UndoDailyYield(huState, nHeight, V6_activation)) {
            return validationState.Invalid(false, REJECT_INVALID, "undo-daily-yield-failed",
                strprintf("Failed to undo daily yield at height %d", nHeight));
        }

        LogPrint(BCLog::HU, "DisconnectKHUBlock: Undid daily yield at height %u, Ur=%d\n",
                 nHeight, huState.Ur);
    }

    // PHASE 6: Undo DOMC REVEAL instant (Phase 6.2)
    // Must be undone AFTER Daily Yield, BEFORE DOMC cycle (reverse order of Connect)
    // At REVEAL height: restore R_next to value before ProcessRevealInstant
    uint32_t cycleStart = khu_domc::GetCurrentCycleId(nHeight, V6_activation);
    if (khu_domc::IsRevealHeight(nHeight, cycleStart)) {
        // Read previous state to restore R_next
        HuGlobalState prevState;
        if (db->ReadKHUState(nHeight - 1, prevState)) {
            huState.R_next = prevState.R_next;
            LogPrint(BCLog::HU, "DisconnectKHUBlock: Undid DOMC REVEAL at height %u, R_next restored to %u\n",
                     nHeight, huState.R_next);
        } else {
            // Fallback: reset R_next to 0 (no REVEAL processed)
            huState.R_next = 0;
            LogPrint(BCLog::HU, "DisconnectKHUBlock: Undid DOMC REVEAL at height %u, R_next reset to 0 (fallback)\n",
                     nHeight);
        }
    }

    // PHASE 6: Undo DOMC cycle finalization (Phase 6.2)
    // Must be undone AFTER REVEAL instant, BEFORE DAO (reverse order of Connect)
    // At cycle boundary: undo R_annual update from previous cycle finalization
    if (khu_domc::IsDomcCycleBoundary(nHeight, V6_activation)) {
        if (!khu_domc::UndoFinalizeDomcCycle(huState, nHeight, consensusParams)) {
            return validationState.Invalid(false, REJECT_INVALID, "undo-domc-cycle-failed",
                strprintf("Failed to undo DOMC cycle finalization at height %d", nHeight));
        }

        LogPrint(BCLog::HU, "DisconnectKHUBlock: Undid DOMC cycle finalization at height %u, R_annual=%u\n",
                 nHeight, huState.R_annual);
    }

    // DAO T payments are handled via KHU DAO proposal system

    // PHASE 6: Undo DAO Treasury (Phase 6.3)
    // Must be undone AFTER DOMC (reverse order of Connect)
    if (!khu_dao::UndoDaoTreasuryIfNeeded(huState, nHeight, consensusParams)) {
        return validationState.Invalid(false, REJECT_INVALID, "undo-dao-treasury-failed",
            strprintf("Failed to undo DAO treasury at height %d", nHeight));
    }

    // Verify invariants after UNDO operations (CRITICAL: ensures state integrity)
    if (!huState.CheckInvariants()) {
        return validationState.Invalid(false, REJECT_INVALID, "khu-undo-invariant-failed",
            strprintf("KHU invariants violated after undo at height %d (C=%d U=%d Cr=%d Ur=%d)",
                      nHeight, huState.C, huState.U, huState.Cr, huState.Ur));
    }

    // Erase state at this height (previous state remains intact)
    if (!db->EraseKHUState(nHeight)) {
        return validationState.Error(strprintf("Failed to erase KHU state at height %d", nHeight));
    }

    // Phase 3: Also erase commitment if present (non-finalized)
    if (commitmentDB && commitmentDB->HaveCommitment(nHeight)) {
        if (!commitmentDB->EraseCommitment(nHeight)) {
            LogPrint(BCLog::HU, "KHU: Warning - failed to erase commitment at height %d during reorg\n", nHeight);
            // Non-fatal - continue with reorg
        }
    }

    LogPrint(BCLog::HU, "KHU: Disconnected block %d (undone %zu transactions)\n", nHeight, block.vtx.size());

    return true;
}
