// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef HU_CONSENSUS_PARAMS_H
#define HU_CONSENSUS_PARAMS_H

#include "amount.h"
#include "optional.h"
#include "uint256.h"
#include <map>
#include <string>
#include <vector>

namespace Consensus {

/**
 * Genesis Masternode entry for DMN bootstrap.
 * These MNs are injected into the DMN list at block 0 to enable DMM block production.
 *
 * Like ETH2/Cosmos, genesis MNs are defined in the initial state, not via transactions.
 * - No IP address: MNs announce their service address via P2P after launch
 * - No ProRegTx needed: their legitimacy comes from being in the genesis state
 * - Collateral is created at block 1 (premine) to their owner addresses
 */
struct GenesisMN {
    std::string ownerAddress;        // Owner address (receives 10k collateral at block 1)
    std::string operatorPubKey;      // Operator pubkey (hex, 33 bytes compressed ECDSA) - signs blocks
    std::string payoutAddress;       // Payout address (receives MN rewards)
    // Note: votingKey defaults to owner, IP announced via P2P
};

/**
* Index into Params.vUpgrades and NetworkUpgradeInfo
*
* Being array indices, these MUST be numbered consecutively.
*
* The order of these indices MUST match the order of the upgrades on-chain, as
* several functions depend on the enum being sorted.
*/
enum UpgradeIndex : uint32_t {
    BASE_NETWORK,
    UPGRADE_BIP65,
    UPGRADE_V3_4,
    UPGRADE_V4_0,
    UPGRADE_V5_0,
    UPGRADE_V5_2,
    UPGRADE_V5_3,
    UPGRADE_V5_5,
    UPGRADE_V5_6,
    UPGRADE_V6_0,
    UPGRADE_TESTDUMMY,
    // NOTE: Also add new upgrades to NetworkUpgradeInfo in upgrades.cpp
    MAX_NETWORK_UPGRADES
};

struct NetworkUpgrade {
    /**
     * The first protocol version which will understand the new consensus rules
     */
    int nProtocolVersion;

    /**
     * Height of the first block for which the new consensus rules will be active
     */
    int nActivationHeight;

    /**
     * Special value for nActivationHeight indicating that the upgrade is always active.
     * This is useful for testing, as it means tests don't need to deal with the activation
     * process (namely, faking a chain of somewhat-arbitrary length).
     *
     * New blockchains that want to enable upgrade rules from the beginning can also use
     * this value. However, additional care must be taken to ensure the genesis block
     * satisfies the enabled rules.
     */
    static constexpr int ALWAYS_ACTIVE = 0;

    /**
     * Special value for nActivationHeight indicating that the upgrade will never activate.
     * This is useful when adding upgrade code that has a testnet activation height, but
     * should remain disabled on mainnet.
     */
    static constexpr int NO_ACTIVATION_HEIGHT = -1;

    /**
     * The hash of the block at height nActivationHeight, if known. This is set manually
     * after a network upgrade activates.
     *
     * We use this in IsInitialBlockDownload to detect whether we are potentially being
     * fed a fake alternate chain. We use NU activation blocks for this purpose instead of
     * the checkpoint blocks, because network upgrades (should) have significantly more
     * scrutiny than regular releases. nMinimumChainWork MUST be set to at least the chain
     * work of this block, otherwise this detection will have false positives.
     */
    Optional<uint256> hashActivationBlock;
};

/**
 * Parameters that influence chain consensus.
 */
struct Params {
    uint256 hashGenesisBlock;
    // HU: Genesis coinbase maturity (minimal, since block reward = 0)
    // Only affects genesis outputs, no new coinbase after genesis
    static constexpr int HU_COINBASE_MATURITY = 10;
    CAmount nMaxMoneyOut;
    // HU: Masternode collateral amount (network-specific)
    CAmount nMNCollateralAmt;
    // HU: Block reward = 0 (all inflation via R% yield on ZKHU)
    CAmount nMNBlockReward;
    CAmount nNewMNBlockReward;

    // KHU V6: Block reward = 0 immediately at V6 activation
    // Economy is governed by R% yield (40%→7% over 33 years) and T (treasury)
    // BLOCKS_PER_YEAR is used for R_MAX_dynamic calculation: max(700, 4000 - year*100)
    static constexpr int BLOCKS_PER_YEAR = 525600;  // 365 days * 1440 blocks/day

    // KHU DAO Treasury (Phase 6 - Automatic budget)
    // Budget = (U × R_annual) / 10000 / T_DIVISOR / 365 every 1440 blocks (daily)
    // With T_DIVISOR=8 and R=40%: ~5% annual (scales with R% over 33 years)
    // Runs in PARALLEL with block reward DAO (years 0-6)
    // CONTINUES after block reward ends (year 6+)
    std::string strDaoTreasuryAddress;

    int64_t nTargetTimespan;
    int64_t nTargetTimespanV2;
    int64_t nTargetSpacing;
    int nTimeSlotLength;

    // ═══════════════════════════════════════════════════════════════════════
    // KHU Timing Parameters (network-specific)
    // ═══════════════════════════════════════════════════════════════════════

    // ZKHU Staking maturity (before yield accrues)
    // Mainnet: 4320 blocks (3 days) | Testnet: 60 blocks (~1h) | Regtest: 10 blocks
    int nZKHUMaturityBlocks;

