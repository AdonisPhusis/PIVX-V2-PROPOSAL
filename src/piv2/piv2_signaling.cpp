// Copyright (c) 2025 The PIV2 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "piv2/piv2_signaling.h"

#include "activemasternode.h"
#include "chain.h"
#include "chainparams.h"
#include "evo/deterministicmns.h"
#include "hash.h"
#include "key.h"
#include "logging.h"
#include "netmessagemaker.h"
#include "piv2/piv2_quorum.h"
#include "protocol.h"
#include "util/system.h"
#include "validation.h"

namespace hu {

std::unique_ptr<CHuSignalingManager> huSignalingManager;

// ============================================================================
// Initialization
// ============================================================================

void InitHuSignaling()
{
    huSignalingManager = std::make_unique<CHuSignalingManager>();
    LogPrintf("HU Signaling: Initialized\n");
}

void ShutdownHuSignaling()
{
    huSignalingManager.reset();
    LogPrintf("HU Signaling: Shutdown\n");
}

// ============================================================================
// CHuSignalingManager Implementation
// ============================================================================

bool CHuSignalingManager::OnNewBlock(const CBlockIndex* pindex, CConnman* connman)
{
    if (!pindex || !connman) {
        return false;
    }

    // Only masternodes sign blocks
    if (!fMasterNode || !activeMasternodeManager || !activeMasternodeManager->IsReady()) {
        return false;
    }

    const uint256& blockHash = pindex->GetBlockHash();

    {
        LOCK(cs);
        // Already signed this block?
        if (setSignedBlocks.count(blockHash)) {
            return false;
        }
    }

    // Check if we're in the quorum for this block
    const Consensus::Params& consensus = Params().GetConsensus();
    CDeterministicMNList mnList = deterministicMNManager->GetListForBlock(pindex->pprev);

    int cycleIndex = GetHuCycleIndex(pindex->nHeight, consensus.nHuQuorumRotationBlocks);
    uint256 prevCycleHash = pindex->pprev ? pindex->pprev->GetBlockHash() : uint256();

    if (!IsInHuQuorum(mnList, cycleIndex, prevCycleHash, activeMasternodeManager->GetProTx())) {
        LogPrint(BCLog::HU, "HU Signaling: Not in quorum for block %s at height %d\n",
                 blockHash.ToString().substr(0, 16), pindex->nHeight);
        return false;
    }

    // Sign the block
    CHuSignature sig;
    if (!SignBlock(blockHash, sig)) {
        LogPrintf("HU Signaling: ERROR - Failed to sign block %s\n", blockHash.ToString().substr(0, 16));
        return false;
    }

    {
        LOCK(cs);
        setSignedBlocks.insert(blockHash);

        // Add to local cache
        mapSigCache[blockHash][sig.proTxHash] = sig.vchSig;
    }

    // Add to finality handler
    if (huFinalityHandler) {
        huFinalityHandler->AddSignature(sig);
    }

    // Broadcast to network
    BroadcastSignature(sig, connman);

    LogPrintf("HU Signaling: Signed and broadcast signature for block %s at height %d\n",
              blockHash.ToString().substr(0, 16), pindex->nHeight);

    return true;
}

bool CHuSignalingManager::ProcessHuSignature(const CHuSignature& sig, CNode* pfrom, CConnman* connman)
{
    // Basic validation
    if (sig.blockHash.IsNull() || sig.proTxHash.IsNull() || sig.vchSig.empty()) {
        LogPrint(BCLog::HU, "HU Signaling: Invalid signature structure\n");
        return false;
    }

    // Check if we already have this signature
    {
        LOCK(cs);
        auto it = mapSigCache.find(sig.blockHash);
        if (it != mapSigCache.end() && it->second.count(sig.proTxHash)) {
            // Already have this signature
            return false;
        }
    }

    // Get the block index
    const CBlockIndex* pindex = nullptr;
    {
        LOCK(cs_main);
        auto it = mapBlockIndex.find(sig.blockHash);
        if (it == mapBlockIndex.end()) {
            LogPrint(BCLog::HU, "HU Signaling: Unknown block %s for signature\n",
                     sig.blockHash.ToString().substr(0, 16));
            return false;
        }
        pindex = it->second;
    }

    // Validate the signature
    if (!ValidateSignature(sig, pindex)) {
        LogPrint(BCLog::HU, "HU Signaling: Invalid signature from %s for block %s\n",
                 sig.proTxHash.ToString().substr(0, 16), sig.blockHash.ToString().substr(0, 16));
        return false;
    }

    // Add to cache and finality handler
    {
        LOCK(cs);
        mapSigCache[sig.blockHash][sig.proTxHash] = sig.vchSig;
    }

    if (huFinalityHandler) {
        huFinalityHandler->AddSignature(sig);
    }

    // Check if we just reached quorum
    const Consensus::Params& consensus = Params().GetConsensus();
    int sigCount = GetSignatureCount(sig.blockHash);
    if (sigCount == consensus.nHuQuorumThreshold) {
        LogPrintf("HU Signaling: Block %s reached quorum (%d/%d signatures)\n",
                  sig.blockHash.ToString().substr(0, 16), sigCount, consensus.nHuQuorumSize);
    }

    // Relay to other peers
    BroadcastSignature(sig, connman, pfrom);

    LogPrint(BCLog::HU, "HU Signaling: Accepted signature %d/%d from %s for block %s\n",
             sigCount, consensus.nHuQuorumThreshold,
             sig.proTxHash.ToString().substr(0, 16), sig.blockHash.ToString().substr(0, 16));

    return true;
}

bool CHuSignalingManager::SignBlock(const uint256& blockHash, CHuSignature& sigOut)
{
    if (!activeMasternodeManager || !activeMasternodeManager->IsReady()) {
        return false;
    }

    // Get operator key
    CKey operatorKey;
    CDeterministicMNCPtr dmn;
    auto keyResult = activeMasternodeManager->GetOperatorKey(operatorKey, dmn);
    if (!keyResult) {
        LogPrintf("HU Signaling: Failed to get operator key: %s\n", keyResult.getError());
        return false;
    }

    // Create message to sign: "HUSIG" || blockHash
    CHashWriter ss(SER_GETHASH, 0);
    ss << std::string("HUSIG");
    ss << blockHash;
    uint256 msgHash = ss.GetHash();

    // Sign with ECDSA
    std::vector<unsigned char> vchSig;
    if (!operatorKey.SignCompact(msgHash, vchSig)) {
        LogPrintf("HU Signaling: Failed to sign block hash\n");
        return false;
    }

    sigOut.blockHash = blockHash;
    sigOut.proTxHash = activeMasternodeManager->GetProTx();
    sigOut.vchSig = vchSig;

    return true;
}

bool CHuSignalingManager::ValidateSignature(const CHuSignature& sig, const CBlockIndex* pindex) const
{
    if (!pindex || !pindex->pprev) {
        return false;
    }

    const Consensus::Params& consensus = Params().GetConsensus();

    // Get the MN list at the block's height
    CDeterministicMNList mnList = deterministicMNManager->GetListForBlock(pindex->pprev);

    // Check if the signer is in the quorum
    int cycleIndex = GetHuCycleIndex(pindex->nHeight, consensus.nHuQuorumRotationBlocks);
    uint256 prevCycleHash = pindex->pprev->GetBlockHash();

    if (!IsInHuQuorum(mnList, cycleIndex, prevCycleHash, sig.proTxHash)) {
        LogPrint(BCLog::HU, "HU Signaling: Signer %s not in quorum for height %d\n",
                 sig.proTxHash.ToString().substr(0, 16), pindex->nHeight);
        return false;
    }

    // Get the MN's operator pubkey
    CDeterministicMNCPtr dmn = mnList.GetMN(sig.proTxHash);
    if (!dmn) {
        LogPrint(BCLog::HU, "HU Signaling: Unknown MN %s\n", sig.proTxHash.ToString().substr(0, 16));
        return false;
    }

    // Recreate the message hash
    CHashWriter ss(SER_GETHASH, 0);
    ss << std::string("HUSIG");
    ss << sig.blockHash;
    uint256 msgHash = ss.GetHash();

    // Recover pubkey from compact signature
    CPubKey recoveredPubKey;
    if (!recoveredPubKey.RecoverCompact(msgHash, sig.vchSig)) {
        LogPrint(BCLog::HU, "HU Signaling: Failed to recover pubkey from signature\n");
        return false;
    }

    // Verify it matches the operator pubkey
    // The operator pubkey is stored directly in the DMN state as CPubKey
    if (recoveredPubKey != dmn->pdmnState->pubKeyOperator) {
        LogPrint(BCLog::HU, "HU Signaling: Signature pubkey mismatch for MN %s\n",
                 sig.proTxHash.ToString().substr(0, 16));
        return false;
    }

    return true;
}

void CHuSignalingManager::BroadcastSignature(const CHuSignature& sig, CConnman* connman, CNode* pfrom)
{
    if (!connman) {
        return;
    }

    {
        LOCK(cs);
        // Track relayed signatures to avoid spam
        if (mapRelayedSigs[sig.blockHash].count(sig.proTxHash)) {
            return;  // Already relayed this signature
        }
        mapRelayedSigs[sig.blockHash].insert(sig.proTxHash);
    }

    // Broadcast to all peers except the one we received it from
    connman->ForEachNode([&](CNode* pnode) {
        if (pnode == pfrom) {
            return;  // Don't send back to sender
        }
        if (!pnode->fSuccessfullyConnected || pnode->fDisconnect) {
            return;
        }

        CNetMsgMaker msgMaker(pnode->GetSendVersion());
        connman->PushMessage(pnode, msgMaker.Make(NetMsgType::HUSIG, sig));
    });
}

bool CHuSignalingManager::HasSigned(const uint256& blockHash) const
{
    LOCK(cs);
    return setSignedBlocks.count(blockHash) > 0;
}

int CHuSignalingManager::GetSignatureCount(const uint256& blockHash) const
{
    LOCK(cs);
    auto it = mapSigCache.find(blockHash);
    if (it == mapSigCache.end()) {
        return 0;
    }
    return static_cast<int>(it->second.size());
}

bool CHuSignalingManager::HasQuorum(const uint256& blockHash) const
{
    const Consensus::Params& consensus = Params().GetConsensus();
    return GetSignatureCount(blockHash) >= consensus.nHuQuorumThreshold;
}

void CHuSignalingManager::Cleanup(int nCurrentHeight)
{
    LOCK(cs);

    // Only cleanup every 100 blocks
    if (nCurrentHeight - nLastCleanupHeight < 100) {
        return;
    }
    nLastCleanupHeight = nCurrentHeight;

    // We need block heights to clean up properly
    // For now, just limit cache size
    const size_t nMaxCacheSize = 500;

    if (mapSigCache.size() > nMaxCacheSize) {
        // Remove oldest entries (this is a simple approach)
        while (mapSigCache.size() > nMaxCacheSize / 2) {
            mapSigCache.erase(mapSigCache.begin());
        }
    }

    if (mapRelayedSigs.size() > nMaxCacheSize) {
        while (mapRelayedSigs.size() > nMaxCacheSize / 2) {
            mapRelayedSigs.erase(mapRelayedSigs.begin());
        }
    }

    if (setSignedBlocks.size() > nMaxCacheSize) {
        auto it = setSignedBlocks.begin();
        while (setSignedBlocks.size() > nMaxCacheSize / 2 && it != setSignedBlocks.end()) {
            it = setSignedBlocks.erase(it);
        }
    }

    LogPrint(BCLog::HU, "HU Signaling: Cleanup complete. Cache sizes: sigs=%zu, relayed=%zu, signed=%zu\n",
             mapSigCache.size(), mapRelayedSigs.size(), setSignedBlocks.size());
}

void CHuSignalingManager::Clear()
{
    LOCK(cs);
    setSignedBlocks.clear();
    mapRelayedSigs.clear();
    mapSigCache.clear();
    nLastCleanupHeight = 0;
}

// ============================================================================
// Global Functions
// ============================================================================

void NotifyBlockConnected(const CBlockIndex* pindex, CConnman* connman)
{
    if (!huSignalingManager) {
        return;
    }

    huSignalingManager->OnNewBlock(pindex, connman);
    huSignalingManager->Cleanup(pindex->nHeight);
}

bool PreviousBlockHasQuorum(const CBlockIndex* pindexPrev)
{
    if (!pindexPrev) {
        return true;  // Genesis - no previous block to check
    }

    const Consensus::Params& consensus = Params().GetConsensus();

    // Bootstrap phase: first N blocks don't require quorum
    // This allows the network to start producing blocks
    const int nBootstrapBlocks = 10;
    if (pindexPrev->nHeight < nBootstrapBlocks) {
        LogPrint(BCLog::HU, "HU Signaling: Bootstrap phase (height %d < %d), skipping quorum check\n",
                 pindexPrev->nHeight, nBootstrapBlocks);
        return true;
    }

    // Check if previous block has quorum
    const uint256& prevHash = pindexPrev->GetBlockHash();

    if (huSignalingManager && huSignalingManager->HasQuorum(prevHash)) {
        return true;
    }

    // Also check the finality handler (for persisted data)
    if (huFinalityHandler) {
        CHuFinality finality;
        if (huFinalityHandler->GetFinality(prevHash, finality)) {
            if (finality.HasFinality(consensus.nHuQuorumThreshold)) {
                return true;
            }
        }
    }

    // Check DB for persisted finality
    if (pHuFinalityDB && pHuFinalityDB->IsBlockFinal(prevHash, consensus.nHuQuorumThreshold)) {
        return true;
    }

    int sigCount = huSignalingManager ? huSignalingManager->GetSignatureCount(prevHash) : 0;
    LogPrint(BCLog::HU, "HU Signaling: Previous block %s lacks quorum (%d/%d signatures)\n",
             prevHash.ToString().substr(0, 16), sigCount, consensus.nHuQuorumThreshold);

    return false;
}

} // namespace hu
