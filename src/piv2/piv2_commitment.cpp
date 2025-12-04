// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "piv2/piv2_commitment.h"

#include "hash.h"
#include "piv2/piv2_state.h"
#include "util/system.h"

#include <algorithm>

// HU quorum threshold: 8/12 = 67% of members must sign
static constexpr double QUORUM_THRESHOLD = 0.67;

bool HuStateCommitment::IsValid() const
{
    if (nHeight == 0) {
        return false;
    }
    if (hashState.IsNull()) {
        return false;
    }
    if (quorumHash.IsNull()) {
        return false;
    }
    return true;
}

bool HuStateCommitment::HasQuorum() const
{
    if (signers.empty()) {
        return false;
    }

    int signerCount = 0;
    for (bool signer : signers) {
        if (signer) {
            signerCount++;
        }
    }

    int totalMembers = signers.size();
    double signerRatio = static_cast<double>(signerCount) / static_cast<double>(totalMembers);

    return signerRatio >= QUORUM_THRESHOLD;
}

uint256 HuStateCommitment::GetHash() const
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << *this;
    return ss.GetHash();
}

uint256 ComputeKHUStateHash(const HuGlobalState& state)
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << state.C;
    ss << state.U;
    ss << state.Cr;
    ss << state.Ur;
    ss << state.nHeight;

    return ss.GetHash();
}

HuStateCommitment CreateHUStateCommitment(
    const HuGlobalState& state,
    const uint256& quorumHash)
{
    HuStateCommitment commitment;

    commitment.nHeight = state.nHeight;
    commitment.hashState = ComputeKHUStateHash(state);
    commitment.quorumHash = quorumHash;

    return commitment;
}

bool VerifyKHUStateCommitment(
    const HuStateCommitment& commitment,
    const HuGlobalState& state)
{
    if (!commitment.IsValid()) {
        LogPrint(BCLog::HU, "HU: Invalid commitment structure at height %d\n", commitment.nHeight);
        return false;
    }

    if (commitment.nHeight != static_cast<uint32_t>(state.nHeight)) {
        LogPrint(BCLog::HU, "HU: Commitment height mismatch: %d != %d\n",
                 commitment.nHeight, state.nHeight);
        return false;
    }

    uint256 computedHash = ComputeKHUStateHash(state);
    if (commitment.hashState != computedHash) {
        LogPrint(BCLog::HU, "HU: State hash mismatch at height %d\n", commitment.nHeight);
        return false;
    }

    if (!commitment.HasQuorum()) {
        LogPrint(BCLog::HU, "HU: Commitment lacks quorum at height %d\n", commitment.nHeight);
        return false;
    }

    LogPrint(BCLog::HU, "HU: State commitment verified at height %d\n", commitment.nHeight);

    return true;
}

bool CheckReorgConflict(uint32_t nHeight, const uint256& hashState)
{
    return true;
}
