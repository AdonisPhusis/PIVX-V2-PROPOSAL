// Copyright (c) 2025 The PIVHU Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PIVHU_HU_FINALITY_H
#define PIVHU_HU_FINALITY_H

#include "dbwrapper.h"
#include "serialize.h"
#include "sync.h"
#include "uint256.h"

#include <map>
#include <vector>

class CBlockIndex;

/**
 * HU Finality System - ECDSA-based block finality
 *
 * Quorum configuration per network (from consensus params):
 * - Mainnet: 12/8 (12 MNs, 8 signatures for finality)
 * - Testnet: 3/2 (3 MNs, 2 signatures for finality)
 * - Regtest: 1/1 (1 MN, 1 signature for finality)
 *
 * Parameters are network-specific and read from Consensus::Params:
 * - nHuQuorumSize: Number of MNs in quorum
 * - nHuQuorumThreshold: Minimum signatures for finality
 * - nHuQuorumRotationBlocks: Blocks per quorum cycle
 * - nHuLeaderTimeoutSeconds: DMM leader timeout
 * - nHuMaxReorgDepth: Max reorg depth before finality enforcement
 */

namespace hu {

// NOTE: These legacy constants are kept for backward compatibility
// but all new code should use Consensus::Params from Params().GetConsensus()
// Access via: const Consensus::Params& consensus = Params().GetConsensus();
//             consensus.nHuQuorumSize, consensus.nHuQuorumThreshold, etc.
static const int HU_QUORUM_SIZE_DEFAULT = 12;           // Default for mainnet
static const int HU_FINALITY_THRESHOLD_DEFAULT = 8;     // Default for mainnet
static const int HU_CYCLE_LENGTH_DEFAULT = 12;          // Default rotation
static const int HU_FINALITY_DEPTH_DEFAULT = 12;        // Default max reorg
static const int DMM_LEADER_TIMEOUT_SECONDS_DEFAULT = 45; // Default timeout

/**
 * Single HU signature for a block
 */
struct CHuSignature {
    uint256 blockHash;
    uint256 proTxHash;          // Signing MN's proTxHash
    std::vector<unsigned char> vchSig;  // ECDSA signature

    SERIALIZE_METHODS(CHuSignature, obj)
    {
        READWRITE(obj.blockHash, obj.proTxHash, obj.vchSig);
    }
};

/**
 * HU Finality data for a block
 * Stores all collected signatures
 */
class CHuFinality {
public:
    uint256 blockHash;
    int nHeight{0};
    std::map<uint256, std::vector<unsigned char>> mapSignatures; // proTxHash -> sig

    CHuFinality() = default;
    explicit CHuFinality(const uint256& hash, int height) : blockHash(hash), nHeight(height) {}

    /**
     * Check if block has reached finality threshold
     * @param nThreshold - from consensus.nHuQuorumThreshold (8/2/1 per network)
     */
    bool HasFinality(int nThreshold) const {
        return static_cast<int>(mapSignatures.size()) >= nThreshold;
    }

    // Backward compatibility - uses default threshold (mainnet)
    bool HasFinality() const { return HasFinality(HU_FINALITY_THRESHOLD_DEFAULT); }

    size_t GetSignatureCount() const { return mapSignatures.size(); }

    SERIALIZE_METHODS(CHuFinality, obj)
    {
        READWRITE(obj.blockHash, obj.nHeight, obj.mapSignatures);
    }
};

/**
 * HU Finality Handler
 * Manages finality signatures and enforcement
 */
class CHuFinalityHandler {
private:
    mutable RecursiveMutex cs;
    std::map<uint256, CHuFinality> mapFinality;  // blockHash -> finality data
    std::map<int, uint256> mapHeightToBlock;     // height -> blockHash (for quick lookup)

public:
    CHuFinalityHandler() = default;

    /**
     * Check if a block has HU finality (≥8 signatures)
     */
    bool HasFinality(int nHeight, const uint256& blockHash) const;

    /**
     * Check if accepting a block at given height/hash would conflict
     * with an already-finalized block
     */
    bool HasConflictingFinality(int nHeight, const uint256& blockHash) const;

    /**
     * Add a signature to a block's finality data
     * @return true if signature was new and valid
     */
    bool AddSignature(const CHuSignature& sig);

    /**
     * Get finality data for a block
     */
    bool GetFinality(const uint256& blockHash, CHuFinality& finalityOut) const;

    /**
     * Mark a block as final (called when ≥8 signatures collected)
     */
    void MarkBlockFinal(int nHeight, const uint256& blockHash);

    /**
     * Get signature count for a block
     */
    int GetSignatureCount(const uint256& blockHash) const;

    /**
     * Clear all finality data (for testing)
     */
    void Clear();
};

// Global handler instance
extern std::unique_ptr<CHuFinalityHandler> huFinalityHandler;

/**
 * CHuFinalityDB - LevelDB persistence for HU finality data
 *
 * Stores finality records indexed by blockHash.
 * Separate from block data to keep block hash immutable.
 */
class CHuFinalityDB : public CDBWrapper {
public:
    CHuFinalityDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    /**
     * Write finality data for a block
     */
    bool WriteFinality(const CHuFinality& finality);

    /**
     * Read finality data for a block
     * @return true if found, false otherwise
     */
    bool ReadFinality(const uint256& blockHash, CHuFinality& finality) const;

    /**
     * Check if finality data exists for a block
     */
    bool HasFinality(const uint256& blockHash) const;

    /**
     * Erase finality data (for reorg handling)
     */
    bool EraseFinality(const uint256& blockHash);

    /**
     * Check if a block is final (exists and meets threshold)
     * @param nThreshold - from consensus.nHuQuorumThreshold
     */
    bool IsBlockFinal(const uint256& blockHash, int nThreshold) const;
};

// Global DB instance
extern std::unique_ptr<CHuFinalityDB> pHuFinalityDB;

/**
 * Initialize HU finality system
 * @param nCacheSize - LevelDB cache size
 * @param fWipe - wipe database on init
 */
void InitHuFinality(size_t nCacheSize = (1 << 20), bool fWipe = false);

/**
 * Shutdown HU finality system
 */
void ShutdownHuFinality();

/**
 * Check if a block is HU-final (cannot be reorged)
 * Uses global consensus params for threshold
 */
bool IsBlockHuFinal(const uint256& blockHash);

/**
 * Check if a reorg to newTip would violate HU finality
 * @param pindexNew - proposed new tip
 * @param pindexFork - fork point
 * @return true if reorg is blocked by finality
 */
bool WouldViolateHuFinality(const CBlockIndex* pindexNew, const CBlockIndex* pindexFork);

} // namespace hu

#endif // PIVHU_HU_FINALITY_H
