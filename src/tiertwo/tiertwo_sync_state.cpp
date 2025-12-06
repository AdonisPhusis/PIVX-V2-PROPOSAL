// Copyright (c) 2021 The PIVX Core developers
// Copyright (c) 2025 The PIV2 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include "tiertwo/tiertwo_sync_state.h"
#include "chain.h"
#include "validation.h"
#include "utiltime.h"
#include "logging.h"
#include "net.h"

TierTwoSyncState g_tiertwo_sync_state;

// Bootstrap height: blocks 0-5 are exempt from HU quorum requirement
// Block 0: Genesis
// Block 1: Premine
// Block 2: Collateral tx confirmation
// Blocks 3-5: ProRegTx (3 MNs)
// Block 6+: DMM active, requires HU quorum
static const int PIV2_BOOTSTRAP_HEIGHT = 5;

// Cold start timeout: if the tip is older than this, consider the network stale
// and allow masternodes to restart block production without recent finality.
// Testnet: 5 * 120s = 600s (10 minutes) - ~10 missed blocks
// For mainnet: consider 30 * 120s = 3600s (1 hour)
static const int64_t PIV2_STALE_CHAIN_TIMEOUT = 5 * PIV2_SYNC_TIMEOUT;

// Maximum blocks behind best peer before we consider ourselves "behind"
// If a peer announces a height > localHeight + this tolerance, we're not synced
static const int PIV2_PEER_HEIGHT_TOLERANCE = 2;

/**
 * Get the maximum height announced by any connected peer
 * Returns -1 if no peers connected or g_connman not available
 */
static int GetBestPeerHeight()
{
    if (!g_connman) {
        return -1;
    }

    int bestHeight = -1;
    g_connman->ForEachNode([&bestHeight](CNode* pnode) {
        // nStartingHeight is set when the peer connects and sends version message
        // It represents the best height the peer had when connecting
        int peerHeight = pnode->nStartingHeight;
        if (peerHeight > bestHeight) {
            bestHeight = peerHeight;
        }
    });

    return bestHeight;
}

/**
 * PIV2: Simplified sync check based on HU finality
 *
 * Logic (ordered by priority):
 * 1. Bootstrap phase (height <= 5): Always synced
 * 2. Cold start recovery: If tip is very old (stale chain), bypass all checks
 * 3. Recent finality: Synced if we received a finalized block recently (< 120s)
 * 4. Peer height check: NOT synced if any peer announces height > local + 2
 * 5. Never synced during IBD (initial block download) - we might be behind
 *
 * The cold start case handles network-wide restarts where:
 * - All nodes have the same stale tip
 * - No recent finalized blocks exist
 * - The chain has been dead for a while (tip very old)
 * - We need to allow DMM to produce the next block to restart finality
 *
 * IMPORTANT: Cold start and finality checks MUST come BEFORE peer/IBD checks because:
 * - IsInitialBlockDownload() returns true if tip is too old (nMaxTipAge)
 * - On a stale testnet, IBD will stay true forever (chicken-and-egg)
 * - Cold start allows DMM to produce a new block
 * - After cold start, HU finality gives us confidence we're synced
 * - IBD is legacy Bitcoin logic that doesn't understand HU
 *
 * CRITICAL: The peer height check prevents a node from producing blocks while
 * behind peers. This avoids creating fork branches. A node must download
 * blocks from peers before participating in consensus.
 */
bool TierTwoSyncState::IsBlockchainSynced() const
{
    // Get current chain tip first - needed for all checks
    const CBlockIndex* tip = nullptr;
    {
        LOCK(cs_main);
        tip = chainActive.Tip();
    }

    if (!tip) {
        LogPrint(BCLog::MASTERNODE, "IsBlockchainSynced: false (no tip)\n");
        return false;
    }

    int height = tip->nHeight;
    int64_t tipTime = tip->GetBlockTime();
    int64_t now = GetTime();
    int64_t tipAge = now - tipTime;

    // 1. Bootstrap phase: blocks 0-5 are always considered synced
    // This allows DMM to start producing block 6 without waiting for HU signatures
    if (height <= PIV2_BOOTSTRAP_HEIGHT) {
        LogPrint(BCLog::MASTERNODE, "IsBlockchainSynced: true (bootstrap phase, height=%d)\n", height);
        return true;
    }

    // 2. Cold start recovery: tip is very old (network was stopped)
    // This MUST be checked BEFORE IBD because IBD stays true on stale tips!
    // If tip is stale, we're on a dead network that needs restart.
    // We bypass IBD check to allow DMM to produce the next block.
    if (tipAge > PIV2_STALE_CHAIN_TIMEOUT) {
        LogPrintf("IsBlockchainSynced: true (COLD START RECOVERY - tip age=%ds, bypassing IBD)\n",
                 (int)tipAge);
        return true;
    }

    // 3. Recent finality: if we received a finalized block recently, we're synced
    // This MUST be checked BEFORE IBD because:
    // - After cold start recovery, the first block gets created and finalized
    // - But IBD may still be true (legacy Bitcoin code uses nMaxTipAge)
    // - We should trust HU finality over legacy IBD logic
    int64_t lastFinalized = m_last_finalized_time.load();
    if (lastFinalized > 0 && (now - lastFinalized) <= PIV2_SYNC_TIMEOUT) {
        LogPrint(BCLog::MASTERNODE, "IsBlockchainSynced: true (recent finality, age=%ds)\n",
                 (int)(now - lastFinalized));
        return true;
    }

    // 4. CRITICAL: If any peer announces a height significantly ahead, we're NOT synced
    // This prevents producing blocks while behind (would cause forks)
    // Must download blocks from peers before participating in consensus
    int bestPeerHeight = GetBestPeerHeight();
    if (bestPeerHeight > 0 && bestPeerHeight > height + PIV2_PEER_HEIGHT_TOLERANCE) {
        LogPrint(BCLog::MASTERNODE, "IsBlockchainSynced: false (behind peers, local=%d, best peer=%d)\n",
                 height, bestPeerHeight);
        return false;
    }

    // 5. Never say "synced" during IBD - we might be behind
    // Note: This is checked AFTER cold start, finality, AND peer height checks
    if (IsInitialBlockDownload()) {
        LogPrint(BCLog::MASTERNODE, "IsBlockchainSynced: false (IBD in progress)\n");
        return false;
    }

    // 6. Not synced - waiting for finality or still catching up
    LogPrint(BCLog::MASTERNODE, "IsBlockchainSynced: false (waiting for finality, lastFinalized=%ds ago, tipAge=%ds)\n",
             lastFinalized > 0 ? (int)(now - lastFinalized) : -1, (int)tipAge);
    return false;
}

void TierTwoSyncState::OnFinalizedBlock(int height, int64_t timestamp)
{
    m_last_finalized_height.store(height);
    m_last_finalized_time.store(timestamp);
    LogPrint(BCLog::MASTERNODE, "OnFinalizedBlock: height=%d, timestamp=%d\n", height, (int)timestamp);
}
