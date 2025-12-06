// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * PIVX V2 Wallet RPC Commands
 *
 * Wallet-dependent RPC commands for PIVX V2 Chain operations.
 * These are registered via RegisterHUWalletRPCCommands.
 *
 * Nomenclature:
 *   HU   - Native transparent coin
 *   sHU  - Shielded HU (Sapling Z-to-Z privacy)
 *   KHU  - Locked HU (preparation for staking)
 *   ZKHU - Staked KHU (shielded + yield accumulation)
 *
 * Operations:
 *   mint/redeem  - HU <-> KHU conversion
 *   lock/unlock  - KHU <-> ZKHU staking with yield
 *   shield/unshield - HU <-> sHU privacy
 */

#include "chain.h"
#include "chainparams.h"
#include "key_io.h"
#include "net.h"  // for g_connman (tx relay)
#include "piv2/piv2_mint.h"
#include "piv2/piv2_redeem.h"
#include "piv2/piv2_lock.h"   // For MIN_LOCK_AMOUNT
#include "piv2/piv2_state.h"
#include "piv2/piv2_unlock.h"
#include "piv2/piv2_validation.h"
#include "piv2/zkpiv2_db.h"
#include "piv2/zkpiv2_memo.h"
#include "streams.h"
#include "primitives/transaction.h"
#include "rpc/server.h"
#include "sapling/key_io_sapling.h"
#include "sapling/saplingscriptpubkeyman.h"
#include "sapling/transaction_builder.h"
#include "script/sign.h"
#include "sync.h"
#include "txmempool.h"
#include "utilmoneystr.h"
#include "util/validation.h"
#include "validation.h"
#include "wallet/fees.h"
#include "wallet/piv2_wallet.h"
#include "wallet/rpcwallet.h"
#include "wallet/wallet.h"

#include <univalue.h>

/**
 * ComputeWitnessForZKHUNote - Compute witness by scanning blockchain (fallback)
 *
 * When the wallet's witness cache is incomplete (e.g., after fast block generation
 * or wallet restart), this function rebuilds the witness from scratch by scanning
 * the blockchain and building the Sapling merkle tree.
 *
 * @param targetCm Note commitment to find
 * @param targetHeight Block height where the note was created
 * @param witnessOut Output: the computed witness
 * @param anchorOut Output: the tree root (anchor)
 * @return true if witness was successfully computed
 */
static bool ComputeWitnessForZKHUNote(
    const uint256& targetCm,
    int targetHeight,
    SaplingWitness& witnessOut,
    uint256& anchorOut)
{
    LOCK(cs_main);

    // Build Sapling merkle tree from genesis up to current tip
    SaplingMerkleTree saplingTree;
    bool foundNote = false;
    int notePosition = -1;

    // Scan blocks from beginning to find our note and build the tree
    for (int height = 1; height <= chainActive.Height(); height++) {
        CBlockIndex* pindex = chainActive[height];
        if (!pindex) continue;

        CBlock block;
        if (!ReadBlockFromDisk(block, pindex)) {
            LogPrintf("ComputeWitnessForZKHUNote: Failed to read block at height %d\n", height);
            return false;
        }

        // Process each transaction's Sapling outputs
        for (const auto& tx : block.vtx) {
            if (!tx->IsShieldedTx() || !tx->sapData) continue;

            for (size_t i = 0; i < tx->sapData->vShieldedOutput.size(); i++) {
                const uint256& cmu = tx->sapData->vShieldedOutput[i].cmu;

                if (cmu == targetCm && !foundNote) {
                    // This is our note - capture witness at this point
                    saplingTree.append(cmu);
                    witnessOut = saplingTree.witness();
                    foundNote = true;
                    notePosition = saplingTree.size() - 1;
                    LogPrint(BCLog::HU, "ComputeWitnessForZKHUNote: Found note at height %d, position %d\n",
                             height, notePosition);
                } else if (foundNote) {
                    // Note already found - append to update witness
                    saplingTree.append(cmu);
                    witnessOut.append(cmu);
                } else {
                    // Haven't found our note yet - just build tree
                    saplingTree.append(cmu);
                }
            }
        }
    }

    if (!foundNote) {
        LogPrintf("ComputeWitnessForZKHUNote: Note commitment not found in blockchain\n");
        return false;
    }

    anchorOut = witnessOut.root();
    LogPrint(BCLog::HU, "ComputeWitnessForZKHUNote: Success - anchor=%s\n",
             anchorOut.GetHex().substr(0, 16).c_str());

    return true;
}

/**
 * hubalance - Get PIVX V2 wallet balance
 *
 * Nomenclature:
 * - HU: transparent native coin
 * - KHU: locked HU (preparation for staking)
 * - ZKHU: staked KHU (with yield)
 */
static UniValue khubalance(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(
            "piv2balance\n"
            "\nReturns the PIVX V2 chain balance breakdown for this wallet.\n"
            "\nResult:\n"
            "{\n"
            "  \"hu\": {                    (object) PIVX V2 transparent balance\n"
            "    \"available\": n,          (numeric) PIVX V2 available for transactions\n"
            "    \"immature\": n,           (numeric) PIVX V2 immature (coinbase)\n"
            "    \"locked\": n              (numeric) PIVX V2 locked (collateral)\n"
            "  },\n"
            "  \"khu\": {                   (object) KHU locked balance\n"
            "    \"transparent\": n,        (numeric) KHU balance (locked, not yet staking)\n"
            "    \"zkhu\": n,               (numeric) ZKHU staking balance\n"
            "    \"pending_yield_estimated\": n,  (numeric) Estimated pending yield\n"
            "    \"total\": n,              (numeric) Total KHU + ZKHU balance\n"
            "    \"utxo_count\": n,         (numeric) Number of KHU UTXOs\n"
            "    \"note_count\": n          (numeric) Number of ZKHU notes\n"
            "  }\n"
            "}\n"
            "\nNote: pending_yield_estimated is an APPROXIMATION for display purposes.\n"
            "PIVX V2 'available' is used to pay transaction fees for all operations.\n"
            "\nExamples:\n"
            + HelpExampleCli("piv2balance", "")
            + HelpExampleRpc("piv2balance", "")
        );
    }

    CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) {
        throw JSONRPCError(RPC_WALLET_NOT_FOUND, "Wallet not found");
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    // KHU balances
    CAmount nTransparent = GetKHUBalance(pwallet);
    CAmount nLocked = GetKHULockedBalance(pwallet);

    HuGlobalState state;
    uint16_t R_annual = 0;
    if (GetCurrentKHUState(state)) {
        R_annual = state.R_annual;
    }

    CAmount nPendingYield = GetKHUPendingYieldEstimate(pwallet, R_annual);

    // PIV2 balances (base coin, for fees)
    // Note: GetAvailableBalance() now correctly excludes KHU_MINT outputs (colored coins)
    CAmount nPIV2Available = pwallet->GetAvailableBalance();
    CAmount nPIV2Immature = pwallet->GetImmatureBalance();
    CAmount nPIV2Locked = pwallet->GetLockedCoins();  // MN collateral locks

    // Total wallet value = PIV2 + KHU_T + ZKHU
    CAmount nTotalWallet = nPIV2Available + nTransparent + nLocked;

    // Build KHU object
    UniValue khuObj(UniValue::VOBJ);
    khuObj.pushKV("transparent", ValueFromAmount(nTransparent));
    khuObj.pushKV("zkhu", ValueFromAmount(nLocked));
    khuObj.pushKV("pending_yield_estimated", ValueFromAmount(nPendingYield));
    khuObj.pushKV("total", ValueFromAmount(nTransparent + nLocked + nPendingYield));
    khuObj.pushKV("utxo_count", (int64_t)pwallet->khuData.mapKHUCoins.size());
    khuObj.pushKV("note_count", (int64_t)GetUnspentZKHUNotes(pwallet).size());

    // Build PIV2 object (base coin - liquid PIV2 for fees)
    UniValue piv2Obj(UniValue::VOBJ);
    piv2Obj.pushKV("available", ValueFromAmount(nPIV2Available));
    piv2Obj.pushKV("immature", ValueFromAmount(nPIV2Immature));
    piv2Obj.pushKV("locked_collateral", ValueFromAmount(nPIV2Locked));  // MN collateral

    // Build wallet summary (unified view)
    UniValue walletObj(UniValue::VOBJ);
    walletObj.pushKV("total", ValueFromAmount(nTotalWallet));
    walletObj.pushKV("piv2_spendable", ValueFromAmount(nPIV2Available));
    walletObj.pushKV("khu_transparent", ValueFromAmount(nTransparent));
    walletObj.pushKV("khu_staked", ValueFromAmount(nLocked));

    // Build result
    UniValue result(UniValue::VOBJ);
    result.pushKV("wallet", walletObj);  // Unified view first
    result.pushKV("khu", khuObj);
    result.pushKV("piv2", piv2Obj);

    return result;
}

/**
 * khulistunspent - List unspent KHU UTXOs
 */
static UniValue khulistunspent(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 2) {
        throw std::runtime_error(
            "piv2listunspent ( minconf maxconf )\n"
            "\nReturns a list of unspent KHU_T UTXOs (Phase 8a).\n"
            "\nArguments:\n"
            "1. minconf    (numeric, optional, default=1) Minimum confirmations\n"
            "2. maxconf    (numeric, optional, default=9999999) Maximum confirmations\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"txid\": \"hash\",         (string) Transaction ID\n"
            "    \"vout\": n,              (numeric) Output index\n"
            "    \"address\": \"addr\",      (string) Destination address\n"
            "    \"amount\": n,            (numeric) Amount in satoshis\n"
            "    \"confirmations\": n,     (numeric) Number of confirmations\n"
            "    \"spendable\": true|false,(boolean) Can be spent\n"
            "    \"lockd\": true|false    (boolean) Is locked as ZKHU\n"
            "  },\n"
            "  ...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("piv2listunspent", "")
            + HelpExampleCli("piv2listunspent", "6 9999999")
            + HelpExampleRpc("piv2listunspent", "6, 9999999")
        );
    }

    CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) {
        throw JSONRPCError(RPC_WALLET_NOT_FOUND, "Wallet not found");
    }

    int nMinDepth = 1;
    int nMaxDepth = 9999999;

    if (request.params.size() > 0) {
        nMinDepth = request.params[0].get_int();
    }
    if (request.params.size() > 1) {
        nMaxDepth = request.params[1].get_int();
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    int nCurrentHeight = chainActive.Height();

    UniValue results(UniValue::VARR);

    for (std::map<COutPoint, KHUCoinEntry>::const_iterator it = pwallet->khuData.mapKHUCoins.begin();
         it != pwallet->khuData.mapKHUCoins.end(); ++it) {
        const KHUCoinEntry& entry = it->second;

        int nDepth = nCurrentHeight - entry.nConfirmedHeight + 1;

        if (nDepth < nMinDepth || nDepth > nMaxDepth) continue;

        CTxDestination dest;
        std::string address = "unknown";
        if (ExtractDestination(entry.coin.scriptPubKey, dest)) {
            address = EncodeDestination(dest);
        }

        UniValue obj(UniValue::VOBJ);
        obj.pushKV("txid", entry.txhash.GetHex());
        obj.pushKV("vout", (int64_t)entry.vout);
        obj.pushKV("address", address);
        obj.pushKV("amount", ValueFromAmount(entry.coin.amount));
        obj.pushKV("confirmations", nDepth);
        obj.pushKV("spendable", entry.coin.IsSpendable());
        obj.pushKV("locked", entry.coin.fLocked);

        results.push_back(obj);
    }

    return results;
}

/**
 * mint - Mint KHU from HU (HU → KHU)
 *
 * Nomenclature: HU (transparent) → KHU (locked, preparation for staking)
 */
