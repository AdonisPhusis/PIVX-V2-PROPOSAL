// Copyright (c) 2025 The PIVHU Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PIVHU_HU_QUORUM_H
#define PIVHU_HU_QUORUM_H

#include "evo/deterministicmns.h"
#include "piv2/piv2_finality.h"
#include "uint256.h"

#include <vector>

class CBlockIndex;

namespace hu {

/**
 * HU Quorum System - Deterministic MN selection for block finality
 *
 * Each cycle of HU_CYCLE_LENGTH blocks (12) has a fixed quorum of HU_QUORUM_SIZE (12) MNs.
 *
 * BLUEPRINT REQUIREMENT (CRITICAL - BFT Security):
 * Selection is deterministic based on:
 *   seed = Hash(lastFinalizedBlockHash || cycleIndex || "HU_QUORUM")
 *
 * Using lastFinalizedBlockHash (not prevCycleBlockHash) prevents adversaries from
 * manipulating quorum selection, since finalized blocks cannot be reverted (BFT guarantee).
 *
 * Bootstrap Exception: Cycle 0 (blocks 0-11) uses null/genesis hash since no
 * finalized blocks exist yet.
 *
 * The quorum members sign blocks in their cycle. When HU_FINALITY_THRESHOLD (8)
 * signatures are collected, the block is considered final.
 */

/**
 * Get the cycle index for a given block height
 * @param nHeight Block height
 * @param nCycleLength From consensus.nHuQuorumRotationBlocks (default: 12)
 * @return Cycle index (nHeight / nCycleLength)
 */
inline int GetHuCycleIndex(int nHeight, int nCycleLength = HU_CYCLE_LENGTH_DEFAULT)
{
    return nHeight / nCycleLength;
}

/**
 * Get the first block height of a cycle
 * @param cycleIndex Cycle index
 * @param nCycleLength From consensus.nHuQuorumRotationBlocks (default: 12)
 * @return First block height in the cycle
 */
inline int GetHuCycleStartHeight(int cycleIndex, int nCycleLength = HU_CYCLE_LENGTH_DEFAULT)
{
    return cycleIndex * nCycleLength;
}

/**
 * Compute the seed for quorum selection
 * @param seedBlockHash Hash to use for seed (should be lastFinalizedBlockHash per blueprint)
 * @param cycleIndex Current cycle index
 * @return Deterministic seed for MN selection
 *
 * Note: Per BLUEPRINT requirement, the caller should pass lastFinalizedBlockHash
 * for BFT security. See GetHuQuorumForHeight() for the proper implementation.
 */
uint256 ComputeHuQuorumSeed(const uint256& seedBlockHash, int cycleIndex);

/**
 * Select the HU quorum for a given cycle
 *
 * @param mnList Deterministic MN list at the cycle start
 * @param cycleIndex Cycle index
 * @param prevCycleBlockHash Hash of last block in previous cycle
 * @return Vector of HU_QUORUM_SIZE MNs (or fewer if not enough valid MNs)
 */
std::vector<CDeterministicMNCPtr> GetHuQuorum(
    const CDeterministicMNList& mnList,
    int cycleIndex,
    const uint256& prevCycleBlockHash);

/**
 * Check if a masternode is in the HU quorum for a given cycle
 *
 * @param mnList Deterministic MN list
 * @param cycleIndex Cycle index
 * @param prevCycleBlockHash Hash of last block in previous cycle
 * @param proTxHash ProRegTx hash of the MN to check
 * @return true if MN is in the quorum
 */
bool IsInHuQuorum(
    const CDeterministicMNList& mnList,
    int cycleIndex,
    const uint256& prevCycleBlockHash,
    const uint256& proTxHash);

/**
 * Get the HU quorum for a specific block height
 * Convenience function that determines cycle and gets appropriate block hash
 *
 * @param pindexPrev Previous block index (tip when creating block at nHeight)
 * @param nHeight Target block height
 * @param mnList Deterministic MN list
 * @return Vector of quorum MNs
 */
std::vector<CDeterministicMNCPtr> GetHuQuorumForHeight(
    const CBlockIndex* pindexPrev,
    int nHeight,
    const CDeterministicMNList& mnList);

/**
 * Compute MN score for quorum selection (used internally)
 * @param seed Quorum seed
 * @param proTxHash MN's proTxHash
 * @return Score for sorting
 */
uint256 ComputeHuQuorumMemberScore(const uint256& seed, const uint256& proTxHash);

} // namespace hu

#endif // PIVHU_HU_QUORUM_H
