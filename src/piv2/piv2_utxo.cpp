// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "piv2/piv2_utxo.h"

#include "coins.h"
#include "piv2/piv2_statedb.h"
#include "sync.h"
#include "util/system.h"
#include "utilmoneystr.h"

#include <unordered_map>

// External function to get DB (defined in khu_validation.cpp)
extern CKHUStateDB* GetKHUStateDB();

// Phase 2: KHU UTXO tracking with LevelDB persistence
// In-memory cache for performance, backed by LevelDB for persistence
static RecursiveMutex cs_khu_utxos;
static std::unordered_map<COutPoint, CKHUUTXO, SaltedOutpointHasher> mapKHUUTXOs GUARDED_BY(cs_khu_utxos);
static bool fKHUUTXOsLoaded = false;

// Initialize UTXO cache from database (called at startup)
static void LoadKHUUTXOsFromDB()
{
    AssertLockHeld(cs_khu_utxos);

    if (fKHUUTXOsLoaded) return;

    CKHUStateDB* db = GetKHUStateDB();
    if (!db) {
        LogPrintf("%s: WARNING - KHU DB not available yet\n", __func__);
        return;
    }

    std::vector<std::pair<COutPoint, CKHUUTXO>> utxos;
    if (db->LoadAllKHUUTXOs(utxos)) {
        for (const auto& pair : utxos) {
            mapKHUUTXOs[pair.first] = pair.second;
        }
        LogPrint(BCLog::HU, "%s: Loaded %zu KHU UTXOs from database\n", __func__, utxos.size());
        fKHUUTXOsLoaded = true;
    }
}

bool AddKHUCoin(CCoinsViewCache& view, const COutPoint& outpoint, const CKHUUTXO& coin)
{
    LOCK(cs_khu_utxos);

    // Ensure cache is loaded
    LoadKHUUTXOsFromDB();

    LogPrint(BCLog::HU, "%s: adding %s KHU at %s:%d (height %d)\n",
             __func__, FormatMoney(coin.amount), outpoint.hash.ToString().substr(0,16).c_str(),
             outpoint.n, coin.nHeight);

    // Vérifier que le coin n'existe pas déjà
    auto it = mapKHUUTXOs.find(outpoint);
    if (it != mapKHUUTXOs.end()) {
        bool isSpent = it->second.IsSpent();
        LogPrint(BCLog::HU, "%s: outpoint=%s already exists (spent=%d)\n",
                 __func__, outpoint.ToString(), isSpent);
        if (!isSpent) {
            return error("%s: coin already exists and not spent at %s", __func__, outpoint.ToString());
        }
    }

    // Ajouter le coin à la cache
    mapKHUUTXOs[outpoint] = coin;

    // Persister dans LevelDB
    CKHUStateDB* db = GetKHUStateDB();
    if (db) {
        if (!db->WriteKHUUTXO(outpoint, coin)) {
            LogPrintf("ERROR: %s: Failed to persist UTXO to database\n", __func__);
            // Continue anyway - in-memory cache is updated
        } else {
            LogPrint(BCLog::HU, "%s: Persisted UTXO to LevelDB at %s\n", __func__, outpoint.ToString());
        }
    } else {
        LogPrintf("WARNING: %s: KHU StateDB not available for UTXO persistence\n", __func__);
    }

    LogPrint(BCLog::HU, "%s: added %s KHU at %s\n",
             __func__, FormatMoney(coin.amount), outpoint.ToString());

    return true;
}

