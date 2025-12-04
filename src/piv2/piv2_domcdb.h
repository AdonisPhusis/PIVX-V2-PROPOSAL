// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef HU_HU_DOMCDB_H
#define HU_HU_DOMCDB_H

#include "dbwrapper.h"
#include "piv2/piv2_domc.h"
#include "primitives/transaction.h"

#include <stdint.h>
#include <vector>

/**
 * CKHUDomcDB - LevelDB persistence layer for DOMC votes
 *
 * Phase 6.2: Stores DOMC commit/reveal votes from masternodes
 *
 * DATABASE KEYS:
 * - 'D' + 'C' + mnOutpoint + cycleId -> DomcCommit (commit vote)
 * - 'D' + 'R' + mnOutpoint + cycleId -> DomcReveal (reveal vote)
 * - 'D' + 'I' + cycleId -> std::vector<COutPoint> (index of MNs in cycle)
 *
 * ARCHITECTURE:
 * - Commits stored during commit phase (cycle_start + 132480 → 152640)
 * - Reveals stored during reveal phase (cycle_start + 152640 → 172800)
 * - At cycle boundary: read all reveals, calculate median(R)
 * - Reorg support: erase votes when unwinding blocks
 */
class CKHUDomcDB : public CDBWrapper
{
public:
    explicit CKHUDomcDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

private:
    CKHUDomcDB(const CKHUDomcDB&);
    void operator=(const CKHUDomcDB&);

public:
    // ========================================================================
    // COMMIT operations
    // ========================================================================

    /**
     * WriteCommit - Store DOMC commit vote
     *
     * Key: 'D' + 'C' + mnOutpoint + cycleId
     *
     * @param commit Commit to store
     * @return true on success, false on failure
     */
    bool WriteCommit(const khu_domc::DomcCommit& commit);

    /**
     * ReadCommit - Read DOMC commit vote
     *
     * @param mnOutpoint Masternode collateral outpoint
     * @param cycleId Cycle ID (cycle start height)
     * @param commit Output parameter for commit
     * @return true if commit exists, false otherwise
     */
    bool ReadCommit(const COutPoint& mnOutpoint, uint32_t cycleId,
                    khu_domc::DomcCommit& commit);

    /**
     * HaveCommit - Check if commit exists
     *
     * @param mnOutpoint Masternode collateral outpoint
     * @param cycleId Cycle ID
     * @return true if commit exists
     */
    bool HaveCommit(const COutPoint& mnOutpoint, uint32_t cycleId);

    /**
     * EraseCommit - Delete commit (for reorg)
     *
     * @param mnOutpoint Masternode collateral outpoint
     * @param cycleId Cycle ID
     * @return true on success
     */
    bool EraseCommit(const COutPoint& mnOutpoint, uint32_t cycleId);

    // ========================================================================
    // REVEAL operations
    // ========================================================================

    /**
     * WriteReveal - Store DOMC reveal vote
     *
     * Key: 'D' + 'R' + mnOutpoint + cycleId
     *
     * @param reveal Reveal to store
     * @return true on success, false on failure
     */
    bool WriteReveal(const khu_domc::DomcReveal& reveal);

    /**
     * ReadReveal - Read DOMC reveal vote
     *
     * @param mnOutpoint Masternode collateral outpoint
     * @param cycleId Cycle ID (cycle start height)
     * @param reveal Output parameter for reveal
     * @return true if reveal exists, false otherwise
     */
    bool ReadReveal(const COutPoint& mnOutpoint, uint32_t cycleId,
                    khu_domc::DomcReveal& reveal);

    /**
     * HaveReveal - Check if reveal exists
     *
     * @param mnOutpoint Masternode collateral outpoint
     * @param cycleId Cycle ID
     * @return true if reveal exists
     */
    bool HaveReveal(const COutPoint& mnOutpoint, uint32_t cycleId);

    /**
     * EraseReveal - Delete reveal (for reorg)
     *
     * @param mnOutpoint Masternode collateral outpoint
     * @param cycleId Cycle ID
     * @return true on success
     */
    bool EraseReveal(const COutPoint& mnOutpoint, uint32_t cycleId);

    // ========================================================================
    // CYCLE INDEX operations
    // ========================================================================

    /**
     * AddMasternodeToCycleIndex - Add masternode to cycle index
     *
     * Maintains list of masternodes that voted in a cycle.
     * Used for GetRevealsForCycle() to efficiently collect all reveals.
     *
     * @param cycleId Cycle ID
     * @param mnOutpoint Masternode collateral outpoint
     * @return true on success
     */
    bool AddMasternodeToCycleIndex(uint32_t cycleId, const COutPoint& mnOutpoint);

    /**
     * GetMasternodesForCycle - Get list of masternodes in cycle
     *
     * @param cycleId Cycle ID
     * @param mnOutpoints Output parameter for masternode list
     * @return true if index exists, false otherwise
     */
    bool GetMasternodesForCycle(uint32_t cycleId, std::vector<COutPoint>& mnOutpoints);

    /**
     * GetRevealsForCycle - Collect all valid reveals for a cycle
     *
     * Used by CalculateDomcMedian() to gather R proposals.
     * Returns only reveals that have matching commits.
     *
     * @param cycleId Cycle ID (cycle start height)
     * @param reveals Output parameter for reveal list
     * @return true if any reveals found, false if none
     */
    bool GetRevealsForCycle(uint32_t cycleId, std::vector<khu_domc::DomcReveal>& reveals);

    /**
     * EraseCycleIndex - Delete cycle index (for reorg)
     *
     * @param cycleId Cycle ID
     * @return true on success
     */
    bool EraseCycleIndex(uint32_t cycleId);

    /**
     * EraseCycleData - Delete all data for a cycle (for reorg)
     *
     * Removes all commits, reveals, and index for a given cycle.
     * Called by UndoFinalizeDomcCycle to clean up after reorg.
     *
     * @param cycleId Cycle ID (cycle start height)
     * @return true on success, false on failure
     */
    bool EraseCycleData(uint32_t cycleId);
};

// ============================================================================
// Global accessor functions (following pattern from khu_validation.cpp)
// ============================================================================

/**
 * InitKHUDomcDB - Initialize global DOMC database
 *
 * Called during node initialization.
 *
 * @param nCacheSize Cache size in bytes
 * @param fReindex True if reindexing
 * @return true on success, false on failure
 */
bool InitKHUDomcDB(size_t nCacheSize, bool fReindex);

/**
 * GetKHUDomcDB - Get global DOMC database instance
 *
 * @return Pointer to DOMC DB, or nullptr if not initialized
 */
CKHUDomcDB* GetKHUDomcDB();

#endif // HU_HU_DOMCDB_H
