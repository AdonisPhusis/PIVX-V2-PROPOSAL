// Copyright (c) 2021 The PIVX Core developers
// Copyright (c) 2025 The PIV2 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#ifndef HU_TIERTWO_TIERTWO_SYNC_STATE_H
#define HU_TIERTWO_TIERTWO_SYNC_STATE_H

#include <atomic>

class uint256;  // Forward declaration for legacy functions

// PIV2: Simplified sync states (legacy phases kept for compatibility)
#define MASTERNODE_SYNC_INITIAL 0
#define MASTERNODE_SYNC_SPORKS 1
#define MASTERNODE_SYNC_LIST 2      // Legacy, unused
#define MASTERNODE_SYNC_MNW 3       // Legacy, unused
#define MASTERNODE_SYNC_FINISHED 999

// PIV2: Sync is based on receiving finalized blocks (HU quorum)
// Bootstrap phase: first 10 blocks are always considered synced
#define PIV2_BOOTSTRAP_BLOCKS 10
// Sync timeout: consider synced if we received a finalized block in last 60 seconds
#define PIV2_SYNC_TIMEOUT 60

class TierTwoSyncState {
public:
    // PIV2: Synced if we received a finalized block recently OR in bootstrap phase
    bool IsBlockchainSynced() const;
    bool IsSynced() const { return IsBlockchainSynced(); }
    bool IsSporkListSynced() const { return true; }  // PIV2: Always true
    bool IsMasternodeListSynced() const { return true; }  // PIV2: DMN list from genesis

    // PIV2: Called when a finalized block is received (has HU quorum)
    void OnFinalizedBlock(int height, int64_t timestamp);

    // PIV2: Set current chain height (called from validation)
    void SetChainHeight(int height) { m_chain_height.store(height); }
    int GetChainHeight() const { return m_chain_height.load(); }

    // Legacy compatibility (no-ops for removed functions)
    void SetBlockchainSync(bool f, int64_t cur_time) { /* PIV2: No-op */ }
    void SetCurrentSyncPhase(int sync_phase) { /* PIV2: No-op */ }
    int GetSyncPhase() const { return IsSynced() ? MASTERNODE_SYNC_FINISHED : MASTERNODE_SYNC_INITIAL; }
    void ResetData() { m_last_finalized_time.store(0); m_last_finalized_height.store(0); }

    // Legacy sync tracking functions (no-ops, kept for compatibility)
    void AddedMasternodeList(const uint256& hash) { /* PIV2: No-op */ }
    void AddedMasternodeWinner(const uint256& hash) { /* PIV2: No-op */ }
    void EraseSeenMNB(const uint256& hash) { /* PIV2: No-op */ }
    void EraseSeenMNW(const uint256& hash) { /* PIV2: No-op */ }

private:
    std::atomic<int> m_chain_height{0};
    std::atomic<int> m_last_finalized_height{0};
    std::atomic<int64_t> m_last_finalized_time{0};
};

extern TierTwoSyncState g_tiertwo_sync_state;

#endif // HU_TIERTWO_TIERTWO_SYNC_STATE_H
