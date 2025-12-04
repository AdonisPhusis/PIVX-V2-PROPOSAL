// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "wallet/piv2_wallet.h"

#include "chain.h"
#include "chainparams.h"
#include "piv2/piv2_coins.h"
#include "piv2/piv2_state.h"
#include "piv2/piv2_validation.h"
#include "piv2/piv2_yield.h"  // For khu_yield::GetMaturityBlocks()
#include "logging.h"
#include "primitives/transaction.h"
#include "sapling/saplingscriptpubkeyman.h"
#include "script/ismine.h"
#include "utilmoneystr.h"
#include "validation.h"
#include "wallet/wallet.h"
#include "wallet/walletdb.h"

// ============================================================================
// Balance Functions
// ============================================================================

CAmount GetKHUBalance(const CWallet* pwallet)
{
    LOCK(pwallet->cs_wallet);
    return pwallet->khuData.nKHUBalance;
}

CAmount GetKHULockedBalance(const CWallet* pwallet)
{
    LOCK(pwallet->cs_wallet);
    return pwallet->khuData.nKHULocked;
}

CAmount GetKHUPendingYieldEstimate(const CWallet* pwallet, uint16_t R_annual)
{
    // DÉTERMINISTE: R% est fixe, le yield est calculé avec la même formule que consensus
    // Formule: (amount × R_annual / 10000) × daysLocked / 365
    // Cette valeur est EXACTE (pas d'approximation) car R% est constant à un instant T

    LOCK(pwallet->cs_wallet);

    if (R_annual == 0) return 0;

    CAmount totalYield = 0;
    int currentHeight = chainActive.Height();

    // Itérer sur mapZKHUNotes (notes stakées), PAS mapKHUCoins (coins transparents)
    for (std::map<uint256, ZKHUNoteEntry>::const_iterator it = pwallet->khuData.mapZKHUNotes.begin();
         it != pwallet->khuData.mapZKHUNotes.end(); ++it) {
        const ZKHUNoteEntry& entry = it->second;

        // Skip spent notes
        if (entry.fSpent) continue;

        // Maturity check (network-aware: mainnet=4320, testnet=60, regtest=10)
        const uint32_t MATURITY_BLOCKS = khu_yield::GetMaturityBlocks();
        if (entry.nLockStartHeight == 0) continue;
        if ((uint32_t)currentHeight < entry.nLockStartHeight + MATURITY_BLOCKS) continue;

        // Calcul déterministe: days locked depuis maturity
        uint32_t blocksLocked = currentHeight - entry.nLockStartHeight;
        uint32_t daysLocked = blocksLocked / 1440; // BLOCKS_PER_DAY

        // Formule consensus: (amount × R_annual / 10000) / 365 × daysLocked
        // Utilise int64_t pour éviter overflow (identique à khu_yield.cpp)
        int64_t annualYield = (static_cast<int64_t>(entry.amount) * R_annual) / 10000;
        int64_t yield = (annualYield * daysLocked) / 365;

        totalYield += yield;
    }

    return totalYield;
}

// ============================================================================
// Coin Management Functions
// ============================================================================

bool AddKHUCoinToWallet(CWallet* pwallet, const COutPoint& outpoint,
                        const CKHUUTXO& coin, int nHeight)
{
    LOCK(pwallet->cs_wallet);

    // Check if this output belongs to us
    if (::IsMine(*pwallet, coin.scriptPubKey) == ISMINE_NO) {
        LogPrint(BCLog::HU, "AddKHUCoinToWallet: outpoint=%s:%d not mine, skipping\n",
                 outpoint.hash.GetHex().substr(0, 16).c_str(), outpoint.n);
        return false;
    }

    // Check if already tracked
    if (pwallet->khuData.mapKHUCoins.count(outpoint)) {
        LogPrint(BCLog::HU, "AddKHUCoinToWallet: outpoint=%s:%d already tracked (amount=%s, height=%d)\n",
                 outpoint.hash.GetHex().substr(0, 16).c_str(), outpoint.n,
                 FormatMoney(pwallet->khuData.mapKHUCoins[outpoint].coin.amount),
                 pwallet->khuData.mapKHUCoins[outpoint].nConfirmedHeight);
        return true;
    }

    // Create entry
    KHUCoinEntry entry(coin, outpoint.hash, outpoint.n, nHeight);

    // Add to map
    pwallet->khuData.mapKHUCoins[outpoint] = entry;

    // Update cached balance
    pwallet->khuData.UpdateBalance();

    // Persist to database
    if (!WriteKHUCoinToDB(pwallet, outpoint, entry)) {
        LogPrintf("ERROR: AddKHUCoinToWallet: Failed to persist to DB\n");
        return false;
    }

    LogPrint(BCLog::HU, "AddKHUCoinToWallet: Added %s:%d amount=%lld\n",
             outpoint.hash.GetHex().substr(0, 16).c_str(), outpoint.n, coin.amount);

    return true;
}