static UniValue khumint(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "mint amount\n"
            "\nMint KHU from HU. Locks HU coins in preparation for staking.\n"
            "\nArguments:\n"
            "1. amount    (numeric, required) Amount of HU to convert to KHU\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\": \"hash\",          (string) Transaction ID\n"
            "  \"amount_khu\": n,         (numeric) KHU minted (satoshis)\n"
            "  \"fee\": n                 (numeric) Transaction fee (satoshis)\n"
            "}\n"
            "\nNote: The invariant C == U is maintained. HU is locked and KHU is created.\n"
            "\nExamples:\n"
            + HelpExampleCli("mint", "100")
            + HelpExampleRpc("mint", "100")
        );
    }

    CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) {
        throw JSONRPCError(RPC_WALLET_NOT_FOUND, "Wallet not found");
    }

    CAmount nAmount = AmountFromValue(request.params[0]);
    if (nAmount <= 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Amount must be positive");
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    uint32_t V6_activation = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight;
    if ((uint32_t)chainActive.Height() < V6_activation) {
        throw JSONRPCError(RPC_INVALID_REQUEST,
            strprintf("KHU not active until block %u (current: %d)", V6_activation, chainActive.Height()));
    }

    CAmount nBalance = pwallet->GetBalance().m_mine_trusted;
    if (nAmount > nBalance) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            strprintf("Insufficient PIV balance. Have: %s, Need: %s",
                     FormatMoney(nBalance), FormatMoney(nAmount)));
    }

    // Fee calculation: use minRelayTxFee which matches block assembler requirements
    // Block assembler requires fee >= blockMinTxFee.GetFee(txSize), which equals minRelayTxFee
    const size_t BASE_TX_SIZE = 150; // base tx overhead (outputs, payload, etc.)
    const size_t INPUT_SIZE = 180;   // estimated size per signed input
    const CAmount MIN_TX_FEE = 10000; // Minimum relay fee (0.0001 PIV)

    CPubKey newKey;
    if (!pwallet->GetKeyFromPool(newKey, false)) {
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out");
    }
    CTxDestination dest = newKey.GetID();

    CMutableTransaction mtx;
    mtx.nVersion = CTransaction::TxVersion::SAPLING;  // KHU txs require Sapling version
    mtx.nType = CTransaction::TxType::KHU_MINT;

    // Output 1: KHU_T output (prepare script first for payload)
    CScript khuScript = GetScriptForDestination(dest);

    // Create and serialize payload (required for special txs)
    CMintKHUPayload payload(nAmount, khuScript);
    CDataStream ds(SER_NETWORK, PROTOCOL_VERSION);
    ds << payload;
    mtx.extraPayload = std::vector<uint8_t>(ds.begin(), ds.end());

    // Output 0: OP_RETURN burn marker
    CScript burnScript;
    burnScript << OP_RETURN;
    mtx.vout.push_back(CTxOut(0, burnScript));

    // Output 1: KHU_T output
    mtx.vout.push_back(CTxOut(nAmount, khuScript));

    // Select coins for input - first pass to estimate fee
    std::vector<COutput> vAvailableCoins;
    pwallet->AvailableCoins(&vAvailableCoins);

    // Initial coin selection with estimated fee (use minRelayTxFee for block assembler compatibility)
    size_t nInitialSize = BASE_TX_SIZE + INPUT_SIZE;
    CAmount nEstimatedFee = std::max(MIN_TX_FEE, ::minRelayTxFee.GetFee(nInitialSize));
    CAmount nTotalRequired = nAmount + nEstimatedFee;
    CAmount nValueIn = 0;
    size_t nInputCount = 0;

    for (const COutput& out : vAvailableCoins) {
        if (nValueIn >= nTotalRequired) break;

        // BUG FIX: Skip KHU coins - MINT should ONLY use PIV inputs, never KHU outputs
        // This prevents chained MINTs from accidentally spending previous MINT's KHU output
        COutPoint outpoint(out.tx->GetHash(), out.i);
        if (pwallet->khuData.mapKHUCoins.count(outpoint)) {
            LogPrint(BCLog::HU, "khumint: Skipping confirmed KHU coin %s:%d\n",
                     out.tx->GetHash().GetHex().substr(0, 16).c_str(), out.i);
            continue;
        }

        // BUG FIX #2: Also skip unconfirmed KHU outputs from pending MINT transactions
        // When a KHU_MINT is in mempool but not yet confirmed, its vout[1] is a KHU output
        // that should NOT be used as input for another MINT
        if (out.tx->tx->nType == CTransaction::TxType::KHU_MINT && out.i == 1) {
            LogPrint(BCLog::HU, "khumint: Skipping unconfirmed KHU output from MINT tx %s:%d\n",
                     out.tx->GetHash().GetHex().substr(0, 16).c_str(), out.i);
            continue;
        }

        mtx.vin.push_back(CTxIn(out.tx->GetHash(), out.i));
        nValueIn += out.tx->tx->vout[out.i].nValue;
        nInputCount++;

        // Recalculate estimated fee based on actual input count (with minimum floor)
        size_t nEstimatedSize = BASE_TX_SIZE + (nInputCount * INPUT_SIZE);
        nEstimatedFee = std::max(MIN_TX_FEE, ::minRelayTxFee.GetFee(nEstimatedSize));
        nTotalRequired = nAmount + nEstimatedFee;
    }

    if (nValueIn < nTotalRequired) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            strprintf("Unable to select sufficient coins. Have: %s, Need: %s (including fee: %s)",
                     FormatMoney(nValueIn), FormatMoney(nTotalRequired), FormatMoney(nEstimatedFee)));
    }

    // Calculate final fee based on actual tx size (with minimum floor)
    size_t nActualSize = BASE_TX_SIZE + (nInputCount * INPUT_SIZE);
    CAmount nFee = std::max(MIN_TX_FEE, ::minRelayTxFee.GetFee(nActualSize));

    // Output 2: Change back to PIV (if any)
    CAmount nChange = nValueIn - nAmount - nFee;
    if (nChange > 0) {
        CPubKey changeKey;
        if (!pwallet->GetKeyFromPool(changeKey, true)) {
            throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out for change");
        }
        CScript changeScript = GetScriptForDestination(changeKey.GetID());
        mtx.vout.push_back(CTxOut(nChange, changeScript));
    }

    // Sign transaction
    for (size_t i = 0; i < mtx.vin.size(); ++i) {
        const COutPoint& outpoint = mtx.vin[i].prevout;
        const CWalletTx* wtx = pwallet->GetWalletTx(outpoint.hash);
        if (!wtx) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Input transaction not found");
        }

        const CScript& scriptPubKey = wtx->tx->vout[outpoint.n].scriptPubKey;
        CAmount amount = wtx->tx->vout[outpoint.n].nValue;

        SignatureData sigdata;
        // Use SIGVERSION_SAPLING for Sapling version transactions
        SigVersion sigversion = mtx.isSaplingVersion() ? SIGVERSION_SAPLING : SIGVERSION_BASE;
        if (!ProduceSignature(MutableTransactionSignatureCreator(pwallet, &mtx, i, amount, SIGHASH_ALL),
                             scriptPubKey, sigdata, sigversion)) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Signing transaction failed");
        }
        UpdateTransaction(mtx, i, sigdata);
    }

    // Broadcast transaction
    CTransactionRef txRef = MakeTransactionRef(std::move(mtx));
    CValidationState state;

    if (!AcceptToMemoryPool(mempool, state, txRef, false, nullptr)) {
        throw JSONRPCError(RPC_TRANSACTION_REJECTED,
            strprintf("Transaction rejected: %s", FormatStateMessage(state)));
    }

    // Relay transaction to peers
    if (g_connman) {
        CInv inv(MSG_TX, txRef->GetHash());
        g_connman->ForEachNode([&inv](CNode* pnode) {
            pnode->PushInventory(inv);
        });
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", txRef->GetHash().GetHex());
    result.pushKV("amount_khu", ValueFromAmount(nAmount));
    result.pushKV("fee", ValueFromAmount(nFee));

    return result;
}

/**
 * redeem - Redeem KHU back to HU (KHU → HU)
 *
 * Nomenclature: KHU (locked) → HU (transparent)
 */
static UniValue khuredeem(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "redeem amount\n"
            "\nRedeem KHU back to HU. Unlocks KHU coins back to transparent HU.\n"
            "\nArguments:\n"
            "1. amount    (numeric, required) Amount of KHU to convert back to HU\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\": \"hash\",          (string) Transaction ID\n"
            "  \"amount_hu\": n,          (numeric) HU redeemed (satoshis)\n"
            "  \"fee\": n                 (numeric) Transaction fee (satoshis)\n"
            "}\n"
            "\nNote: The invariant C == U is maintained. KHU is released and HU is returned.\n"
            "\nExamples:\n"
            + HelpExampleCli("redeem", "100")
            + HelpExampleRpc("redeem", "100")
        );
    }

    CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) {
        throw JSONRPCError(RPC_WALLET_NOT_FOUND, "Wallet not found");
    }

    CAmount nAmount = AmountFromValue(request.params[0]);
    if (nAmount <= 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Amount must be positive");
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    uint32_t V6_activation = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight;
    if ((uint32_t)chainActive.Height() < V6_activation) {
        throw JSONRPCError(RPC_INVALID_REQUEST,
            strprintf("KHU not active until block %u (current: %d)", V6_activation, chainActive.Height()));
    }

    // ═══════════════════════════════════════════════════════════════════════
    // CLAUDE.md §2.1: "Tous les frais KHU sont payés en PIV non-bloqué"
    // Fee is paid from separate PIV input, NOT from KHU amount
    // ═══════════════════════════════════════════════════════════════════════

    CAmount nKHUBalance = GetKHUBalance(pwallet);
    if (nAmount > nKHUBalance) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            strprintf("Insufficient KHU_T balance. Have: %s, Need: %s",
                     FormatMoney(nKHUBalance), FormatMoney(nAmount)));
    }

    // ═══════════════════════════════════════════════════════════════════════
    // FEE CALCULATION: Use minRelayTxFee which matches block assembler requirements
    // Block assembler requires fee >= blockMinTxFee.GetFee(txSize), which equals minRelayTxFee
    // ═══════════════════════════════════════════════════════════════════════
    const size_t BASE_TX_SIZE = 150;    // Base tx overhead + payload
    const size_t INPUT_SIZE = 180;      // Per input (signature + script)
    const size_t OUTPUT_SIZE = 34;      // Per output (scriptPubKey + amount)
    const CAmount MIN_TX_FEE = 10000;   // Minimum relay fee (0.0001 PIV)

    // Select KHU UTXOs first to know input count
    CAmount nKHUValueIn = 0;
    std::vector<COutPoint> vKHUInputs;

    for (std::map<COutPoint, KHUCoinEntry>::const_iterator it = pwallet->khuData.mapKHUCoins.begin();
         it != pwallet->khuData.mapKHUCoins.end(); ++it) {
        const COutPoint& outpoint = it->first;
        const KHUCoinEntry& entry = it->second;

        if (entry.coin.fLocked) continue;
        if (nKHUValueIn >= nAmount) break;

        vKHUInputs.push_back(outpoint);
        nKHUValueIn += entry.coin.amount;
    }

    if (nKHUValueIn < nAmount) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            "Unable to select sufficient KHU UTXOs");
    }

    // Estimate tx size: KHU inputs + 1 PIV input + up to 3 outputs
    size_t nEstimatedInputs = vKHUInputs.size() + 1;  // KHU + PIV for fee
    size_t nEstimatedOutputs = 3;  // PIV output + KHU change + PIV change
    size_t nEstimatedSize = BASE_TX_SIZE + (nEstimatedInputs * INPUT_SIZE) + (nEstimatedOutputs * OUTPUT_SIZE);
    CAmount nFee = std::max(MIN_TX_FEE, ::minRelayTxFee.GetFee(nEstimatedSize));

    // Check PIV balance for fee (separate from KHU)
    CAmount nPIVBalance = pwallet->GetAvailableBalance();
    if (nFee > nPIVBalance) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            strprintf("Insufficient PIV for fee: have %s, need %s",
                     FormatMoney(nPIVBalance), FormatMoney(nFee)));
    }

    // Select PIV UTXO for fee (smallest one >= fee)
    std::vector<COutput> vPIVCoins;
    pwallet->AvailableCoins(&vPIVCoins);
    COutPoint pivFeeInput;
    CAmount nPIVInputValue = 0;
    CScript pivFeeScript;
    bool foundPIVInput = false;
    CAmount bestExcess = std::numeric_limits<CAmount>::max();

    for (const COutput& coin : vPIVCoins) {
        // Skip KHU coins (check if this specific outpoint is a KHU coin, not the whole tx)
        COutPoint coinOutpoint(coin.tx->GetHash(), coin.i);
        if (pwallet->khuData.mapKHUCoins.count(coinOutpoint)) continue;
        CAmount value = coin.Value();
        if (value >= nFee) {
            CAmount excess = value - nFee;
            if (excess < bestExcess) {
                pivFeeInput = COutPoint(coin.tx->GetHash(), coin.i);
                nPIVInputValue = value;
                pivFeeScript = coin.tx->tx->vout[coin.i].scriptPubKey;
                foundPIVInput = true;
                bestExcess = excess;
                if (excess == 0) break;
            }
        }
    }

    if (!foundPIVInput) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            "No suitable PIV UTXO found for fee payment");
    }

    // Recalculate fee with actual input count (with minimum floor)
    size_t nActualSize = BASE_TX_SIZE + ((vKHUInputs.size() + 1) * INPUT_SIZE) + (nEstimatedOutputs * OUTPUT_SIZE);
    nFee = std::max(MIN_TX_FEE, ::minRelayTxFee.GetFee(nActualSize));

    CPubKey newKey;
    if (!pwallet->GetKeyFromPool(newKey, false)) {
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out");
    }
    CTxDestination dest = newKey.GetID();

    CMutableTransaction mtx;
    mtx.nVersion = CTransaction::TxVersion::SAPLING;
    mtx.nType = CTransaction::TxType::KHU_REDEEM;

    CScript pivScript = GetScriptForDestination(dest);

    // Create and serialize payload
    CRedeemKHUPayload payload(nAmount, pivScript);
    CDataStream ds(SER_NETWORK, PROTOCOL_VERSION);
    ds << payload;
    mtx.extraPayload = std::vector<uint8_t>(ds.begin(), ds.end());

    LogPrint(BCLog::HU, "khuredeem: %zu KHU inputs for amount=%s\n",
              vKHUInputs.size(), FormatMoney(nAmount));

    // Add KHU inputs first
    for (const COutPoint& outpoint : vKHUInputs) {
        mtx.vin.push_back(CTxIn(outpoint));
    }
    size_t nKHUInputCount = mtx.vin.size();

    // Add PIV input for fee
    mtx.vin.push_back(CTxIn(pivFeeInput));

    // Output 0: PIV output (full amount - fee comes from PIV input)
    mtx.vout.push_back(CTxOut(nAmount, pivScript));

    // Output 1: KHU_T change (if any)
    CAmount nKHUChange = nKHUValueIn - nAmount;
    if (nKHUChange > 0) {
        CPubKey khuChangeKey;
        if (!pwallet->GetKeyFromPool(khuChangeKey, true)) {
            throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out for KHU change");
        }
        CScript khuChangeScript = GetScriptForDestination(khuChangeKey.GetID());
        mtx.vout.push_back(CTxOut(nKHUChange, khuChangeScript));
    }

    // Output 2: PIV change (if any)
    CAmount nPIVChange = nPIVInputValue - nFee;
    if (nPIVChange > 0) {
        CPubKey pivChangeKey;
        if (!pwallet->GetKeyFromPool(pivChangeKey, true)) {
            throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out for PIV change");
        }
        CScript pivChangeScript = GetScriptForDestination(pivChangeKey.GetID());
        mtx.vout.push_back(CTxOut(nPIVChange, pivChangeScript));
    }

    // Sign KHU inputs
    for (size_t i = 0; i < nKHUInputCount; ++i) {
        const COutPoint& outpoint = mtx.vin[i].prevout;
        auto it = pwallet->khuData.mapKHUCoins.find(outpoint);
        if (it == pwallet->khuData.mapKHUCoins.end()) {
            throw JSONRPCError(RPC_WALLET_ERROR, "KHU input not found");
        }

        const CScript& scriptPubKey = it->second.coin.scriptPubKey;
        CAmount amount = it->second.coin.amount;

        SignatureData sigdata;
        SigVersion sigversion = mtx.isSaplingVersion() ? SIGVERSION_SAPLING : SIGVERSION_BASE;
        if (!ProduceSignature(MutableTransactionSignatureCreator(pwallet, &mtx, i, amount, SIGHASH_ALL),
                             scriptPubKey, sigdata, sigversion)) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Signing KHU input failed");
        }
        UpdateTransaction(mtx, i, sigdata);
    }

    // Sign PIV input (last input)
    {
        SignatureData sigdata;
        SigVersion sigversion = mtx.isSaplingVersion() ? SIGVERSION_SAPLING : SIGVERSION_BASE;
        if (!ProduceSignature(MutableTransactionSignatureCreator(pwallet, &mtx, nKHUInputCount, nPIVInputValue, SIGHASH_ALL),
                             pivFeeScript, sigdata, sigversion)) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Signing PIV fee input failed");
        }
        UpdateTransaction(mtx, nKHUInputCount, sigdata);
    }

    // Broadcast transaction
    CTransactionRef txRef = MakeTransactionRef(std::move(mtx));

    LogPrint(BCLog::HU, "khuredeem: broadcasting tx %s\n",
              txRef->GetHash().ToString().substr(0,16).c_str());

    CValidationState state;

    if (!AcceptToMemoryPool(mempool, state, txRef, false, nullptr)) {
        throw JSONRPCError(RPC_TRANSACTION_REJECTED,
            strprintf("Transaction rejected: %s", FormatStateMessage(state)));
    }

    // Relay transaction to peers
    if (g_connman) {
        CInv inv(MSG_TX, txRef->GetHash());
        g_connman->ForEachNode([&inv](CNode* pnode) {
            pnode->PushInventory(inv);
        });
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", txRef->GetHash().GetHex());
    result.pushKV("amount_piv", ValueFromAmount(nAmount));
    result.pushKV("fee", ValueFromAmount(nFee));
    result.pushKV("fee_source", "separate_piv_input");

    return result;
}

