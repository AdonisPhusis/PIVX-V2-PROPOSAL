// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2015-2022 The PIVX Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activemasternode.h"

#include "addrman.h"
#include "blockassembler.h"
#include "net.h"  // For g_connman and CNode
#include "consensus/merkle.h"
#include "consensus/mn_validation.h"
#include "evo/blockproducer.h"
#include "key_io.h"
#include "masternode.h"
#include "masternodeconfig.h"
#include "masternodeman.h"
#include "messagesigner.h"
#include "netbase.h"
#include "primitives/block.h"
#include "protocol.h"
#include "shutdown.h"
#include "tiertwo/net_masternodes.h"
#include "tiertwo/tiertwo_sync_state.h"
#include "piv2/piv2_signaling.h"
#include "utiltime.h"
#include "validation.h"
#include "wallet/wallet.h"

// Keep track of the active Masternode
CActiveDeterministicMasternodeManager* activeMasternodeManager{nullptr};

// Definition of static constexpr members (required for ODR-use in C++14)
constexpr int CActiveDeterministicMasternodeManager::DMM_BLOCK_INTERVAL_SECONDS;
constexpr int CActiveDeterministicMasternodeManager::DMM_MISSED_BLOCK_TIMEOUT;

static bool GetLocalAddress(CService& addrRet)
{
    // First try to find whatever our own local address is known internally.
    // Addresses could be specified via 'externalip' or 'bind' option, discovered via UPnP
    // or added by TorController. Use some random dummy IPv4 peer to prefer the one
    // reachable via IPv4.
    CNetAddr addrDummyPeer;
    bool fFound{false};
    if (LookupHost("8.8.8.8", addrDummyPeer, false)) {
        fFound = GetLocal(addrRet, &addrDummyPeer) && CActiveDeterministicMasternodeManager::IsValidNetAddr(addrRet);
    }
    if (!fFound && Params().IsRegTestNet()) {
        if (Lookup("127.0.0.1", addrRet, GetListenPort(), false)) {
            fFound = true;
        }
    }
    if (!fFound) {
        // If we have some peers, let's try to find our local address from one of them
        g_connman->ForEachNodeContinueIf([&fFound, &addrRet](CNode* pnode) {
            if (pnode->addr.IsIPv4())
                fFound = GetLocal(addrRet, &pnode->addr) && CActiveDeterministicMasternodeManager::IsValidNetAddr(addrRet);
            return !fFound;
        });
    }
    return fFound;
}

std::string CActiveDeterministicMasternodeManager::GetStatus() const
{
    switch (state) {
        case MASTERNODE_WAITING_FOR_PROTX:    return "Waiting for ProTx to appear on-chain";
        case MASTERNODE_POSE_BANNED:          return "Masternode was PoSe banned";
        case MASTERNODE_REMOVED:              return "Masternode removed from list";
        case MASTERNODE_OPERATOR_KEY_CHANGED: return "Operator key changed or revoked";
        case MASTERNODE_PROTX_IP_CHANGED:     return "IP address specified in ProTx changed";
        case MASTERNODE_READY:                return "Ready";
        case MASTERNODE_ERROR:                return "Error. " + strError;
        default:                              return "Unknown";
    }
}

OperationResult CActiveDeterministicMasternodeManager::SetOperatorKey(const std::string& strMNOperatorPrivKey)
{
    LOCK(cs_main); // Lock cs_main so the node doesn't perform any action while we setup the Masternode
    LogPrintf("Initializing deterministic masternode...\n");
    if (strMNOperatorPrivKey.empty()) {
        return errorOut("ERROR: Masternode operator priv key cannot be empty.");
    }

    CKey opSk = KeyIO::DecodeSecret(strMNOperatorPrivKey);
    if (!opSk.IsValid()) {
        return errorOut(_("Invalid mnoperatorprivatekey. Please see the documentation."));
    }
    info.keyOperator = opSk;
    info.pubKeyOperator = info.keyOperator.GetPubKey();
    return {true};
}

