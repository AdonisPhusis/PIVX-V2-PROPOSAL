// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef HU_WALLET_KHU_WALLET_H
#define HU_WALLET_KHU_WALLET_H

#include "amount.h"
#include "piv2/piv2_coins.h"
#include "piv2/piv2_unlock.h" // For GetZKHUMaturityBlocks()
#include "piv2/zkpiv2_memo.h"
#include "primitives/transaction.h"
#include "serialize.h"
#include "uint256.h"
#include "utiltime.h"

#include <map>
#include <vector>

class CWallet;
class COutput;
class CCoinsViewCache;

/**
 * KHU Wallet Extension — Phase 8a (Transparent)
 *
 * Extension for CWallet to track KHU_T colored coin UTXOs.
 * This is NOT a separate wallet — it extends the existing CWallet.
 *
 * PRINCIPES:
 * - Réutilise l'infrastructure wallet PIVX existante
 * - Pas de logique consensus (délégué à khu_validation)
 * - Tracking UTXOs "mine" uniquement
 * - Persistence via wallet.dat (prefix "khucoin")
 *
 * Source: docs/blueprints/08-PHASE8-RPC-WALLET.md
 */

/**
 * KHUCoinEntry - Entry for wallet's KHU coin tracking
 *
 * Wrapper autour de CKHUUTXO avec métadonnées wallet.
 */
struct KHUCoinEntry {
    //! The KHU UTXO data
    CKHUUTXO coin;

    //! Transaction hash containing this output
    uint256 txhash;

    //! Output index in transaction
    uint32_t vout;

    //! Block height when confirmed (0 if unconfirmed)
    int nConfirmedHeight;

    //! Time received in wallet
    int64_t nTimeReceived;

    KHUCoinEntry() : vout(0), nConfirmedHeight(0), nTimeReceived(0) {}

    KHUCoinEntry(const CKHUUTXO& coinIn, const uint256& txhashIn, uint32_t voutIn, int heightIn)
        : coin(coinIn), txhash(txhashIn), vout(voutIn), nConfirmedHeight(heightIn), nTimeReceived(GetTime()) {}

    COutPoint GetOutPoint() const {
        return COutPoint(txhash, vout);
    }

    SERIALIZE_METHODS(KHUCoinEntry, obj) {
        READWRITE(obj.coin, obj.txhash, obj.vout, obj.nConfirmedHeight, obj.nTimeReceived);
    }
};

/**
 * ZKHUNoteEntry - Entry for wallet's ZKHU note tracking (Phase 8b)
 *
 * Tracks Sapling notes created by KHU_LOCK transactions.
 * Links to the underlying Sapling note data with ZKHU-specific metadata.
 */
struct ZKHUNoteEntry {
    //! Sapling outpoint (txid + output index)
    SaplingOutPoint op;

    //! Note commitment (cm) for identification
    uint256 cm;

    //! Decoded ZKHUMemo data
    uint32_t nLockStartHeight;
    CAmount amount;
    CAmount Ur_accumulated;

    //! Nullifier (for tracking spends)
    uint256 nullifier;

    //! Has this note been spent (unlockd)?
    bool fSpent;

    //! Block height when confirmed
    int nConfirmedHeight;

    //! Time received in wallet
    int64_t nTimeReceived;

    ZKHUNoteEntry()
        : nLockStartHeight(0), amount(0), Ur_accumulated(0),
          fSpent(false), nConfirmedHeight(0), nTimeReceived(0) {}

    ZKHUNoteEntry(const SaplingOutPoint& opIn, const uint256& cmIn,
                  uint32_t lockHeight, CAmount amountIn, const uint256& nullifierIn, int heightIn)
        : op(opIn), cm(cmIn), nLockStartHeight(lockHeight), amount(amountIn),
          Ur_accumulated(0), nullifier(nullifierIn), fSpent(false),
          nConfirmedHeight(heightIn), nTimeReceived(GetTime()) {}

    //! Check if note is mature for unstaking (network-aware maturity)
    //! MAINNET/TESTNET: 4320 blocks (~3 days)
    //! REGTEST: 1260 blocks (~21 hours for fast testing)
    bool IsMature(int currentHeight) const {
        return !fSpent && (uint32_t)(currentHeight - nConfirmedHeight) >= GetZKHUMaturityBlocks();
    }

    //! Get blocks lockd
    int GetBlocksLocked(int currentHeight) const {
        if (nConfirmedHeight == 0) return 0;
        return currentHeight - nConfirmedHeight;
    }

    SERIALIZE_METHODS(ZKHUNoteEntry, obj) {
        READWRITE(obj.op, obj.cm, obj.nLockStartHeight, obj.amount, obj.Ur_accumulated,
                  obj.nullifier, obj.fSpent, obj.nConfirmedHeight, obj.nTimeReceived);
    }
};

/**
 * KHU Wallet Data Container
 *
 * Contains all KHU-specific data for the wallet.
 * This is embedded in CWallet, not a separate class.
 */
class KHUWalletData {
public:
    //! Map of KHU UTXOs owned by this wallet: outpoint -> entry
    std::map<COutPoint, KHUCoinEntry> mapKHUCoins;

    //! Map of ZKHU notes owned by this wallet: note_commitment -> entry (Phase 8b)
    std::map<uint256, ZKHUNoteEntry> mapZKHUNotes;

