// Copyright (c) 2025 The PIVHU Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "piv2/piv2_finality.h"

#include "chain.h"
#include "chainparams.h"
#include "logging.h"
#include "tiertwo/tiertwo_sync_state.h"
#include "utiltime.h"
#include "validation.h"

#include <boost/filesystem.hpp>

namespace hu {

std::unique_ptr<CHuFinalityHandler> huFinalityHandler;
std::unique_ptr<CHuFinalityDB> pHuFinalityDB;

// DB key prefix for finality records
static const char DB_HU_FINALITY = 'F';

// ============================================================================
// CHuFinalityDB Implementation
// ============================================================================

CHuFinalityDB::CHuFinalityDB(size_t nCacheSize, bool fMemory, bool fWipe)
    : CDBWrapper(GetDataDir() / "hu_finality", nCacheSize, fMemory, fWipe)
{
}

bool CHuFinalityDB::WriteFinality(const CHuFinality& finality)
{
    return Write(std::make_pair(DB_HU_FINALITY, finality.blockHash), finality);
}

bool CHuFinalityDB::ReadFinality(const uint256& blockHash, CHuFinality& finality) const
{
    return Read(std::make_pair(DB_HU_FINALITY, blockHash), finality);
}

bool CHuFinalityDB::HasFinality(const uint256& blockHash) const
{
    return Exists(std::make_pair(DB_HU_FINALITY, blockHash));
}

bool CHuFinalityDB::EraseFinality(const uint256& blockHash)
{
    return Erase(std::make_pair(DB_HU_FINALITY, blockHash));
}

bool CHuFinalityDB::IsBlockFinal(const uint256& blockHash, int nThreshold) const
{
    CHuFinality finality;
    if (!ReadFinality(blockHash, finality)) {
        return false;
    }
    return finality.HasFinality(nThreshold);
}

// ============================================================================
// Global Functions
// ============================================================================

void InitHuFinality(size_t nCacheSize, bool fWipe)
{
    const Consensus::Params& consensus = Params().GetConsensus();

    // Initialize in-memory handler
    huFinalityHandler = std::make_unique<CHuFinalityHandler>();

    // Initialize LevelDB persistence
    pHuFinalityDB = std::make_unique<CHuFinalityDB>(nCacheSize, false, fWipe);

    LogPrintf("HU Finality: Initialized (quorum=%d/%d, timeout=%ds, maxReorg=%d)\n",
              consensus.nHuQuorumThreshold,
              consensus.nHuQuorumSize,
              consensus.nHuLeaderTimeoutSeconds,
              consensus.nHuMaxReorgDepth);
}

void ShutdownHuFinality()
{
    pHuFinalityDB.reset();
    huFinalityHandler.reset();
    LogPrintf("HU Finality: Shutdown\n");
}

bool IsBlockHuFinal(const uint256& blockHash)
{
    if (!pHuFinalityDB) {
        return false;
    }

    const Consensus::Params& consensus = Params().GetConsensus();
    return pHuFinalityDB->IsBlockFinal(blockHash, consensus.nHuQuorumThreshold);
}

bool WouldViolateHuFinality(const CBlockIndex* pindexNew, const CBlockIndex* pindexFork)
{
    if (!pindexNew || !pindexFork || !pHuFinalityDB) {
        return false;
    }

    const Consensus::Params& consensus = Params().GetConsensus();

    // Walk from fork point to current tip, checking for finalized blocks
    const CBlockIndex* pindex = chainActive.Tip();
    while (pindex && pindex != pindexFork) {
        if (pHuFinalityDB->IsBlockFinal(pindex->GetBlockHash(), consensus.nHuQuorumThreshold)) {
            LogPrint(BCLog::HU, "HU Finality: Reorg blocked - block %s at height %d is finalized\n",
                     pindex->GetBlockHash().ToString().substr(0, 16), pindex->nHeight);
            return true;
        }
        pindex = pindex->pprev;
    }

    return false;
}

bool CHuFinalityHandler::HasFinality(int nHeight, const uint256& blockHash) const
{
    LOCK(cs);

    // Check if we have finality data for this block
    auto it = mapFinality.find(blockHash);
    if (it == mapFinality.end()) {
        return false;
    }

    // Verify height matches
    if (it->second.nHeight != nHeight) {
        LogPrint(BCLog::HU, "HU Finality: Height mismatch for %s (expected %d, got %d)\n",
                 blockHash.ToString().substr(0, 16), nHeight, it->second.nHeight);
        return false;
    }

    return it->second.HasFinality();
}

bool CHuFinalityHandler::HasConflictingFinality(int nHeight, const uint256& blockHash) const
{
    LOCK(cs);

    // Check if there's a different finalized block at this height
    auto heightIt = mapHeightToBlock.find(nHeight);
    if (heightIt == mapHeightToBlock.end()) {
        return false; // No finalized block at this height
    }

    // If same hash, no conflict
    if (heightIt->second == blockHash) {
        return false;
    }

    // Check if the other block actually has finality
    auto finalityIt = mapFinality.find(heightIt->second);
    if (finalityIt == mapFinality.end()) {
        return false;
    }

    if (finalityIt->second.HasFinality()) {
        LogPrint(BCLog::HU, "HU Finality: Conflicting block at height %d. Finalized: %s, Attempted: %s\n",
                 nHeight,
                 heightIt->second.ToString().substr(0, 16),
                 blockHash.ToString().substr(0, 16));
        return true;
    }

    return false;
}

bool CHuFinalityHandler::AddSignature(const CHuSignature& sig)
{
    LOCK(cs);

    // Get or create finality entry
    auto& finality = mapFinality[sig.blockHash];
    if (finality.blockHash.IsNull()) {
        finality.blockHash = sig.blockHash;
        // Note: nHeight should be set by caller via MarkBlockFinal or separate method
    }

    // Check if we already have this signature
    if (finality.mapSignatures.count(sig.proTxHash)) {
        LogPrint(BCLog::HU, "HU Finality: Duplicate signature from %s for block %s\n",
                 sig.proTxHash.ToString().substr(0, 16),
                 sig.blockHash.ToString().substr(0, 16));
        return false;
    }

    // Add signature
    finality.mapSignatures[sig.proTxHash] = sig.vchSig;

    const Consensus::Params& consensus = Params().GetConsensus();
    const int nThreshold = consensus.nHuQuorumThreshold;

    LogPrint(BCLog::HU, "HU Finality: Added signature %zu/%d from %s for block %s\n",
             finality.mapSignatures.size(), nThreshold,
             sig.proTxHash.ToString().substr(0, 16),
             sig.blockHash.ToString().substr(0, 16));

    // Check if we just reached finality
    if (static_cast<int>(finality.mapSignatures.size()) == nThreshold) {
        // Get block height from mapBlockIndex if not set
        int nHeight = finality.nHeight;
        if (nHeight <= 0) {
            LOCK(cs_main);
            auto it = mapBlockIndex.find(sig.blockHash);
            if (it != mapBlockIndex.end()) {
                nHeight = it->second->nHeight;
                finality.nHeight = nHeight;
            }
        }

        LogPrintf("HU Finality: Block %s at height %d reached finality (%d signatures)\n",
                  sig.blockHash.ToString().substr(0, 16), nHeight, nThreshold);

        // Update height->block mapping if we have the height
        if (nHeight > 0) {
            mapHeightToBlock[nHeight] = sig.blockHash;

            // PIV2: Notify sync state that we have a finalized block
            // This is critical for DMM to know it can produce the next block
            g_tiertwo_sync_state.OnFinalizedBlock(nHeight, GetTime());
            LogPrint(BCLog::HU, "HU Finality: Notified sync state of finalized block at height %d\n",
                     nHeight);
        }
    }

    return true;
}

bool CHuFinalityHandler::GetFinality(const uint256& blockHash, CHuFinality& finalityOut) const
{
    LOCK(cs);

    auto it = mapFinality.find(blockHash);
    if (it == mapFinality.end()) {
        return false;
    }

    finalityOut = it->second;
    return true;
}

void CHuFinalityHandler::MarkBlockFinal(int nHeight, const uint256& blockHash)
{
    LOCK(cs);

    auto& finality = mapFinality[blockHash];
    finality.blockHash = blockHash;
    finality.nHeight = nHeight;

    // Update height mapping
    mapHeightToBlock[nHeight] = blockHash;

    LogPrint(BCLog::HU, "HU Finality: Marked block %s at height %d as final candidate\n",
             blockHash.ToString().substr(0, 16), nHeight);
}

int CHuFinalityHandler::GetSignatureCount(const uint256& blockHash) const
{
    LOCK(cs);

    auto it = mapFinality.find(blockHash);
    if (it == mapFinality.end()) {
        return 0;
    }

    return static_cast<int>(it->second.mapSignatures.size());
}

void CHuFinalityHandler::Clear()
{
    LOCK(cs);
    mapFinality.clear();
    mapHeightToBlock.clear();
}

} // namespace hu
