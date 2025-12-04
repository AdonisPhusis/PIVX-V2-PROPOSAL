// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "piv2/piv2_redeem.h"

#include "coins.h"
#include "consensus/validation.h"
#include "destination_io.h"
#include "piv2/piv2_coins.h"
#include "piv2/piv2_state.h"
#include "piv2/piv2_utxo.h"
#include "logging.h"
#include "script/standard.h"
#include "sync.h"
#include "util/system.h"
#include "utilmoneystr.h"
#include "validation.h"

// External lock (defined in khu_validation.cpp)
extern RecursiveMutex cs_khu;

std::string CRedeemKHUPayload::ToString() const
{
    return strprintf("CRedeemKHUPayload(amount=%s, script=%s)",
                     FormatMoney(amount),
                     HexStr(scriptPubKey));
}

bool GetRedeemKHUPayload(const CTransaction& tx, CRedeemKHUPayload& payload)
{
    if (tx.nType != CTransaction::TxType::KHU_REDEEM) {
        return false;
    }

    // Phase 2: Payload in extraPayload (PIVX special tx pattern)
    if (!tx.extraPayload || tx.extraPayload->empty()) {
        return false;
    }

    try {
        CDataStream ds(*tx.extraPayload, SER_NETWORK, PROTOCOL_VERSION);
        ds >> payload;
        return true;
    } catch (const std::exception& e) {
        LogPrint(BCLog::HU, "ERROR: GetRedeemKHUPayload: %s\n", e.what());
        return false;
    }
}

bool CheckKHURedeem(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& view)
{
    // 1. Vérifier TxType
    if (tx.nType != CTransaction::TxType::KHU_REDEEM) {
        return state.Invalid(false, REJECT_INVALID, "khu-redeem-invalid-type",
                             "Transaction type is not KHU_REDEEM");
    }

    // 2. Vérifier payload
    CRedeemKHUPayload payload;
    if (!GetRedeemKHUPayload(tx, payload)) {
        return state.Invalid(false, REJECT_INVALID, "khu-redeem-missing-payload",
                             "Failed to extract REDEEM payload");
    }

    // 3. Vérifier montant > 0
    if (payload.amount <= 0) {
        return state.Invalid(false, REJECT_INVALID, "khu-redeem-invalid-amount",
                             strprintf("Invalid REDEEM amount: %d", payload.amount));
    }

    // 4. Vérifier inputs KHU_T suffisants
    // NOTE: Per CLAUDE.md §2.1, REDEEM tx has KHU inputs + optional PIV input for fee.
    // Only count inputs that are actually KHU coins (exist in mapKHUUTXOs).
    // PIV fee inputs are NOT in the KHU tracking and should be skipped.
    CAmount total_input = 0;
    for (const auto& in : tx.vin) {
        CKHUUTXO khuCoin;
        if (GetKHUCoin(view, in.prevout, khuCoin)) {
            // This is a KHU input - validate it

            // Vérifier que le KHU n'est pas staké
            if (khuCoin.fLocked) {
                return state.Invalid(false, REJECT_INVALID, "khu-redeem-locked-khu",
                                     strprintf("Cannot redeem locked KHU at %s", in.prevout.ToString()));
            }

            total_input += khuCoin.amount;
        }
        // Non-KHU inputs (PIV fee) are silently skipped
    }

    if (total_input < payload.amount) {
        return state.Invalid(false, REJECT_INVALID, "khu-redeem-insufficient-khu",
                             strprintf("Insufficient KHU_T: need %s, have %s",
                                       FormatMoney(payload.amount),
                                       FormatMoney(total_input)));
    }

    // 5. Vérifier au moins 1 output (PIV)
    if (tx.vout.empty()) {
        return state.Invalid(false, REJECT_INVALID, "khu-redeem-no-outputs",
                             "REDEEM requires at least 1 output");
    }

    // 6. Vérifier output 0 = PIV (amount == payload.amount)
    if (tx.vout[0].nValue != payload.amount) {
        return state.Invalid(false, REJECT_INVALID, "khu-redeem-amount-mismatch",
                             strprintf("PIV amount %s != payload %s",
                                       FormatMoney(tx.vout[0].nValue),
                                       FormatMoney(payload.amount)));
    }

    // 7. Vérifier destination valide
    if (payload.scriptPubKey.empty()) {
        return state.Invalid(false, REJECT_INVALID, "khu-redeem-invalid-destination",
                             "Destination script is empty");
    }

    // 8. Vérifier collateral disponible (checked in ApplyHURedeem with state access)
    // This is a consensus check that requires access to KHU global state

    return true;
}