    // DOMC (Democratic Oversight of Monetary Committee) cycle
    // Mainnet: 129600 blocks (~90 days) | Testnet: 10080 blocks (~1 week) | Regtest: 100 blocks
    int nDOMCCycleBlocks;
    int nDOMCCommitOffset;      // Offset from cycle start for commit phase
    int nDOMCRevealOffset;      // Offset from cycle start for reveal phase
    int nDOMCPhaseDuration;     // Duration of commit/reveal phases

    // DAO Treasury proposal cycle
    // Mainnet: 43200 blocks (~30 days) | Testnet: 1440 blocks (~1 day) | Regtest: 30 blocks
    int nDAOCycleBlocks;
    int nDAOSubmitWindow;       // Proposal submission window
    int nDAOStudyWindow;        // Community study window
    int nDAOVoteWindow;         // Masternode voting window

    // R% yield parameters
    // R_annual initial = 4000 (40%), floor = 700 (7%), decay = 100/year
    int nRInitial;              // Initial R% in basis points (4000 = 40%)
    int nRFloor;                // Minimum R% in basis points (700 = 7%)
    int nRDecayPerYear;         // Annual decay in basis points (100 = 1%)

    // Daily yield/treasury update interval
    int nBlocksPerDay;          // Blocks per day (1440 for 1-min blocks)

    // ═══════════════════════════════════════════════════════════════════════
    // HU DMM + Finality Parameters (network-specific)
    // ═══════════════════════════════════════════════════════════════════════

    // Block timing (informational, for timeout calculations)
    int nHuBlockTimeSeconds;        // Target block time (60s mainnet)

    // Quorum configuration
    int nHuQuorumSize;              // Number of MNs in HU quorum (12 mainnet)
    int nHuQuorumThreshold;         // Minimum signatures for finality (8 mainnet)
    int nHuQuorumRotationBlocks;    // Quorum rotation interval (12 mainnet)

    // DMM leader timeout
    int nHuLeaderTimeoutSeconds;    // Timeout before fallback to next MN (45s mainnet)
    int nHuFallbackRecoverySeconds; // Recovery window for fallback MNs (15s testnet/mainnet)

    // DMM Bootstrap phase - special rules for cold start
    // During bootstrap (height <= nDMMBootstrapHeight):
    // - Producer = always primary (scores[0]), no fallback slot calculation
    // - nTime = max(prevTime + 1, nNow) instead of slot-aligned time
    // This prevents timestamp issues when syncing a fresh chain from genesis
    int nDMMBootstrapHeight;        // Bootstrap phase height (5 testnet, 10 mainnet)

    // Reorg protection
    int nHuMaxReorgDepth;           // Max reorg depth before finality (12 mainnet)

    // spork keys
    std::string strSporkPubKey;
    std::string strSporkPubKeyOld;
    int64_t nTime_EnforceNewSporkKey;
    int64_t nTime_RejectOldSporkKey;

    // height-based activations
    int height_last_invalid_UTXO;

    // validation by-pass
    int64_t nPivxBadBlockTime;
    unsigned int nPivxBadBlockBits;

    // Map with network updates
    NetworkUpgrade vUpgrades[MAX_NETWORK_UPGRADES];

    // DMN Genesis bootstrap - MNs to inject at block 0 for DMM to work
    std::vector<GenesisMN> genesisMNs;

    int64_t TargetTimespan(const bool fV2 = true) const { return fV2 ? nTargetTimespanV2 : nTargetTimespan; }
    bool MoneyRange(const CAmount& nValue) const { return (nValue >= 0 && nValue <= nMaxMoneyOut); }
    bool IsTimeProtocolV2(const int nHeight) const { return NetworkUpgradeActive(nHeight, UPGRADE_V4_0); }

    // ═══════════════════════════════════════════════════════════════════════════
    // PIV2 Masternode Collateral Maturity
    // ═══════════════════════════════════════════════════════════════════════════
    // Prevents rapid MN registration/deregistration attacks on quorum
    // Values are set per-network in chainparams.cpp
    // ═══════════════════════════════════════════════════════════════════════════
    int nMasternodeCollateralMinConf{1};  // Default, overridden per network

    int MasternodeCollateralMinConf() const { return nMasternodeCollateralMinConf; }

    int FutureBlockTimeDrift(const int nHeight) const
    {
        // HU: TimeV2 always active (14 seconds)
        if (IsTimeProtocolV2(nHeight)) return nTimeSlotLength - 1;
        // Fallback (shouldn't be reached in HU genesis chain)
        return nTimeSlotLength - 1;
    }

    bool IsValidBlockTimeStamp(const int64_t nTime, const int nHeight) const
    {
        // Before time protocol V2, blocks can have arbitrary timestamps
        if (!IsTimeProtocolV2(nHeight)) return true;
        // Time protocol v2 requires time in slots
        return (nTime % nTimeSlotLength) == 0;
    }

    /**
     * Returns true if the given network upgrade is active as of the given block
     * height. Caller must check that the height is >= 0 (and handle unknown
     * heights).
     */
    bool NetworkUpgradeActive(int nHeight, Consensus::UpgradeIndex idx) const;
};
} // namespace Consensus

#endif // HU_CONSENSUS_PARAMS_H