/**
 * khugetinfo - Get comprehensive KHU wallet and network info
 */
static UniValue khugetinfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(
            "piv2getinfo\n"
            "\nReturns comprehensive KHU wallet and network information.\n"
            "\nResult:\n"
            "{\n"
            "  \"wallet\": {\n"
            "    \"khu_transparent\": n,    (numeric) KHU_T balance\n"
            "    \"khu_locked\": n,         (numeric) ZKHU locked balance\n"
            "    \"khu_total\": n,          (numeric) Total KHU balance\n"
            "    \"utxo_count\": n,         (numeric) Number of KHU UTXOs\n"
            "    \"note_count\": n          (numeric) Number of ZKHU notes\n"
            "  },\n"
            "  \"network\": {\n"
            "    \"height\": n,             (numeric) Current block height\n"
            "    \"C\": n,                  (numeric) Total collateral (PIV backing KHU)\n"
            "    \"U\": n,                  (numeric) Total KHU_T supply\n"
            "    \"Cr\": n,                 (numeric) Reward pool\n"
            "    \"Ur\": n,                 (numeric) Unlock rights\n"
            "    \"T\": n,                  (numeric) DAO Treasury\n"
            "    \"R_annual_pct\": x.xx,    (numeric) Annual yield rate %\n"
            "    \"invariants_ok\": true|false\n"
            "  },\n"
            "  \"activation\": {\n"
            "    \"khu_active\": true|false,(boolean) Is KHU system active\n"
            "    \"activation_height\": n,  (numeric) V6 activation height\n"
            "    \"blocks_until_active\": n (numeric) Blocks until activation (0 if active)\n"
            "  }\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("piv2getinfo", "")
            + HelpExampleRpc("piv2getinfo", "")
        );
    }

    CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) {
        throw JSONRPCError(RPC_WALLET_NOT_FOUND, "Wallet not found");
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    // Wallet info
    CAmount nTransparent = GetKHUBalance(pwallet);
    CAmount nLocked = GetKHULockedBalance(pwallet);

    UniValue wallet(UniValue::VOBJ);
    wallet.pushKV("khu_transparent", ValueFromAmount(nTransparent));
    wallet.pushKV("khu_locked", ValueFromAmount(nLocked));
    wallet.pushKV("khu_total", ValueFromAmount(nTransparent + nLocked));
    wallet.pushKV("utxo_count", (int64_t)pwallet->khuData.mapKHUCoins.size());
    wallet.pushKV("note_count", (int64_t)GetUnspentZKHUNotes(pwallet).size());

    // Network info
    HuGlobalState state;
    UniValue network(UniValue::VOBJ);

    if (GetCurrentKHUState(state)) {
        network.pushKV("height", (int64_t)state.nHeight);
        network.pushKV("C", ValueFromAmount(state.C));
        network.pushKV("U", ValueFromAmount(state.U));
        network.pushKV("Cr", ValueFromAmount(state.Cr));
        network.pushKV("Ur", ValueFromAmount(state.Ur));
        network.pushKV("T", ValueFromAmount(state.T));
        network.pushKV("R_annual_pct", state.R_annual / 100.0);
        network.pushKV("invariants_ok", state.CheckInvariants());
    } else {
        network.pushKV("height", chainActive.Height());
        network.pushKV("C", 0);
        network.pushKV("U", 0);
        network.pushKV("Cr", 0);
        network.pushKV("Ur", 0);
        network.pushKV("T", 0);
        network.pushKV("R_annual_pct", 0.0);
        network.pushKV("invariants_ok", true);
    }

    // Activation info
    uint32_t V6_activation = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight;
    int nCurrentHeight = chainActive.Height();
    bool fActive = (uint32_t)nCurrentHeight >= V6_activation;
    int nBlocksUntil = fActive ? 0 : (int)(V6_activation - nCurrentHeight);

    UniValue activation(UniValue::VOBJ);
    activation.pushKV("khu_active", fActive);
    activation.pushKV("activation_height", (int64_t)V6_activation);
    activation.pushKV("blocks_until_active", nBlocksUntil);

    // Build result
    UniValue result(UniValue::VOBJ);
    result.pushKV("wallet", wallet);
    result.pushKV("network", network);
    result.pushKV("activation", activation);

    return result;
}

/**
 * khusend - Send KHU_T to an address
 *
 * Creates a KHU transfer transaction (not MINT/REDEEM, just transfer).
 * Note: This is a simple transparent transfer of KHU_T UTXOs.
 */
static UniValue khusend(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 3) {
        throw std::runtime_error(
            "piv2send \"address\" amount ( \"comment\" )\n"
            "\nSend KHU_T to a given address.\n"
            "\nArguments:\n"
            "1. \"address\"    (string, required) The PIVX address to send to\n"
            "2. amount         (numeric, required) The amount of KHU_T to send\n"
            "3. \"comment\"    (string, optional) A comment (not stored on chain)\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\": \"hash\",      (string) Transaction ID\n"
            "  \"amount\": n,          (numeric) Amount sent\n"
            "  \"fee\": n,             (numeric) Transaction fee\n"
            "  \"to\": \"address\"     (string) Recipient address\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("piv2send", "\"DMJRSsuU9zfyrvxVaAEFQqK4MxZg34fk\" 100")
            + HelpExampleRpc("piv2send", "\"DMJRSsuU9zfyrvxVaAEFQqK4MxZg34fk\", 100")
        );
    }

    CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) {
        throw JSONRPCError(RPC_WALLET_NOT_FOUND, "Wallet not found");
    }

    // Parse address
    std::string strAddress = request.params[0].get_str();
    CTxDestination dest = DecodeDestination(strAddress);
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid PIVX address");
    }

    // Parse amount
    CAmount nAmount = AmountFromValue(request.params[1]);
    if (nAmount <= 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Amount must be positive");
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    // Check V6 activation
    uint32_t V6_activation = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight;
    if ((uint32_t)chainActive.Height() < V6_activation) {
        throw JSONRPCError(RPC_INVALID_REQUEST,
            strprintf("KHU not active until block %u (current: %d)", V6_activation, chainActive.Height()));
    }

    // ═══════════════════════════════════════════════════════════════════════
    // CLAUDE.md §2.1: "Tous les frais KHU sont payés en PIV non-bloqué"
    // Fee is paid from separate PIV input, NOT from KHU amount
    // ═══════════════════════════════════════════════════════════════════════

    // Check KHU balance (fee is separate in PIV)
    CAmount nKHUBalance = GetKHUBalance(pwallet);
    CAmount nFee = 10000; // 0.0001 PIV

    if (nAmount > nKHUBalance) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            strprintf("Insufficient KHU_T balance. Have: %s, Need: %s",
                     FormatMoney(nKHUBalance), FormatMoney(nAmount)));
    }

    // Check PIV balance for fee (separate from KHU)
    CAmount nPIVBalance = pwallet->GetAvailableBalance();
    if (nFee > nPIVBalance) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            strprintf("Insufficient PIV for fee: have %s, need %s",
                     FormatMoney(nPIVBalance), FormatMoney(nFee)));
    }

    // Select PIV UTXO for fee (smallest one >= fee)
    std::vector<COutput> vPIVCoins;
    pwallet->AvailableCoins(&vPIVCoins);

    COutPoint pivFeeInput;
    CAmount nPIVInputValue = 0;
    bool foundPIVInput = false;

    // Sort by value ascending to find smallest suitable UTXO
    std::sort(vPIVCoins.begin(), vPIVCoins.end(),
        [](const COutput& a, const COutput& b) {
            return a.tx->tx->vout[a.i].nValue < b.tx->tx->vout[b.i].nValue;
        });

    for (const COutput& out : vPIVCoins) {
        CAmount value = out.tx->tx->vout[out.i].nValue;
        if (value >= nFee) {
            pivFeeInput = COutPoint(out.tx->tx->GetHash(), out.i);
            nPIVInputValue = value;
            foundPIVInput = true;
            break;
        }
    }

    if (!foundPIVInput) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            "No suitable PIV UTXO found for transaction fee");
    }

    // Select KHU UTXOs (for amount only, NOT fee)
    CAmount nKHUValueIn = 0;
    std::vector<COutPoint> vKHUInputs;

    for (std::map<COutPoint, KHUCoinEntry>::const_iterator it = pwallet->khuData.mapKHUCoins.begin();
         it != pwallet->khuData.mapKHUCoins.end(); ++it) {
        const COutPoint& outpoint = it->first;
        const KHUCoinEntry& entry = it->second;

        if (entry.coin.fLocked) continue;
        if (nKHUValueIn >= nAmount) break;

        vKHUInputs.push_back(outpoint);
        nKHUValueIn += entry.coin.amount;
    }

    if (nKHUValueIn < nAmount) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            "Unable to select sufficient KHU UTXOs");
    }

    // Create transaction
    // Note: This is a simple KHU_T transfer - no type change
    CMutableTransaction mtx;
    mtx.nVersion = CTransaction::TxVersion::LEGACY;
    mtx.nType = CTransaction::TxType::NORMAL; // Regular transfer, not MINT/REDEEM

    // Add KHU inputs
    for (const COutPoint& outpoint : vKHUInputs) {
        mtx.vin.push_back(CTxIn(outpoint));
    }
    size_t nKHUInputCount = mtx.vin.size();

    // Add PIV input for fee
    mtx.vin.push_back(CTxIn(pivFeeInput));

    // Output 0: KHU_T to recipient (full amount - fee is separate)
    CScript recipientScript = GetScriptForDestination(dest);
    mtx.vout.push_back(CTxOut(nAmount, recipientScript));

    // Output 1: KHU_T change (if any)
    CAmount nKHUChange = nKHUValueIn - nAmount;
    if (nKHUChange > 0) {
        CPubKey changeKey;
        if (!pwallet->GetKeyFromPool(changeKey, true)) {
            throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out for KHU change");
        }
        CScript changeScript = GetScriptForDestination(changeKey.GetID());
        mtx.vout.push_back(CTxOut(nKHUChange, changeScript));
    }

    // Output 2: PIV change (if any)
    CAmount nPIVChange = nPIVInputValue - nFee;
    if (nPIVChange > 0) {
        CPubKey pivChangeKey;
        if (!pwallet->GetKeyFromPool(pivChangeKey, true)) {
            throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out for PIV change");
        }
        CScript pivChangeScript = GetScriptForDestination(pivChangeKey.GetID());
        mtx.vout.push_back(CTxOut(nPIVChange, pivChangeScript));
    }

    // Sign KHU inputs
    for (size_t i = 0; i < nKHUInputCount; ++i) {
        const COutPoint& outpoint = mtx.vin[i].prevout;
        auto it = pwallet->khuData.mapKHUCoins.find(outpoint);
        if (it == pwallet->khuData.mapKHUCoins.end()) {
            throw JSONRPCError(RPC_WALLET_ERROR, "KHU input not found");
        }

        const CScript& scriptPubKey = it->second.coin.scriptPubKey;
        CAmount amount = it->second.coin.amount;

        SignatureData sigdata;
        SigVersion sigversion = mtx.isSaplingVersion() ? SIGVERSION_SAPLING : SIGVERSION_BASE;
        if (!ProduceSignature(MutableTransactionSignatureCreator(pwallet, &mtx, i, amount, SIGHASH_ALL),
                             scriptPubKey, sigdata, sigversion)) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Signing KHU input failed");
        }
        UpdateTransaction(mtx, i, sigdata);
    }

    // Sign PIV input (last input)
    {
        size_t pivInputIdx = nKHUInputCount;
        const CWalletTx* wtx = pwallet->GetWalletTx(pivFeeInput.hash);
        if (!wtx) {
            throw JSONRPCError(RPC_WALLET_ERROR, "PIV input transaction not found");
        }
        const CScript& pivScriptPubKey = wtx->tx->vout[pivFeeInput.n].scriptPubKey;

        SignatureData sigdata;
        SigVersion sigversion = mtx.isSaplingVersion() ? SIGVERSION_SAPLING : SIGVERSION_BASE;
        if (!ProduceSignature(MutableTransactionSignatureCreator(pwallet, &mtx, pivInputIdx, nPIVInputValue, SIGHASH_ALL),
                             pivScriptPubKey, sigdata, sigversion)) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Signing PIV fee input failed");
        }
        UpdateTransaction(mtx, pivInputIdx, sigdata);
    }

    // Broadcast
    CTransactionRef txRef = MakeTransactionRef(std::move(mtx));
    CValidationState state;

    if (!AcceptToMemoryPool(mempool, state, txRef, false, nullptr)) {
        throw JSONRPCError(RPC_TRANSACTION_REJECTED,
            strprintf("Transaction rejected: %s", FormatStateMessage(state)));
    }

    // Relay transaction to peers
    if (g_connman) {
        CInv inv(MSG_TX, txRef->GetHash());
        g_connman->ForEachNode([&inv](CNode* pnode) {
            pnode->PushInventory(inv);
        });
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", txRef->GetHash().GetHex());
    result.pushKV("amount", ValueFromAmount(nAmount));
    result.pushKV("fee", ValueFromAmount(nFee));
    result.pushKV("to", strAddress);

    return result;
}

