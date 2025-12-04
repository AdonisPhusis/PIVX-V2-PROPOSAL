// Copyright (c) 2020 The Dash developers
// Copyright (c) 2021-2022 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#ifndef HU_TIERTWO_NET_MASTERNODES_H
#define HU_TIERTWO_NET_MASTERNODES_H

#include "net.h"
#include "sync.h"
#include "threadinterrupt.h"
#include "uint256.h"

#include <thread>

class CAddress;
class CConnman;
class CChainParams;
class CNode;
class CScheduler;

class TierTwoConnMan
{
public:
    struct Options {
        bool m_has_specified_outgoing;
    };

    explicit TierTwoConnMan(CConnman* _connman);
    ~TierTwoConnMan();

    // Add DMN to the pending connection list
    bool addPendingMasternode(const uint256& proTxHash);

    // Adds the DMNs to the pending to probe list
    void addPendingProbeConnections(const std::set<uint256>& proTxHashes);

    // Set the local DMN so the node does not try to connect to himself
    void setLocalDMN(const uint256& pro_tx_hash) { WITH_LOCK(cs_vPendingMasternodes, local_dmn_pro_tx_hash = pro_tx_hash;); }

    // Clear connections cache
    void clear();

    // Manages the MN connections
    void ThreadOpenMasternodeConnections();
    void start(CScheduler& scheduler, const TierTwoConnMan::Options& options);
    void stop();
    void interrupt();

private:
    CThreadInterrupt interruptNet;
    std::thread threadOpenMasternodeConnections;

    mutable RecursiveMutex cs_vPendingMasternodes;
    std::vector<uint256> vPendingMasternodes GUARDED_BY(cs_vPendingMasternodes);
    std::set<uint256> masternodePendingProbes GUARDED_BY(cs_vPendingMasternodes);

    // The local DMN
    Optional<uint256> local_dmn_pro_tx_hash GUARDED_BY(cs_vPendingMasternodes){nullopt};

    // parent connections manager
    CConnman* connman;

    void openConnection(const CAddress& addrConnect, bool isProbe);
    void doMaintenance();
};

#endif // HU_TIERTWO_NET_MASTERNODES_H
