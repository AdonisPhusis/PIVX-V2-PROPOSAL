// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "piv2/piv2_mint.h"

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

std::string CMintKHUPayload::ToString() const
{
    return strprintf("CMintKHUPayload(amount=%s, script=%s)",
                     FormatMoney(amount),
                     HexStr(scriptPubKey));
}

bool GetMintKHUPayload(const CTransaction& tx, CMintKHUPayload& payload)
{
    if (tx.nType != CTransaction::TxType::KHU_MINT) {
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
        LogPrint(BCLog::HU, "ERROR: GetMintKHUPayload: %s\n", e.what());
        return false;
    }
}

bool CheckKHUMint(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& view)
{
    // 1. Vérifier TxType
    if (tx.nType != CTransaction::TxType::KHU_MINT) {
        return state.Invalid(false, REJECT_INVALID, "khu-mint-invalid-type",
                             "Transaction type is not KHU_MINT");
    }

    // 2. Vérifier payload
    CMintKHUPayload payload;
    if (!GetMintKHUPayload(tx, payload)) {
        return state.Invalid(false, REJECT_INVALID, "khu-mint-missing-payload",
                             "Failed to extract MINT payload");
    }

    // 3. Vérifier montant > 0
    if (payload.amount <= 0) {
        return state.Invalid(false, REJECT_INVALID, "khu-mint-invalid-amount",
                             strprintf("Invalid MINT amount: %d", payload.amount));
    }

    // 4. Vérifier inputs PIV suffisants
    CAmount total_input = 0;
    for (const auto& in : tx.vin) {
        const Coin& coin = view.AccessCoin(in.prevout);
        if (coin.IsSpent()) {
            return state.Invalid(false, REJECT_INVALID, "khu-mint-missing-input",
                                 strprintf("Input not found: %s", in.prevout.ToString()));
        }
        total_input += coin.out.nValue;
    }

    if (total_input < payload.amount) {
        return state.Invalid(false, REJECT_INVALID, "khu-mint-insufficient-funds",
                             strprintf("Insufficient PIV: need %s, have %s",
                                       FormatMoney(payload.amount),
                                       FormatMoney(total_input)));
    }

    // 5. Vérifier au moins 2 outputs (proof-of-burn + KHU_T)
    if (tx.vout.size() < 2) {
        return state.Invalid(false, REJECT_INVALID, "khu-mint-invalid-outputs",
                             "MINT requires at least 2 outputs");
    }

    // 6. Vérifier output 0 = proof-of-burn (OP_RETURN)
    if (!tx.vout[0].scriptPubKey.IsUnspendable()) {
        return state.Invalid(false, REJECT_INVALID, "khu-mint-invalid-burn",
                             "Output 0 must be OP_RETURN (proof-of-burn)");
    }

    // 7. Vérifier montant output 0 == payload.amount (PIV verrouillé)
    if (tx.vout[0].nValue != payload.amount) {
        return state.Invalid(false, REJECT_INVALID, "khu-mint-burn-mismatch",
                             strprintf("Burn amount %s != payload %s",
                                       FormatMoney(tx.vout[0].nValue),
                                       FormatMoney(payload.amount)));
    }

    // 8. Vérifier output 1 = KHU_T (amount == payload.amount)
    if (tx.vout[1].nValue != payload.amount) {
        return state.Invalid(false, REJECT_INVALID, "khu-mint-amount-mismatch",
                             strprintf("KHU_T amount %s != payload %s",
                                       FormatMoney(tx.vout[1].nValue),
                                       FormatMoney(payload.amount)));
    }

    // 9. Vérifier destination valide
    if (payload.scriptPubKey.empty()) {
        return state.Invalid(false, REJECT_INVALID, "khu-mint-invalid-destination",
                             "Destination script is empty");
    }

    return true;
}