/**
 * khurescan - Rescan blockchain for KHU coins
 *
 * Usage: khurescan [startheight]
 *
 * Rescans the blockchain for KHU transactions belonging to this wallet.
 * Useful after importing keys or if wallet is out of sync.
 */
static UniValue khurescan(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1) {
        throw std::runtime_error(
            "piv2rescan ( startheight )\n"
            "\nRescan blockchain for KHU transactions belonging to this wallet.\n"
            "\nArguments:\n"
            "1. startheight    (numeric, optional, default=0) Block height to start scanning from\n"
            "\nResult:\n"
            "{\n"
            "  \"scanned_blocks\": n,     (numeric) Number of blocks scanned\n"
            "  \"khu_coins_found\": n,    (numeric) Number of KHU coins found\n"
            "  \"khu_balance\": n.nnn,    (numeric) KPIVX V2 transparent balance\n"
            "  \"khu_locked\": n.nnn,     (numeric) KHU locked balance\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("piv2rescan", "")
            + HelpExampleCli("piv2rescan", "100000")
            + HelpExampleRpc("piv2rescan", "100000")
        );
    }

    CWallet* pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) {
        throw JSONRPCError(RPC_WALLET_NOT_FOUND, "Wallet not found");
    }

    // Get start height
    int nStartHeight = 0;
    if (!request.params[0].isNull()) {
        nStartHeight = request.params[0].get_int();
        if (nStartHeight < 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid start height (must be >= 0)");
        }
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    int nCurrentHeight = chainActive.Height();
    if (nStartHeight > nCurrentHeight) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
            strprintf("Start height %d is greater than current height %d", nStartHeight, nCurrentHeight));
    }

    // Perform scan
    if (!ScanForKHUCoins(pwallet, nStartHeight)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to scan for KHU coins");
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("scanned_blocks", nCurrentHeight - nStartHeight + 1);
    result.pushKV("khu_coins_found", (int)pwallet->khuData.mapKHUCoins.size());
    result.pushKV("khu_balance", ValueFromAmount(pwallet->khuData.nKHUBalance));
    result.pushKV("khu_locked", ValueFromAmount(pwallet->khuData.nKHULocked));

    return result;
}

/**
 * lock - Lock KHU to ZKHU (KHU → ZKHU)
 *
 * Nomenclature: KHU (locked) → ZKHU (staked with yield)
 * Converts KHU to ZKHU shielded staking notes with yield accumulation.
 *
 * IMPORTANT: LOCK is a form conversion only (U→Z), no economic effect.
 *            The yield is accumulated per-note and paid at UNLOCK.
 */
static UniValue khulock(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "lock amount\n"
            "\nLock KHU to ZKHU shielded staking notes. Yield accumulates until unlock.\n"
            "\nArguments:\n"
            "1. amount    (numeric, required) Amount of KHU to lock\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\": \"hash\",           (string) Transaction ID\n"
            "  \"amount\": n,              (numeric) Amount locked\n"
            "  \"lock_height\": n,         (numeric) Lock start height\n"
            "  \"maturity_height\": n,     (numeric) Height when unlock is allowed\n"
            "  \"note_commitment\": \"hash\" (string) ZKHU note commitment (cm)\n"
            "  \"sapling_address\": \"addr\" (string) ZKHU destination address\n"
            "}\n"
            "\nNotes:\n"
            "- Minimum lock amount: 1 HU (anti-spam protection)\n"
            "- Minimum maturity: 4320 blocks (3 days) before unlocking\n"
            "- Yield accumulates based on R_annual (governed by DOMC)\n"
            "- LOCK is a form conversion only - U decreases, Z increases\n"
            "\nExamples:\n"
            + HelpExampleCli("lock", "100")
            + HelpExampleRpc("lock", "100")
        );
    }

    CWallet* pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) {
        throw JSONRPCError(RPC_WALLET_NOT_FOUND, "Wallet not found");
    }

    EnsureWalletIsUnlocked(pwallet);

    // Parse amount
    CAmount nAmount = AmountFromValue(request.params[0]);
    if (nAmount <= 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Amount must be positive");
    }

    // Anti-spam: Minimum lock amount check (per khu_notes.h)
    if (nAmount < MIN_LOCK_AMOUNT) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
            strprintf("Lock amount %s is below minimum %s (1 PIV). "
                     "This protects the network from micro-lock spam.",
                     FormatMoney(nAmount), FormatMoney(MIN_LOCK_AMOUNT)));
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    // Check KHU activation
    int nCurrentHeight = chainActive.Height();
    const Consensus::Params& consensus = Params().GetConsensus();

    if (!consensus.NetworkUpgradeActive(nCurrentHeight, Consensus::UPGRADE_V6_0)) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "KHU system not yet activated");
    }

    // Check Sapling activation
    if (!consensus.NetworkUpgradeActive(nCurrentHeight, Consensus::UPGRADE_V5_0)) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "Sapling not yet activated (required for ZKHU)");
    }

    // Check KHU balance (fee is paid in PIV, not KHU - per CLAUDE.md §2.1)
    CAmount nKHUBalance = GetKHUBalance(pwallet);

    if (nAmount > nKHUBalance) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            strprintf("Insufficient KHU balance: have %s, need %s",
                     FormatMoney(nKHUBalance), FormatMoney(nAmount)));
    }

    // ═══════════════════════════════════════════════════════════════════════
    // FEE CALCULATION: Use shielded fee formula like PIVX Sapling transactions
    // Formula: minRelayTxFee.GetFee(txSize) * K, where K = 100 for shielded tx
    // See: validation.cpp GetShieldedTxMinFee()
    // ═══════════════════════════════════════════════════════════════════════
    const size_t BASE_SAPLING_TX_SIZE = 500;   // Base Sapling tx overhead
    const size_t INPUT_SIZE = 180;              // Per transparent input
    const size_t SAPLING_OUTPUT_SIZE = 948;     // Sapling shielded output (OutputDescription)
    const size_t OUTPUT_SIZE = 34;              // Transparent output (change)
    const unsigned int SHIELDED_FEE_K = 100;    // HU shielded tx fee multiplier

    // Select KHU_T UTXOs first to know input count
    CAmount nKHUValueIn = 0;
    std::vector<COutPoint> vKHUInputs;

    for (std::map<COutPoint, KHUCoinEntry>::const_iterator it = pwallet->khuData.mapKHUCoins.begin();
         it != pwallet->khuData.mapKHUCoins.end(); ++it) {
        const COutPoint& outpoint = it->first;
        const KHUCoinEntry& entry = it->second;

        if (entry.coin.fLocked) continue;
        if (nKHUValueIn >= nAmount) break;

        vKHUInputs.push_back(outpoint);
        nKHUValueIn += entry.coin.amount;
    }

    if (nKHUValueIn < nAmount) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            "Unable to select sufficient KHU UTXOs");
    }

    // Estimate tx size: KHU inputs + 1 PIV input + 1 Sapling output + KHU change + PIV change
    size_t nEstimatedInputs = vKHUInputs.size() + 1;  // KHU + PIV for fee
    size_t nEstimatedSize = BASE_SAPLING_TX_SIZE + (nEstimatedInputs * INPUT_SIZE) +
                            SAPLING_OUTPUT_SIZE + (2 * OUTPUT_SIZE);  // KHU + PIV change
    // Use shielded fee: minRelayTxFee * K (same as PIVX Sapling transactions)
    CAmount nFee = ::minRelayTxFee.GetFee(nEstimatedSize) * SHIELDED_FEE_K;

    // Check PIV balance for fee
    CAmount nPIVBalance = pwallet->GetAvailableBalance();
    if (nFee > nPIVBalance) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            strprintf("Insufficient PIV for fee: have %s, need %s",
                     FormatMoney(nPIVBalance), FormatMoney(nFee)));
    }

    // Select PIV UTXO for fee (smallest one >= fee to minimize change)
    std::vector<COutput> vPIVCoins;
    pwallet->AvailableCoins(&vPIVCoins);
    COutPoint pivFeeInput;
    CAmount nPIVInputValue = 0;
    CScript pivFeeScript;
    bool foundPIVInput = false;
    CAmount bestExcess = std::numeric_limits<CAmount>::max();

    for (const COutput& coin : vPIVCoins) {
        // Skip KHU coins (check if this specific outpoint is a KHU coin, not the whole tx)
        COutPoint coinOutpoint(coin.tx->GetHash(), coin.i);
        if (pwallet->khuData.mapKHUCoins.count(coinOutpoint)) continue;
        CAmount value = coin.Value();
        if (value >= nFee) {
            CAmount excess = value - nFee;
            if (excess < bestExcess) {
                pivFeeInput = COutPoint(coin.tx->GetHash(), coin.i);
                nPIVInputValue = value;
                pivFeeScript = coin.tx->tx->vout[coin.i].scriptPubKey;
                foundPIVInput = true;
                bestExcess = excess;
                if (excess == 0) break; // Perfect match
            }
        }
    }

    if (!foundPIVInput) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            "No suitable PIV UTXO found for fee payment");
    }

    // Recalculate fee with actual input count (shielded formula)
    size_t nActualSize = BASE_SAPLING_TX_SIZE + ((vKHUInputs.size() + 1) * INPUT_SIZE) +
                         SAPLING_OUTPUT_SIZE + (2 * OUTPUT_SIZE);
    nFee = ::minRelayTxFee.GetFee(nActualSize) * SHIELDED_FEE_K;

    // Generate or get Sapling address for ZKHU note
    libzcash::SaplingPaymentAddress saplingAddr;
    SaplingScriptPubKeyMan* saplingMan = pwallet->GetSaplingScriptPubKeyMan();
    if (!saplingMan || !saplingMan->IsEnabled()) {
        throw JSONRPCError(RPC_WALLET_ERROR,
            "Sapling not enabled in wallet. Run 'upgradetohd' first.");
    }

    saplingAddr = pwallet->GenerateNewSaplingZKey();

    // Get output viewing key (OVK) for the note
    uint256 ovk = saplingMan->getCommonOVK();

    // Create ZKHUMemo with lock metadata
    uint32_t nLockHeight = nCurrentHeight + 1; // Note will be in next block
    const uint32_t maturityBlocks = GetZKHUMaturityBlocks(); // Network-aware

    ZKHUMemo memo;
    memcpy(memo.magic, "ZKHU", 4);
    memo.version = 1;
    memo.nLockStartHeight = nLockHeight;
    memo.amount = nAmount;
    memo.Ur_accumulated = 0; // Initial lock, no accumulated yield yet

    std::array<unsigned char, 512> memoBytes = memo.Serialize();

    // Build transaction using TransactionBuilder
    TransactionBuilder builder(consensus, pwallet);
    builder.SetFee(nFee);
    builder.SetType(CTransaction::TxType::KHU_LOCK);  // Set type BEFORE building

    // Add KHU_T transparent inputs
    for (const COutPoint& outpoint : vKHUInputs) {
        auto it = pwallet->khuData.mapKHUCoins.find(outpoint);
        if (it == pwallet->khuData.mapKHUCoins.end()) {
            throw JSONRPCError(RPC_WALLET_ERROR, "KHU input not found");
        }
        builder.AddTransparentInput(outpoint, it->second.coin.scriptPubKey, it->second.coin.amount);
    }

    // Add PIV transparent input (for fee)
    builder.AddTransparentInput(pivFeeInput, pivFeeScript, nPIVInputValue);

    // Add Sapling output (ZKHU note) with ZKHUMemo
    builder.AddSaplingOutput(ovk, saplingAddr, nAmount, memoBytes);

    // IMPORTANT: Output ordering for wallet tracking (per CLAUDE.md §2.1)
    // vout[0] = KHU change (tracked as KHU)
    // vout[1] = PIV change (NOT tracked as KHU)

    // Calculate KHU change (fee is paid from PIV, not KHU)
    CAmount nKHUChange = nKHUValueIn - nAmount;
    if (nKHUChange > 0) {
        CPubKey khuChangeKey;
        if (!pwallet->GetKeyFromPool(khuChangeKey, true)) {
            throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out for KHU change");
        }
        builder.AddTransparentOutput(khuChangeKey.GetID(), nKHUChange);
    }

    // Calculate PIV change (PIV input minus fee)
    CAmount nPIVChange = nPIVInputValue - nFee;
    if (nPIVChange > 0) {
        CPubKey pivChangeKey;
        if (!pwallet->GetKeyFromPool(pivChangeKey, true)) {
            throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out for PIV change");
        }
        builder.AddTransparentOutput(pivChangeKey.GetID(), nPIVChange);
    }

    // Build with dummy signatures first (to calculate size)
    TransactionBuilderResult buildResult = builder.Build(true);
    if (buildResult.IsError()) {
        throw JSONRPCError(RPC_WALLET_ERROR,
            strprintf("Failed to build lock transaction: %s", buildResult.GetError()));
    }

    // Clear dummy signatures/proofs before creating real ones
    // (required for TransactionBuilder workflow)
    builder.ClearProofsAndSignatures();

    // Now prove and sign
    TransactionBuilderResult proveResult = builder.ProveAndSign();
    if (proveResult.IsError()) {
        throw JSONRPCError(RPC_WALLET_ERROR,
            strprintf("Failed to prove/sign lock transaction: %s", proveResult.GetError()));
    }

    CTransaction finalTx = proveResult.GetTxOrThrow();

    // Transaction type was already set via builder.SetType() before Build()
    // DO NOT modify the transaction after Build() - it would invalidate Sapling proofs
    CTransactionRef txRef = MakeTransactionRef(finalTx);

    // Broadcast transaction
    CValidationState state;
    if (!AcceptToMemoryPool(mempool, state, txRef, false, nullptr)) {
        throw JSONRPCError(RPC_TRANSACTION_REJECTED,
            strprintf("Transaction rejected: %s", FormatStateMessage(state)));
    }

    // Relay transaction to peers
    if (g_connman) {
        CInv inv(MSG_TX, txRef->GetHash());
        g_connman->ForEachNode([&inv](CNode* pnode) {
            pnode->PushInventory(inv);
        });
    }

    // Get note commitment from the Sapling output
    std::string noteCommitment = "pending"; // Note commitment is derived from the output
    if (txRef->IsShieldedTx() && txRef->sapData->vShieldedOutput.size() > 0) {
        noteCommitment = txRef->sapData->vShieldedOutput[0].cmu.GetHex();
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", txRef->GetHash().GetHex());
    result.pushKV("amount", ValueFromAmount(nAmount));
    result.pushKV("lock_height", (int64_t)nLockHeight);
    result.pushKV("maturity_height", (int64_t)(nLockHeight + maturityBlocks));
    result.pushKV("note_commitment", noteCommitment);
    result.pushKV("sapling_address", KeyIO::EncodePaymentAddress(saplingAddr));

    return result;
}

