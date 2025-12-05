// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2022 The PIVX Core developers
// Copyright (c) 2025 The PIV2 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#ifndef HU_MASTERNODE_SYNC_H
#define HU_MASTERNODE_SYNC_H

#include "net.h"
#include <string>

/**
 * PIV2: Simplified masternode sync
 *
 * Legacy sync is replaced by HU finality-based sync.
 * This class provides minimal compatibility interface.
 */
class CMasternodeSync
{
public:
    CMasternodeSync();

    std::string GetSyncStatus();
    void ProcessSyncStatusMsg(int nItemID, int itemCount);

    void Reset();
    void Process();
    bool SyncWithNode(CNode* pnode);
    bool NotCompleted();
    void UpdateBlockchainSynced(bool isRegTestNet);
    void ClearFulfilledRequest();
};

extern CMasternodeSync masternodeSync;

#endif // HU_MASTERNODE_SYNC_H