bool RemoveKHUCoinFromWallet(CWallet* pwallet, const COutPoint& outpoint)
{
    LOCK(pwallet->cs_wallet);

    auto it = pwallet->khuData.mapKHUCoins.find(outpoint);
    if (it == pwallet->khuData.mapKHUCoins.end()) {
        return false;
    }

    // Remove from map
    pwallet->khuData.mapKHUCoins.erase(it);

    // Update cached balance
    pwallet->khuData.UpdateBalance();

    // Remove from database
    if (!EraseKHUCoinFromDB(pwallet, outpoint)) {
        LogPrintf("ERROR: RemoveKHUCoinFromWallet: Failed to erase from DB\n");
    }

    LogPrint(BCLog::HU, "RemoveKHUCoinFromWallet: Removed %s:%d\n",
             outpoint.hash.GetHex().substr(0, 16), outpoint.n);

    return true;
}

std::vector<COutput> GetAvailableKHUCoins(const CWallet* pwallet, int minDepth)
{
    LOCK2(cs_main, pwallet->cs_wallet);

    std::vector<COutput> vCoins;
    int nCurrentHeight = chainActive.Height();

    for (std::map<COutPoint, KHUCoinEntry>::const_iterator it = pwallet->khuData.mapKHUCoins.begin();
         it != pwallet->khuData.mapKHUCoins.end(); ++it) {
        const COutPoint& outpoint = it->first;
        const KHUCoinEntry& entry = it->second;

        // Skip locked coins
        if (entry.coin.fLocked) continue;

        // Check depth
        int nDepth = nCurrentHeight - entry.nConfirmedHeight + 1;
        if (nDepth < minDepth) continue;

        // Find the wallet transaction
        const CWalletTx* wtx = pwallet->GetWalletTx(outpoint.hash);
        if (!wtx) continue;

        // Create COutput
        // Note: COutput constructor expects (wtx, vout, depth, spendable, solvable, safe)
        bool fSpendable = true;
        bool fSolvable = true;
        bool fSafe = (nDepth >= 1);

        vCoins.emplace_back(wtx, entry.vout, nDepth, fSpendable, fSolvable, fSafe);
    }

    return vCoins;
}

// ============================================================================
// Blockchain Scanning
// ============================================================================