OperationResult CActiveDeterministicMasternodeManager::GetOperatorKey(CKey& key, CDeterministicMNCPtr& dmn) const
{
    if (!IsReady()) {
        return errorOut("Active masternode not ready");
    }
    dmn = deterministicMNManager->GetListAtChainTip().GetValidMN(info.proTxHash);
    if (!dmn) {
        return errorOut(strprintf("Active masternode %s not registered or PoSe banned", info.proTxHash.ToString()));
    }
    // PIVHU: CPubKey direct comparison (no .Get())
    if (info.pubKeyOperator != dmn->pdmnState->pubKeyOperator) {
        return errorOut("Active masternode operator key changed or revoked");
    }
    // return key
    key = info.keyOperator;
    return {true};
}

void CActiveDeterministicMasternodeManager::Init(const CBlockIndex* pindexTip)
{
    // set masternode arg if called from RPC
    if (!fMasterNode) {
        gArgs.ForceSetArg("-masternode", "1");
        fMasterNode = true;
    }

    if (!deterministicMNManager->IsDIP3Enforced(pindexTip->nHeight)) {
        state = MASTERNODE_ERROR;
        strError = "Evo upgrade is not active yet.";
        LogPrintf("%s -- ERROR: %s\n", __func__, strError);
        return;
    }

    LOCK(cs_main);

    // Check that our local network configuration is correct
    if (!fListen) {
        // listen option is probably overwritten by smth else, no good
        state = MASTERNODE_ERROR;
        strError = "Masternode must accept connections from outside. Make sure listen configuration option is not overwritten by some another parameter.";
        LogPrintf("%s ERROR: %s\n", __func__, strError);
        return;
    }

    if (!GetLocalAddress(info.service)) {
        state = MASTERNODE_ERROR;
        strError = "Can't detect valid external address. Please consider using the externalip configuration option if problem persists. Make sure to use IPv4 address only.";
        LogPrintf("%s ERROR: %s\n", __func__, strError);
        return;
    }

    CDeterministicMNList mnList = deterministicMNManager->GetListForBlock(pindexTip);

    LogPrintf("%s: Looking for MN with operator key %s at height %d (mnList size: %d)\n",
              __func__, HexStr(info.pubKeyOperator), pindexTip->nHeight, mnList.GetValidMNsCount());

    CDeterministicMNCPtr dmn = mnList.GetMNByOperatorKey(info.pubKeyOperator);
    if (!dmn) {
        // MN not appeared on the chain yet
        LogPrintf("%s: MN not found in list yet\n", __func__);
        return;
    }

    if (dmn->IsPoSeBanned()) {
        state = MASTERNODE_POSE_BANNED;
        return;
    }

    LogPrintf("%s: proTxHash=%s, proTx=%s\n", __func__, dmn->proTxHash.ToString(), dmn->ToString());

    // PIV2: Check if this is a genesis MN (registered at height 0)
    // Genesis MNs are trusted and skip IP verification to allow flexibility during testnet
    bool isGenesisMN = (dmn->pdmnState->nRegisteredHeight == 0);

    if (!isGenesisMN && info.service != dmn->pdmnState->addr) {
        state = MASTERNODE_ERROR;
        strError = strprintf("Local address %s does not match the address from ProTx (%s)",
                             info.service.ToStringIPPort(), dmn->pdmnState->addr.ToStringIPPort());
        LogPrintf("%s ERROR: %s\n", __func__, strError);
        return;
    }

    if (isGenesisMN && info.service != dmn->pdmnState->addr) {
        // Genesis MN running on different IP - update the service address for local operations
        LogPrintf("%s: Genesis MN detected, accepting local address %s (configured: %s)\n",
                  __func__, info.service.ToStringIPPort(), dmn->pdmnState->addr.ToStringIPPort());
    }

    // Check socket connectivity (skip on regtest and for genesis MNs)
    if (!Params().IsRegTestNet() && !isGenesisMN) {
        const std::string& strService = info.service.ToString();
        LogPrintf("%s: Checking inbound connection to '%s'\n", __func__, strService);
        SOCKET hSocket = CreateSocket(info.service);
        if (hSocket == INVALID_SOCKET) {
            state = MASTERNODE_ERROR;
            strError = "DMN connectivity check failed, could not create socket to DMN running at " + strService;
            LogPrintf("%s -- ERROR: %s\n", __func__, strError);
            return;
        }
        bool fConnected = ConnectSocketDirectly(info.service, hSocket, nConnectTimeout, true) && IsSelectableSocket(hSocket);
        CloseSocket(hSocket);

        if (!fConnected) {
            state = MASTERNODE_ERROR;
            strError = "DMN connectivity check failed, could not connect to DMN running at " + strService;
            LogPrintf("%s ERROR: %s\n", __func__, strError);
            return;
        }
    } else {
        LogPrintf("%s: Skipping connectivity check (regtest=%d, genesisMN=%d)\n",
                  __func__, Params().IsRegTestNet(), isGenesisMN);
    }

    info.proTxHash = dmn->proTxHash;
    g_connman->GetTierTwoConnMan()->setLocalDMN(info.proTxHash);
    state = MASTERNODE_READY;
    LogPrintf("Deterministic Masternode initialized\n");

    // Start the DMM block producer scheduler
    StartDMMScheduler();
}

