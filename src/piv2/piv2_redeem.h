// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef HU_HU_REDEEM_H
#define HU_HU_REDEEM_H

#include "amount.h"
#include "piv2/piv2_state.h"
#include "primitives/transaction.h"
#include "script/standard.h"
#include "serialize.h"

class CCoinsViewCache;
class CValidationState;

/**
 * REDEEM Payload (KHU_T → PIV)
 *
 * Source: docs/blueprints/03-MINT-REDEEM.md section 3.2
 * Phase: 2
 *
 * RÈGLE FONDAMENTALE: REDEEM détruit KHU_T et libère PIV en ratio 1:1
 *                     C -= amount, U -= amount (atomique)
 */
struct CRedeemKHUPayload {
    CAmount amount;          //! Montant KHU_T à détruire
    CScript scriptPubKey;    //! Script destinataire PIV (Phase 2: simple serialization)

    CRedeemKHUPayload() : amount(0) {}
    CRedeemKHUPayload(CAmount amountIn, const CScript& scriptIn)
        : amount(amountIn), scriptPubKey(scriptIn) {}

    SERIALIZE_METHODS(CRedeemKHUPayload, obj) {
        READWRITE(obj.amount);
        READWRITE(obj.scriptPubKey);
    }

    std::string ToString() const;
};

/**
 * Extract REDEEM payload from transaction
 *
 * @param[in]  tx       Transaction to extract from
 * @param[out] payload  Extracted payload
 * @return true if extraction successful
 */
bool GetRedeemKHUPayload(const CTransaction& tx, CRedeemKHUPayload& payload);

/**
 * Validation REDEEM (Consensus Rules)
 *
 * Source: docs/blueprints/03-MINT-REDEEM.md section 3.3
 *
 * Checks:
 * 1. TxType == KHU_REDEEM
 * 2. Payload exists and valid
 * 3. amount > 0
 * 4. Inputs KHU_T suffisants
 * 5. KHU_T pas verrouillé (fLocked = false)
 * 6. Output[0] = PIV (amount == payload.amount)
 * 7. Destination valide
 * 8. Collateral disponible (state.C >= amount)
 *
 * @param[in] tx     Transaction à valider
 * @param[in] state  Validation state (for error reporting)
 * @param[in] view   UTXO set view
 * @return true if validation passes
 */
bool CheckKHURedeem(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& view);

/**
 * Apply REDEEM to global state (Consensus Critical)
 *
 * Source: docs/blueprints/03-MINT-REDEEM.md section 3.4
 *
 * RÈGLE ATOMIQUE CRITIQUE:
 *   state.C -= amount;  // ADJACENT
 *   state.U -= amount;  // PAS D'INSTRUCTION ENTRE LES DEUX
 *
 * Side effects:
 * - Diminue C et U (atomique)
 * - Détruit UTXO KHU_T dans view
 * - Crée UTXO PIV
 * - Valide invariants (C==U, Cr==Ur)
 *
 * ⚠️ LOCK: cs_khu MUST be held (AssertLockHeld)
 *
 * @param[in]     tx      Transaction REDEEM
 * @param[in,out] state   KHU global state (mutated: C-, U-)
 * @param[in,out] view    Coins view (mutated: spends KHU_T, creates PIV)
 * @param[in]     nHeight Block height
 * @return true if application successful
 */
bool ApplyHURedeem(const CTransaction& tx, HuGlobalState& state, CCoinsViewCache& view, uint32_t nHeight);

/**
 * Undo REDEEM during reorg (Consensus Critical)
 *
 * Reverses ApplyHURedeem:
 * - Augmente C et U (atomique)
 * - Restaure UTXO KHU_T
 * - Supprime UTXO PIV
 * - Valide invariants
 *
 * ⚠️ LOCK: cs_khu MUST be held
 *
 * @param[in]     tx      Transaction REDEEM à annuler
 * @param[in,out] state   KHU global state (mutated: C+, U+)
 * @param[in,out] view    Coins view (mutated: restores KHU_T, removes PIV)
 * @return true if undo successful
 */
bool UndoKHURedeem(const CTransaction& tx, HuGlobalState& state, CCoinsViewCache& view);

#endif // HU_HU_REDEEM_H