void ProcessHUTransactionForWallet(CWallet* pwallet, const CTransactionRef& tx, int nHeight)
{
    LOCK(pwallet->cs_wallet);

    const uint256& txhash = tx->GetHash();

    LogPrint(BCLog::HU, "%s: tx %s type=%d height=%d\n",
              __func__, txhash.GetHex().substr(0, 16).c_str(), (int)tx->nType, nHeight);

    // STEP 1: Check if any input spends our tracked KHU UTXOs
    // This handles ALL transaction types including regular transfers via khusend
    // NOTE: Skip coinbase transactions (they don't spend any UTXOs)
    if (!tx->IsCoinBase()) {
        for (const auto& vin : tx->vin) {
            if (pwallet->khuData.mapKHUCoins.count(vin.prevout)) {
                LogPrint(BCLog::HU, "ProcessHUTransactionForWallet: Removing spent KHU coin %s:%d at height %d\n",
                         vin.prevout.hash.GetHex().substr(0, 16).c_str(), vin.prevout.n, nHeight);
                RemoveKHUCoinFromWallet(pwallet, vin.prevout);
            }
        }
    }

    // STEP 2: Process KHU-specific transaction types for output creation
    if (tx->nType == CTransaction::TxType::KHU_MINT) {
        // MINT creates KHU_T output at vout[1]
        LogPrintf("ProcessHUTransactionForWallet: KHU_MINT tx=%s vout.size=%zu height=%d\n",
                  txhash.GetHex().substr(0, 16).c_str(), tx->vout.size(), nHeight);

        if (tx->vout.size() >= 2) {
            const CTxOut& out = tx->vout[1];
            isminetype mine = ::IsMine(*pwallet, out.scriptPubKey);
            LogPrintf("ProcessHUTransactionForWallet: MINT vout[1] amount=%s IsMine=%d\n",
                      FormatMoney(out.nValue).c_str(), (int)mine);

            if (mine != ISMINE_NO) {
                CKHUUTXO coin(out.nValue, out.scriptPubKey, nHeight);
                bool added = AddKHUCoinToWallet(pwallet, COutPoint(txhash, 1), coin, nHeight);
                LogPrintf("ProcessHUTransactionForWallet: MINT AddKHUCoinToWallet returned %d, mapKHUCoins.size=%zu\n",
                          added, pwallet->khuData.mapKHUCoins.size());
            }
        } else {
            LogPrintf("ProcessHUTransactionForWallet: MINT ERROR - vout.size < 2!\n");
        }
    }
    else if (tx->nType == CTransaction::TxType::KHU_UNLOCK) {
        LogPrintf("%s: KHU_UNLOCK detected, txid=%s, vout.size=%zu\n",
                  __func__, txhash.ToString().substr(0, 16).c_str(), tx->vout.size());

        // UNLOCK creates (with privacy split):
        // - vout[0] = KHU_T output 1 (first split)
        // - vout[1] = KHU_T output 2 (second split) - ONLY when vout.size >= 3
        // - vout[2] = PIV change (fee change) - NOT KHU
        //
        // Privacy split: when vout.size >= 3, track both vout[0] and vout[1] as KHU
        // No privacy split: when vout.size == 2, track only vout[0] as KHU
        size_t nKHUOutputs = (tx->vout.size() >= 3) ? 2 : 1;  // Privacy split = 2 KHU outputs

        for (size_t i = 0; i < nKHUOutputs && i < tx->vout.size(); ++i) {
            const CTxOut& out = tx->vout[i];
            if (::IsMine(*pwallet, out.scriptPubKey) != ISMINE_NO) {
                CKHUUTXO coin(out.nValue, out.scriptPubKey, nHeight);
                AddKHUCoinToWallet(pwallet, COutPoint(txhash, i), coin, nHeight);
                LogPrint(BCLog::HU, "%s: added KHU output %s:%zu = %s\n",
                         __func__, txhash.GetHex().substr(0, 16).c_str(), i, FormatMoney(out.nValue));
            }
        }
        // Remaining outputs (if any) are PIV fee change - NOT tracked as KHU

        // BUG 5 FIX: Mark the ZKHU note as spent
        // Since we know the UNLOCK transaction amount, find and mark the matching note
        // The note commitment is stored in the wallet's mapZKHUNotes keyed by cm
        CAmount unlockAmount = 0;
        bool hasSapData = tx->sapData.is_initialized();
        LogPrintf("%s: hasSapData=%d\n", __func__, hasSapData);

        if (hasSapData && tx->sapData->valueBalance > 0) {
            // valueBalance is positive in UNLOCK (inflow from Sapling)
            unlockAmount = tx->sapData->valueBalance;
            LogPrintf("%s: valueBalance=%lld (unlockAmount=%s)\n",
                      __func__, tx->sapData->valueBalance, FormatMoney(unlockAmount).c_str());
        }

        // Find unspent note matching this amount and mark it spent
        // Note: If multiple notes have same amount, we mark the first one found
        bool noteMarked = false;
        size_t mapSize = pwallet->khuData.mapZKHUNotes.size();
        LogPrintf("%s: mapZKHUNotes.size=%zu, searching for amount=%s\n",
                  __func__, mapSize, FormatMoney(unlockAmount).c_str());

        for (auto& pair : pwallet->khuData.mapZKHUNotes) {
            LogPrintf("%s: checking note cm=%s amount=%s fSpent=%d\n",
                      __func__, pair.first.GetHex().substr(0, 16).c_str(),
                      FormatMoney(pair.second.amount).c_str(), pair.second.fSpent);

            if (!pair.second.fSpent && pair.second.amount == unlockAmount) {
                pair.second.fSpent = true;
                WriteZKHUNoteToDB(pwallet, pair.first, pair.second);
                LogPrintf("%s: BUG5 FIX SUCCESS - marked ZKHU note spent cm=%s amount=%s\n",
                          __func__, pair.first.GetHex().substr(0, 16).c_str(), FormatMoney(unlockAmount).c_str());
                noteMarked = true;
                break;
            }
        }
        if (noteMarked) {
            pwallet->khuData.UpdateBalance();
        } else if (unlockAmount > 0) {
            LogPrintf("%s: WARNING - could not find ZKHU note to mark spent amount=%s\n",
                      __func__, FormatMoney(unlockAmount).c_str());
        }
    }
    else if (tx->nType == CTransaction::TxType::KHU_REDEEM) {
        // REDEEM structure (from khuredeem RPC):
        // - vout[0] = PIV destination (redeemed amount) - NOT KHU
        // - vout[1] = KHU_T change (if any) OR PIV fee change if no KHU change
        // - vout[2] = PIV fee change (if KHU change exists) - NOT KHU
        //
        // BUG FIX: vout[1] is KHU change ONLY if there are 3+ outputs.
        // If there are exactly 2 outputs, vout[1] is PIV fee change, NOT KHU.
        //
        // STEP 1 above already removed spent KHU inputs.

        LogPrint(BCLog::HU, "%s: KHU_REDEEM detected, vout.size=%zu\n", __func__, tx->vout.size());

        // vout[0] is always PIV (the redeemed amount) - skip it
        // vout[1] is KHU change ONLY if vout.size() >= 3 (meaning PIV change is at vout[2])
        // If vout.size() == 2, then vout[1] is PIV fee change, NOT KHU
        if (tx->vout.size() >= 3) {
            const CTxOut& khuChangeOut = tx->vout[1];
            isminetype mine = ::IsMine(*pwallet, khuChangeOut.scriptPubKey);
            if (mine != ISMINE_NO) {
                CKHUUTXO coin(khuChangeOut.nValue, khuChangeOut.scriptPubKey, nHeight);
                AddKHUCoinToWallet(pwallet, COutPoint(txhash, 1), coin, nHeight);
                LogPrint(BCLog::HU, "%s: added KHU change from REDEEM %s:1 = %s\n",
                         __func__, txhash.GetHex().substr(0, 16).c_str(), FormatMoney(khuChangeOut.nValue));
            }
        } else {
            LogPrint(BCLog::HU, "%s: KHU_REDEEM has no KHU change (vout.size=%zu, vout[1] is PIV change)\n",
                     __func__, tx->vout.size());
        }
        // vout[2+] are PIV fee change - NOT tracked as KHU
    }
    else if (tx->nType == CTransaction::TxType::KHU_LOCK) {
        // LOCK creates:
        // 1. ZKHU Sapling output (increases locked balance)
        // 2. KHU_T change output (if any)
        // 3. PIV change output for fee (NOT tracked as KHU)

        LogPrint(BCLog::HU, "%s: KHU_LOCK detected, vout.size=%zu\n", __func__, tx->vout.size());

        // Step 1: Get locked amount from Sapling valueBalance and add ZKHU note
        CAmount lockedAmount = 0;
        bool hasSapData = tx->sapData.is_initialized();
        size_t sapOutputs = hasSapData ? tx->sapData->vShieldedOutput.size() : 0;

        if (hasSapData && sapOutputs > 0) {
            // In LOCK, valueBalance is negative (outflow to Sapling)
            CAmount valueBalance = tx->sapData->valueBalance;
            lockedAmount = -valueBalance;

            if (lockedAmount > 0) {
                // Add ZKHU note entry to mapZKHUNotes so UpdateBalance() will count it
                const OutputDescription& sapOut = tx->sapData->vShieldedOutput[0];
                uint256 cm = sapOut.cmu;
                SaplingOutPoint op(txhash, 0);

                // Create ZKHUNoteEntry with the locked amount
                ZKHUNoteEntry noteEntry(op, cm, nHeight, lockedAmount, uint256(), nHeight);

                // Add to map (this will be counted by UpdateBalance)
                pwallet->khuData.mapZKHUNotes[cm] = noteEntry;

                // Persist to database (BUG 4 FIX: was missing, notes lost on restart)
                if (!WriteZKHUNoteToDB(pwallet, cm, noteEntry)) {
                    LogPrintf("ERROR: %s: Failed to persist ZKHU note to DB\n", __func__);
                }

                LogPrint(BCLog::HU, "%s: added ZKHU note cm=%s amount=%d (persisted to DB)\n",
                         __func__, cm.GetHex().substr(0, 16).c_str(), lockedAmount);
            }
        }

        // Step 2: Track ONLY KHU change outputs
        // SIMPLIFICATION: Track the FIRST transparent output as KHU change
        // The second output (if any) is PIV fee change and should NOT be tracked
        // This works because khulock RPC creates outputs in this order:
        // vout[0] = KHU change, vout[1] = PIV change (if any)
        if (tx->vout.size() > 0) {
            const CTxOut& out = tx->vout[0];
            isminetype mine = ::IsMine(*pwallet, out.scriptPubKey);
            if (mine != ISMINE_NO) {
                CKHUUTXO coin(out.nValue, out.scriptPubKey, nHeight);
                AddKHUCoinToWallet(pwallet, COutPoint(txhash, 0), coin, nHeight);
                LogPrint(BCLog::HU, "%s: added KHU change %s:0 = %s\n",
                         __func__, txhash.GetHex().substr(0, 16).c_str(), FormatMoney(out.nValue));
            }
        }
        // vout[1] if present is PIV fee change - NOT tracked as KHU
    }
    // KHU_REDEEM only spends (no KHU outputs created)
    // Its inputs are already handled in STEP 1 above
}