bool SpendKHUCoin(CCoinsViewCache& view, const COutPoint& outpoint)
{
    LOCK(cs_khu_utxos);

    // Ensure cache is loaded
    LoadKHUUTXOsFromDB();

    LogPrint(BCLog::HU, "%s: looking for %s:%d\n",
              __func__, outpoint.hash.ToString().substr(0,16).c_str(), outpoint.n);

    auto it = mapKHUUTXOs.find(outpoint);
    if (it == mapKHUUTXOs.end()) {
        LogPrint(BCLog::HU, "%s: coin not found for %s:%d\n",
                 __func__, outpoint.hash.ToString().substr(0,16).c_str(), outpoint.n);
        return error("%s: coin not found at %s", __func__, outpoint.ToString());
    }

    if (it->second.IsSpent()) {
        LogPrint(BCLog::HU, "SpendKHUCoin: coin already spent for %s:%d\n",
                 outpoint.hash.ToString().substr(0,16).c_str(), outpoint.n);
        return error("SpendKHUCoin: coin already spent at %s", outpoint.ToString());
    }

    LogPrint(BCLog::HU, "SpendKHUCoin: spending %s:%d value=%s\n",
             outpoint.hash.ToString().substr(0,16).c_str(), outpoint.n, FormatMoney(it->second.amount));

    // Supprimer de la cache
    mapKHUUTXOs.erase(it);

    // Supprimer de LevelDB
    CKHUStateDB* db = GetKHUStateDB();
    if (db) {
        if (!db->EraseKHUUTXO(outpoint)) {
            LogPrintf("ERROR: %s: Failed to erase UTXO from database\n", __func__);
            // Continue anyway - in-memory cache is updated
        }
    }

    return true;
}

bool GetKHUCoin(const CCoinsViewCache& view, const COutPoint& outpoint, CKHUUTXO& coin)
{
    LOCK(cs_khu_utxos);

    // Ensure cache is loaded
    LoadKHUUTXOsFromDB();

    LogPrint(BCLog::HU, "%s: looking for %s:%d\n",
              __func__, outpoint.hash.ToString().substr(0,16).c_str(), outpoint.n);

    auto it = mapKHUUTXOs.find(outpoint);
    if (it == mapKHUUTXOs.end()) {
        // Not found is normal for PIV inputs - only log at debug level
        LogPrint(BCLog::HU, "%s: coin not found for %s:%d\n",
                 __func__, outpoint.hash.ToString().substr(0,16).c_str(), outpoint.n);
        return false;
    }

    if (it->second.IsSpent()) {
        LogPrint(BCLog::HU, "GetKHUCoin: coin spent for %s:%d\n",
                 outpoint.hash.ToString().substr(0,16).c_str(), outpoint.n);
        return false;
    }

    coin = it->second;
    LogPrint(BCLog::HU, "GetKHUCoin: found %s:%d value=%s\n",
             outpoint.hash.ToString().substr(0,16).c_str(), outpoint.n, FormatMoney(coin.amount));
    return true;
}

bool HaveKHUCoin(const CCoinsViewCache& view, const COutPoint& outpoint)
{
    LOCK(cs_khu_utxos);

    // Ensure cache is loaded
    LoadKHUUTXOsFromDB();

    auto it = mapKHUUTXOs.find(outpoint);
    if (it == mapKHUUTXOs.end()) {
        return false;
    }

    return !it->second.IsSpent();
}

bool GetKHUCoinFromTracking(const COutPoint& outpoint, CKHUUTXO& coin)
{
    LOCK(cs_khu_utxos);

    // Ensure cache is loaded
    LoadKHUUTXOsFromDB();

    auto it = mapKHUUTXOs.find(outpoint);
    if (it == mapKHUUTXOs.end()) {
        return false;
    }

    if (it->second.IsSpent()) {
        return false;
    }

    coin = it->second;
    return true;
}

// Restore a spent KHU UTXO (used during reorg/undo)
bool RestoreKHUCoin(const COutPoint& outpoint, const CKHUUTXO& coin)
{
    LOCK(cs_khu_utxos);

    LogPrint(BCLog::HU, "%s: restoring %s KHU at %s:%d\n",
             __func__, FormatMoney(coin.amount), outpoint.hash.ToString().substr(0,16).c_str(), outpoint.n);

    // Ajouter à la cache
    mapKHUUTXOs[outpoint] = coin;

    // Persister dans LevelDB
    CKHUStateDB* db = GetKHUStateDB();
    if (db) {
        if (!db->WriteKHUUTXO(outpoint, coin)) {
            LogPrintf("ERROR: %s: Failed to persist restored UTXO to database\n", __func__);
        }
    }

    return true;
}
