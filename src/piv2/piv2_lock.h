// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef HU_HU_LOCK_H
#define HU_HU_LOCK_H

#include "amount.h"
#include "piv2/piv2_state.h"
#include "primitives/transaction.h"

class CCoinsViewCache;
class CValidationState;
namespace Consensus { struct Params; }

/**
 * Minimum lock amount (anti-spam protection)
 *
 * Prevents economic spam attacks with micro-locks.
 * 1 PIV minimum ensures that transaction fees make spam uneconomical.
 *
 * Economic rationale:
 * - Yield for 1 PIV @ 40% for 3 days = 1 × 0.40 × 3/365 = 0.00329 PIV
 * - Transaction fees (lock + unlock) = ~0.0002 PIV
 * - Net positive, but too small to abuse at scale
 *
 * Without minimum:
 * - 0.01 PIV lock yield = 0.0000329 PIV (less than fees)
 * - Spam creates DB bloat without economic benefit
 */
static constexpr CAmount MIN_LOCK_AMOUNT = 1 * COIN;  // 1 PIV minimum

/**
 * LOCK (KHU_T → ZKHU)
 *
 * Source: docs/blueprints/05-ZKHU-SAPLING-LOCK.md section 2
 * Phase: 4 (ZKHU Staking)
 *
 * RÈGLE FONDAMENTALE: LOCK transforme KHU_T en ZKHU (Sapling note)
 *                     État global INCHANGÉ (C, U, Cr, Ur constants)
 *                     Seul le "form" change: transparent → shielded
 */

/**
 * CheckKHULock - Validation LOCK (Consensus Rules)
 *
 * Checks obligatoires :
 * 1. tx.nType == TxType::KHU_LOCK
 * 2. Input KHU_T UTXO existe, amount > 0
 * 3. Input KHU_T non verrouillé (fLocked == false)
 * 4. Sapling output présent (1 note ZKHU)
 * 5. Memo 512 bytes → ZKHUMemo::Deserialize OK, magic == "ZKHU"
 * 6. nLockStartHeight cohérent
 * 7. Aucune modification de C/U/Cr/Ur
 *
 * @param[in] tx         Transaction à valider
 * @param[in] view       UTXO set view
 * @param[in] state      Validation state (for error reporting)
 * @param[in] consensus  Consensus parameters
 * @return true if validation passes
 */
bool CheckKHULock(
    const CTransaction& tx,
    const CCoinsViewCache& view,
    CValidationState& state,
    const Consensus::Params& consensus);

/**
 * ApplyHULock - Application LOCK (Consensus Critical)
 *
 * Implémentation (logique) :
 * 1. SpendKHUCoin(view, prevout)
 * 2. Extraire la note ZKHU (commitment + nullifier)
 * 3. Construire ZKHUNoteData (Ur_accumulated = 0 Phase 4)
 * 4. WriteNote(cm, noteData) dans DB ZKHU
 * 5. zkhuTree.append(cm) + WriteAnchor(root, zkhuTree)
 * 6. Ne pas toucher à state.C/U/Cr/Ur
 *
 * @param[in]     tx      Transaction LOCK
 * @param[in,out] view    Coins view (mutated: spends KHU_T)
 * @param[in,out] state   KHU global state (unchanged: C, U, Cr, Ur)
 * @param[in]     nHeight Block height
 * @return true if application successful
 */
bool ApplyHULock(
    const CTransaction& tx,
    CCoinsViewCache& view,
    HuGlobalState& state,
    int nHeight);

/**
 * UndoKHULock - Undo LOCK during reorg (Consensus Critical)
 *
 * Reverses ApplyHULock:
 * 1. Recréer l'UTXO KHU_T initial (AddKHUCoin)
 * 2. Supprimer la note ZKHU (EraseNote)
 * 3. Ne pas toucher à C/U/Cr/Ur
 *
 * @param[in]     tx      Transaction LOCK à annuler
 * @param[in,out] view    Coins view (mutated: restores KHU_T)
 * @param[in,out] state   KHU global state (unchanged)
 * @return true if undo successful
 */
bool UndoKHULock(
    const CTransaction& tx,
    CCoinsViewCache& view,
    HuGlobalState& state,
    int nHeight);

#endif // HU_HU_LOCK_H