bool ScanForKHUCoins(CWallet* pwallet, int nStartHeight)
{
    LOCK2(cs_main, pwallet->cs_wallet);

    LogPrint(BCLog::HU, "ScanForKHUCoins: Starting scan from height %d\n", nStartHeight);

    // Clear existing KHU coins before full rescan
    if (nStartHeight == 0) {
        pwallet->khuData.Clear();
        // Also clear from database
        WalletBatch batch(pwallet->GetDBHandle());
        // Note: Full clear would need cursor iteration; for now, coins are
        // individually erased via RemoveKHUCoinFromWallet when spent
    }

    const CBlockIndex* pindex = chainActive[nStartHeight];
    if (!pindex) {
        LogPrintf("ERROR: ScanForKHUCoins: Invalid start height %d\n", nStartHeight);
        return false;
    }

    int nScanned = 0;
    int nKHUTxProcessed = 0;

    while (pindex) {
        CBlock block;
        if (!ReadBlockFromDisk(block, pindex)) {
            LogPrintf("ERROR: ScanForKHUCoins: Failed to read block at height %d\n", pindex->nHeight);
            return false;
        }

        for (const auto& tx : block.vtx) {
            // Process ALL transactions to detect:
            // 1. KHU-specific transactions (MINT, REDEEM, LOCK, UNLOCK)
            // 2. Regular transactions that spend our tracked KHU UTXOs (via khusend)
            bool isKHUTx = (tx->nType >= CTransaction::TxType::KHU_MINT &&
                           tx->nType <= CTransaction::TxType::KHU_UNLOCK);

            // For non-KHU transactions, only process if we have KHU coins
            // that might be spent (optimization)
            if (isKHUTx || !pwallet->khuData.mapKHUCoins.empty()) {
                ProcessHUTransactionForWallet(pwallet, tx, pindex->nHeight);
                if (isKHUTx) nKHUTxProcessed++;
            }
        }

        nScanned++;
        if (nScanned % 10000 == 0) {
            LogPrint(BCLog::HU, "ScanForKHUCoins: Scanned %d blocks (height %d), %d KHU coins tracked\n",
                     nScanned, pindex->nHeight, pwallet->khuData.mapKHUCoins.size());
        }

        pindex = chainActive.Next(pindex);
    }

    // Update final balances
    pwallet->khuData.UpdateBalance();

    LogPrint(BCLog::HU, "ScanForKHUCoins: Complete. Scanned %d blocks, %d KHU tx, found %d coins, balance=%d\n",
             nScanned, nKHUTxProcessed, pwallet->khuData.mapKHUCoins.size(), pwallet->khuData.nKHUBalance);

    return true;
}

