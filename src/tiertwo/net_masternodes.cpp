// Copyright (c) 2020 The Dash developers
// Copyright (c) 2021-2022 The PIVX Core developers
// Copyright (c) 2025 The HU Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include "tiertwo/net_masternodes.h"

#include "chainparams.h"
#include "evo/deterministicmns.h"
#include "netmessagemaker.h"
#include "scheduler.h"
#include "tiertwo/masternode_meta_manager.h" // for g_mmetaman
#include "tiertwo/tiertwo_sync_state.h"

TierTwoConnMan::TierTwoConnMan(CConnman* _connman) : connman(_connman) {}
TierTwoConnMan::~TierTwoConnMan() { connman = nullptr; }

bool TierTwoConnMan::addPendingMasternode(const uint256& proTxHash)
{
    LOCK(cs_vPendingMasternodes);
    if (std::find(vPendingMasternodes.begin(), vPendingMasternodes.end(), proTxHash) != vPendingMasternodes.end()) {
        return false;
    }
    vPendingMasternodes.emplace_back(proTxHash);
    return true;
}

void TierTwoConnMan::addPendingProbeConnections(const std::set<uint256>& proTxHashes)
{
    LOCK(cs_vPendingMasternodes);
    masternodePendingProbes.insert(proTxHashes.begin(), proTxHashes.end());
}

void TierTwoConnMan::clear()
{
    LOCK(cs_vPendingMasternodes);
    vPendingMasternodes.clear();
    masternodePendingProbes.clear();
}

void TierTwoConnMan::start(CScheduler& scheduler, const TierTwoConnMan::Options& options)
{
    // Must be started after connman
    assert(connman);
    interruptNet.reset();

    // Connecting to specific addresses, no masternode connections available
    if (options.m_has_specified_outgoing) return;
    // Initiate masternode connections
    threadOpenMasternodeConnections = std::thread(&TraceThread<std::function<void()> >, "mncon", std::function<void()>(std::bind(&TierTwoConnMan::ThreadOpenMasternodeConnections, this)));
    // Cleanup process every 60 seconds
    scheduler.scheduleEvery(std::bind(&TierTwoConnMan::doMaintenance, this), 60 * 1000);
}

void TierTwoConnMan::stop() {
    if (threadOpenMasternodeConnections.joinable()) {
        threadOpenMasternodeConnections.join();
    }
}

void TierTwoConnMan::interrupt()
{
    interruptNet();
}

void TierTwoConnMan::openConnection(const CAddress& addrConnect, bool isProbe)
{
    if (interruptNet) return;
    // Note: using ip:port string connection instead of the addr to bypass the "only connect to single IPs" validation.
    std::string conn = addrConnect.ToStringIPPort();
    CAddress dummyAddr;
    connman->OpenNetworkConnection(dummyAddr, false, nullptr, conn.data(), false, false, false, true, isProbe);
}

class PeerData {
public:
    PeerData(const CService& s, bool disconnect, bool is_mn_conn) : service(s), f_disconnect(disconnect), f_is_mn_conn(is_mn_conn) {}
    const CService service;
    bool f_disconnect{false};
    bool f_is_mn_conn{false};
    bool operator==(const CService& s) const { return service == s; }
};

struct MnService {
public:
    uint256 verif_proreg_tx_hash{UINT256_ZERO};
    bool is_inbound{false};
    bool operator==(const uint256& hash) const { return verif_proreg_tx_hash == hash; }
};