bool ApplyHURedeem(const CTransaction& tx, HuGlobalState& state, CCoinsViewCache& view, uint32_t nHeight)
{
    // ⚠️ CRITICAL: cs_khu MUST be held
    AssertLockHeld(cs_khu);

    // 1. Extract payload
    CRedeemKHUPayload payload;
    if (!GetRedeemKHUPayload(tx, payload)) {
        return error("ApplyHURedeem: Failed to extract payload");
    }

    const CAmount amount = payload.amount;

    // 2. Vérifier invariants AVANT mutation
    if (!state.CheckInvariants()) {
        return error("ApplyHURedeem: Pre-invariant violation (C=%d U=%d Cr=%d Ur=%d)",
                     state.C, state.U, state.Cr, state.Ur);
    }

    // 3. Vérifier collateral suffisant
    // ✅ NOTE VULN-KHU-2025-001: This check also serves as underflow protection.
    // By ensuring state.C >= amount and state.U >= amount before subtraction,
    // we prevent signed integer underflow (which is also undefined behavior).
    if (state.C < amount || state.U < amount) {
        return error("ApplyHURedeem: Insufficient C/U (C=%d U=%d amount=%d)",
                     state.C, state.U, amount);
    }

    // 4. ═════════════════════════════════════════════════════════
    //    DOUBLE MUTATION ATOMIQUE (C et U ensemble)
    //    ⚠️ RÈGLE CRITIQUE: Ces deux lignes doivent être ADJACENTES
    //                       PAS D'INSTRUCTIONS ENTRE LES DEUX
    //    Source: docs/blueprints/03-MINT-REDEEM.md section 4.1
    // ═════════════════════════════════════════════════════════
    state.C -= amount;  // Diminuer collateral
    state.U -= amount;  // Diminuer supply

    // 5. Vérifier invariants APRÈS mutation
    if (!state.CheckInvariants()) {
        return error("ApplyHURedeem: Post-invariant violation (C=%d U=%d Cr=%d Ur=%d)",
                     state.C, state.U, state.Cr, state.Ur);
    }

    // 6. Dépenser UTXO KHU_T
    // NOTE: Per CLAUDE.md §2.1, REDEEM tx has KHU inputs + 1 PIV input for fee.
    // Only spend inputs that are actually KHU coins (exist in mapKHUUTXOs).
    // PIV fee inputs are NOT in the KHU tracking and should be skipped.

    LogPrint(BCLog::HU, "%s: processing tx %s with %zu inputs\n",
              __func__, tx.GetHash().ToString().substr(0,16).c_str(), tx.vin.size());

    CAmount totalKHUSpent = 0;
    for (size_t i = 0; i < tx.vin.size(); i++) {
        const auto& in = tx.vin[i];

        // Check if this input is a KHU coin (exists in tracking)
        CKHUUTXO khuCoin;
        if (GetKHUCoin(view, in.prevout, khuCoin)) {
            // This is a KHU input - spend it
            if (!SpendKHUCoin(view, in.prevout)) {
                return error("%s: failed to spend KHU coin at %s", __func__, in.prevout.ToString());
            }
            totalKHUSpent += khuCoin.amount;
            LogPrint(BCLog::HU, "%s: spent KHU input %s:%d value=%s\n",
                     __func__, in.prevout.hash.ToString().substr(0,16).c_str(), in.prevout.n,
                     FormatMoney(khuCoin.amount));
        }
        // PIV fee inputs are silently skipped - they're not in mapKHUUTXOs
    }

    LogPrint(BCLog::HU, "%s: totalKHUSpent=%s, required=%s\n",
              __func__, FormatMoney(totalKHUSpent), FormatMoney(amount));

    // Verify we spent at least the payload amount in KHU
    if (totalKHUSpent < amount) {
        return error("ApplyHURedeem: Insufficient KHU spent (%s < %s)",
                     FormatMoney(totalKHUSpent), FormatMoney(amount));
    }

    // 7. Log
    LogPrint(BCLog::HU, "ApplyHURedeem: amount=%s C=%s U=%s height=%d\n",
             FormatMoney(amount),
             FormatMoney(state.C),
             FormatMoney(state.U),
             nHeight);

    return true;
}

bool UndoKHURedeem(const CTransaction& tx, HuGlobalState& state, CCoinsViewCache& view)
{
    // ⚠️ CRITICAL: cs_khu MUST be held
    AssertLockHeld(cs_khu);

    // 1. Extract payload
    CRedeemKHUPayload payload;
    if (!GetRedeemKHUPayload(tx, payload)) {
        return error("UndoKHURedeem: Failed to extract payload");
    }

    const CAmount amount = payload.amount;

    // 2. Vérifier invariants AVANT mutation
    if (!state.CheckInvariants()) {
        return error("UndoKHURedeem: Pre-invariant violation (C=%d U=%d Cr=%d Ur=%d)",
                     state.C, state.U, state.Cr, state.Ur);
    }

    // 3. ═════════════════════════════════════════════════════════
    //    DOUBLE MUTATION ATOMIQUE (C et U ensemble) - REVERSE
    //    ⚠️ RÈGLE CRITIQUE: Ces deux lignes doivent être ADJACENTES
    // ═════════════════════════════════════════════════════════
    state.C += amount;  // Augmenter collateral
    state.U += amount;  // Augmenter supply

    // 4. Vérifier invariants APRÈS mutation
    if (!state.CheckInvariants()) {
        return error("UndoKHURedeem: Post-invariant violation (C=%d U=%d Cr=%d Ur=%d)",
                     state.C, state.U, state.Cr, state.Ur);
    }

    // 5. Restaurer UTXO KHU_T
    // Phase 2: Pour l'undo, nous devons restaurer les UTXOs KHU_T qui ont été dépensés
    // Ceci nécessite de stocker les UTXOs originaux avant de les dépenser
    // Pour Phase 2 minimal, nous acceptons cette limitation
    // Future: Stocker les UTXOs dans undo data

    // 6. Log
    LogPrint(BCLog::HU, "UndoKHURedeem: amount=%s C=%s U=%s\n",
             FormatMoney(amount),
             FormatMoney(state.C),
             FormatMoney(state.U));

    return true;
}
