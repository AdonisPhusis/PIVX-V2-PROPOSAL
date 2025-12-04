// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef HU_HU_UTXO_H
#define HU_HU_UTXO_H

#include "piv2/piv2_coins.h"
#include "primitives/transaction.h"

class CCoinsViewCache;

/**
 * KHU UTXO Tracking Extensions for CCoinsViewCache
 *
 * PHASE 2: MINT/REDEEM operations
 *
 * These functions extend CCoinsViewCache to track KHU_T colored coin UTXOs.
 * KHU_T UTXOs are stored separately from regular PIV UTXOs for isolation.
 *
 * RÈGLES:
 * - KHU_T = colored coin UTXO (structure similaire à Coin)
 * - Namespace LevelDB 'K' + 'U' (isolation)
 * - fLocked = false pour Phase 2 (LOCK/UNLOCK = Phase 3)
 */

/**
 * AddKHUCoin - Add a KHU_T UTXO to the cache
 *
 * Called by ApplyHUMint() to create new KHU_T UTXO.
 *
 * @param view Coins view cache
 * @param outpoint Transaction outpoint (txid + vout)
 * @param coin KHU UTXO to add
 * @return true on success
 */
bool AddKHUCoin(CCoinsViewCache& view, const COutPoint& outpoint, const CKHUUTXO& coin);

/**
 * SpendKHUCoin - Mark a KHU_T UTXO as spent
 *
 * Called by ApplyHURedeem() to consume KHU_T UTXO.
 *
 * @param view Coins view cache
 * @param outpoint Transaction outpoint to spend
 * @return true if coin existed and was spent
 */
bool SpendKHUCoin(CCoinsViewCache& view, const COutPoint& outpoint);

/**
 * GetKHUCoin - Retrieve a KHU_T UTXO from the cache
 *
 * Called by CheckKHURedeem() to validate inputs.
 *
 * @param view Coins view cache
 * @param outpoint Transaction outpoint
 * @param coin Output parameter for KHU UTXO
 * @return true if coin exists
 */
bool GetKHUCoin(const CCoinsViewCache& view, const COutPoint& outpoint, CKHUUTXO& coin);

/**
 * HaveKHUCoin - Check if a KHU_T UTXO exists
 *
 * @param view Coins view cache
 * @param outpoint Transaction outpoint
 * @return true if coin exists and is unspent
 */
bool HaveKHUCoin(const CCoinsViewCache& view, const COutPoint& outpoint);

/**
 * GetKHUCoinFromTracking - Retrieve KHU_T UTXO from global tracking map
 *
 * Used when the CCoinsViewCache may not have the coin (e.g., after
 * standard tx validation spent it, but KHU tracking still has it).
 *
 * @param outpoint Transaction outpoint
 * @param coin Output parameter for KHU UTXO
 * @return true if coin exists and is unspent
 */
bool GetKHUCoinFromTracking(const COutPoint& outpoint, CKHUUTXO& coin);

/**
 * RestoreKHUCoin - Restore a spent KHU UTXO (used during reorg/undo)
 *
 * Called by UndoKHURedeem() to restore a KHU_T UTXO that was spent.
 *
 * @param outpoint Transaction outpoint
 * @param coin KHU UTXO to restore
 * @return true on success
 */
bool RestoreKHUCoin(const COutPoint& outpoint, const CKHUUTXO& coin);

#endif // HU_HU_UTXO_H