void CActiveDeterministicMasternodeManager::Reset(masternode_state_t _state, const CBlockIndex* pindexTip)
{
    // Stop the scheduler before reset
    StopDMMScheduler();

    state = _state;
    SetNullProTx();
    // MN might have reappeared in same block with a new ProTx
    Init(pindexTip);
}

void CActiveDeterministicMasternodeManager::UpdatedBlockTip(const CBlockIndex* pindexNew, const CBlockIndex* pindexFork, bool fInitialDownload)
{
    LogPrintf("%s: height=%d, fInitialDownload=%d, fMasterNode=%d, state=%d\n",
              __func__, pindexNew->nHeight, fInitialDownload, fMasterNode, state);

    // Allow MN init at genesis (height 0-1) even during initial download
    bool isBootstrapPhase = (pindexNew->nHeight < 2);

    if (fInitialDownload && !isBootstrapPhase)
        return;

    if (!fMasterNode || !deterministicMNManager->IsDIP3Enforced(pindexNew->nHeight))
        return;

    if (state == MASTERNODE_READY) {
        auto newDmn = deterministicMNManager->GetListForBlock(pindexNew).GetValidMN(info.proTxHash);
        if (newDmn == nullptr) {
            // MN disappeared from MN list
            Reset(MASTERNODE_REMOVED, pindexNew);
            return;
        }

        auto oldDmn = deterministicMNManager->GetListForBlock(pindexNew->pprev).GetMN(info.proTxHash);
        if (oldDmn == nullptr) {
            // should never happen if state is MASTERNODE_READY
            LogPrintf("%s: WARNING: unable to find active mn %s in prev block list %s\n",
                      __func__, info.proTxHash.ToString(), pindexNew->pprev->GetBlockHash().ToString());
            return;
        }

        if (newDmn->pdmnState->pubKeyOperator != oldDmn->pdmnState->pubKeyOperator) {
            // MN operator key changed or revoked
            Reset(MASTERNODE_OPERATOR_KEY_CHANGED, pindexNew);
            return;
        }

        if (newDmn->pdmnState->addr != oldDmn->pdmnState->addr) {
            // MN IP changed
            Reset(MASTERNODE_PROTX_IP_CHANGED, pindexNew);
            return;
        }

        // =============================================
        // DMM Block Producer Scheduler - Try producing
        // =============================================
        // When we receive a new block tip, check if we are the designated
        // producer for the NEXT block and produce if so
        TryProducingBlock(pindexNew);

    } else {
        // MN might have (re)appeared with a new ProTx or we've found some peers
        // and figured out our local address
        Init(pindexNew);
    }
}

bool CActiveDeterministicMasternodeManager::IsValidNetAddr(const CService& addrIn)
{
    // TODO: check IPv6 and TOR addresses
    return Params().IsRegTestNet() || (addrIn.IsIPv4() && IsReachable(addrIn) && addrIn.IsRoutable());
}

// ============================================================================
// DMM Block Producer Scheduler Implementation
// ============================================================================

