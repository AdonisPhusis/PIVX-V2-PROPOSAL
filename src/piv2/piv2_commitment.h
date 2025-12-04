// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef HU_HU_COMMITMENT_H
#define HU_HU_COMMITMENT_H

#include "serialize.h"
#include "uint256.h"

#include <vector>

struct HuGlobalState;

/**
 * HuStateCommitment - MN-signed commitment to HU state
 *
 * Provides cryptographic finality for HU state at each block.
 * Masternodes sign the state hash using ECDSA.
 * Once quorum reached (8/12 signatures), state is finalized.
 */
struct HuStateCommitment
{
    uint32_t nHeight;                              // Block height
    uint256 hashState;                             // State hash: SHA256(C, U, Cr, Ur, height)
    uint256 quorumHash;                            // Quorum identifier
    std::vector<std::vector<unsigned char>> sigs;  // ECDSA signatures from MNs
    std::vector<bool> signers;                     // Bitfield: signers[i] = true if MN i signed

    HuStateCommitment()
    {
        SetNull();
    }

    void SetNull()
    {
        nHeight = 0;
        hashState.SetNull();
        quorumHash.SetNull();
        sigs.clear();
        signers.clear();
    }

    bool IsNull() const
    {
        return (nHeight == 0 && hashState.IsNull());
    }

    bool IsValid() const;
    bool HasQuorum() const;
    uint256 GetHash() const;

    SERIALIZE_METHODS(HuStateCommitment, obj)
    {
        READWRITE(obj.nHeight);
        READWRITE(obj.hashState);
        READWRITE(obj.quorumHash);
        READWRITE(obj.sigs);
        READWRITE(DYNBITSET(obj.signers));
    }
};

uint256 ComputeKHUStateHash(const HuGlobalState& state);

HuStateCommitment CreateHUStateCommitment(
    const HuGlobalState& state,
    const uint256& quorumHash
);

bool VerifyKHUStateCommitment(
    const HuStateCommitment& commitment,
    const HuGlobalState& state
);

bool CheckReorgConflict(uint32_t nHeight, const uint256& hashState);

#endif // HU_HU_COMMITMENT_H
