// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "consensus/mn_validation.h"

#include "activemasternode.h"
#include "chain.h"
#include "consensus/validation.h"
#include "evo/blockproducer.h"
#include "evo/deterministicmns.h"
#include "logging.h"

bool CheckBlockMNOnly(const CBlock& block,
                      const CBlockIndex* pindexPrev,
                      CValidationState& state)
{
    if (!pindexPrev) {
        return state.DoS(100, false, REJECT_INVALID, "bad-mn-prev-null");
    }

    // Genesis block has no producer validation
    if (pindexPrev->nHeight < 0) {
        return true;
    }

    // Get DMN list at previous block
    if (!deterministicMNManager) {
        return state.DoS(100, false, REJECT_INVALID, "bad-mn-manager-null");
    }

    CDeterministicMNList mnList = deterministicMNManager->GetListForBlock(pindexPrev);

    // Before any MN is confirmed, allow blocks without producer signature
    // MNs need at least 1 confirmation after registration to be "confirmed"
    size_t confirmedCount = mnList.GetConfirmedMNsCount();
    if (confirmedCount == 0) {
        LogPrint(BCLog::MASTERNODE, "%s: No confirmed MNs at height %d (total: %d), allowing block\n",
                 __func__, pindexPrev->nHeight + 1, mnList.GetValidMNsCount());
        return true;
    }

    // Verify block producer signature
    return mn_consensus::VerifyBlockProducerSignature(block, pindexPrev, mnList, state);
}

bool IsLocalMNBlockProducer(const CBlockIndex* pindexPrev)
{
    if (!pindexPrev) {
        return false;
    }

    // Check if we have an active masternode
    if (!activeMasternodeManager || !activeMasternodeManager->IsReady()) {
        return false;
    }

    const uint256 localProTx = activeMasternodeManager->GetProTx();
    if (localProTx.IsNull()) {
        return false;
    }

    // Get expected producer
    uint256 expectedProTx;
    if (!GetExpectedBlockProducer(pindexPrev, expectedProTx)) {
        return false;
    }

    bool isProducer = (localProTx == expectedProTx);

    if (isProducer) {
        LogPrint(BCLog::MASTERNODE, "%s: Local MN %s is producer for block %d\n",
                 __func__, localProTx.ToString().substr(0, 16),
                 pindexPrev->nHeight + 1);
    }

    return isProducer;
}

bool GetExpectedBlockProducer(const CBlockIndex* pindexPrev, uint256& proTxHashRet)
{
    proTxHashRet.SetNull();

    if (!pindexPrev || !deterministicMNManager) {
        return false;
    }

    CDeterministicMNList mnList = deterministicMNManager->GetListForBlock(pindexPrev);

    if (mnList.GetValidMNsCount() == 0) {
        return false;
    }

    CDeterministicMNCPtr producer;
    if (!mn_consensus::GetBlockProducer(pindexPrev, mnList, producer)) {
        return false;
    }

    proTxHashRet = producer->proTxHash;
    return true;
}
