// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef HU_HU_VALIDATION_H
#define HU_HU_VALIDATION_H

#include <memory>

class CBlock;
class CBlockIndex;
class CCoinsViewCache;
class CValidationState;
class CKHUStateDB;
class CHUCommitmentDB;
class CZKHUTreeDB;
struct HuGlobalState;

namespace Consensus {
    struct Params;
}

/**
 * ProcessHUBlock - Process KHU state transitions for a block
 *
 * PHASE 1 IMPLEMENTATION:
 * - Loads previous state
 * - Creates new state with updated height/hash
 * - Validates invariants
 * - Persists state to DB (when fJustCheck=false)
 *
 * FUTURE PHASES (NOT IMPLEMENTED YET):
 * - Phase 2: MINT/REDEEM operations
 * - Phase 3: Daily YIELD application
 * - Phase 4: UNLOCK bonus
 * - Phase 5: DOMC governance
 *
 * @param block The block to process
 * @param pindex Block index
 * @param view Coins view cache
 * @param state Validation state (for errors)
 * @param consensusParams Consensus parameters
 * @param fJustCheck If true, only validate without persisting to DB
 * @return true if KHU processing succeeded
 */
bool ProcessHUBlock(const CBlock& block,
                     CBlockIndex* pindex,
                     CCoinsViewCache& view,
                     CValidationState& state,
                     const Consensus::Params& consensusParams,
                     bool fJustCheck = false);

/**
 * DisconnectKHUBlock - Rollback KHU state during reorg
 *
 * PHASE 1-4 IMPLEMENTATION:
 * - Validate reorg depth (<= 12 blocks)
 * - Iterate transactions in REVERSE order
 * - Call UndoKHULock / UndoKHUUnlock for each tx
 * - Erase state at this height
 * - Previous state remains intact
 *
 * @param block Block to disconnect (needed for tx undo)
 * @param pindex Block index to disconnect
 * @param state Validation state
 * @param view Coins view cache
 * @param huState KHU global state (for undo mutations)
 * @return true if disconnect succeeded
 */
bool DisconnectKHUBlock(const CBlock& block,
                       CBlockIndex* pindex,
                       CValidationState& state,
                       CCoinsViewCache& view,
                       HuGlobalState& huState,
                       const Consensus::Params& consensusParams,
                       bool fJustCheck = false);

/**
 * InitKHUStateDB - Initialize the KHU state database
 *
 * Called during node startup. Creates the database if it doesn't exist.
 *
 * @param nCacheSize DB cache size
 * @param fReindex If true, wipe and recreate DB
 * @return true on success
 */
bool InitKHUStateDB(size_t nCacheSize, bool fReindex);

/**
 * GetKHUStateDB - Get global KHU state database instance
 *
 * @return Pointer to KHU state DB (may be nullptr if not initialized)
 */
CKHUStateDB* GetKHUStateDB();

/**
 * GetCurrentKHUState - Get KHU state at chain tip
 *
 * @param state Output parameter for state
 * @return true if state loaded successfully
 */
bool GetCurrentKHUState(HuGlobalState& state);

/**
 * InitKHUCommitmentDB - Initialize the KHU commitment database
 *
 * PHASE 3: Called during node startup. Creates the commitment DB.
 *
 * @param nCacheSize DB cache size
 * @param fReindex If true, wipe and recreate DB
 * @return true on success
 */
bool InitKHUCommitmentDB(size_t nCacheSize, bool fReindex);

/**
 * GetKHUCommitmentDB - Get global KHU commitment database instance
 *
 * PHASE 3: Returns the commitment DB for state finality operations.
 *
 * @return Pointer to KHU commitment DB (may be nullptr if not initialized)
 */
CHUCommitmentDB* GetKHUCommitmentDB();

/**
 * InitZKHUDB - Initialize the ZKHU database
 *
 * PHASE 4/5: Called during node startup. Creates the ZKHU DB for Sapling notes.
 * Stores: anchors, nullifiers, notes, nullifier→cm mappings.
 *
 * @param nCacheSize DB cache size
 * @param fReindex If true, wipe and recreate DB
 * @return true on success
 */
bool InitZKHUDB(size_t nCacheSize, bool fReindex);

/**
 * GetZKHUDB - Get global ZKHU database instance
 *
 * PHASE 4/5: Returns the ZKHU DB for Sapling operations (LOCK/UNLOCK).
 *
 * @return Pointer to ZKHU DB (may be nullptr if not initialized)
 */
CZKHUTreeDB* GetZKHUDB();

#endif // HU_HU_VALIDATION_H
