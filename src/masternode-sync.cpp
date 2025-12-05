// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2022 The PIVX Core developers
// Copyright (c) 2025 The PIV2 Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * PIV2: Simplified masternode sync
 *
 * Legacy sync phases (SPORKS, LIST, MNW) are removed.
 * Sync is now based on HU finality - a node is "synced" if:
 * 1. It's in bootstrap phase (first 10 blocks), OR
 * 2. It received a finalized block (with HU quorum) recently
 *
 * This is handled by TierTwoSyncState, this file provides
 * minimal compatibility for legacy code paths.
 */

#include "masternode-sync.h"
#include "tiertwo/tiertwo_sync_state.h"
#include "util/system.h"

CMasternodeSync masternodeSync;

CMasternodeSync::CMasternodeSync()
{
    Reset();
}

bool CMasternodeSync::NotCompleted()
{
    // PIV2: Sync is complete when we have HU finality
    return !g_tiertwo_sync_state.IsSynced();
}

void CMasternodeSync::UpdateBlockchainSynced(bool isRegTestNet)
{
    // PIV2: No-op, sync is automatic based on HU finality
}

void CMasternodeSync::Reset()
{
    g_tiertwo_sync_state.ResetData();
}

std::string CMasternodeSync::GetSyncStatus()
{
    if (g_tiertwo_sync_state.IsSynced()) {
        return "Synchronized (HU finality)";
    }

    int height = g_tiertwo_sync_state.GetChainHeight();
    if (height < PIV2_BOOTSTRAP_BLOCKS) {
        return strprintf("Bootstrap phase (%d/%d blocks)", height, PIV2_BOOTSTRAP_BLOCKS);
    }

    return "Waiting for finalized blocks...";
}

void CMasternodeSync::ProcessSyncStatusMsg(int nItemID, int nCount)
{
    // PIV2: Legacy sync messages ignored
}

void CMasternodeSync::ClearFulfilledRequest()
{
    // PIV2: No-op
}

void CMasternodeSync::Process()
{
    // PIV2: Sync is automatic, nothing to do
}

bool CMasternodeSync::SyncWithNode(CNode* pnode)
{
    // PIV2: No peer sync needed
    return true;
}
