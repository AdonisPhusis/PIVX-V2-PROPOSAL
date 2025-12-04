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
    int prevCycleStartHeight = GetHuCycleStartHeight(cycleIndex) - 1;

    // Get the block hash at end of previous cycle
    uint256 prevCycleBlockHash;

    if (prevCycleStartHeight < 0) {
        // First cycle - use genesis block hash or null
        prevCycleBlockHash = uint256();
    } else {
        // Walk back to find the block at prevCycleStartHeight
        const CBlockIndex* pindexCycleEnd = pindexPrev;
        while (pindexCycleEnd && pindexCycleEnd->nHeight > prevCycleStartHeight) {
            pindexCycleEnd = pindexCycleEnd->pprev;
        }

        if (pindexCycleEnd && pindexCycleEnd->nHeight == prevCycleStartHeight) {
            prevCycleBlockHash = pindexCycleEnd->GetBlockHash();
        } else {
            // Shouldn't happen in normal operation
            LogPrint(BCLog::HU, "HU Quorum: Could not find block at height %d for cycle %d\n",
                     prevCycleStartHeight, cycleIndex);
            prevCycleBlockHash = pindexPrev->GetBlockHash();
        }
    }

    return GetHuQuorum(mnList, cycleIndex, prevCycleBlockHash);
}

} // namespace hu
