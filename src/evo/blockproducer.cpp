// Copyright (c) 2025 The PIVHU Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "evo/blockproducer.h"

#include "chain.h"
#include "chainparams.h"
#include "consensus/validation.h"
#include "hash.h"
#include "logging.h"
#include "pubkey.h"

#include <algorithm>

namespace mn_consensus {

// Maximum fallback slots before we clamp (1 hour / fallbackWindow)
// Prevents integer overflow and limits how long we wait for any single producer
static const int MAX_FALLBACK_SLOTS = 360;  // 360 * 10s = 1 hour

arith_uint256 ComputeMNBlockScore(const uint256& prevBlockHash, int nHeight, const uint256& proTxHash)
{
    // score = SHA256(prevBlockHash || height || proTxHash)
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << prevBlockHash;
    ss << nHeight;
    ss << proTxHash;
    return UintToArith256(ss.GetHash());
}

/**
 * Calculate the producer slot from block header data.
 *
 * This is a PURE function that depends ONLY on block data (nTime, prevTime).
 * It must produce identical results on ALL nodes for consensus.
 *
 * Slot calculation:
 * - Slot 0 (primary): blockTime in [prevTime, prevTime + leaderTimeout)
 * - Slot 1 (fallback 1): blockTime in [prevTime + leaderTimeout, prevTime + leaderTimeout + fallbackWindow)
 * - Slot 2 (fallback 2): blockTime in [prevTime + leaderTimeout + fallbackWindow, ...)
 * - etc.
 *
 * @param pindexPrev  Previous block index (for prevTime)
 * @param nBlockTime  Block timestamp (from block header)
 * @return            Producer slot index (0 = primary, 1+ = fallback)
 */
int GetProducerSlot(const CBlockIndex* pindexPrev, int64_t nBlockTime)
{
    if (!pindexPrev) {
        return 0;
    }

    const Consensus::Params& consensus = Params().GetConsensus();
    int64_t prevTime = pindexPrev->GetBlockTime();
    int64_t dt = nBlockTime - prevTime;

    // Negative or zero time difference = primary producer
    if (dt <= 0) {
        return 0;
    }

    // Within leader timeout = primary producer (slot 0)
    if (dt < consensus.nHuLeaderTimeoutSeconds) {
        return 0;
    }

    // Past leader timeout = calculate fallback slot
    int64_t extra = dt - consensus.nHuLeaderTimeoutSeconds;
    int slot = 1 + (int)(extra / consensus.nHuFallbackRecoverySeconds);

    // Clamp to max fallback slots
    if (slot > MAX_FALLBACK_SLOTS) {
        slot = MAX_FALLBACK_SLOTS;
    }

    return slot;
}

/**
 * Get the expected block producer based on block header data.
 *
 * This function uses GetProducerSlot() to determine which MN should have
 * produced this block. The result is deterministic and identical on all nodes.
 *
 * IMPORTANT: This function is used BOTH by:
 * 1. The scheduler (to check if local MN should produce)
 * 2. Verification (to check if signature matches expected producer)
 *
 * @param pindexPrev     Previous block index
 * @param nBlockTime     Block timestamp
 * @param mnList         DMN list at pindexPrev
 * @param outMn          [out] Expected producer MN
 * @param outProducerIndex [out] Producer index (0 = primary, 1+ = fallback)
 * @return               true if producer found
 */
bool GetExpectedProducer(const CBlockIndex* pindexPrev,
                         int64_t nBlockTime,
                         const CDeterministicMNList& mnList,
                         CDeterministicMNCPtr& outMn,
                         int& outProducerIndex)
{
    outMn = nullptr;
    outProducerIndex = 0;

    if (!pindexPrev) {
        return false;
    }

    auto scores = CalculateBlockProducerScores(pindexPrev, mnList);
    if (scores.empty()) {
        LogPrint(BCLog::MASTERNODE, "%s: No confirmed MNs for block %d\n",
                 __func__, pindexPrev->nHeight + 1);
        return false;
    }

    int slot = GetProducerSlot(pindexPrev, nBlockTime);
    outProducerIndex = slot % (int)scores.size();  // Wrap around using modulo
    outMn = scores[outProducerIndex].second;

    if (outProducerIndex > 0) {
        LogPrint(BCLog::MASTERNODE, "%s: Block %d expected producer #%d: %s (slot=%d, nTime=%d)\n",
                 __func__, pindexPrev->nHeight + 1, outProducerIndex,
                 outMn->proTxHash.ToString().substr(0, 16), slot, nBlockTime);
    }

    return true;
}

std::vector<std::pair<arith_uint256, CDeterministicMNCPtr>>
CalculateBlockProducerScores(const CBlockIndex* pindexPrev, const CDeterministicMNList& mnList)
{
    std::vector<std::pair<arith_uint256, CDeterministicMNCPtr>> scores;

    if (!pindexPrev) {
        return scores;
    }

    const uint256& prevBlockHash = pindexPrev->GetBlockHash();
    const int nHeight = pindexPrev->nHeight + 1;

    scores.reserve(mnList.GetValidMNsCount());

    // Only valid (non-PoSe-banned), confirmed MNs
    mnList.ForEachMN(true /* onlyValid */, [&](const CDeterministicMNCPtr& dmn) {
        // Skip unconfirmed MNs (prevents hash grinding)
        if (dmn->pdmnState->confirmedHash.IsNull()) {
            return;
        }

        arith_uint256 score = ComputeMNBlockScore(prevBlockHash, nHeight, dmn->proTxHash);
        scores.emplace_back(score, dmn);
    });

    // Sort descending by score
    std::sort(scores.begin(), scores.end(),
        [](const auto& a, const auto& b) {
            if (a.first == b.first) {
                // Tie-breaker: proTxHash lexicographically
                return a.second->proTxHash < b.second->proTxHash;
            }
            return a.first > b.first;
        });

    return scores;
}

bool GetBlockProducer(const CBlockIndex* pindexPrev,
                      const CDeterministicMNList& mnList,
                      CDeterministicMNCPtr& outMn)
{
    outMn = nullptr;

    if (!pindexPrev) {
        return false;
    }

    auto scores = CalculateBlockProducerScores(pindexPrev, mnList);

    if (scores.empty()) {
        LogPrint(BCLog::MASTERNODE, "%s: No confirmed MNs for block %d\n",
                 __func__, pindexPrev->nHeight + 1);
        return false;
    }

    outMn = scores[0].second;

    LogPrint(BCLog::MASTERNODE, "%s: Block %d producer: %s (score: %s)\n",
             __func__, pindexPrev->nHeight + 1,
             outMn->proTxHash.ToString().substr(0, 16),
             scores[0].first.ToString().substr(0, 16));

    return true;
}

bool GetBlockProducerWithFallback(const CBlockIndex* pindexPrev,
                                   const CDeterministicMNList& mnList,
                                   int64_t nBlockTime,
                                   CDeterministicMNCPtr& outMn,
                                   int& outProducerIndex)
{
    outMn = nullptr;
    outProducerIndex = 0;

    if (!pindexPrev) {
        return false;
    }

    auto scores = CalculateBlockProducerScores(pindexPrev, mnList);

    if (scores.empty()) {
        LogPrint(BCLog::MASTERNODE, "%s: No confirmed MNs for block %d\n",
                 __func__, pindexPrev->nHeight + 1);
        return false;
    }

    // Get consensus params for timeout values
    const Consensus::Params& consensusParams = Params().GetConsensus();
    int64_t nPrevBlockTime = pindexPrev->GetBlockTime();
    int64_t nTimeSincePrevBlock = nBlockTime - nPrevBlockTime;

    // Calculate which producer should be active based on time elapsed
    // Primary: 0 to nHuLeaderTimeoutSeconds
    // Fallback 1: nHuLeaderTimeoutSeconds to nHuLeaderTimeoutSeconds + nHuFallbackRecoverySeconds
    // Fallback 2: nHuLeaderTimeoutSeconds + nHuFallbackRecoverySeconds to ...
    // etc.
    //
    // When the fallback index exceeds the number of MNs, we wrap around using modulo.
    // This ensures that even if some MNs are offline, the rotation will eventually
    // give every online MN a chance to produce a block.

    int producerIndex = 0;
    if (nTimeSincePrevBlock > consensusParams.nHuLeaderTimeoutSeconds) {
        // Past primary window, calculate fallback index
        int64_t fallbackTime = nTimeSincePrevBlock - consensusParams.nHuLeaderTimeoutSeconds;
        int rawIndex = 1 + (fallbackTime / consensusParams.nHuFallbackRecoverySeconds);

        // Wrap around using modulo to rotate through all available MNs
        // This ensures offline MNs don't block progress forever
        producerIndex = rawIndex % (int)scores.size();
    }

    outMn = scores[producerIndex].second;
    outProducerIndex = producerIndex;

    if (producerIndex > 0) {
        LogPrintf("%s: Block %d FALLBACK producer #%d: %s (time since prev: %ds)\n",
                 __func__, pindexPrev->nHeight + 1, producerIndex,
                 outMn->proTxHash.ToString().substr(0, 16), nTimeSincePrevBlock);
    } else {
        LogPrint(BCLog::MASTERNODE, "%s: Block %d PRIMARY producer: %s\n",
                 __func__, pindexPrev->nHeight + 1,
                 outMn->proTxHash.ToString().substr(0, 16));
    }

    return true;
}

bool SignBlockMNOnly(CBlock& block, const CKey& operatorKey)
{
    if (!operatorKey.IsValid()) {
        return error("%s: Invalid ECDSA operator key\n", __func__);
    }

    // Sign the block hash with ECDSA
    uint256 hashToSign = block.GetHash();
    std::vector<unsigned char> vchSig;
    if (!operatorKey.Sign(hashToSign, vchSig)) {
        return error("%s: ECDSA signing failed\n", __func__);
    }

    block.vchBlockSig = vchSig;

    // Debug: verify signature immediately
    CPubKey pubKey = operatorKey.GetPubKey();
    bool verified = pubKey.Verify(hashToSign, vchSig);

    LogPrintf("%s: Block %s signed with ECDSA (sig size: %d, pubkey: %s, verified: %d)\n",
             __func__, hashToSign.ToString().substr(0, 16), vchSig.size(),
             HexStr(pubKey).substr(0, 32), verified);

    return true;
}

bool VerifyBlockProducerSignature(const CBlock& block,
                                  const CBlockIndex* pindexPrev,
                                  const CDeterministicMNList& mnList,
                                  CValidationState& state)
{
    if (!pindexPrev) {
        return state.DoS(100, false, REJECT_INVALID, "bad-mn-no-prev");
    }

    // Check signature exists
    if (block.vchBlockSig.empty()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-mn-sig-empty");
    }

    // PIVHU v1: Verify ECDSA signature (typically 70-72 bytes DER encoded)
    if (block.vchBlockSig.size() < 64 || block.vchBlockSig.size() > 73) {
        return state.DoS(100, false, REJECT_INVALID, "bad-mn-sig-size", false,
                         strprintf("Bad ECDSA sig size: %d", block.vchBlockSig.size()));
    }

    // PROPER CONSENSUS: Use GetExpectedProducer based on block.nTime
    //
    // This is deterministic and uses the SAME formula as the scheduler:
    // - Producer slot is computed from (block.nTime - prevTime)
    // - Slot 0 = primary producer
    // - Slot 1+ = fallback producers
    //
    // The scheduler aligns block.nTime to the slot grid when creating blocks.
    // Verification uses this nTime to determine which MN was expected to sign.
    // This ensures production and verification use IDENTICAL rules.

    CDeterministicMNCPtr expectedMn;
    int producerIndex = 0;

    if (!GetExpectedProducer(pindexPrev, block.nTime, mnList, expectedMn, producerIndex)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-mn-no-producers", false,
                         "No confirmed masternodes for block production");
    }

