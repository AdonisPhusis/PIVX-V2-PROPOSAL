// Copyright (c) 2025 The PIVHU Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "piv2/piv2_quorum.h"

#include "arith_uint256.h"
#include "chain.h"
#include "chainparams.h"
#include "hash.h"
#include "logging.h"

#include <algorithm>

namespace hu {

uint256 ComputeHuQuorumSeed(const uint256& prevCycleBlockHash, int cycleIndex)
{
    // seed = SHA256(prevCycleBlockHash || cycleIndex || "HU_QUORUM")
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << prevCycleBlockHash;
    ss << cycleIndex;
    ss << std::string("HU_QUORUM");
    return ss.GetHash();
}

uint256 ComputeHuQuorumMemberScore(const uint256& seed, const uint256& proTxHash)
{
    // score = SHA256(seed || proTxHash)
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << seed;
    ss << proTxHash;
    return ss.GetHash();
}

std::vector<CDeterministicMNCPtr> GetHuQuorum(
    const CDeterministicMNList& mnList,
    int cycleIndex,
    const uint256& prevCycleBlockHash)
{
    std::vector<CDeterministicMNCPtr> result;

    // Compute seed for this cycle
    uint256 seed = ComputeHuQuorumSeed(prevCycleBlockHash, cycleIndex);

    // Collect all valid, confirmed MNs with their scores
    // Using arith_uint256 for comparison operators
    std::vector<std::pair<arith_uint256, CDeterministicMNCPtr>> scoredMns;

    mnList.ForEachMN(true /* onlyValid */, [&](const CDeterministicMNCPtr& dmn) {
        // Skip unconfirmed MNs
        if (dmn->pdmnState->confirmedHash.IsNull()) {
            return;
        }

        uint256 scoreHash = ComputeHuQuorumMemberScore(seed, dmn->proTxHash);
        arith_uint256 score = UintToArith256(scoreHash);
        scoredMns.emplace_back(score, dmn);
    });

    if (scoredMns.empty()) {
        LogPrint(BCLog::HU, "HU Quorum: No valid MNs for cycle %d\n", cycleIndex);
        return result;
    }

    // Sort by score (descending)
    std::sort(scoredMns.begin(), scoredMns.end(),
        [](const auto& a, const auto& b) {
            if (a.first == b.first) {
                // Tie-breaker: proTxHash lexicographically
                return a.second->proTxHash < b.second->proTxHash;
            }
            return a.first > b.first;
        });

    // Take top nHuQuorumSize MNs (from consensus params)
    const Consensus::Params& consensus = Params().GetConsensus();
    size_t quorumSize = std::min(static_cast<size_t>(consensus.nHuQuorumSize), scoredMns.size());
    result.reserve(quorumSize);

    for (size_t i = 0; i < quorumSize; i++) {
        result.push_back(scoredMns[i].second);
    }

    LogPrint(BCLog::HU, "HU Quorum: Selected %zu MNs for cycle %d (seed: %s)\n",
             result.size(), cycleIndex, seed.ToString().substr(0, 16));

    return result;
}

bool IsInHuQuorum(
    const CDeterministicMNList& mnList,
    int cycleIndex,
    const uint256& prevCycleBlockHash,
    const uint256& proTxHash)
{
    auto quorum = GetHuQuorum(mnList, cycleIndex, prevCycleBlockHash);

    for (const auto& mn : quorum) {
        if (mn->proTxHash == proTxHash) {
            return true;
        }
    }

    return false;
}

std::vector<CDeterministicMNCPtr> GetHuQuorumForHeight(
    const CBlockIndex* pindexPrev,
    int nHeight,
    const CDeterministicMNList& mnList)
{
    if (!pindexPrev) {
        return {};
    }

    int cycleIndex = GetHuCycleIndex(nHeight);

    // BLUEPRINT REQUIREMENT:
    // Use lastFinalizedBlockHash for quorum seed to prevent manipulation
    // A finalized block cannot be reverted (BFT guarantee)
    uint256 seedBlockHash;

    if (cycleIndex == 0) {
        // Bootstrap: First cycle (blocks 0-11) - use null/genesis
        seedBlockHash = uint256();
        LogPrint(BCLog::HU, "HU Quorum: Bootstrap cycle 0, using null seed\n");
    } else {
        // Find the last finalized block hash
        // Walk back through finality data to find the most recent finalized block
        if (huFinalityHandler) {
            const Consensus::Params& consensus = Params().GetConsensus();
            bool foundFinalized = false;

            // Search for finalized block, starting from previous cycle
            int searchStart = GetHuCycleStartHeight(cycleIndex) - 1;
            const CBlockIndex* pindex = pindexPrev;

            // Walk back to search start
            while (pindex && pindex->nHeight > searchStart) {
                pindex = pindex->pprev;
            }

            // Now search backwards for a finalized block
            while (pindex && pindex->nHeight >= 0) {
                CHuFinality finality;
                if (huFinalityHandler->GetFinality(pindex->GetBlockHash(), finality)) {
                    if (finality.HasFinality(consensus.nHuQuorumThreshold)) {
                        seedBlockHash = pindex->GetBlockHash();
                        foundFinalized = true;
                        LogPrint(BCLog::HU, "HU Quorum: Using lastFinalizedBlockHash %s at height %d for cycle %d\n",
                                 seedBlockHash.ToString().substr(0, 16), pindex->nHeight, cycleIndex);
                        break;
                    }
                }
                pindex = pindex->pprev;
            }

            if (!foundFinalized) {
                // No finalized block found yet (early in chain)
                // Fallback to previous cycle end block (less secure but necessary for bootstrap)
                int prevCycleStartHeight = GetHuCycleStartHeight(cycleIndex) - 1;
                pindex = pindexPrev;
                while (pindex && pindex->nHeight > prevCycleStartHeight) {
                    pindex = pindex->pprev;
                }
                if (pindex && pindex->nHeight == prevCycleStartHeight) {
                    seedBlockHash = pindex->GetBlockHash();
                } else {
                    seedBlockHash = pindexPrev->GetBlockHash();
                }
                LogPrint(BCLog::HU, "HU Quorum: No finalized block found, using fallback hash for cycle %d\n",
                         cycleIndex);
            }
        } else {
            // Finality handler not initialized (early startup)
            // Use previous cycle block hash as fallback
            int prevCycleStartHeight = GetHuCycleStartHeight(cycleIndex) - 1;
            const CBlockIndex* pindex = pindexPrev;
            while (pindex && pindex->nHeight > prevCycleStartHeight) {
                pindex = pindex->pprev;
            }
            if (pindex && pindex->nHeight == prevCycleStartHeight) {
                seedBlockHash = pindex->GetBlockHash();
            } else {
                seedBlockHash = pindexPrev->GetBlockHash();
            }
            LogPrint(BCLog::HU, "HU Quorum: Finality handler not ready, using prev cycle hash for cycle %d\n",
                     cycleIndex);
        }
    }

    return GetHuQuorum(mnList, cycleIndex, seedBlockHash);
}

} // namespace hu
