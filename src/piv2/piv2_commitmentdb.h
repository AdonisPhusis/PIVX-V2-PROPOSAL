// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef HU_HU_COMMITMENTDB_H
#define HU_HU_COMMITMENTDB_H

#include "dbwrapper.h"
#include "piv2/piv2_commitment.h"

#include <stdint.h>

/**
 * CHUCommitmentDB - LevelDB persistence for HU state commitments
 *
 * Keys: 'K'+'C'+height -> HuStateCommitment, 'K'+'L' -> latestFinalizedHeight
 */
class CHUCommitmentDB : public CDBWrapper
{
public:
    explicit CHUCommitmentDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

private:
    CHUCommitmentDB(const CHUCommitmentDB&);
    void operator=(const CHUCommitmentDB&);

public:
    bool WriteCommitment(uint32_t nHeight, const HuStateCommitment& commitment);
    bool ReadCommitment(uint32_t nHeight, HuStateCommitment& commitment);
    bool HaveCommitment(uint32_t nHeight);
    bool EraseCommitment(uint32_t nHeight);
    uint32_t GetLatestFinalizedHeight();
    bool SetLatestFinalizedHeight(uint32_t nHeight);
    bool IsFinalizedAt(uint32_t nHeight);
};

#endif // HU_HU_COMMITMENTDB_H
