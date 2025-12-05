// Copyright (c) 2014-2021 The Dash Core developers
// Copyright (c) 2015-2022 The PIVX Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef HU_ACTIVEMASTERNODE_H
#define HU_ACTIVEMASTERNODE_H

#include "key.h"
#include "evo/deterministicmns.h"
#include "operationresult.h"
#include "sync.h"
#include "validationinterface.h"

#include <atomic>
#include <chrono>
#include <thread>

class CActiveDeterministicMasternodeManager;

#define ACTIVE_MASTERNODE_INITIAL 0 // initial state
#define ACTIVE_MASTERNODE_SYNC_IN_PROCESS 1
#define ACTIVE_MASTERNODE_NOT_CAPABLE 3
#define ACTIVE_MASTERNODE_STARTED 4

extern CActiveDeterministicMasternodeManager* activeMasternodeManager;

struct CActiveMasternodeInfo
{
    // ECDSA keys for the active Masternode
    CPubKey pubKeyOperator;
    CKey keyOperator;
    // Initialized while registering Masternode
    uint256 proTxHash{UINT256_ZERO};
    CService service;
};

class CActiveDeterministicMasternodeManager : public CValidationInterface
{
public:
    enum masternode_state_t {
        MASTERNODE_WAITING_FOR_PROTX,
        MASTERNODE_POSE_BANNED,
        MASTERNODE_REMOVED,
        MASTERNODE_OPERATOR_KEY_CHANGED,
        MASTERNODE_PROTX_IP_CHANGED,
        MASTERNODE_READY,
        MASTERNODE_ERROR,
    };

private:
    masternode_state_t state{MASTERNODE_WAITING_FOR_PROTX};
    std::string strError;
    CActiveMasternodeInfo info;

    // DMM Block Producer Scheduler
    std::atomic<int64_t> nLastBlockProduced{0};
    std::atomic<int> nLastProducedHeight{0};
    std::atomic<bool> fDMMSchedulerRunning{false};
    std::thread dmmSchedulerThread;
    static constexpr int DMM_BLOCK_INTERVAL_SECONDS = 60;  // Block interval (matches nTargetSpacing)
    static constexpr int DMM_MISSED_BLOCK_TIMEOUT = 90;   // Seconds before considering a missed block (1.5x interval)

public:
    ~CActiveDeterministicMasternodeManager() override { StopDMMScheduler(); }
    void UpdatedBlockTip(const CBlockIndex* pindexNew, const CBlockIndex* pindexFork, bool fInitialDownload) override;

    void Init(const CBlockIndex* pindexTip);
    void Reset(masternode_state_t _state, const CBlockIndex* pindexTip);
    // Sets the Deterministic Masternode Operator's private/public key
    OperationResult SetOperatorKey(const std::string& strMNOperatorPrivKey);
    // PIVHU: If the active masternode is ready, and the keyID matches with the registered one,
    // return private key, keyID, and pointer to dmn.
    OperationResult GetOperatorKey(CKey& key, CDeterministicMNCPtr& dmn) const;
    // Directly return the operator secret key saved in the manager, without performing any validation
    const CKey* OperatorKey() const { return &info.keyOperator; }
    void SetNullProTx() { info.proTxHash = UINT256_ZERO; }
    const uint256 GetProTx() const { return info.proTxHash; }

    const CActiveMasternodeInfo* GetInfo() const { return &info; }
    masternode_state_t GetState() const { return state; }
    std::string GetStatus() const;
    bool IsReady() const { return state == MASTERNODE_READY; }

    static bool IsValidNetAddr(const CService& addrIn);

    // ========================================
    // DMM Block Producer Scheduler (HU v1)
    // ========================================

    /**
     * Try to produce a block if this MN is the designated producer.
     * Called from UpdatedBlockTip when a new block is received,
     * or periodically via the scheduler thread.
     *
     * @param pindexPrev The previous block index (tip)
     * @return true if block was produced and broadcast
     */
    bool TryProducingBlock(const CBlockIndex* pindexPrev);

    /**
     * Check if local MN is the designated block producer for the next block.
     * @param pindexPrev Previous block
     * @return true if we should produce the next block
     */
    bool IsLocalBlockProducer(const CBlockIndex* pindexPrev) const;

    /**
     * Start the periodic DMM scheduler thread.
     * Called when MN transitions to READY state.
     */
    void StartDMMScheduler();

    /**
     * Stop the DMM scheduler thread.
     * Called on shutdown or when MN is no longer ready.
     */
    void StopDMMScheduler();
};

// Responsible for initializing the masternode
OperationResult initMasternode(const std::string& strMasterNodePrivKey, const std::string& strMasterNodeAddr, bool isFromInit);


// Responsible for activating the Masternode and pinging the network (legacy MN list)
class CActiveMasternode
{
private:
    int status{ACTIVE_MASTERNODE_INITIAL};
    std::string notCapableReason;

public:
    CActiveMasternode() = default;

    // Initialized by init.cpp
    // Keys for the main Masternode
    CPubKey pubKeyMasternode;
    CKey privKeyMasternode;

    // Initialized while registering Masternode
    Optional<CTxIn> vin{nullopt};
    CService service;

    /// Manage status of main Masternode
    void ManageStatus();
    void ResetStatus();
    std::string GetStatusMessage() const;
    int GetStatus() const { return status; }

    /// Ping Masternode
    bool SendMasternodePing(std::string& errorMessage);
    /// Enable cold wallet mode (run a Masternode with no funds)
    bool EnableHotColdMasterNode(CTxIn& vin, CService& addr);

    void GetKeys(CKey& privKeyMasternode, CPubKey& pubKeyMasternode) const;
};

// PIVHU: Compatibility code - get vin and keys for deterministic masternode (ECDSA only)
bool GetActiveMasternodeKeys(CTxIn& vin, Optional<CKey>& key, CKey& ecdsaKey);
// PIVHU: Get active masternode ECDSA operator keys for DMN
bool GetActiveDMNKeys(CKey& key, CTxIn& vin);

#endif // HU_ACTIVEMASTERNODE_H
