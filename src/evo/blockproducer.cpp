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

    int producerIndex = 0;
    if (nTimeSincePrevBlock > consensusParams.nHuLeaderTimeoutSeconds) {
        // Past primary window, calculate fallback index
        int64_t fallbackTime = nTimeSincePrevBlock - consensusParams.nHuLeaderTimeoutSeconds;
        producerIndex = 1 + (fallbackTime / consensusParams.nHuFallbackRecoverySeconds);

        // Cap to available MNs
        if (producerIndex >= (int)scores.size()) {
            producerIndex = scores.size() - 1;
        }
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

    // Get expected producer
    CDeterministicMNCPtr expectedMn;
    if (!GetBlockProducer(pindexPrev, mnList, expectedMn)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-mn-no-producer", false,
                         "No valid MN for this height");
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

    // Get operator pubkey (ECDSA)
    const CPubKey& pubKey = expectedMn->pdmnState->pubKeyOperator;
    if (!pubKey.IsValid()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-mn-operator-key", false,
                         strprintf("Invalid operator key: %s", expectedMn->proTxHash.ToString()));
    }

    // Verify ECDSA signature
    uint256 hashToVerify = block.GetHash();
    if (!pubKey.Verify(hashToVerify, block.vchBlockSig)) {
        LogPrintf("%s: Signature verification FAILED:\n"
                  "  - Block hash: %s\n"
                  "  - Sig size: %d\n"
                  "  - Expected pubkey: %s\n"
                  "  - Expected MN: %s\n",
                  __func__, hashToVerify.ToString(), block.vchBlockSig.size(),
                  HexStr(pubKey), expectedMn->proTxHash.ToString());
        return state.DoS(100, false, REJECT_INVALID, "bad-mn-sig-verify", false,
                         strprintf("ECDSA sig verification failed. Expected producer: %s",
                                   expectedMn->proTxHash.ToString()));
    }

    LogPrint(BCLog::MASTERNODE, "%s: Block %s verified (ECDSA), producer: %s\n",
             __func__, block.GetHash().ToString().substr(0, 16),
             expectedMn->proTxHash.ToString().substr(0, 16));

    return true;
}

} // namespace mn_consensus