    // Get operator pubkey (ECDSA)
    const CPubKey& pubKey = expectedMn->pdmnState->pubKeyOperator;
    if (!pubKey.IsValid()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-mn-invalid-key", false,
                         strprintf("Invalid operator key for expected producer %s",
                                   expectedMn->proTxHash.ToString().substr(0, 16)));
    }

    // Verify signature against expected producer
    uint256 hashToVerify = block.GetHash();
    if (!pubKey.Verify(hashToVerify, block.vchBlockSig)) {
        // Log detailed failure info for debugging
        LogPrintf("%s: Signature verification FAILED:\n"
                  "  - Block hash: %s\n"
                  "  - Block nTime: %d\n"
                  "  - PrevBlock time: %d\n"
                  "  - Expected producer #%d: %s\n"
                  "  - Sig size: %d\n",
                  __func__, hashToVerify.ToString().substr(0, 16),
                  block.nTime, pindexPrev->GetBlockTime(),
                  producerIndex, expectedMn->proTxHash.ToString().substr(0, 16),
                  block.vchBlockSig.size());

        return state.DoS(100, false, REJECT_INVALID, "bad-mn-sig-verify", false,
                         strprintf("ECDSA sig verification failed - expected producer #%d: %s",
                                   producerIndex, expectedMn->proTxHash.ToString().substr(0, 16)));
    }

    // Success!
    if (producerIndex > 0) {
        LogPrintf("%s: Block %s verified (ECDSA), fallback producer #%d: %s\n",
                 __func__, block.GetHash().ToString().substr(0, 16), producerIndex,
                 expectedMn->proTxHash.ToString().substr(0, 16));
    } else {
        LogPrint(BCLog::MASTERNODE, "%s: Block %s verified (ECDSA), primary producer: %s\n",
                 __func__, block.GetHash().ToString().substr(0, 16),
                 expectedMn->proTxHash.ToString().substr(0, 16));
    }

    return true;
}

} // namespace mn_consensus