    //! Map of ZKHU nullifiers to note commitments (for spend detection)
    std::map<uint256, uint256> mapZKHUNullifiers;

    //! Cached KHU transparent balance
    CAmount nKHUBalance{0};

    //! Cached KHU locked balance (ZKHU notes)
    CAmount nKHULocked{0};

    KHUWalletData() = default;

    //! Clear all KHU data
    void Clear() {
        mapKHUCoins.clear();
        mapZKHUNotes.clear();
        mapZKHUNullifiers.clear();
        nKHUBalance = 0;
        nKHULocked = 0;
    }

    //! Recalculate cached balances from maps
    void UpdateBalance() {
        nKHUBalance = 0;
        nKHULocked = 0;

        // Transparent KHU_T balance
        for (std::map<COutPoint, KHUCoinEntry>::const_iterator it = mapKHUCoins.begin();
             it != mapKHUCoins.end(); ++it) {
            const KHUCoinEntry& entry = it->second;
            if (!entry.coin.fLocked) {
                nKHUBalance += entry.coin.amount;
            }
        }

        // Locked ZKHU balance (unspent notes only)
        for (std::map<uint256, ZKHUNoteEntry>::const_iterator it = mapZKHUNotes.begin();
             it != mapZKHUNotes.end(); ++it) {
            const ZKHUNoteEntry& entry = it->second;
            if (!entry.fSpent) {
                nKHULocked += entry.amount;
            }
        }
    }
};

/**
 * KHU Wallet Functions
 *
 * Ces fonctions opèrent sur CWallet avec son KHUWalletData intégré.
 * Elles sont déclarées ici et implémentées dans khu_wallet.cpp.
 */

//! Add a KHU coin to the wallet (returns true if it's ours)
bool AddKHUCoinToWallet(CWallet* pwallet, const COutPoint& outpoint,
                        const CKHUUTXO& coin, int nHeight);

//! Remove a spent KHU coin from the wallet
bool RemoveKHUCoinFromWallet(CWallet* pwallet, const COutPoint& outpoint);

//! Get available (unspent, non-lockd) KHU coins
std::vector<COutput> GetAvailableKHUCoins(const CWallet* pwallet, int minDepth = 1);

//! Get KHU transparent balance
CAmount GetKHUBalance(const CWallet* pwallet);

//! Get KHU locked balance (Phase 8b)
CAmount GetKHULockedBalance(const CWallet* pwallet);

/**
 * Get pending yield for display purposes.
 *
 * DÉTERMINISTE: R% est fixe à un instant T, le calcul est exact.
 * Formule identique au consensus: (amount × R_annual / 10000) × daysLocked / 365
 *
 * NOTE: Cette valeur représente le yield accumulé pour les notes stakées.
 * Le yield réel sera appliqué quotidiennement par le consensus engine.
 *
 * @param pwallet Wallet pointer
 * @param R_annual Annual rate in basis points (from HuGlobalState)
 * @return Pending yield amount (satoshis)
 */
CAmount GetKHUPendingYieldEstimate(const CWallet* pwallet, uint16_t R_annual);

//! Scan blockchain for KHU coins belonging to this wallet
bool ScanForKHUCoins(CWallet* pwallet, int nStartHeight);

//! Process a KHU transaction for wallet tracking
void ProcessHUTransactionForWallet(CWallet* pwallet, const CTransactionRef& tx, int nHeight);

/**
 * Wallet Persistence Functions (wallet.dat)
 */

//! Write a KHU coin to wallet database
bool WriteKHUCoinToDB(CWallet* pwallet, const COutPoint& outpoint, const KHUCoinEntry& entry);

//! Erase a KHU coin from wallet database
bool EraseKHUCoinFromDB(CWallet* pwallet, const COutPoint& outpoint);

//! Load all KHU coins from wallet database
bool LoadKHUCoinsFromDB(CWallet* pwallet);

/**
 * ZKHU Note Functions (Phase 8b)
 */

//! Add a ZKHU note to the wallet (called when KHU_LOCK tx is processed)
bool AddZKHUNoteToWallet(CWallet* pwallet, const SaplingOutPoint& op, const uint256& cm,
                         const ZKHUMemo& memo, const uint256& nullifier, int nHeight);

//! Mark a ZKHU note as spent (called when KHU_UNLOCK tx spends the nullifier)
bool MarkZKHUNoteSpent(CWallet* pwallet, const uint256& nullifier);

//! Get list of unspent ZKHU notes
std::vector<ZKHUNoteEntry> GetUnspentZKHUNotes(const CWallet* pwallet);

//! Get a specific ZKHU note by commitment
const ZKHUNoteEntry* GetZKHUNote(const CWallet* pwallet, const uint256& cm);

//! Write a ZKHU note to wallet database
bool WriteZKHUNoteToDB(CWallet* pwallet, const uint256& cm, const ZKHUNoteEntry& entry);

//! Erase a ZKHU note from wallet database
bool EraseZKHUNoteFromDB(CWallet* pwallet, const uint256& cm);

//! Process a KHU_LOCK transaction for ZKHU note tracking
void ProcessHULockForWallet(CWallet* pwallet, const CTransactionRef& tx, int nHeight);

//! Process a KHU_UNLOCK transaction to mark notes spent
void ProcessHUUnlockForWallet(CWallet* pwallet, const CTransactionRef& tx);

#endif // HU_WALLET_KHU_WALLET_H