// ============================================================================
// Database Persistence
// ============================================================================

bool WriteKHUCoinToDB(CWallet* pwallet, const COutPoint& outpoint, const KHUCoinEntry& entry)
{
    WalletBatch batch(pwallet->GetDBHandle());
    return batch.WriteKHUCoin(outpoint, entry);
}

bool EraseKHUCoinFromDB(CWallet* pwallet, const COutPoint& outpoint)
{
    WalletBatch batch(pwallet->GetDBHandle());
    return batch.EraseKHUCoin(outpoint);
}

bool LoadKHUCoinsFromDB(CWallet* pwallet)
{
    LOCK(pwallet->cs_wallet);

    // NOTE: KHU coins are now loaded automatically during wallet load
    // via ReadKeyValue() in walletdb.cpp, which handles "khucoin" records.
    //
    // This function is kept for explicit calls to finalize/refresh balances.
    // The actual loading happens in WalletBatch::LoadWallet() -> ReadKeyValue().

    if (pwallet->khuData.mapKHUCoins.empty()) {
        LogPrint(BCLog::HU, "LoadKHUCoinsFromDB: No KHU coins in wallet\n");
        return true;
    }

    // Recalculate cached balances from loaded coins
    pwallet->khuData.UpdateBalance();

    LogPrint(BCLog::HU, "LoadKHUCoinsFromDB: Finalized %d KHU coins, balance=%d, lockd=%d\n",
             pwallet->khuData.mapKHUCoins.size(),
             pwallet->khuData.nKHUBalance,
             pwallet->khuData.nKHULocked);

    return true;
}

