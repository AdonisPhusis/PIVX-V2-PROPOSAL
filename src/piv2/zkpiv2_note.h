// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef HU_HU_ZKHU_NOTE_H
#define HU_HU_ZKHU_NOTE_H

#include "amount.h"
#include "serialize.h"
#include "uint256.h"

#include <stdint.h>

/**
 * ZKHUNoteData - Private staking note metadata
 *
 * Source: docs/blueprints/05-ZKHU-SAPLING-LOCK.md
 * Phase: 4 (ZKHU Staking)
 *
 * RÈGLE CRITIQUE:
 * - Ur_accumulated est PER-NOTE (pas global Ur_at_lock)
 * - Phase 4: Ur_accumulated = 0 (no yield yet)
 * - Phase 5+: Ur_accumulated > 0 (incremented by yield engine)
 */
struct ZKHUNoteData
{
    CAmount  amount;              // KHU amount lockd (satoshis)
    uint32_t nLockStartHeight;   // Lock start height
    CAmount  Ur_accumulated;      // Phase 4: always 0, Phase 5: per-note yield
    uint256  nullifier;           // Nullifier of the note
    uint256  cm;                  // Commitment (cmu)
    bool     bSpent;              // True if note was spent via UNLOCK (excludes from yield calc)

    ZKHUNoteData()
        : amount(0), nLockStartHeight(0), Ur_accumulated(0), bSpent(false)
    {}

    ZKHUNoteData(CAmount amountIn, uint32_t heightIn, CAmount urIn, const uint256& nullifierIn, const uint256& cmIn)
        : amount(amountIn),
          nLockStartHeight(heightIn),
          Ur_accumulated(urIn),
          nullifier(nullifierIn),
          cm(cmIn),
          bSpent(false)
    {}

    SERIALIZE_METHODS(ZKHUNoteData, obj)
    {
        READWRITE(obj.amount, obj.nLockStartHeight, obj.Ur_accumulated);
        READWRITE(obj.nullifier, obj.cm, obj.bSpent);
    }
};

#endif // HU_HU_ZKHU_NOTE_H
