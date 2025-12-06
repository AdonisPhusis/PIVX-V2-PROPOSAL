// Copyright (c) 2014-2021 The Dash Core developers
// Copyright (c) 2015-2022 The PIVX Core developers
// Copyright (c) 2025 The PIV2 Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PIV2_ACTIVEMASTERNODE_H
#define PIV2_ACTIVEMASTERNODE_H

#include "key.h"
#include "evo/deterministicmns.h"
#include "operationresult.h"
#include "sync.h"
#include "validationinterface.h"

#include <atomic>
#include <thread>

class CActiveDeterministicMasternodeManager;

extern CActiveDeterministicMasternodeManager* activeMasternodeManager;

struct CActiveMasternodeInfo
{
    CPubKey pubKeyOperator;
    CKey keyOperator;
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

    std::atomic<int64_t> nLastBlockProduced{0};
    std::atomic<int> nLastProducedHeight{0};
    std::atomic<bool> fDMMSchedulerRunning{false};
    std::thread dmmSchedulerThread;
    static constexpr int DMM_BLOCK_INTERVAL_SECONDS = 60;    // Minimum time between blocks we produce
    static constexpr int DMM_CHECK_INTERVAL_SECONDS = 5;     // How often to check if we should produce
    static constexpr int DMM_MISSED_BLOCK_TIMEOUT = 90;

public:
    ~CActiveDeterministicMasternodeManager() override { StopDMMScheduler(); }
    void UpdatedBlockTip(const CBlockIndex* pindexNew, const CBlockIndex* pindexFork, bool fInitialDownload) override;

    void Init(const CBlockIndex* pindexTip);
    void Reset(masternode_state_t _state, const CBlockIndex* pindexTip);
    OperationResult SetOperatorKey(const std::string& strMNOperatorPrivKey);
    OperationResult GetOperatorKey(CKey& key, CDeterministicMNCPtr& dmn) const;
    const CKey* OperatorKey() const { return &info.keyOperator; }
    void SetNullProTx() { info.proTxHash = UINT256_ZERO; }
    const uint256 GetProTx() const { return info.proTxHash; }

    const CActiveMasternodeInfo* GetInfo() const { return &info; }
    masternode_state_t GetState() const { return state; }
    std::string GetStatus() const;
    bool IsReady() const { return state == MASTERNODE_READY; }

    static bool IsValidNetAddr(const CService& addrIn);

    bool TryProducingBlock(const CBlockIndex* pindexPrev);

    /**
     * Check if local MN is the designated block producer.
     *
     * @param pindexPrev       Previous block index
     * @param outAlignedTime   [out] The aligned block timestamp to use if producing
     * @return                 true if local MN should produce the next block
     */
    bool IsLocalBlockProducer(const CBlockIndex* pindexPrev, int64_t& outAlignedTime) const;

    void StartDMMScheduler();
    void StopDMMScheduler();
};

bool GetActiveMasternodeKeys(CTxIn& vin, Optional<CKey>& key, CKey& operatorKey);
bool GetActiveDMNKeys(CKey& key, CTxIn& vin);

#endif // PIV2_ACTIVEMASTERNODE_H
