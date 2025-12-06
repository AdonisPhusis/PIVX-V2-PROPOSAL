// Copyright (c) 2021 The PIVX Core developers
// Copyright (c) 2025 The PIV2 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include "tiertwo/tiertwo_sync_state.h"
#include "utiltime.h"

TierTwoSyncState g_tiertwo_sync_state;

// Bootstrap height: blocks 0-5 are exempt from HU quorum requirement
// Block 0: Genesis
// Block 1: Premine
// Block 2: Collateral tx confirmation
// Blocks 3-5: ProRegTx (3 MNs)
// Block 6+: DMM active, requires HU quorum
static const int PIV2_BOOTSTRAP_HEIGHT = 5;

// Cold start recovery: if the network has been down for this long,
// allow masternodes to resume block production without recent finalized blocks
// This breaks the "chicken and egg" deadlock when restarting a stale network
static const int64_t PIV2_COLD_START_TIMEOUT = 300; // 5 minutes

/**
 * PIV2: Simplified sync check based on HU finality
 *
 * During bootstrap (height <= 5): Always synced
 * After bootstrap: Synced if we received a finalized block (with HU quorum) in the last 120 seconds.
 * Cold start recovery: If startup time > 5 minutes AND no finalized blocks received,
 *                      allow sync to prevent network deadlock after downtime.
 */
bool TierTwoSyncState::IsBlockchainSynced() const
{
    int chainHeight = m_chain_height.load();

    // Bootstrap phase: blocks 0-5 are always considered synced
    // This allows DMM to start producing block 6 without waiting for HU signatures
    if (chainHeight <= PIV2_BOOTSTRAP_HEIGHT) {
        return true;
    }

    // After bootstrap: check if we received a finalized block recently
    int64_t lastFinalized = m_last_finalized_time.load();
    int64_t now = GetTime();

    // Normal case: we received a finalized block recently
    if ((now - lastFinalized) <= PIV2_SYNC_TIMEOUT) {
        return true;
    }

    // Cold start recovery: allow sync if node has been running long enough
    // This handles the case where:
    // 1. Network was down (no finalized blocks for a while)
    // 2. All nodes restart with bootstrap data
    // 3. No node can produce blocks because IsBlockchainSynced() returns false
    // 4. IsBlockchainSynced() returns false because no new finalized blocks
    // Solution: After startup grace period, assume sync is OK to break the deadlock
    int64_t startupTime = m_startup_time.load();
    if (startupTime > 0 && (now - startupTime) >= PIV2_COLD_START_TIMEOUT) {
        // Node has been running for 5+ minutes without receiving finalized blocks
        // This is a cold start situation - allow sync to resume block production
        return true;
    }

    return false;
}

void TierTwoSyncState::OnFinalizedBlock(int height, int64_t timestamp)
{
    m_last_finalized_height.store(height);
    m_last_finalized_time.store(timestamp);
}
