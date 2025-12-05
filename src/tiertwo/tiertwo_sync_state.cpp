// Copyright (c) 2021 The PIVX Core developers
// Copyright (c) 2025 The PIV2 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include "tiertwo/tiertwo_sync_state.h"
#include "utiltime.h"

TierTwoSyncState g_tiertwo_sync_state;

/**
 * PIV2: Simplified sync check based on HU finality
 *
 * Synced if:
 * 1. We're in bootstrap phase (first 10 blocks) - always synced
 * 2. We received a finalized block (with HU quorum) in the last 60 seconds
 */
bool TierTwoSyncState::IsBlockchainSynced() const
{
    int height = m_chain_height.load();

    // Bootstrap phase: always synced for first 10 blocks
    if (height < PIV2_BOOTSTRAP_BLOCKS) {
        return true;
    }

    // Check if we received a finalized block recently
    int64_t lastFinalized = m_last_finalized_time.load();
    int64_t now = GetTime();

    return (now - lastFinalized) < PIV2_SYNC_TIMEOUT;
}

void TierTwoSyncState::OnFinalizedBlock(int height, int64_t timestamp)
{
    m_last_finalized_height.store(height);
    m_last_finalized_time.store(timestamp);
}