bool CActiveDeterministicMasternodeManager::IsLocalBlockProducer(const CBlockIndex* pindexPrev) const
{
    if (!pindexPrev) {
        return false;
    }

    // Must be ready
    if (!IsReady()) {
        return false;
    }

    // Get the MN list at this height
    CDeterministicMNList mnList = deterministicMNManager->GetListForBlock(pindexPrev);

    // Get expected producer for next block
    CDeterministicMNCPtr expectedMn;
    if (!mn_consensus::GetBlockProducer(pindexPrev, mnList, expectedMn)) {
        // No confirmed MNs yet - we can't produce
        return false;
    }

    // Check if it's us
    bool isUs = (expectedMn->proTxHash == info.proTxHash);

    if (isUs) {
        LogPrint(BCLog::MASTERNODE, "DMM-SCHEDULER: Local MN %s is producer for block %d\n",
                 info.proTxHash.ToString().substr(0, 16), pindexPrev->nHeight + 1);
    }

    return isUs;
}

bool CActiveDeterministicMasternodeManager::TryProducingBlock(const CBlockIndex* pindexPrev)
{
    if (!pindexPrev) {
        return false;
    }

    // Basic state checks
    if (!IsReady()) {
        return false;
    }

    // PIV2: Check sync state (includes bootstrap phase check)
    if (!g_tiertwo_sync_state.IsBlockchainSynced()) {
        static int64_t nLastSyncWarnTime = 0;
        int64_t nNow = GetTime();
        if (nNow - nLastSyncWarnTime > 30) {
            LogPrintf("DMM-SCHEDULER: Waiting for blockchain sync (height=%d)\n", pindexPrev->nHeight);
            nLastSyncWarnTime = nNow;
        }
        return false;
    }

    // Quorum check: previous block must have 2/3 signatures
    if (!hu::PreviousBlockHasQuorum(pindexPrev)) {
        static int64_t nLastQuorumWarnTime = 0;
        int64_t nNow = GetTime();
        if (nNow - nLastQuorumWarnTime > 30) {
            const Consensus::Params& consensus = Params().GetConsensus();
            int sigCount = hu::huSignalingManager ? hu::huSignalingManager->GetSignatureCount(pindexPrev->GetBlockHash()) : 0;
            LogPrintf("DMM-SCHEDULER: Waiting for quorum on block %d (%d/%d signatures)\n",
                      pindexPrev->nHeight, sigCount, consensus.nHuQuorumThreshold);
            nLastQuorumWarnTime = nNow;
        }
        return false;
    }

    // Rate limiting - prevent double production
    int64_t nNow = GetTime();
    int nNextHeight = pindexPrev->nHeight + 1;

    if (nLastProducedHeight.load() >= nNextHeight) {
        // Already produced for this height
        return false;
    }

    if (nNow - nLastBlockProduced.load() < DMM_BLOCK_INTERVAL_SECONDS) {
        // Too soon since last block
        return false;
    }

    // Check if we are the designated producer
    if (!IsLocalBlockProducer(pindexPrev)) {
        return false;
    }

    LogPrintf("DMM-SCHEDULER: Block producer for height %d is local MN %s - creating block...\n",
              nNextHeight, info.proTxHash.ToString().substr(0, 16));

    // Get operator key
    CKey operatorKey;
    CDeterministicMNCPtr dmn;
    auto keyResult = GetOperatorKey(operatorKey, dmn);
    if (!keyResult) {
        LogPrintf("DMM-SCHEDULER: ERROR - Failed to get operator key: %s\n", keyResult.getError());
        return false;
    }

    // Get payout script from the MN registration (already a CScript)
    CScript scriptPubKey = dmn->pdmnState->scriptPayout;

    // Create block template
    std::unique_ptr<CBlockTemplate> pblocktemplate;
    {
        LOCK(cs_main);
        pblocktemplate = BlockAssembler(Params(), false).CreateNewBlock(
            scriptPubKey,
            nullptr,    // pwallet
            true,       // fMNBlock
            nullptr,    // availableCoins
            false,      // fNoMempoolTx
            false,      // fTestValidity - we'll sign and validate ourselves
            const_cast<CBlockIndex*>(pindexPrev),
            false,      // stopOnNewBlock
            true        // fIncludeQfc
        );
    }

    if (!pblocktemplate) {
        LogPrintf("DMM-SCHEDULER: ERROR - CreateNewBlock failed\n");
        return false;
    }

    CBlock* pblock = &pblocktemplate->block;

    // Finalize merkle root (not done by CreateNewBlock when fTestValidity=false)
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);

    // Sign the block with operator key
    if (!mn_consensus::SignBlockMNOnly(*pblock, operatorKey)) {
        LogPrintf("DMM-SCHEDULER: ERROR - SignBlockMNOnly failed\n");
        return false;
    }

    LogPrintf("DMM-SCHEDULER: Block %s signed successfully (sig size: %d)\n",
              pblock->GetHash().ToString().substr(0, 16), pblock->vchBlockSig.size());

    // Submit the block
    std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(*pblock);
    bool fAccepted = ProcessNewBlock(shared_pblock, nullptr);

    if (fAccepted) {
        nLastBlockProduced.store(nNow);
        nLastProducedHeight.store(nNextHeight);
        LogPrintf("DMM-SCHEDULER: Block %s submitted and ACCEPTED at height %d\n",
                  pblock->GetHash().ToString().substr(0, 16), nNextHeight);
        return true;
    } else {
        LogPrintf("DMM-SCHEDULER: Block %s REJECTED\n", pblock->GetHash().ToString().substr(0, 16));
        return false;
    }
}