/**
 * unlock - Unlock ZKHU to KHU (ZKHU → KHU + Yield)
 *
 * Nomenclature: ZKHU (staked) → KHU (locked) + Yield
 * Converts ZKHU shielded staking notes back to KHU with accumulated yield.
 *
 * IMPORTANT: UNLOCK applies DOUBLE FLUX:
 *            C += yield, U += (P+yield), Z -= P, Cr -= yield, Ur -= yield
 *            This preserves invariants while transferring yield from reward pool.
 */
static UniValue khuunlock(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1) {
        throw std::runtime_error(
            "unlock ( \"note_commitment\" )\n"
            "\nUnlock ZKHU shielded staking notes back to KHU with accumulated yield.\n"
            "\nArguments:\n"
            "1. note_commitment  (string, optional) Specific note to unlock (default: oldest mature note)\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\": \"hash\",           (string) Transaction ID\n"
            "  \"principal\": n,           (numeric) Original locked amount\n"
            "  \"yield_bonus\": n,         (numeric) Accumulated yield bonus\n"
            "  \"total\": n,               (numeric) Total amount received (principal + yield)\n"
            "  \"lock_duration_blocks\": n,(numeric) How long the note was locked\n"
            "  \"lock_duration_days\": n   (numeric) Approximate days locked\n"
            "}\n"
            "\nNotes:\n"
            "- Requires 4320 blocks maturity (3 days minimum lock)\n"
            "- Yield is calculated based on R_annual and lock duration\n"
            "- UNLOCK applies DOUBLE FLUX: C+, U+, Z-, Cr-, Ur- (preserves invariants)\n"
            "\nExamples:\n"
            + HelpExampleCli("unlock", "")
            + HelpExampleCli("unlock", "\"abc123...\"")
            + HelpExampleRpc("unlock", "")
        );
    }

    CWallet* pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) {
        throw JSONRPCError(RPC_WALLET_NOT_FOUND, "Wallet not found");
    }

    EnsureWalletIsUnlocked(pwallet);

    LOCK2(cs_main, pwallet->cs_wallet);

    // Check KHU activation
    int nCurrentHeight = chainActive.Height();
    const Consensus::Params& consensus = Params().GetConsensus();

    if (!consensus.NetworkUpgradeActive(nCurrentHeight, Consensus::UPGRADE_V6_0)) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "KHU system not yet activated");
    }

    // Check Sapling activation
    if (!consensus.NetworkUpgradeActive(nCurrentHeight, Consensus::UPGRADE_V5_0)) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "Sapling not yet activated (required for ZKHU)");
    }

    const uint32_t ZKHU_MATURITY_BLOCKS = GetZKHUMaturityBlocks();  // Network-aware

    // ═══════════════════════════════════════════════════════════════════════
    // FEE CALCULATION: Use shielded fee formula like PIVX Sapling transactions
    // Formula: minRelayTxFee.GetFee(txSize) * K, where K = 100 for shielded tx
    // See: validation.cpp GetShieldedTxMinFee()
    // ═══════════════════════════════════════════════════════════════════════
    const size_t BASE_SAPLING_TX_SIZE = 500;    // Base Sapling tx overhead
    const size_t INPUT_SIZE = 180;               // Per transparent input
    const size_t SAPLING_SPEND_SIZE = 384;       // Sapling shielded spend (SpendDescription)
    const size_t OUTPUT_SIZE = 34;               // Transparent output
    const unsigned int SHIELDED_FEE_K = 100;     // HU shielded tx fee multiplier

    // Estimate: 1 Sapling spend + 1 PIV input + 2 outputs (KHU + PIV change)
    size_t nEstimatedSize = BASE_SAPLING_TX_SIZE + SAPLING_SPEND_SIZE + INPUT_SIZE + (2 * OUTPUT_SIZE);
    // Use shielded fee: minRelayTxFee * K (same as PIVX Sapling transactions)
    CAmount nFee = ::minRelayTxFee.GetFee(nEstimatedSize) * SHIELDED_FEE_K;

    // ═══════════════════════════════════════════════════════════════════════
    // CLAUDE.md §2.1: "Tous les frais KHU sont payés en PIV non-bloqué"
    // Fee is paid from separate PIV input, NOT from yield/principal
    // ═══════════════════════════════════════════════════════════════════════

    // Check PIV balance for fee (separate from KHU)
    CAmount nPIVBalance = pwallet->GetAvailableBalance();
    if (nFee > nPIVBalance) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            strprintf("Insufficient PIV for fee: have %s, need %s",
                     FormatMoney(nPIVBalance), FormatMoney(nFee)));
    }

    // Select PIV UTXO for fee (smallest one >= fee)
    std::vector<COutput> vPIVCoins;
    pwallet->AvailableCoins(&vPIVCoins);

    COutPoint pivFeeInput;
    CScript pivFeeScript;
    CAmount nPIVInputValue = 0;
    bool foundPIVInput = false;

    // Sort by value ascending to find smallest suitable UTXO
    std::sort(vPIVCoins.begin(), vPIVCoins.end(),
        [](const COutput& a, const COutput& b) {
            return a.tx->tx->vout[a.i].nValue < b.tx->tx->vout[b.i].nValue;
        });

    for (const COutput& out : vPIVCoins) {
        CAmount value = out.tx->tx->vout[out.i].nValue;
        if (value >= nFee) {
            pivFeeInput = COutPoint(out.tx->tx->GetHash(), out.i);
            pivFeeScript = out.tx->tx->vout[out.i].scriptPubKey;
            nPIVInputValue = value;
            foundPIVInput = true;
            break;
        }
    }

    if (!foundPIVInput) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            "No suitable PIV UTXO found for transaction fee");
    }

    // Find the ZKHU note to unlock
    ZKHUNoteEntry* targetNote = nullptr;
    uint256 targetCm;

    if (!request.params[0].isNull()) {
        // User specified a specific note
        targetCm = uint256S(request.params[0].get_str());
        auto it = pwallet->khuData.mapZKHUNotes.find(targetCm);
        if (it == pwallet->khuData.mapZKHUNotes.end()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Note commitment not found in wallet");
        }
        targetNote = &it->second;
    } else {
        // Find oldest mature note
        int oldestHeight = INT_MAX;
        for (auto& pair : pwallet->khuData.mapZKHUNotes) {
            ZKHUNoteEntry& entry = pair.second;
            if (entry.fSpent) continue;
            if (!entry.IsMature(nCurrentHeight)) continue;

            if (entry.nConfirmedHeight < oldestHeight) {
                oldestHeight = entry.nConfirmedHeight;
                targetNote = &entry;
                targetCm = pair.first;
            }
        }
    }

    if (!targetNote) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            "No mature ZKHU notes available for unstaking. "
            "Notes require 4320 blocks (3 days) maturity.");
    }

    if (targetNote->fSpent) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Note has already been spent");
    }

    if (!targetNote->IsMature(nCurrentHeight)) {
        int blocksRemaining = ZKHU_MATURITY_BLOCKS - targetNote->GetBlocksLocked(nCurrentHeight);
        throw JSONRPCError(RPC_INVALID_PARAMETER,
            strprintf("Note not mature yet. %d blocks remaining (approximately %.1f days)",
                     blocksRemaining, blocksRemaining / 1440.0));
    }

    // Get the corresponding Sapling note from wallet using GetNotes()
    // This properly decrypts the note and retrieves the correct rcm (randomness)
    const SaplingOutPoint& saplingOp = targetNote->op;
    std::vector<SaplingOutPoint> saplingOutpoints = {saplingOp};
    std::vector<SaplingNoteEntry> saplingEntries;

    SaplingScriptPubKeyMan* saplingMan = pwallet->GetSaplingScriptPubKeyMan();
    if (!saplingMan) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Sapling not enabled in wallet");
    }

    saplingMan->GetNotes(saplingOutpoints, saplingEntries);

    if (saplingEntries.empty()) {
        throw JSONRPCError(RPC_WALLET_ERROR,
            "Could not retrieve Sapling note data. The note may not belong to this wallet "
            "or the wallet may need to be rescanned.");
    }

    const SaplingNoteEntry& noteEntry = saplingEntries[0];
    const libzcash::SaplingNote& note = noteEntry.note;  // Contains correct rcm!
    libzcash::SaplingPaymentAddress saplingAddr = noteEntry.address;
    CAmount principal = note.value();

    // Get the spending key
    libzcash::SaplingExtendedSpendingKey sk;
    if (!pwallet->GetSaplingExtendedSpendingKey(saplingAddr, sk)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Spending key not found for note address");
    }

    // Get actual accumulated yield from consensus database
    // CRITICAL: The RPC MUST use the same yield value as consensus
    // to avoid "output amount mismatch" errors during block validation
    int blocksLocked = targetNote->GetBlocksLocked(nCurrentHeight);
    uint32_t daysLocked = blocksLocked / 1440;

    CAmount yieldBonus = 0;
    CZKHUTreeDB* zkhuDB = GetZKHUDB();
    if (zkhuDB) {
        ZKHUNoteData consensusNote;
        if (zkhuDB->ReadNote(targetCm, consensusNote)) {
            yieldBonus = consensusNote.Ur_accumulated;
            LogPrint(BCLog::HU, "khuunlock: Using consensus yield=%s for cm=%s\n",
                     FormatMoney(yieldBonus), targetCm.GetHex().substr(0, 16).c_str());
        } else {
            LogPrintf("khuunlock: WARNING - note not found in consensus DB, yieldBonus=0\n");
        }
    }

    // CLAUDE.md §2.1: Fee is separate in PIV, NOT deducted from KHU output
    CAmount totalKHUOutput = principal + yieldBonus;
    if (totalKHUOutput <= 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Output amount would be zero or negative");
    }

    // PIV change output (if any)
    CAmount nPIVChange = nPIVInputValue - nFee;

    // Get witness and anchor for the note
    std::vector<SaplingOutPoint> ops = {saplingOp};
    std::vector<Optional<SaplingWitness>> witnesses;
    uint256 anchor;
    saplingMan->GetSaplingNoteWitnesses(ops, witnesses, anchor);

    // Diagnostic: Check if LOCK tx is in wallet and has Sapling note data
    {
        auto wtxIt = pwallet->mapWallet.find(saplingOp.hash);
        if (wtxIt == pwallet->mapWallet.end()) {
            LogPrintf("khuunlock: DIAGNOSTIC - LOCK tx NOT in mapWallet! txid=%s\n",
                      saplingOp.hash.GetHex().substr(0, 16));
        } else {
            const CWalletTx& wtx = wtxIt->second;
            LogPrintf("khuunlock: DIAGNOSTIC - LOCK tx in mapWallet, txid=%s, "
                      "mapSaplingNoteData.size=%zu, IsShieldedTx=%d\n",
                      saplingOp.hash.GetHex().substr(0, 16),
                      wtx.mapSaplingNoteData.size(),
                      wtx.tx->IsShieldedTx() ? 1 : 0);

            auto ndIt = wtx.mapSaplingNoteData.find(saplingOp);
            if (ndIt == wtx.mapSaplingNoteData.end()) {
                LogPrintf("khuunlock: DIAGNOSTIC - SaplingOutPoint NOT in mapSaplingNoteData\n");
            } else {
                LogPrintf("khuunlock: DIAGNOSTIC - SaplingOutPoint in mapSaplingNoteData, "
                          "witnesses.size=%zu, witnessHeight=%d\n",
                          ndIt->second.witnesses.size(),
                          ndIt->second.witnessHeight);
            }
        }
    }

    SaplingWitness witness;
    bool usedFallback = false;
    if (witnesses.empty() || !witnesses[0]) {
        // Fallback: compute witness by scanning blockchain
        // TODO: Realign ZKHU on standard Sapling note/witness pipeline
        //       (FindMySaplingNotes + IncrementNoteWitnesses)
        //       This fallback is temporary for testing.
        LogPrintf("khuunlock: WITNESS_SOURCE=FALLBACK (wallet cache miss), computing from blockchain...\n");
        usedFallback = true;

        if (!ComputeWitnessForZKHUNote(targetCm, targetNote->nConfirmedHeight, witness, anchor)) {
            throw JSONRPCError(RPC_WALLET_ERROR,
                "Failed to compute witness for ZKHU note. The note may not exist in the blockchain. "
                "Try running 'khurescannotes' or restarting the wallet.");
        }
    } else {
        LogPrintf("khuunlock: WITNESS_SOURCE=STANDARD_PIPELINE (wallet cache hit)\n");
        witness = witnesses[0].get();
    }

    // Build transaction using TransactionBuilder
    TransactionBuilder builder(consensus, pwallet);
    builder.SetFee(nFee);
    builder.SetType(CTransaction::TxType::KHU_UNLOCK);  // Set type BEFORE building

    // Create and serialize UNLOCK payload with note commitment (Phase 5/8 fix)
    // This allows consensus to look up the note directly by cm without nullifier mapping
    CUnlockKHUPayload unlockPayload(targetCm);
    CDataStream payloadStream(SER_NETWORK, PROTOCOL_VERSION);
    payloadStream << unlockPayload;
    builder.SetExtraPayload(std::vector<uint8_t>(payloadStream.begin(), payloadStream.end()));

    // Add the Sapling spend (ZKHU note)
    builder.AddSaplingSpend(sk.expsk, note, anchor, witness);

    // Add PIV transparent input for fee (CLAUDE.md §2.1)
    builder.AddTransparentInput(pivFeeInput, pivFeeScript, nPIVInputValue);

    // ═══════════════════════════════════════════════════════════════════════
    // PRIVACY ENHANCEMENT: Split output into 2 parts with random ratio (20-80%)
    // This makes it harder to link LOCK input to UNLOCK output
    // ═══════════════════════════════════════════════════════════════════════
    int splitPercent = 20 + GetRandInt(61);  // Random 20% to 80%
    CAmount part1 = (totalKHUOutput * splitPercent) / 100;
    CAmount part2 = totalKHUOutput - part1;  // Remainder to ensure exact sum

    // Get 2 fresh addresses for privacy
    CPubKey newKey1, newKey2;
    if (!pwallet->GetKeyFromPool(newKey1, false)) {
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out for KHU output 1");
    }
    if (!pwallet->GetKeyFromPool(newKey2, false)) {
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out for KHU output 2");
    }

    // Add both outputs (total = principal + yield, split randomly)
    builder.AddTransparentOutput(newKey1.GetID(), part1);
    builder.AddTransparentOutput(newKey2.GetID(), part2);

    LogPrint(BCLog::HU, "khuunlock: Privacy split %d%% - part1=%s, part2=%s (total=%s)\n",
             splitPercent, FormatMoney(part1), FormatMoney(part2), FormatMoney(totalKHUOutput));

    // Set transparent change address for PIV change (let builder handle it)
    // The builder will calculate: change = valueBalance - fee + tIns - tOuts
    // For UNLOCK: change = principal - fee + PIVinput - (principal) = PIVinput - fee
    CPubKey pivChangeKey;
    if (!pwallet->GetKeyFromPool(pivChangeKey, true)) {
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out for PIV change");
    }
    builder.SendChangeTo(pivChangeKey.GetID());

    // Build with dummy signatures first
    TransactionBuilderResult buildResult = builder.Build(true);
    if (buildResult.IsError()) {
        throw JSONRPCError(RPC_WALLET_ERROR,
            strprintf("Failed to build unlock transaction: %s", buildResult.GetError()));
    }

    // Clear dummy signatures/proofs before creating real ones
    builder.ClearProofsAndSignatures();

    // Prove and sign
    TransactionBuilderResult proveResult = builder.ProveAndSign();
    if (proveResult.IsError()) {
        throw JSONRPCError(RPC_WALLET_ERROR,
            strprintf("Failed to prove/sign unlock transaction: %s", proveResult.GetError()));
    }

    CTransaction finalTx = proveResult.GetTxOrThrow();

    // Transaction type was already set via builder.SetType() before Build()
    // DO NOT modify the transaction after Build() - it would invalidate Sapling proofs
    CTransactionRef txRef = MakeTransactionRef(finalTx);

    // Broadcast transaction
    CValidationState validationState;
    if (!AcceptToMemoryPool(mempool, validationState, txRef, false, nullptr)) {
        throw JSONRPCError(RPC_TRANSACTION_REJECTED,
            strprintf("Transaction rejected: %s", FormatStateMessage(validationState)));
    }

    // Relay transaction to peers
    if (g_connman) {
        CInv inv(MSG_TX, txRef->GetHash());
        g_connman->ForEachNode([&inv](CNode* pnode) {
            pnode->PushInventory(inv);
        });
    }

    // Mark the note as spent locally (consensus will verify)
    MarkZKHUNoteSpent(pwallet, targetNote->nullifier);

    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", txRef->GetHash().GetHex());
    result.pushKV("principal", ValueFromAmount(principal));
    result.pushKV("yield_bonus", ValueFromAmount(yieldBonus));
    result.pushKV("total", ValueFromAmount(totalKHUOutput)); // Full amount (fee is separate in PIV)
    result.pushKV("fee", ValueFromAmount(nFee));
    result.pushKV("lock_duration_blocks", blocksLocked);
    result.pushKV("lock_duration_days", (double)daysLocked);

    // Privacy split info
    UniValue splitInfo(UniValue::VOBJ);
    splitInfo.pushKV("split_percent", splitPercent);
    splitInfo.pushKV("output1", ValueFromAmount(part1));
    splitInfo.pushKV("output2", ValueFromAmount(part2));
    result.pushKV("privacy_split", splitInfo);

    return result;
}