// ============================================================================
// ZKHU Note Functions (Phase 8b)
// ============================================================================

bool AddZKHUNoteToWallet(CWallet* pwallet, const SaplingOutPoint& op, const uint256& cm,
                         const ZKHUMemo& memo, const uint256& nullifier, int nHeight)
{
    LOCK(pwallet->cs_wallet);

    // Check if already tracked
    if (pwallet->khuData.mapZKHUNotes.count(cm)) {
        LogPrint(BCLog::HU, "AddZKHUNoteToWallet: Already tracking note %s\n",
                 cm.GetHex().substr(0, 16));
        return true;
    }

    // Create entry
    ZKHUNoteEntry entry(op, cm, memo.nLockStartHeight, memo.amount, nullifier, nHeight);

    // Add to map
    pwallet->khuData.mapZKHUNotes[cm] = entry;

    // Add nullifier mapping
    if (!nullifier.IsNull()) {
        pwallet->khuData.mapZKHUNullifiers[nullifier] = cm;
    }

    // Update cached balance
    pwallet->khuData.UpdateBalance();

    // Persist to database
    if (!WriteZKHUNoteToDB(pwallet, cm, entry)) {
        LogPrintf("ERROR: AddZKHUNoteToWallet: Failed to persist to DB\n");
        return false;
    }

    LogPrint(BCLog::HU, "AddZKHUNoteToWallet: Added note %s amount=%d lockHeight=%d\n",
             cm.GetHex().substr(0, 16), memo.amount, memo.nLockStartHeight);

    return true;
}

bool MarkZKHUNoteSpent(CWallet* pwallet, const uint256& nullifier)
{
    LOCK(pwallet->cs_wallet);

    // Find note by nullifier
    auto nullIt = pwallet->khuData.mapZKHUNullifiers.find(nullifier);
    if (nullIt == pwallet->khuData.mapZKHUNullifiers.end()) {
        return false; // Not our note
    }

    const uint256& cm = nullIt->second;
    auto noteIt = pwallet->khuData.mapZKHUNotes.find(cm);
    if (noteIt == pwallet->khuData.mapZKHUNotes.end()) {
        return false;
    }

    // Mark as spent
    noteIt->second.fSpent = true;

    // Update cached balance
    pwallet->khuData.UpdateBalance();

    // Update database
    if (!WriteZKHUNoteToDB(pwallet, cm, noteIt->second)) {
        LogPrintf("ERROR: MarkZKHUNoteSpent: Failed to update DB\n");
    }

    LogPrint(BCLog::HU, "MarkZKHUNoteSpent: Note %s marked spent\n",
             cm.GetHex().substr(0, 16));

    return true;
}