void TierTwoConnMan::ThreadOpenMasternodeConnections()
{
    const auto& chainParams = Params();
    bool triedConnect = false;
    while (!interruptNet) {

        // Retry every 0.1 seconds if a connection was created, otherwise 1.5 seconds
        int sleepTime = triedConnect ? 100 : (chainParams.IsRegTestNet() ? 200 : 1500);
        if (!interruptNet.sleep_for(std::chrono::milliseconds(sleepTime))) {
            return;
        }

        triedConnect = false;

        if (!fMasterNode || !g_tiertwo_sync_state.IsBlockchainSynced() || !g_connman->GetNetworkActive()) {
            continue;
        }

        // Gather all connected peers first, so we don't
        // try to connect to an already connected peer
        std::vector<PeerData> connectedNodes;
        std::vector<MnService> connectedMnServices;
        connman->ForEachNode([&](const CNode* pnode) {
            connectedNodes.emplace_back(PeerData{pnode->addr, pnode->fDisconnect, pnode->m_masternode_connection});
            if (!pnode->verifiedProRegTxHash.IsNull()) {
                connectedMnServices.emplace_back(MnService{pnode->verifiedProRegTxHash, pnode->fInbound});
            }
        });

        // Try to connect to a single MN per cycle
        CDeterministicMNCPtr dmnToConnect{nullptr};
        // Current list
        auto mnList = deterministicMNManager->GetListAtChainTip();
        int64_t currentTime = GetAdjustedTime();
        bool isProbe = false;
        {
            LOCK(cs_vPendingMasternodes);

            // First try to connect to pending MNs
            if (!vPendingMasternodes.empty()) {
                auto dmn = mnList.GetValidMN(vPendingMasternodes.front());
                vPendingMasternodes.erase(vPendingMasternodes.begin());
                if (dmn) {
                    auto peerData = std::find(connectedNodes.begin(), connectedNodes.end(), dmn->pdmnState->addr);
                    if (peerData == std::end(connectedNodes)) {
                        dmnToConnect = dmn;
                        LogPrint(BCLog::NET_MN, "%s -- opening pending masternode connection to %s, service=%s\n",
                                 __func__, dmn->proTxHash.ToString(), dmn->pdmnState->addr.ToString());
                    }
                }
            }

            // If no node was selected, let's try to probe nodes connection
            if (!dmnToConnect) {
                std::vector<CDeterministicMNCPtr> pending;
                for (auto it = masternodePendingProbes.begin(); it != masternodePendingProbes.end(); ) {
                    auto dmn = mnList.GetMN(*it);
                    if (!dmn) {
                        it = masternodePendingProbes.erase(it);
                        continue;
                    }

                    // Discard already connected outbound MNs
                    auto mnService = std::find(connectedMnServices.begin(), connectedMnServices.end(), dmn->proTxHash);
                    bool connectedAndOutbound = mnService != std::end(connectedMnServices) && !mnService->is_inbound;
                    if (connectedAndOutbound) {
                        // we already have an outbound connection to this MN so there is no need to probe it again
                        g_mmetaman.GetMetaInfo(dmn->proTxHash)->SetLastOutboundSuccess(currentTime);
                        it = masternodePendingProbes.erase(it);
                        continue;
                    }

                    ++it;

                    int64_t lastAttempt = g_mmetaman.GetMetaInfo(dmn->proTxHash)->GetLastOutboundAttempt();
                    // back off trying connecting to an address if we already tried recently
                    if (currentTime - lastAttempt < chainParams.QuorumConnectionRetryTimeout()) {
                        continue;
                    }
                    pending.emplace_back(dmn);
                }

                // Select a random node to connect
                if (!pending.empty()) {
                    dmnToConnect = pending[GetRandInt((int)pending.size())];
                    masternodePendingProbes.erase(dmnToConnect->proTxHash);
                    isProbe = true;

                    LogPrint(BCLog::NET_MN, "CConnman::%s -- probing masternode %s, service=%s\n",
                             __func__, dmnToConnect->proTxHash.ToString(), dmnToConnect->pdmnState->addr.ToString());
                }
            }
        }

        // No DMN to connect
        if (!dmnToConnect || interruptNet) {
            continue;
        }

        // Update last attempt and try connection
        g_mmetaman.GetMetaInfo(dmnToConnect->proTxHash)->SetLastOutboundAttempt(currentTime);
        triedConnect = true;

        // Now connect
        openConnection(CAddress(dmnToConnect->pdmnState->addr, NODE_NETWORK), isProbe);
        // should be in the list now if connection was opened
        bool connected = connman->ForNode(dmnToConnect->pdmnState->addr, CConnman::AllNodes, [&](CNode* pnode) {
            if (pnode->fDisconnect) { LogPrintf("about to be disconnected\n");
                return false;
            }
            return true;
        });
        if (!connected) {
            LogPrint(BCLog::NET_MN, "TierTwoConnMan::%s -- connection failed for masternode  %s, service=%s\n",
                     __func__, dmnToConnect->proTxHash.ToString(), dmnToConnect->pdmnState->addr.ToString());
            // reset last outbound success
            g_mmetaman.GetMetaInfo(dmnToConnect->proTxHash)->SetLastOutboundSuccess(0);
        }
    }
}

static void ProcessMasternodeConnections(CConnman& connman, TierTwoConnMan& tierTwoConnMan)
{
    // Don't disconnect masternode connections when we have less than the desired amount of outbound nodes
    int nonMasternodeCount = 0;
    connman.ForEachNode([&](CNode* pnode) {
        if (!pnode->fInbound && !pnode->fFeeler && !pnode->fAddnode && !pnode->m_masternode_connection && !pnode->m_masternode_probe_connection) {
            nonMasternodeCount++;
        }
    });
    if (nonMasternodeCount < (int) connman.GetMaxOutboundNodeCount()) {
        return;
    }

    connman.ForEachNode([&](CNode* pnode) {
        // we're only disconnecting m_masternode_connection connections
        if (!pnode->m_masternode_connection) return;
        // we're only disconnecting outbound connections (inbound connections are disconnected in AcceptConnection())
        if (pnode->fInbound) return;
        // we're not disconnecting masternode probes for at least a few seconds
        if (pnode->m_masternode_probe_connection && GetSystemTimeInSeconds() - pnode->nTimeConnected < 5) return;

        if (fLogIPs) {
            LogPrintf("Closing Masternode connection: peer=%d, addr=%s\n", pnode->GetId(), pnode->addr.ToString());
        } else {
            LogPrintf("Closing Masternode connection: peer=%d\n", pnode->GetId());
        }
        pnode->fDisconnect = true;
    });
}

void TierTwoConnMan::doMaintenance()
{
    if(!g_tiertwo_sync_state.IsBlockchainSynced() || interruptNet) {
        return;
    }
    ProcessMasternodeConnections(*connman, *this);
}