/**
 * khudiagnostics - Comprehensive KHU state diagnostic (wallet-only, read-only)
 *
 * Returns a unified view of KHU state from both consensus and wallet perspectives.
 * Useful for debugging wallet/consensus mismatches.
 *
 * NO STATE MODIFICATION - pure read operation.
 */
static UniValue khudiagnostics(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1) {
        throw std::runtime_error(
            "piv2diagnostics ( verbose )\n"
            "\nReturns comprehensive KHU diagnostic information.\n"
            "\nThis command is READ-ONLY and does NOT modify any state.\n"
            "Useful for debugging wallet/consensus synchronization issues.\n"
            "\nArguments:\n"
            "1. verbose    (boolean, optional, default=false) Include detailed UTXO/note lists\n"
            "\nResult:\n"
            "{\n"
            "  \"consensus_state\": {         (object) State from HuGlobalState\n"
            "    \"C\": n,                    (numeric) PIV collateral\n"
            "    \"U\": n,                    (numeric) KHU_T transparent supply\n"
            "    \"Z\": n,                    (numeric) ZKHU shielded supply\n"
            "    \"Cr\": n,                   (numeric) Reward pool\n"
            "    \"Ur\": n,                   (numeric) Reward rights\n"
            "    \"T\": n,                    (numeric) DAO Treasury\n"
            "    \"R_annual_pct\": n,         (numeric) Annual yield rate %\n"
            "    \"height\": n,               (numeric) State height\n"
            "    \"invariants_ok\": true|false\n"
            "  },\n"
            "  \"wallet_khu_utxos\": {        (object) KHU_T UTXOs in wallet\n"
            "    \"count\": n,                (numeric) Number of UTXOs\n"
            "    \"total\": n,                (numeric) Sum of UTXO amounts\n"
            "    \"by_origin\": {             (object) Breakdown by parent tx type\n"
            "      \"mint\": n,\n"
            "      \"redeem_change\": n,\n"
            "      \"lock_change\": n,\n"
            "      \"unlock\": n,\n"
            "      \"other\": n\n"
            "    },\n"
            "    \"utxos\": [ ... ]           (array, if verbose) Detailed UTXO list\n"
            "  },\n"
            "  \"wallet_locked_notes\": {     (object) ZKHU notes in wallet\n"
            "    \"count\": n,                (numeric) Total note count\n"
            "    \"total\": n,                (numeric) Sum of note amounts (principal)\n"
            "    \"mature_count\": n,         (numeric) Mature notes (>= 4320 blocks)\n"
            "    \"mature_total\": n,         (numeric) Sum of mature note amounts\n"
            "    \"immature_count\": n,       (numeric) Immature notes (< 4320 blocks)\n"
            "    \"immature_total\": n,       (numeric) Sum of immature note amounts\n"
            "    \"notes\": [ ... ]           (array, if verbose) Detailed note list\n"
            "  },\n"
            "  \"sync_status\": {             (object) Wallet/consensus sync check\n"
            "    \"wallet_U_matches_consensus\": true|false,\n"
            "    \"wallet_Z_matches_consensus\": true|false,\n"
            "    \"wallet_U\": n,             (numeric) Wallet's view of U\n"
            "    \"consensus_U\": n,          (numeric) Consensus U\n"
            "    \"wallet_Z\": n,             (numeric) Wallet's view of Z\n"
            "    \"consensus_Z\": n,          (numeric) Consensus Z\n"
            "    \"discrepancy_U\": n,        (numeric) Difference (wallet - consensus)\n"
            "    \"discrepancy_Z\": n         (numeric) Difference (wallet - consensus)\n"
            "  }\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("piv2diagnostics", "")
            + HelpExampleCli("piv2diagnostics", "true")
            + HelpExampleRpc("piv2diagnostics", "true")
        );
    }

    CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) {
        throw JSONRPCError(RPC_WALLET_NOT_FOUND, "Wallet not found");
    }

    bool fVerbose = false;
    if (!request.params[0].isNull()) {
        // Accept both boolean and integer (for CLI compatibility)
        if (request.params[0].isBool()) {
            fVerbose = request.params[0].get_bool();
        } else if (request.params[0].isNum()) {
            fVerbose = (request.params[0].get_int() != 0);
        } else if (request.params[0].isStr()) {
            std::string val = request.params[0].get_str();
            fVerbose = (val == "true" || val == "1");
        }
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    int nCurrentHeight = chainActive.Height();
    const uint32_t ZKHU_MATURITY_BLOCKS = GetZKHUMaturityBlocks();  // Network-aware

    // ═══════════════════════════════════════════════════════════════════════
    // SECTION 1: Consensus State (from HuGlobalState)
    // ═══════════════════════════════════════════════════════════════════════
    UniValue consensusState(UniValue::VOBJ);
    HuGlobalState state;
    bool hasState = GetCurrentKHUState(state);

    if (hasState) {
        consensusState.pushKV("C", ValueFromAmount(state.C));
        consensusState.pushKV("U", ValueFromAmount(state.U));
        consensusState.pushKV("Z", ValueFromAmount(state.Z));
        consensusState.pushKV("Cr", ValueFromAmount(state.Cr));
        consensusState.pushKV("Ur", ValueFromAmount(state.Ur));
        consensusState.pushKV("T", ValueFromAmount(state.T));
        consensusState.pushKV("R_annual_pct", state.R_annual / 100.0);
        consensusState.pushKV("height", (int64_t)state.nHeight);
        consensusState.pushKV("invariants_ok", state.CheckInvariants());
    } else {
        consensusState.pushKV("C", 0);
        consensusState.pushKV("U", 0);
        consensusState.pushKV("Z", 0);
        consensusState.pushKV("Cr", 0);
        consensusState.pushKV("Ur", 0);
        consensusState.pushKV("T", 0);
        consensusState.pushKV("R_annual_pct", 0.0);
        consensusState.pushKV("height", nCurrentHeight);
        consensusState.pushKV("invariants_ok", true);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // SECTION 2: Wallet KHU UTXOs (from mapKHUCoins)
    // ═══════════════════════════════════════════════════════════════════════
    UniValue walletUtxos(UniValue::VOBJ);
    CAmount totalUtxoAmount = 0;
    int utxoCount = 0;

    // Track origin by looking at parent transaction type
    CAmount fromMint = 0;
    CAmount fromRedeemChange = 0;
    CAmount fromLockChange = 0;
    CAmount fromUnlock = 0;
    CAmount fromOther = 0;

    UniValue utxoList(UniValue::VARR);

    for (const auto& pair : pwallet->khuData.mapKHUCoins) {
        const COutPoint& outpoint = pair.first;
        const KHUCoinEntry& entry = pair.second;

        // Skip locked UTXOs (they're tracked separately as ZKHU notes)
        if (entry.coin.fLocked) continue;

        totalUtxoAmount += entry.coin.amount;
        utxoCount++;

        // Determine origin by looking up parent tx type
        std::string origin = "unknown";
        const CWalletTx* parentWtx = pwallet->GetWalletTx(outpoint.hash);
        if (parentWtx && parentWtx->tx) {
            CTransaction::TxType txType = static_cast<CTransaction::TxType>(parentWtx->tx->nType);
            switch (txType) {
                case CTransaction::TxType::KHU_MINT:
                    origin = "mint";
                    fromMint += entry.coin.amount;
                    break;
                case CTransaction::TxType::KHU_REDEEM:
                    origin = "redeem_change";
                    fromRedeemChange += entry.coin.amount;
                    break;
                case CTransaction::TxType::KHU_LOCK:
                    origin = "lock_change";
                    fromLockChange += entry.coin.amount;
                    break;
                case CTransaction::TxType::KHU_UNLOCK:
                    origin = "unlock";
                    fromUnlock += entry.coin.amount;
                    break;
                default:
                    origin = "other";
                    fromOther += entry.coin.amount;
                    break;
            }
        } else {
            fromOther += entry.coin.amount;
        }

        if (fVerbose) {
            UniValue utxoObj(UniValue::VOBJ);
            utxoObj.pushKV("txid", outpoint.hash.GetHex());
            utxoObj.pushKV("vout", (int64_t)outpoint.n);
            utxoObj.pushKV("amount", ValueFromAmount(entry.coin.amount));
            utxoObj.pushKV("confirmed_height", (int64_t)entry.nConfirmedHeight);
            utxoObj.pushKV("origin", origin);
            utxoList.push_back(utxoObj);
        }
    }

    walletUtxos.pushKV("count", utxoCount);
    walletUtxos.pushKV("total", ValueFromAmount(totalUtxoAmount));

    UniValue byOrigin(UniValue::VOBJ);
    byOrigin.pushKV("mint", ValueFromAmount(fromMint));
    byOrigin.pushKV("redeem_change", ValueFromAmount(fromRedeemChange));
    byOrigin.pushKV("lock_change", ValueFromAmount(fromLockChange));
    byOrigin.pushKV("unlock", ValueFromAmount(fromUnlock));
    byOrigin.pushKV("other", ValueFromAmount(fromOther));
    walletUtxos.pushKV("by_origin", byOrigin);

    if (fVerbose) {
        walletUtxos.pushKV("utxos", utxoList);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // SECTION 3: Wallet Locked Notes (from mapZKHUNotes)
    // ═══════════════════════════════════════════════════════════════════════
    UniValue walletNotes(UniValue::VOBJ);
    int noteCount = 0;
    int matureCount = 0;
    int immatureCount = 0;
    CAmount totalNoteAmount = 0;
    CAmount matureAmount = 0;
    CAmount immatureAmount = 0;

    UniValue noteList(UniValue::VARR);

    for (const auto& pair : pwallet->khuData.mapZKHUNotes) {
        const uint256& cm = pair.first;
        const ZKHUNoteEntry& entry = pair.second;

        // Skip spent notes
        if (entry.fSpent) continue;

        noteCount++;
        totalNoteAmount += entry.amount;

        bool isMature = entry.IsMature(nCurrentHeight);
        int blocksLocked = entry.GetBlocksLocked(nCurrentHeight);

        if (isMature) {
            matureCount++;
            matureAmount += entry.amount;
        } else {
            immatureCount++;
            immatureAmount += entry.amount;
        }

        if (fVerbose) {
            UniValue noteObj(UniValue::VOBJ);
            noteObj.pushKV("cm", cm.GetHex());
            noteObj.pushKV("amount", ValueFromAmount(entry.amount));
            noteObj.pushKV("lock_height", (int64_t)entry.nLockStartHeight);
            noteObj.pushKV("blocks_locked", blocksLocked);
            noteObj.pushKV("is_mature", isMature);
            if (!isMature) {
                noteObj.pushKV("blocks_to_mature", (int64_t)(ZKHU_MATURITY_BLOCKS - blocksLocked));
            }
            noteList.push_back(noteObj);
        }
    }

    walletNotes.pushKV("count", noteCount);
    walletNotes.pushKV("total", ValueFromAmount(totalNoteAmount));
    walletNotes.pushKV("mature_count", matureCount);
    walletNotes.pushKV("mature_total", ValueFromAmount(matureAmount));
    walletNotes.pushKV("immature_count", immatureCount);
    walletNotes.pushKV("immature_total", ValueFromAmount(immatureAmount));

    if (fVerbose) {
        walletNotes.pushKV("notes", noteList);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // SECTION 4: Sync Status (compare wallet vs consensus)
    // ═══════════════════════════════════════════════════════════════════════
    UniValue syncStatus(UniValue::VOBJ);

    CAmount walletU = totalUtxoAmount;  // Wallet's view of U (transparent KHU)
    CAmount walletZ = totalNoteAmount;  // Wallet's view of Z (lockd ZKHU)
    CAmount consensusU = hasState ? state.U : 0;
    CAmount consensusZ = hasState ? state.Z : 0;

    // Note: Wallet's view may not match consensus if:
    // - Wallet doesn't track ALL KHU UTXOs (e.g., sent to other addresses)
    // - Pending transactions not yet confirmed
    // - Wallet needs rescan
    // This is expected - wallet tracks only OUR UTXOs, consensus tracks ALL

    syncStatus.pushKV("wallet_U", ValueFromAmount(walletU));
    syncStatus.pushKV("consensus_U", ValueFromAmount(consensusU));
    syncStatus.pushKV("wallet_Z", ValueFromAmount(walletZ));
    syncStatus.pushKV("consensus_Z", ValueFromAmount(consensusZ));
    syncStatus.pushKV("discrepancy_U", ValueFromAmount(walletU - consensusU));
    syncStatus.pushKV("discrepancy_Z", ValueFromAmount(walletZ - consensusZ));

    // Match check: wallet should be <= consensus (wallet tracks OUR coins only)
    // A mismatch (wallet > consensus) indicates a bug
    bool uOk = (walletU <= consensusU);
    bool zOk = (walletZ <= consensusZ);
    syncStatus.pushKV("wallet_U_valid", uOk);
    syncStatus.pushKV("wallet_Z_valid", zOk);

    // ═══════════════════════════════════════════════════════════════════════
    // Build final result
    // ═══════════════════════════════════════════════════════════════════════
    UniValue result(UniValue::VOBJ);
    result.pushKV("consensus_state", consensusState);
    result.pushKV("wallet_khu_utxos", walletUtxos);
    result.pushKV("wallet_locked_notes", walletNotes);
    result.pushKV("sync_status", syncStatus);

    return result;
}

/**
 * listlocked - List locked ZKHU staking notes
 *
 * Lists all ZKHU notes owned by this wallet with their staking status.
 * Nomenclature: ZKHU = staked KHU (with yield accumulation)
 */
static UniValue khulistlocked(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 0) {
        throw std::runtime_error(
            "listlocked\n"
            "\nList all ZKHU staking notes belonging to this wallet.\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"note_commitment\": \"hash\",  (string) Note commitment (cm)\n"
            "    \"amount\": n,                (numeric) Locked amount in ZKHU\n"
            "    \"lock_height\": n,           (numeric) Lock start height\n"
            "    \"blocks_locked\": n,         (numeric) Blocks since lock\n"
            "    \"is_mature\": true|false,    (boolean) Can be unlocked (>= 4320 blocks)\n"
            "    \"estimated_yield\": n        (numeric) Estimated yield bonus\n"
            "  },\n"
            "  ...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("listlocked", "")
            + HelpExampleRpc("listlocked", "")
        );
    }

    CWallet* pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) {
        throw JSONRPCError(RPC_WALLET_NOT_FOUND, "Wallet not found");
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    int nCurrentHeight = chainActive.Height();
    const uint32_t ZKHU_MATURITY_BLOCKS = GetZKHUMaturityBlocks();  // Network-aware

    // Get current R_annual for yield estimation
    HuGlobalState state;
    uint16_t R_annual = 0;
    if (GetCurrentKHUState(state)) {
        R_annual = state.R_annual;
    }

    UniValue results(UniValue::VARR);

    // Iterate through wallet's ZKHU notes
    std::vector<ZKHUNoteEntry> unspentNotes = GetUnspentZKHUNotes(pwallet);

    for (const ZKHUNoteEntry& entry : unspentNotes) {
        int blocksLocked = entry.GetBlocksLocked(nCurrentHeight);
        bool isMature = entry.IsMature(nCurrentHeight);

        // Estimate yield (approximation for display)
        CAmount estimatedYield = 0;
        if (R_annual > 0 && blocksLocked >= (int)ZKHU_MATURITY_BLOCKS) {
            uint32_t daysLocked = blocksLocked / 1440;
            CAmount annualYield = (entry.amount * R_annual) / 10000;
            estimatedYield = (annualYield * daysLocked) / 365;
        }

        UniValue noteObj(UniValue::VOBJ);
        noteObj.pushKV("note_commitment", entry.cm.GetHex());
        noteObj.pushKV("amount", ValueFromAmount(entry.amount));
        noteObj.pushKV("lock_height", (int64_t)entry.nLockStartHeight);
        noteObj.pushKV("blocks_locked", blocksLocked);
        noteObj.pushKV("is_mature", isMature);
        noteObj.pushKV("estimated_yield", ValueFromAmount(estimatedYield));

        results.push_back(noteObj);
    }

    return results;
}

/**
 * getauditstate - Complete monetary audit with supply breakdown and invariant verification
 *
 * Shows global supply state with clear breakdown:
 * - PIV2 total supply and distribution
 * - KHU (minted from PIV2) breakdown
 * - ZKHU (staked KHU) with yield info
 * - All economic invariants
 * - Global alarm status
 */
static UniValue khuauditstate(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 0) {
        throw std::runtime_error(
            "getauditstate\n"
            "\nComplete monetary audit showing global supply state and invariant verification.\n"
            "\nThis command provides full visibility into the monetary system:\n"
            "- Global supply breakdown (PIV2, KHU, ZKHU)\n"
            "- Yield and fee statistics\n"
            "- Economic invariant checks\n"
            "- Inflation monitoring\n"
            "- Global alarm status\n"
            "\nExamples:\n"
            + HelpExampleCli("getauditstate", "")
            + HelpExampleRpc("getauditstate", "")
        );
    }

    LOCK(cs_main);

    int nHeight = chainActive.Height();

    // Get current KHU state
    HuGlobalState state;
    bool hasState = GetCurrentKHUState(state);

    // Initialize values from state
    CAmount C = hasState ? state.C : 0;      // Total KHU collateral (PIV2 locked)
    CAmount U = hasState ? state.U : 0;      // KHU transparent (spendable)
    CAmount Z = hasState ? state.Z : 0;      // ZKHU staked (locked for yield)
    CAmount Cr = hasState ? state.Cr : 0;    // Cumulative redeemed (KHU -> PIV2)
    CAmount Ur = hasState ? state.Ur : 0;    // Cumulative unlocked (ZKHU -> KHU)
    CAmount T = hasState ? state.T : 0;      // DAO Treasury balance
    int R_annual = hasState ? state.R_annual : 4000;  // Annual yield rate in basis points

    // Calculate supply metrics
    // Genesis premine for testnet
    CAmount genesisPremine = 500000 * COIN;
    // Block rewards (estimate: 10 PIV2 per block after genesis)
    CAmount blockRewards = (nHeight > 0) ? (nHeight - 1) * 10 * COIN : 0;
    // Estimated total PIV2 supply
    CAmount estimatedTotalSupply = genesisPremine + blockRewards;
    // PIV2 liquid (not locked as KHU collateral, not in DAO treasury)
    CAmount piv2Liquid = estimatedTotalSupply - C - T;  // Total - Collateral - DAO Treasury

    // Calculate yield metrics
    // Annual yield rate: R_annual basis points (e.g., 4000 = 40%)
    double annualYieldPercent = R_annual / 100.0;
    // Estimated daily yield on current ZKHU balance
    CAmount dailyYieldEstimate = (Z * R_annual) / (365 * 10000);
    // Estimated annual inflation from yield (if all ZKHU earns full year)
    CAmount annualYieldInflation = (Z * R_annual) / 10000;

    // ═══════════════════════════════════════════════════════════════════════
    // BUILD RESULT
    // ═══════════════════════════════════════════════════════════════════════
    UniValue result(UniValue::VOBJ);

    // Header
    result.pushKV("block_height", nHeight);
    result.pushKV("timestamp", GetTime());

    // ═══════════════════════════════════════════════════════════════════════
    // 1. GLOBAL SUPPLY
    // ═══════════════════════════════════════════════════════════════════════
    UniValue globalSupply(UniValue::VOBJ);
    globalSupply.pushKV("piv2_total_estimated", ValueFromAmount(estimatedTotalSupply));
    globalSupply.pushKV("piv2_liquid", ValueFromAmount(piv2Liquid));
    globalSupply.pushKV("piv2_locked_as_khu", ValueFromAmount(C));
    globalSupply.pushKV("dao_treasury", ValueFromAmount(T));
    result.pushKV("global_supply", globalSupply);

    // ═══════════════════════════════════════════════════════════════════════
    // 2. KHU BREAKDOWN (Minted from PIV2)
    // ═══════════════════════════════════════════════════════════════════════
    UniValue khuState(UniValue::VOBJ);
    khuState.pushKV("total_minted_C", ValueFromAmount(C));
    khuState.pushKV("transparent_U", ValueFromAmount(U));
    khuState.pushKV("staked_as_zkhu_Z", ValueFromAmount(Z));
    khuState.pushKV("cumulative_redeemed_Cr", ValueFromAmount(Cr));
    khuState.pushKV("formula", strprintf("C = U + Z : %s = %s + %s",
        FormatMoney(C), FormatMoney(U), FormatMoney(Z)));
    result.pushKV("khu_state", khuState);

    // ═══════════════════════════════════════════════════════════════════════
    // 3. ZKHU & YIELD INFO
    // ═══════════════════════════════════════════════════════════════════════
    UniValue zkhuYield(UniValue::VOBJ);
    zkhuYield.pushKV("zkhu_staked_Z", ValueFromAmount(Z));
    zkhuYield.pushKV("cumulative_unlocked_Ur", ValueFromAmount(Ur));
    zkhuYield.pushKV("annual_yield_rate_percent", annualYieldPercent);
    zkhuYield.pushKV("annual_yield_rate_bps", R_annual);
    zkhuYield.pushKV("estimated_daily_yield", ValueFromAmount(dailyYieldEstimate));
    zkhuYield.pushKV("estimated_annual_yield_inflation", ValueFromAmount(annualYieldInflation));
    zkhuYield.pushKV("formula", strprintf("Cr = Ur : %s = %s",
        FormatMoney(Cr), FormatMoney(Ur)));
    result.pushKV("zkhu_yield", zkhuYield);

    // ═══════════════════════════════════════════════════════════════════════
    // 4. INVARIANTS CHECK
    // ═══════════════════════════════════════════════════════════════════════
    UniValue invariants(UniValue::VOBJ);
    UniValue alerts(UniValue::VARR);
    bool allInvariantsOk = true;

    // Invariant 1: C == U + Z (Conservation of KHU)
    bool inv1_ok = (C == U + Z);
    invariants.pushKV("C_equals_U_plus_Z", inv1_ok);
    if (!inv1_ok) {
        allInvariantsOk = false;
        CAmount diff = C - (U + Z);
        std::string alert = strprintf("CRITICAL: KHU Conservation violated! C=%s != U+Z=%s (diff=%s)",
            FormatMoney(C), FormatMoney(U + Z), FormatMoney(diff));
        alerts.push_back(alert);
        LogPrintf("AUDIT ALERT: %s\n", alert);
    }

    // Invariant 2: Cr == Ur (Redemption consistency)
    bool inv2_ok = (Cr == Ur);
    invariants.pushKV("Cr_equals_Ur", inv2_ok);
    if (!inv2_ok) {
        allInvariantsOk = false;
        std::string alert = strprintf("CRITICAL: Redemption mismatch! Cr=%s != Ur=%s",
            FormatMoney(Cr), FormatMoney(Ur));
        alerts.push_back(alert);
        LogPrintf("AUDIT ALERT: %s\n", alert);
    }

    // Invariant 3: No negative values
    bool inv3_ok = (C >= 0 && U >= 0 && Z >= 0 && Cr >= 0 && Ur >= 0 && T >= 0);
    invariants.pushKV("no_negative_values", inv3_ok);
    if (!inv3_ok) {
        allInvariantsOk = false;
        alerts.push_back("CRITICAL: Negative value detected in monetary state!");
        LogPrintf("AUDIT ALERT: Negative value in state!\n");
    }

    // Invariant 4: U <= C and Z <= C (Can't have more transparent/staked than minted)
    bool inv4_ok = (U <= C && Z <= C);
    invariants.pushKV("balances_within_bounds", inv4_ok);
    if (!inv4_ok) {
        allInvariantsOk = false;
        alerts.push_back("CRITICAL: Balance exceeds collateral!");
        LogPrintf("AUDIT ALERT: Balance exceeds collateral!\n");
    }

    // Invariant 5: Yield rate sanity (0-100% annual)
    bool inv5_ok = (R_annual >= 0 && R_annual <= 10000);
    invariants.pushKV("yield_rate_sane", inv5_ok);
    if (!inv5_ok) {
        allInvariantsOk = false;
        alerts.push_back("WARNING: Yield rate outside expected range!");
    }

    invariants.pushKV("all_ok", allInvariantsOk);
    result.pushKV("invariants", invariants);

    // ═══════════════════════════════════════════════════════════════════════
    // 5. INFLATION MONITORING
    // ═══════════════════════════════════════════════════════════════════════
    UniValue inflation(UniValue::VOBJ);

    // Inflation sources
    CAmount inflationFromBlocks = blockRewards;
    CAmount inflationFromYield = annualYieldInflation;

    inflation.pushKV("block_rewards_issued", ValueFromAmount(inflationFromBlocks));
    inflation.pushKV("estimated_annual_yield_emission", ValueFromAmount(inflationFromYield));
    inflation.pushKV("dao_treasury_balance", ValueFromAmount(T));
    inflation.pushKV("net_new_supply", ValueFromAmount(inflationFromBlocks));

    // Inflation rate calculation (annualized)
    double inflationRate = 0.0;
    if (estimatedTotalSupply > 0) {
        // Daily block inflation annualized + yield inflation
        CAmount dailyBlockInflation = 10 * COIN * 1440;  // ~1440 blocks/day
        CAmount annualBlockInflation = dailyBlockInflation * 365;
        inflationRate = ((double)(annualBlockInflation + inflationFromYield) / estimatedTotalSupply) * 100.0;
    }
    inflation.pushKV("estimated_annual_inflation_percent", inflationRate);

    // Anomaly detection
    bool inflationAnomaly = (!inv1_ok || !inv2_ok);  // Any invariant violation is an anomaly
    inflation.pushKV("anomaly_detected", inflationAnomaly);
    if (inflationAnomaly) {
        alerts.push_back("ALERT: Monetary anomaly detected - invariant violation!");
    }

    result.pushKV("inflation_monitor", inflation);

    // ═══════════════════════════════════════════════════════════════════════
    // 6. GLOBAL ALARM STATUS
    // ═══════════════════════════════════════════════════════════════════════
    UniValue alarm(UniValue::VOBJ);

    std::string status;
    std::string statusEmoji;
    if (!inv1_ok || !inv2_ok || !inv3_ok || !inv4_ok) {
        status = "CRITICAL";
        statusEmoji = "[!!!]";
    } else if (!inv5_ok || inflationAnomaly) {
        status = "WARNING";
        statusEmoji = "[!]";
    } else {
        status = "OK";
        statusEmoji = "[OK]";
    }

    alarm.pushKV("status", status);
    alarm.pushKV("indicator", statusEmoji);
    alarm.pushKV("message", status == "OK"
        ? "All monetary invariants verified. System operating normally."
        : "Monetary anomaly detected! Review alerts immediately.");
    alarm.pushKV("alerts_count", (int)alerts.size());
    alarm.pushKV("alerts", alerts);

    // Add info if no KHU operations yet
    if (!hasState && nHeight > 0) {
        alarm.pushKV("info", "KHU system not yet initialized (no mint/redeem operations performed)");
    }

    result.pushKV("alarm", alarm);

    // ═══════════════════════════════════════════════════════════════════════
    // 7. SUMMARY LINE (for quick reading)
    // ═══════════════════════════════════════════════════════════════════════
    std::string summary = strprintf(
        "%s PIV2: %s | KHU(C): %s = U:%s + Z:%s | DAO: %s | Yield: %.1f%%/yr",
        statusEmoji,
        FormatMoney(estimatedTotalSupply),
        FormatMoney(C),
        FormatMoney(U),
        FormatMoney(Z),
        FormatMoney(T),
        annualYieldPercent
    );
    result.pushKV("summary", summary);

    return result;
}

// PIVX V2 Wallet RPC command table
// Nomenclature: HU (transparent), sHU (shielded), KHU (locked), ZKHU (staked)
static const CRPCCommand huWalletCommands[] = {
    //  category    name                      actor (function)            okSafe  argNames
    //  ----------- ------------------------  ------------------------    ------  ----------
    // PIVX V2 balance and info
    { "piv2",         "piv2balance",              &khubalance,                true,   {} },
    { "piv2",         "piv2listunspent",          &khulistunspent,            true,   {"minconf", "maxconf"} },
    { "piv2",         "piv2getinfo",              &khugetinfo,                true,   {} },
    { "piv2",         "piv2send",                 &khusend,                   false,  {"address", "amount", "comment"} },
    { "piv2",         "piv2rescan",               &khurescan,                 false,  {"startheight"} },
    // KHU operations: HU <-> KHU (mint/redeem)
    { "piv2",         "mint",                   &khumint,                   false,  {"amount"} },
    { "piv2",         "redeem",                 &khuredeem,                 false,  {"amount"} },
    // ZKHU operations: KHU <-> ZKHU (lock/unlock with yield)
    { "piv2",         "lock",                   &khulock,                   false,  {"amount"} },
    { "piv2",         "unlock",                 &khuunlock,                 false,  {"note_commitment"} },
    { "piv2",         "listlocked",             &khulistlocked,             true,   {} },
    // Audit & Diagnostics
    { "piv2",         "getauditstate",            &khuauditstate,             true,   {} },
    { "piv2",         "piv2diagnostics",          &khudiagnostics,            true,   {"verbose"} },
};

void RegisterHUWalletRPCCommands(CRPCTable& t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(huWalletCommands); vcidx++)
        t.appendCommand(huWalletCommands[vcidx].name, &huWalletCommands[vcidx]);
}
