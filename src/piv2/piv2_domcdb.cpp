// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "piv2/piv2_domcdb.h"

#include "logging.h"
#include "util/system.h"

#include <memory>

// Database key prefixes
static const char DB_DOMC = 'D';
static const char DB_DOMC_COMMIT = 'C';
static const char DB_DOMC_REVEAL = 'R';
static const char DB_DOMC_INDEX = 'I';

// Global DOMC database instance
static std::unique_ptr<CKHUDomcDB> pkhudomcdb;

// ============================================================================
// CKHUDomcDB implementation
// ============================================================================

CKHUDomcDB::CKHUDomcDB(size_t nCacheSize, bool fMemory, bool fWipe) :
    CDBWrapper(GetDataDir() / "khu" / "domc", nCacheSize, fMemory, fWipe)
{
}

// ============================================================================
// COMMIT operations
// ============================================================================

bool CKHUDomcDB::WriteCommit(const khu_domc::DomcCommit& commit)
{
    // Key: 'D' + 'C' + mnOutpoint + cycleId
    auto key = std::make_pair(DB_DOMC,
                              std::make_pair(DB_DOMC_COMMIT,
                                           std::make_pair(commit.mnOutpoint, commit.nCycleId)));
    return Write(key, commit);
}

bool CKHUDomcDB::ReadCommit(const COutPoint& mnOutpoint, uint32_t cycleId,
                            khu_domc::DomcCommit& commit)
{
    auto key = std::make_pair(DB_DOMC,
                              std::make_pair(DB_DOMC_COMMIT,
                                           std::make_pair(mnOutpoint, cycleId)));
    return Read(key, commit);
}

bool CKHUDomcDB::HaveCommit(const COutPoint& mnOutpoint, uint32_t cycleId)
{
    auto key = std::make_pair(DB_DOMC,
                              std::make_pair(DB_DOMC_COMMIT,
                                           std::make_pair(mnOutpoint, cycleId)));
    return Exists(key);
}

bool CKHUDomcDB::EraseCommit(const COutPoint& mnOutpoint, uint32_t cycleId)
{
    auto key = std::make_pair(DB_DOMC,
                              std::make_pair(DB_DOMC_COMMIT,
                                           std::make_pair(mnOutpoint, cycleId)));
    return Erase(key);
}

// ============================================================================
// REVEAL operations
// ============================================================================

bool CKHUDomcDB::WriteReveal(const khu_domc::DomcReveal& reveal)
{
    // Key: 'D' + 'R' + mnOutpoint + cycleId
    auto key = std::make_pair(DB_DOMC,
                              std::make_pair(DB_DOMC_REVEAL,
                                           std::make_pair(reveal.mnOutpoint, reveal.nCycleId)));
    return Write(key, reveal);
}

bool CKHUDomcDB::ReadReveal(const COutPoint& mnOutpoint, uint32_t cycleId,
                            khu_domc::DomcReveal& reveal)
{
    auto key = std::make_pair(DB_DOMC,
                              std::make_pair(DB_DOMC_REVEAL,
                                           std::make_pair(mnOutpoint, cycleId)));
    return Read(key, reveal);
}

bool CKHUDomcDB::HaveReveal(const COutPoint& mnOutpoint, uint32_t cycleId)
{
    auto key = std::make_pair(DB_DOMC,
                              std::make_pair(DB_DOMC_REVEAL,
                                           std::make_pair(mnOutpoint, cycleId)));
    return Exists(key);
}

bool CKHUDomcDB::EraseReveal(const COutPoint& mnOutpoint, uint32_t cycleId)
{
    auto key = std::make_pair(DB_DOMC,
                              std::make_pair(DB_DOMC_REVEAL,
                                           std::make_pair(mnOutpoint, cycleId)));
    return Erase(key);
}

// ============================================================================
// CYCLE INDEX operations
// ============================================================================

bool CKHUDomcDB::AddMasternodeToCycleIndex(uint32_t cycleId, const COutPoint& mnOutpoint)
{
    // Read existing index
    std::vector<COutPoint> mnOutpoints;
    GetMasternodesForCycle(cycleId, mnOutpoints);

    // Check if already in index
    for (const auto& existing : mnOutpoints) {
        if (existing == mnOutpoint) {
            return true; // Already indexed
        }
    }

    // Add to index
    mnOutpoints.push_back(mnOutpoint);

    // Write updated index
    auto key = std::make_pair(DB_DOMC, std::make_pair(DB_DOMC_INDEX, cycleId));
    return Write(key, mnOutpoints);
}

bool CKHUDomcDB::GetMasternodesForCycle(uint32_t cycleId, std::vector<COutPoint>& mnOutpoints)
{
    auto key = std::make_pair(DB_DOMC, std::make_pair(DB_DOMC_INDEX, cycleId));
    return Read(key, mnOutpoints);
}

bool CKHUDomcDB::GetRevealsForCycle(uint32_t cycleId, std::vector<khu_domc::DomcReveal>& reveals)
{
    reveals.clear();

    // Get list of masternodes that participated in this cycle
    std::vector<COutPoint> mnOutpoints;
    if (!GetMasternodesForCycle(cycleId, mnOutpoints)) {
        return false; // No masternodes in this cycle
    }

    // Collect reveals from all masternodes
    for (const auto& mnOutpoint : mnOutpoints) {
        khu_domc::DomcReveal reveal;
        if (ReadReveal(mnOutpoint, cycleId, reveal)) {
            reveals.push_back(reveal);
        }
    }

    return !reveals.empty();
}

bool CKHUDomcDB::EraseCycleIndex(uint32_t cycleId)
{
    auto key = std::make_pair(DB_DOMC, std::make_pair(DB_DOMC_INDEX, cycleId));
    return Erase(key);
}

bool CKHUDomcDB::EraseCycleData(uint32_t cycleId)
{
    // Get list of masternodes that participated in this cycle
    std::vector<COutPoint> mnOutpoints;
    bool hasIndex = GetMasternodesForCycle(cycleId, mnOutpoints);

    if (hasIndex) {
        // Erase all commits and reveals for this cycle
        for (const auto& mnOutpoint : mnOutpoints) {
            // Erase commit (if exists)
            EraseCommit(mnOutpoint, cycleId);

            // Erase reveal (if exists)
            EraseReveal(mnOutpoint, cycleId);
        }

        LogPrint(BCLog::HU, "EraseCycleData: Erased %zu commits/reveals for cycle %u\n",
                 mnOutpoints.size(), cycleId);
    }

    // Erase the cycle index
    bool result = EraseCycleIndex(cycleId);

    if (result) {
        LogPrint(BCLog::HU, "EraseCycleData: Successfully cleaned up cycle %u\n", cycleId);
    }

    return result;
}

// ============================================================================
// Global accessor functions
// ============================================================================

bool InitKHUDomcDB(size_t nCacheSize, bool fReindex)
{
    try {
        pkhudomcdb.reset();
        pkhudomcdb = std::make_unique<CKHUDomcDB>(nCacheSize, false, fReindex);
        LogPrint(BCLog::HU, "KHU: Initialized DOMC database (Phase 6.2 Governance)\n");
        return true;
    } catch (const std::exception& e) {
        LogPrintf("ERROR: Failed to initialize KHU DOMC database: %s\n", e.what());
        return false;
    }
}

CKHUDomcDB* GetKHUDomcDB()
{
    return pkhudomcdb.get();
}
