// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef HU_HU_MINT_H
#define HU_HU_MINT_H

#include "amount.h"
#include "piv2/piv2_state.h"
#include "primitives/transaction.h"
#include "script/standard.h"
#include "serialize.h"

class CCoinsViewCache;
class CValidationState;

/**
 * MINT Payload (PIV → KHU_T)
 *
 * Source: docs/blueprints/03-MINT-REDEEM.md section 2.2
 * Phase: 2
 *
 * RÈGLE FONDAMENTALE: MINT verrouille PIV et crée KHU_T en ratio 1:1
 *                     C += amount, U += amount (atomique)
 */
struct CMintKHUPayload {
    CAmount amount;          //! Montant PIV à verrouiller
    CScript scriptPubKey;    //! Script destinataire KHU_T (Phase 2: simple serialization)

    CMintKHUPayload() : amount(0) {}
    CMintKHUPayload(CAmount amountIn, const CScript& scriptIn)
        : amount(amountIn), scriptPubKey(scriptIn) {}

    SERIALIZE_METHODS(CMintKHUPayload, obj) {
        READWRITE(obj.amount);
        READWRITE(obj.scriptPubKey);
    }

    std::string ToString() const;
};

/**
 * Extract MINT payload from transaction
 *
 * @param[in]  tx       Transaction to extract from
 * @param[out] payload  Extracted payload
 * @return true if extraction successful
 */
bool GetMintKHUPayload(const CTransaction& tx, CMintKHUPayload& payload);

/**
 * Validation MINT (Consensus Rules)
 *
 * Source: docs/blueprints/03-MINT-REDEEM.md section 2.3
 *
 * Checks:
 * 1. TxType == KHU_MINT
 * 2. Payload exists and valid
 * 3. amount > 0
 * 4. Inputs PIV suffisants
 * 5. Output[0] = proof-of-burn (OP_RETURN)
 * 6. Output[1] = KHU_T (amount == payload.amount)
 * 7. Destination valide
 *
 * @param[in] tx     Transaction à valider
 * @param[in] state  Validation state (for error reporting)
 * @param[in] view   UTXO set view
 * @return true if validation passes
 */
bool CheckKHUMint(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& view);

/**
 * Apply MINT to global state (Consensus Critical)
 *
 * Source: docs/blueprints/03-MINT-REDEEM.md section 2.4
 *
 * RÈGLE ATOMIQUE CRITIQUE:
 *   state.C += amount;  // ADJACENT
 *   state.U += amount;  // PAS D'INSTRUCTION ENTRE LES DEUX
 *
 * Side effects:
 * - Augmente C et U (atomique)
 * - Crée UTXO KHU_T dans view
 * - Valide invariants (C==U, Cr==Ur)
 *
 * ⚠️ LOCK: cs_khu MUST be held (AssertLockHeld)
 *
 * @param[in]     tx      Transaction MINT
 * @param[in,out] state   KHU global state (mutated: C+, U+)
 * @param[in,out] view    Coins view (mutated: adds KHU_T UTXO)
 * @param[in]     nHeight Block height
 * @return true if application successful
 */
bool ApplyHUMint(const CTransaction& tx, HuGlobalState& state, CCoinsViewCache& view, uint32_t nHeight);

/**
 * Undo MINT during reorg (Consensus Critical)
 *
 * Reverses ApplyHUMint:
 * - Diminue C et U (atomique)
 * - Supprime UTXO KHU_T
 * - Valide invariants
 *
 * ⚠️ LOCK: cs_khu MUST be held
 *
 * @param[in]     tx      Transaction MINT à annuler
 * @param[in,out] state   KHU global state (mutated: C-, U-)
 * @param[in,out] view    Coins view (mutated: removes KHU_T UTXO)
 * @return true if undo successful
 */
bool UndoKHUMint(const CTransaction& tx, HuGlobalState& state, CCoinsViewCache& view);

#endif // HU_HU_MINT_H
