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

/**
 * PIV2: Simplified sync check based on HU finality
 *
 * During bootstrap (height <= 5): Always synced
 * After bootstrap: Synced if we received a finalized block (with HU quorum) in the last 60 seconds.
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

    return (now - lastFinalized) <= PIV2_SYNC_TIMEOUT;
}

void TierTwoSyncState::OnFinalizedBlock(int height, int64_t timestamp)
{
    m_last_finalized_height.store(height);
    m_last_finalized_time.store(timestamp);
}