void CActiveDeterministicMasternodeManager::StartDMMScheduler()
{
    if (fDMMSchedulerRunning.load()) {
        LogPrint(BCLog::MASTERNODE, "DMM-SCHEDULER: Already running\n");
        return;
    }

    fDMMSchedulerRunning.store(true);
    LogPrintf("DMM-SCHEDULER: Starting periodic block producer thread (interval=%ds)\n",
              DMM_BLOCK_INTERVAL_SECONDS);

    dmmSchedulerThread = std::thread([this]() {
        while (fDMMSchedulerRunning.load() && !ShutdownRequested()) {
            // Sleep first to avoid immediate block production
            for (int i = 0; i < DMM_BLOCK_INTERVAL_SECONDS * 10 && fDMMSchedulerRunning.load() && !ShutdownRequested(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            if (!fDMMSchedulerRunning.load() || ShutdownRequested()) {
                break;
            }

            // Get current chain tip
            const CBlockIndex* pindexTip = nullptr;
            {
                LOCK(cs_main);
                pindexTip = chainActive.Tip();
            }

            if (pindexTip && IsReady()) {
                TryProducingBlock(pindexTip);
            }
        }
        LogPrintf("DMM-SCHEDULER: Periodic thread stopped\n");
    });
}

void CActiveDeterministicMasternodeManager::StopDMMScheduler()
{
    if (!fDMMSchedulerRunning.load()) {
        return;
    }

    LogPrintf("DMM-SCHEDULER: Stopping periodic thread...\n");
    fDMMSchedulerRunning.store(false);

    if (dmmSchedulerThread.joinable()) {
        dmmSchedulerThread.join();
    }
    LogPrintf("DMM-SCHEDULER: Stopped\n");
}


// ============================================================================
// DMN-Only Helper Functions (Legacy system removed)
// ============================================================================

bool GetActiveDMNKeys(CKey& key, CTxIn& vin)
{
    if (activeMasternodeManager == nullptr) {
        return error("%s: Active Masternode not initialized", __func__);
    }
    CDeterministicMNCPtr dmn;
    auto res = activeMasternodeManager->GetOperatorKey(key, dmn);
    if (!res) {
        return error("%s: %s", __func__, res.getError());
    }
    vin = CTxIn(dmn->collateralOutpoint);
    return true;
}

bool GetActiveMasternodeKeys(CTxIn& vin, Optional<CKey>& key, CKey& operatorKey)
{
    // PIV2: DMN-only, no legacy fallback
    if (activeMasternodeManager == nullptr) {
        return error("%s: Active Masternode not initialized (DMN required)", __func__);
    }
    key = nullopt;
    return GetActiveDMNKeys(operatorKey, vin);
}