std::vector<ZKHUNoteEntry> GetUnspentZKHUNotes(const CWallet* pwallet)
{
    LOCK(pwallet->cs_wallet);

    std::vector<ZKHUNoteEntry> result;
    for (std::map<uint256, ZKHUNoteEntry>::const_iterator it = pwallet->khuData.mapZKHUNotes.begin();
         it != pwallet->khuData.mapZKHUNotes.end(); ++it) {
        if (!it->second.fSpent) {
            result.push_back(it->second);
        }
    }
    return result;
}

const ZKHUNoteEntry* GetZKHUNote(const CWallet* pwallet, const uint256& cm)
{
    LOCK(pwallet->cs_wallet);

    auto it = pwallet->khuData.mapZKHUNotes.find(cm);
    if (it == pwallet->khuData.mapZKHUNotes.end()) {
        return nullptr;
    }
    return &it->second;
}

bool WriteZKHUNoteToDB(CWallet* pwallet, const uint256& cm, const ZKHUNoteEntry& entry)
{
    WalletBatch batch(pwallet->GetDBHandle());
    return batch.WriteZKHUNote(cm, entry);
}

bool EraseZKHUNoteFromDB(CWallet* pwallet, const uint256& cm)
{
    WalletBatch batch(pwallet->GetDBHandle());
    return batch.EraseZKHUNote(cm);
}

void ProcessHULockForWallet(CWallet* pwallet, const CTransactionRef& tx, int nHeight)
{
    LOCK(pwallet->cs_wallet);

    if (tx->nType != CTransaction::TxType::KHU_LOCK) return;
    if (!tx->IsShieldedTx()) return;

    // Get the Sapling script pub key manager
    SaplingScriptPubKeyMan* saplingMan = pwallet->GetSaplingScriptPubKeyMan();
    if (!saplingMan) return;

    // Process each Sapling output
    const uint256& txhash = tx->GetHash();

    for (size_t i = 0; i < tx->sapData->vShieldedOutput.size(); ++i) {
        const OutputDescription& output = tx->sapData->vShieldedOutput[i];
        SaplingOutPoint op(txhash, i);

        // Check if this output belongs to us
        auto it = pwallet->mapWallet.find(txhash);
        if (it == pwallet->mapWallet.end()) continue;

        const CWalletTx& wtx = it->second;
        auto noteIt = wtx.mapSaplingNoteData.find(op);
        if (noteIt == wtx.mapSaplingNoteData.end()) continue;

        const SaplingNoteData& nd = noteIt->second;
        if (!nd.IsMyNote()) continue;

        // Try to decode the memo as ZKHUMemo
        if (!nd.memo) continue;

        std::array<unsigned char, 512> memoBytes;
        std::copy(nd.memo->begin(), nd.memo->end(), memoBytes.begin());
        ZKHUMemo memo = ZKHUMemo::Deserialize(memoBytes);

        // Verify ZKHU magic
        if (memcmp(memo.magic, "ZKHU", 4) != 0) continue;

        // Get nullifier
        uint256 nullifier;
        if (nd.nullifier) {
            nullifier = *nd.nullifier;
        }

        // Add to wallet
        AddZKHUNoteToWallet(pwallet, op, output.cmu, memo, nullifier, nHeight);
    }
}

void ProcessHUUnlockForWallet(CWallet* pwallet, const CTransactionRef& tx)
{
    LOCK(pwallet->cs_wallet);

    if (tx->nType != CTransaction::TxType::KHU_UNLOCK) return;
    if (!tx->IsShieldedTx()) return;

    // Check each Sapling spend's nullifier
    for (const SpendDescription& spend : tx->sapData->vShieldedSpend) {
        // Mark the note as spent if it's ours
        MarkZKHUNoteSpent(pwallet, spend.nullifier);
    }
}
