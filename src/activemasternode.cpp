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

    // PIV2 DMM Bootstrap: Allow MN initialization during bootstrap phase (first 10 blocks)
    // At genesis, fInitialDownload is true because there are no recent blocks.
    // Genesis MNs need to initialize and produce blocks to exit this state.
    bool isBootstrapPhase = (pindexNew->nHeight < 10);

    if (fInitialDownload && !isBootstrapPhase)
        return;

    if (fInitialDownload && isBootstrapPhase) {
        LogPrintf("%s: Bootstrap phase detected (height=%d), allowing MN initialization despite fInitialDownload=true\n",
                  __func__, pindexNew->nHeight);
    }

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

    // PIV2 DMM Bootstrap: Allow block production at genesis even if not "synced"
    // At bootstrap, IsBlockchainSynced() returns false because there are no recent blocks.
    // Genesis MNs (height 0) should be able to produce the first blocks.
    bool isBootstrap = (pindexPrev->nHeight < 10);  // First 10 blocks = bootstrap phase

    // Don't produce if initial download (unless bootstrap)
    if (!isBootstrap && !g_tiertwo_sync_state.IsBlockchainSynced()) {
        return false;
    }

    // PIV2 Fork Prevention: During bootstrap, ensure we have peers agreeing on our tip
    // before producing a block. This prevents each node from creating its own chain.
    if (isBootstrap && pindexPrev->nHeight > 0) {
        // Get number of peers that have announced at least our current height
        int nPeersWithOurTip = 0;
        int nConnectedPeers = 0;
        if (g_connman) {
            g_connman->ForEachNode([&](CNode* pnode) {
                if (pnode->fSuccessfullyConnected && !pnode->fDisconnect) {
                    nConnectedPeers++;
                    // Check if peer announced at least our current height when connecting
                    // nStartingHeight is the height the peer reported in their version message
                    if (pnode->nStartingHeight >= pindexPrev->nHeight) {
                        nPeersWithOurTip++;
                    }
                }
            });
        }

        // Require at least 1 peer at our height (for 3 MN network, we need agreement)
        // For genesis block (height 0), allow production without peers
        int nRequiredPeers = 1;
        if (nPeersWithOurTip < nRequiredPeers) {
            static int64_t nLastWarnTime = 0;
            int64_t nNow = GetTime();
            if (nNow - nLastWarnTime > 30) {  // Warn every 30 seconds
                LogPrintf("DMM-SCHEDULER: Bootstrap - waiting for peer consensus (have %d/%d peers at height %d, need %d)\n",
                          nPeersWithOurTip, nConnectedPeers, pindexPrev->nHeight, nRequiredPeers);
                nLastWarnTime = nNow;
            }
            return false;
        }
        LogPrintf("DMM-SCHEDULER: Bootstrap mode active (height=%d, %d/%d peers synced)\n",
                  pindexPrev->nHeight, nPeersWithOurTip, nConnectedPeers);
    } else if (isBootstrap) {
        LogPrintf("DMM-SCHEDULER: Bootstrap mode - genesis block production (height=%d)\n",
                  pindexPrev->nHeight);
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


/********* LEGACY *********/

OperationResult initMasternode(const std::string& _strMasterNodePrivKey, const std::string& _strMasterNodeAddr, bool isFromInit)
{
    if (!isFromInit && fMasterNode) {
        return errorOut( "ERROR: Masternode already initialized.");
    }

    LOCK(cs_main); // Lock cs_main so the node doesn't perform any action while we setup the Masternode
    LogPrintf("Initializing masternode, addr %s..\n", _strMasterNodeAddr.c_str());

    if (_strMasterNodePrivKey.empty()) {
        return errorOut("ERROR: Masternode priv key cannot be empty.");
    }

    if (_strMasterNodeAddr.empty()) {
        return errorOut("ERROR: Empty masternodeaddr");
    }

    // Address parsing.
    const CChainParams& params = Params();
    int nPort = 0;
    int nDefaultPort = params.GetDefaultPort();
    std::string strHost;
    SplitHostPort(_strMasterNodeAddr, nPort, strHost);

    // Allow for the port number to be omitted here and just double check
    // that if a port is supplied, it matches the required default port.
    if (nPort == 0) nPort = nDefaultPort;
    if (nPort != nDefaultPort && !params.IsRegTestNet()) {
        return errorOut(strprintf(_("Invalid -masternodeaddr port %d, only %d is supported on %s-net."),
                                           nPort, nDefaultPort, Params().NetworkIDString()));
    }
    CService addrTest(LookupNumeric(strHost, nPort));
    if (!addrTest.IsValid()) {
        return errorOut(strprintf(_("Invalid -masternodeaddr address: %s"), _strMasterNodeAddr));
    }

    // Peer port needs to match the masternode public one for IPv4 and IPv6.
    // Onion can run in other ports because those are behind a hidden service which has the public port fixed to the default port.
    if (nPort != GetListenPort() && !addrTest.IsTor()) {
        return errorOut(strprintf(_("Invalid -masternodeaddr port %d, isn't the same as the peer port %d"),
                                  nPort, GetListenPort()));
    }

    CKey key;
    CPubKey pubkey;
    if (!CMessageSigner::GetKeysFromSecret(_strMasterNodePrivKey, key, pubkey)) {
        return errorOut(_("Invalid masternodeprivkey. Please see the documentation."));
    }

    activeMasternode.pubKeyMasternode = pubkey;
    activeMasternode.privKeyMasternode = key;
    activeMasternode.service = addrTest;
    fMasterNode = true;

    if (g_tiertwo_sync_state.IsBlockchainSynced()) {
        // Check if the masternode already exists in the list
        CMasternode* pmn = mnodeman.Find(pubkey);
        if (pmn) activeMasternode.EnableHotColdMasterNode(pmn->vin, pmn->addr);
    }

    return {true};
}

//
// Bootup the Masternode, look for a 10000 HU collateral and register on the network
//
void CActiveMasternode::ManageStatus()
{
    if (!fMasterNode) return;
    if (activeMasternodeManager != nullptr) {
        // Deterministic masternode
        return;
    }

    // !TODO: Legacy masternodes - remove after enforcement
    LogPrint(BCLog::MASTERNODE, "CActiveMasternode::ManageStatus() - Begin\n");

    // If a DMN has been registered with same collateral, disable me.
    CMasternode* pmn = mnodeman.Find(pubKeyMasternode);
    if (pmn && deterministicMNManager->GetListAtChainTip().HasMNByCollateral(pmn->vin.prevout)) {
        LogPrintf("%s: Disabling active legacy Masternode %s as the collateral is now registered with a DMN\n",
                         __func__, pmn->vin.prevout.ToString());
        status = ACTIVE_MASTERNODE_NOT_CAPABLE;
        notCapableReason = "Collateral registered with DMN";
        return;
    }

    //need correct blocks to send ping
    if (!Params().IsRegTestNet() && !g_tiertwo_sync_state.IsBlockchainSynced()) {
        status = ACTIVE_MASTERNODE_SYNC_IN_PROCESS;
        LogPrintf("CActiveMasternode::ManageStatus() - %s\n", GetStatusMessage());
        return;
    }

    if (status == ACTIVE_MASTERNODE_SYNC_IN_PROCESS) status = ACTIVE_MASTERNODE_INITIAL;

    if (status == ACTIVE_MASTERNODE_INITIAL || (pmn && status == ACTIVE_MASTERNODE_NOT_CAPABLE)) {
        if (pmn) {
            if (pmn->protocolVersion != PROTOCOL_VERSION) {
                LogPrintf("%s: ERROR Trying to start a masternode running an old protocol version, " /* Continued */
                          "the controller and masternode wallets need to be running the latest release version.\n", __func__);
                return;
            }
            // Update vin and service
            EnableHotColdMasterNode(pmn->vin, pmn->addr);
        }
    }

    if (status != ACTIVE_MASTERNODE_STARTED) {
        // Set defaults
        status = ACTIVE_MASTERNODE_NOT_CAPABLE;
        notCapableReason = "";

        LogPrintf("%s - Checking inbound connection for masternode to '%s'\n", __func__ , service.ToString());

        CAddress addr(service, NODE_NETWORK);
        if (!g_connman->IsNodeConnected(addr)) {
            CNode* node = g_connman->ConnectNode(addr);
            if (!node) {
                notCapableReason =
                        "Masternode address:port connection availability test failed, could not open a connection to the public masternode address (" +
                        service.ToString() + ")";
                LogPrintf("%s - not capable: %s\n", __func__, notCapableReason);
            } else {
                // don't leak allocated object in memory
                delete node;
            }
            return;
        }

        notCapableReason = "Waiting for start message from controller.";
        return;
    }

    //send to all peers
    std::string errorMessage;
    if (!SendMasternodePing(errorMessage)) {
        LogPrintf("CActiveMasternode::ManageStatus() - Error on Ping: %s\n", errorMessage);
    }
}

void CActiveMasternode::ResetStatus()
{
    status = ACTIVE_MASTERNODE_INITIAL;
    ManageStatus();
}

std::string CActiveMasternode::GetStatusMessage() const
{
    switch (status) {
    case ACTIVE_MASTERNODE_INITIAL:
        return "Node just started, not yet activated";
    case ACTIVE_MASTERNODE_SYNC_IN_PROCESS:
        return "Sync in progress. Must wait until sync is complete to start Masternode";
    case ACTIVE_MASTERNODE_NOT_CAPABLE:
        return "Not capable masternode: " + notCapableReason;
    case ACTIVE_MASTERNODE_STARTED:
        return "Masternode successfully started";
    default:
        return "unknown";
    }
}

bool CActiveMasternode::SendMasternodePing(std::string& errorMessage)
{
    if (vin == nullopt) {
        errorMessage = "Active Masternode not initialized";
        return false;
    }

    if (status != ACTIVE_MASTERNODE_STARTED) {
        errorMessage = "Masternode is not in a running status";
        return false;
    }

    if (!privKeyMasternode.IsValid() || !pubKeyMasternode.IsValid()) {
        errorMessage = "Error upon masternode key.\n";
        return false;
    }

    LogPrintf("CActiveMasternode::SendMasternodePing() - Relay Masternode Ping vin = %s\n", vin->ToString());

    const uint256& nBlockHash = mnodeman.GetBlockHashToPing();
    CMasternodePing mnp(*vin, nBlockHash, GetAdjustedTime());
    if (!mnp.Sign(privKeyMasternode, pubKeyMasternode.GetID())) {
        errorMessage = "Couldn't sign Masternode Ping";
        return false;
    }

    // Update lastPing for our masternode in Masternode list
    CMasternode* pmn = mnodeman.Find(vin->prevout);
    if (pmn != nullptr) {
        if (pmn->IsPingedWithin(MasternodePingSeconds(), mnp.sigTime)) {
            errorMessage = "Too early to send Masternode Ping";
            return false;
        }

        // SetLastPing locks the masternode cs, be careful with the lock order.
        pmn->SetLastPing(mnp);
        mnodeman.mapSeenMasternodePing.emplace(mnp.GetHash(), mnp);

        //mnodeman.mapSeenMasternodeBroadcast.lastPing is probably outdated, so we'll update it
        CMasternodeBroadcast mnb(*pmn);
        uint256 hash = mnb.GetHash();
        if (mnodeman.mapSeenMasternodeBroadcast.count(hash)) {
            // SetLastPing locks the masternode cs, be careful with the lock order.
            // TODO: check why are we double setting the last ping here..
            mnodeman.mapSeenMasternodeBroadcast[hash].SetLastPing(mnp);
        }

        mnp.Relay();
        return true;

    } else {
        // Seems like we are trying to send a ping while the Masternode is not registered in the network
        errorMessage = "Masternode List doesn't include our Masternode, shutting down Masternode pinging service! " + vin->ToString();
        status = ACTIVE_MASTERNODE_NOT_CAPABLE;
        notCapableReason = errorMessage;
        return false;
    }
}

// when starting a Masternode, this can enable to run as a hot wallet with no funds
bool CActiveMasternode::EnableHotColdMasterNode(CTxIn& newVin, CService& newService)
{
    if (!fMasterNode) return false;

    status = ACTIVE_MASTERNODE_STARTED;

    //The values below are needed for signing mnping messages going forward
    vin = newVin;
    service = newService;

    LogPrintf("CActiveMasternode::EnableHotColdMasterNode() - Enabled! You may shut down the cold daemon.\n");

    return true;
}

void CActiveMasternode::GetKeys(CKey& _privKeyMasternode, CPubKey& _pubKeyMasternode) const
{
    if (!privKeyMasternode.IsValid() || !pubKeyMasternode.IsValid()) {
        throw std::runtime_error("Error trying to get masternode keys");
    }
    _privKeyMasternode = privKeyMasternode;
    _pubKeyMasternode = pubKeyMasternode;
}

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
    if (activeMasternodeManager != nullptr) {
        key = nullopt;
        return GetActiveDMNKeys(operatorKey, vin);
    }
    // Fallback path (DMN not initialized)
    if (activeMasternode.vin == nullopt) {
        return error("%s: Active Masternode not initialized", __func__);
    }
    if (activeMasternode.GetStatus() != ACTIVE_MASTERNODE_STARTED) {
        return error("%s: MN not started (%s)", __func__, activeMasternode.GetStatusMessage());
    }
    vin = *activeMasternode.vin;
    CKey sk;
    CPubKey pk;
    activeMasternode.GetKeys(sk, pk);
    key = Optional<CKey>(sk);
    operatorKey = sk;
    return true;
}
