// Copyright (c) 2014-2019 The Dash Core developers
// Copyright (c) 2025 The PIVHU Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "evo/evonotificationinterface.h"

#include "evo/deterministicmns.h"
#include "evo/mnauth.h"
#include "piv2/piv2_finality.h"
#include "validation.h"

void EvoNotificationInterface::InitializeCurrentBlockTip()
{
    LOCK(cs_main);
    deterministicMNManager->SetTipIndex(chainActive.Tip());
}

void EvoNotificationInterface::AcceptedBlockHeader(const CBlockIndex* pindexNew)
{
    // HU finality is signature-based at block validation time, not header acceptance
}

void EvoNotificationInterface::UpdatedBlockTip(const CBlockIndex *pindexNew, const CBlockIndex *pindexFork, bool fInitialDownload)
{
    // HU finality is handled during block validation in ConnectBlock
}

void EvoNotificationInterface::NotifyMasternodeListChanged(bool undo, const CDeterministicMNList& oldMNList, const CDeterministicMNListDiff& diff)
{
    CMNAuth::NotifyMasternodeListChanged(undo, oldMNList, diff);
}
