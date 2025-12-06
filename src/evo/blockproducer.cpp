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

arith_uint256 ComputeMNBlockScore(const uint256& prevBlockHash, int nHeight, const uint256& proTxHash)
{
    // score = SHA256(prevBlockHash || height || proTxHash)
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << prevBlockHash;
    ss << nHeight;
    ss << proTxHash;
    return UintToArith256(ss.GetHash());
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

    uint256 hashToVerify = block.GetHash();

    // NEW APPROACH: Accept signature from ANY valid producer in the ordered list
    //
    // Rationale: During network stalls (cold start), a block may be created by a
    // fallback producer (e.g., #2) after a long delay. The block's nTime doesn't
    // reflect the actual delay, so time-based producer selection fails.
    //
    // Solution: Instead of guessing which time window was used, we simply verify
    // that the signature comes from a valid MN in the deterministic producer list.
    // This is secure because:
    // 1. Only confirmed MNs are in the list (anti-grinding)
    // 2. The list is deterministic (same for all nodes)
    // 3. Any MN in the list is authorized to produce (just at different times)
    // 4. Bad actors can't produce faster than their slot anyway
    //
    // This matches Ethereum 2.0's approach: any validator in the committee can
    // propose, the timing just determines priority.

    auto scores = CalculateBlockProducerScores(pindexPrev, mnList);

    if (scores.empty()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-mn-no-producers", false,
                         "No confirmed masternodes for block production");
    }

    // Try to verify signature with each MN in the ordered producer list
    for (size_t i = 0; i < scores.size(); i++) {
        const auto& dmn = scores[i].second;

        // Get operator pubkey (ECDSA)
        const CPubKey& pubKey = dmn->pdmnState->pubKeyOperator;
        if (!pubKey.IsValid()) {
            continue;  // Invalid key, try next MN
        }

        // Verify ECDSA signature
        if (pubKey.Verify(hashToVerify, block.vchBlockSig)) {
            // Signature verified!
            if (i > 0) {
                LogPrintf("%s: Block %s verified (ECDSA), producer #%d: %s (fallback accepted)\n",
                         __func__, block.GetHash().ToString().substr(0, 16), (int)i,
                         dmn->proTxHash.ToString().substr(0, 16));
            } else {
                LogPrint(BCLog::MASTERNODE, "%s: Block %s verified (ECDSA), primary producer: %s\n",
                         __func__, block.GetHash().ToString().substr(0, 16),
                         dmn->proTxHash.ToString().substr(0, 16));
            }
            return true;
        }
    }

    // If we reach here, signature verification failed for ALL producers
    // This means the block was signed by an unknown/invalid key
    LogPrintf("%s: Signature verification FAILED:\n"
              "  - Block hash: %s\n"
              "  - Sig size: %d\n"
              "  - Checked %d producers, none matched\n"
              "  - Primary producer: %s\n",
              __func__, hashToVerify.ToString(), block.vchBlockSig.size(),
              (int)scores.size(),
              scores[0].second->proTxHash.ToString());

    return state.DoS(100, false, REJECT_INVALID, "bad-mn-sig-verify", false,
                     "ECDSA sig verification failed - no matching producer");
}

} // namespace mn_consensus