bool ApplyHUMint(const CTransaction& tx, HuGlobalState& state, CCoinsViewCache& view, uint32_t nHeight)
{
    // ⚠️ CRITICAL: cs_khu MUST be held
    AssertLockHeld(cs_khu);

    // 1. Extract payload
    CMintKHUPayload payload;
    if (!GetMintKHUPayload(tx, payload)) {
        return error("ApplyHUMint: Failed to extract payload");
    }

    const CAmount amount = payload.amount;

    // 2. Vérifier invariants AVANT mutation
    if (!state.CheckInvariants()) {
        return error("ApplyHUMint: Pre-invariant violation (C=%d U=%d Cr=%d Ur=%d)",
                     state.C, state.U, state.Cr, state.Ur);
    }

    // 3. ═════════════════════════════════════════════════════════
    //    DOUBLE MUTATION ATOMIQUE (C et U ensemble)
    //    ⚠️ RÈGLE CRITIQUE: Ces deux lignes doivent être ADJACENTES
    //                       PAS D'INSTRUCTIONS ENTRE LES DEUX
    //    Source: docs/blueprints/03-MINT-REDEEM.md section 4.1
    // ═════════════════════════════════════════════════════════

    // ✅ FIX VULN-KHU-2025-001: Vérifier overflow AVANT mutation
    // CRITICAL: Signed integer overflow in C++ is undefined behavior (UB).
    // Without this check, overflow could lead to unpredictable results where
    // C and U might end up with different values, breaking the C==U invariant.
    if (state.C > (std::numeric_limits<CAmount>::max() - amount)) {
        return error("ApplyHUMint: Overflow would occur on C (C=%d amount=%d)",
                     state.C, amount);
    }
    if (state.U > (std::numeric_limits<CAmount>::max() - amount)) {
        return error("ApplyHUMint: Overflow would occur on U (U=%d amount=%d)",
                     state.U, amount);
    }

    state.C += amount;  // Augmenter collateral - Safe: overflow checked above
    state.U += amount;  // Augmenter supply - Safe: overflow checked above

    // 4. Vérifier invariants APRÈS mutation
    if (!state.CheckInvariants()) {
        return error("ApplyHUMint: Post-invariant violation (C=%d U=%d Cr=%d Ur=%d)",
                     state.C, state.U, state.Cr, state.Ur);
    }

    // 5. Créer UTXO KHU_T
    CKHUUTXO newCoin(amount, payload.scriptPubKey, nHeight);
    newCoin.fIsKHU = true;
    newCoin.fLocked = false;
    newCoin.nLockStartHeight = 0;

    COutPoint khuOutpoint(tx.GetHash(), 1);  // Output index 1 = KHU_T

    if (!AddKHUCoin(view, khuOutpoint, newCoin)) {
        return error("%s: failed to add KHU coin", __func__);
    }

    LogPrint(BCLog::HU, "%s: added KHU coin %s:%d value=%s (C=%s, U=%s)\n",
             __func__, khuOutpoint.hash.ToString().substr(0,16).c_str(), khuOutpoint.n,
             FormatMoney(amount), FormatMoney(state.C), FormatMoney(state.U));

    return true;
}

bool UndoKHUMint(const CTransaction& tx, HuGlobalState& state, CCoinsViewCache& view)
{
    // ⚠️ CRITICAL: cs_khu MUST be held
    AssertLockHeld(cs_khu);

    // 1. Extract payload
    CMintKHUPayload payload;
    if (!GetMintKHUPayload(tx, payload)) {
        return error("UndoKHUMint: Failed to extract payload");
    }

    const CAmount amount = payload.amount;

    // 2. Vérifier invariants AVANT mutation
    if (!state.CheckInvariants()) {
        return error("UndoKHUMint: Pre-invariant violation (C=%d U=%d Cr=%d Ur=%d)",
                     state.C, state.U, state.Cr, state.Ur);
    }

    // 3. Vérifier montants suffisants
    if (state.C < amount || state.U < amount) {
        return error("UndoKHUMint: Insufficient C/U (C=%d U=%d amount=%d)",
                     state.C, state.U, amount);
    }

    // 4. ═════════════════════════════════════════════════════════
    //    DOUBLE MUTATION ATOMIQUE (C et U ensemble) - REVERSE
    //    ⚠️ RÈGLE CRITIQUE: Ces deux lignes doivent être ADJACENTES
    // ═════════════════════════════════════════════════════════
    state.C -= amount;  // Diminuer collateral
    state.U -= amount;  // Diminuer supply

    // 5. Vérifier invariants APRÈS mutation
    if (!state.CheckInvariants()) {
        return error("UndoKHUMint: Post-invariant violation (C=%d U=%d Cr=%d Ur=%d)",
                     state.C, state.U, state.Cr, state.Ur);
    }

    // 6. Supprimer UTXO KHU_T
    COutPoint khuOutpoint(tx.GetHash(), 1);  // Output index 1 = KHU_T
    if (!SpendKHUCoin(view, khuOutpoint)) {
        return error("UndoKHUMint: Failed to spend KHU coin");
    }

    // 7. Log
    LogPrint(BCLog::HU, "UndoKHUMint: amount=%s C=%s U=%s\n",
             FormatMoney(amount),
             FormatMoney(state.C),
             FormatMoney(state.U));

    return true;
}
