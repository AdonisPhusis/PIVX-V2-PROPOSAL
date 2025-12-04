// Copyright (c) 2025 The PIVHU Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef HU_EVO_BLOCKPRODUCER_H
#define HU_EVO_BLOCKPRODUCER_H

#include "arith_uint256.h"
#include "evo/deterministicmns.h"
#include "key.h"
#include "primitives/block.h"
#include "uint256.h"

class CBlockIndex;
class CValidationState;

namespace mn_consensus {

/**
 * MN-only block production for PIVHU chain.
 *
 * PIVHU uses pure MN-only consensus from genesis - NO PoS.
 * Masternodes produce all blocks. Selection is hash-based and deterministic:
 *
 *   score(MN) = H(prevBlockHash || height+1 || proTxHash)
 *
 * The MN with the highest score is the legitimate producer for that block.
 *
 * Block signatures use ECDSA with the operator key (~72 bytes DER encoded)
 *
 * Properties:
 * - Deterministic: Same inputs = same selection
 * - Unpredictable: Cannot be known until previous block hash is known
 * - Fair: Each MN has equal probability based on its proTxHash
 * - No grinding: proTxHash is committed before any block it could influence
 */

/**
 * Compute block producer score for a masternode.
 *
 * score = SHA256(prevBlockHash || height || proTxHash)
 *
 * @param prevBlockHash  Hash of the previous block
 * @param nHeight        Height of the block being produced
 * @param proTxHash      ProRegTx hash of the masternode
 * @return               Score as arith_uint256 (higher = selected)
 */
arith_uint256 ComputeMNBlockScore(const uint256& prevBlockHash, int nHeight, const uint256& proTxHash);

/**
 * Select the block producer for a given height.
 *
 * @param pindexPrev     Previous block index
 * @param mnList         DMN list at pindexPrev
 * @param outMn          [out] Selected masternode
 * @return               true if producer found
 */
bool GetBlockProducer(const CBlockIndex* pindexPrev,
                      const CDeterministicMNList& mnList,
                      CDeterministicMNCPtr& outMn);

/**
 * Calculate all MN scores for debugging/verification.
 *
 * @param pindexPrev     Previous block index
 * @param mnList         DMN list
 * @return               Vector of (score, MN) pairs, sorted descending
 */
std::vector<std::pair<arith_uint256, CDeterministicMNCPtr>>
CalculateBlockProducerScores(const CBlockIndex* pindexPrev, const CDeterministicMNList& mnList);

/**
 * Sign block with MN operator ECDSA key.
 *
 * @param block          Block to sign
 * @param operatorKey    ECDSA private key (operator)
 * @return               true if signed
 */
bool SignBlockMNOnly(CBlock& block, const CKey& operatorKey);

/**
 * Verify block signature matches expected producer.
 *
 * @param block          Block to verify
 * @param pindexPrev     Previous block
 * @param mnList         DMN list at pindexPrev
 * @param state          Validation state
 * @return               true if valid
 */
bool VerifyBlockProducerSignature(const CBlock& block,
                                  const CBlockIndex* pindexPrev,
                                  const CDeterministicMNList& mnList,
                                  CValidationState& state);

} // namespace mn_consensus

#endif // HU_EVO_BLOCKPRODUCER_H
