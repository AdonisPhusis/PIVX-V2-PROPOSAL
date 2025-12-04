// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef HU_HU_STATEDB_H
#define HU_HU_STATEDB_H

#include "dbwrapper.h"
#include "piv2/piv2_state.h"
#include "piv2/piv2_coins.h"
#include "primitives/transaction.h"

#include <stdint.h>
#include <vector>

/**
 * CKHUStateDB - LevelDB persistence layer for KHU global state
 *
 * Database keys:
 * - 'K' + 'S' + height -> HuGlobalState
 *
 * The database stores KHU state snapshots at each block height.
 * This allows for efficient state retrieval and reorg handling.
 */
class CKHUStateDB : public CDBWrapper
{
public:
    explicit CKHUStateDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

private:
    CKHUStateDB(const CKHUStateDB&);
    void operator=(const CKHUStateDB&);

public:
    /**
     * WriteKHUState - Persist KHU state for a given height
     *
     * @param nHeight Block height
     * @param state KHU state to write
     * @return true on success, false on failure
     */
    bool WriteKHUState(int nHeight, const HuGlobalState& state);

    /**
     * ReadKHUState - Read KHU state at a given height
     *
     * @param nHeight Block height
     * @param state Output parameter for state
     * @return true if state exists, false otherwise
     */
    bool ReadKHUState(int nHeight, HuGlobalState& state);

    /**
     * ExistsKHUState - Check if state exists at height
     *
     * @param nHeight Block height
     * @return true if state exists
     */
    bool ExistsKHUState(int nHeight);

    /**
     * EraseKHUState - Delete state at height (used during reorg)
     *
     * @param nHeight Block height
     * @return true on success
     */
    bool EraseKHUState(int nHeight);

    /**
     * LoadKHUState_OrGenesis - Load state or return genesis state
     *
     * If state doesn't exist at height, returns genesis state (all zeros).
     * This is used during activation of KHU upgrade.
     *
     * @param nHeight Block height
     * @return KHU state (existing or genesis)
     */
    HuGlobalState LoadKHUState_OrGenesis(int nHeight);

    // ═══════════════════════════════════════════════════════════════════════
    // KHU UTXO Persistence (Phase 2)
    // Database keys: 'K' + 'U' + outpoint -> CKHUUTXO
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * WriteKHUUTXO - Persist a KHU UTXO
     *
     * @param outpoint Transaction outpoint (txid + vout)
     * @param utxo KHU UTXO to write
     * @return true on success
     */
    bool WriteKHUUTXO(const COutPoint& outpoint, const CKHUUTXO& utxo);

    /**
     * ReadKHUUTXO - Read a KHU UTXO
     *
     * @param outpoint Transaction outpoint
     * @param utxo Output parameter for UTXO
     * @return true if UTXO exists
     */
    bool ReadKHUUTXO(const COutPoint& outpoint, CKHUUTXO& utxo);

    /**
     * EraseKHUUTXO - Delete a KHU UTXO (when spent)
     *
     * @param outpoint Transaction outpoint
     * @return true on success
     */
    bool EraseKHUUTXO(const COutPoint& outpoint);

    /**
     * ExistsKHUUTXO - Check if UTXO exists
     *
     * @param outpoint Transaction outpoint
     * @return true if UTXO exists
     */
    bool ExistsKHUUTXO(const COutPoint& outpoint);

    /**
     * LoadAllKHUUTXOs - Load all UTXOs into memory cache
     *
     * Called at startup to populate the in-memory cache.
     *
     * @param utxos Output vector for all UTXOs
     * @return true on success
     */
    bool LoadAllKHUUTXOs(std::vector<std::pair<COutPoint, CKHUUTXO>>& utxos);
};

#endif // HU_HU_STATEDB_H
