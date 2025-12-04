// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef HU_HU_NOTES_H
#define HU_HU_NOTES_H

#include "amount.h"
#include "sapling/sapling.h"
#include "serialize.h"
#include "uint256.h"

#include <array>
#include <stdint.h>

/**
 * ZKHUNoteData - Private staking note metadata
 *
 * Source: docs/blueprints/05-ZKHU-SAPLING-LOCK.md section 3.2
 * Phase: 4 (ZKHU Staking)
 *
 * Cette structure représente les métadonnées d'une note ZKHU stakée.
 * Chaque note ZKHU est une note Sapling avec 512 bytes de memo custom.
 *
 * RÈGLE CRITIQUE:
 * - Ur_accumulated est PER-NOTE (pas global Ur_at_lock)
 * - Phase 4: Ur_accumulated = 0 (no yield yet)
 * - Phase 5+: Ur_accumulated > 0 (incremented by yield engine)
 */
struct ZKHUNoteData
{
    CAmount amount;                // Montant KHU_T staké (atoms)
    uint32_t nLockStartHeight;    // Block height où LOCK a eu lieu
    int64_t Ur_accumulated;        // Reward accumulated per-note (Phase 4: =0, Phase 5+: >0)
    uint256 nullifier;             // Sapling nullifier (unique, prevents double-spend)
    uint256 cm;                    // Sapling commitment (public, in Merkle tree)

    ZKHUNoteData()
    {
        SetNull();
    }

    ZKHUNoteData(CAmount amountIn, uint32_t heightIn, int64_t urIn, const uint256& nullifierIn, const uint256& cmIn)
        : amount(amountIn),
          nLockStartHeight(heightIn),
          Ur_accumulated(urIn),
          nullifier(nullifierIn),
          cm(cmIn)
    {}

    void SetNull()
    {
        amount = 0;
        nLockStartHeight = 0;
        Ur_accumulated = 0;
        nullifier.SetNull();
        cm.SetNull();
    }

    bool IsNull() const
    {
        return (amount == 0 && nLockStartHeight == 0 && nullifier.IsNull());
    }

    /**
     * GetBonus - Calcule le bonus UNLOCK pour cette note
     *
     * Phase 4: bonus = 0 (Ur_accumulated = 0)
     * Phase 5+: bonus = Ur_accumulated (incremented by daily yield)
     *
     * ⚠️ CRITICAL: Bonus is PER-NOTE, NOT global (Ur_now - Ur_at_lock)
     */
    CAmount GetBonus() const
    {
        return Ur_accumulated;
    }

    SERIALIZE_METHODS(ZKHUNoteData, obj)
    {
        READWRITE(obj.amount);
        READWRITE(obj.nLockStartHeight);
        READWRITE(obj.Ur_accumulated);  // ✅ PAS Ur_at_lock !
        READWRITE(obj.nullifier);
        READWRITE(obj.cm);
    }
};

/**
 * ZKHUMemo - 512-byte Sapling memo for ZKHU notes
 *
 * Source: docs/blueprints/05-ZKHU-SAPLING-LOCK.md section 3.2
 * Phase: 4
 *
 * Format (512 bytes total):
 * - [0:4]   Magic "ZKHU" (4 bytes)
 * - [4:5]   Version (1 byte, currently 1)
 * - [5:9]   nLockStartHeight (4 bytes LE)
 * - [9:17]  amount (8 bytes LE)
 * - [17:25] Ur_accumulated (8 bytes LE) — ✅ PAS Ur_at_lock !
 * - [25:512] Padding zeros (487 bytes)
 *
 * Total: 4 + 1 + 4 + 8 + 8 + 487 = 512 bytes
 */
struct ZKHUMemo
{
    static constexpr size_t MEMO_SIZE = ZC_MEMO_SIZE;  // 512 bytes
    static constexpr size_t MAGIC_SIZE = 4;
    static constexpr char MAGIC[5] = "ZKHU";
    static constexpr uint8_t VERSION = 1;

    std::array<unsigned char, MEMO_SIZE> data;

    ZKHUMemo()
    {
        data.fill(0);
    }

    /**
     * Encode - Créer memo à partir de ZKHUNoteData
     *
     * ⚠️ CRITICAL: Memo contains Ur_accumulated (per-note), NOT Ur_at_lock (global snapshot)
     */
    static ZKHUMemo Encode(const ZKHUNoteData& noteData);

    /**
     * Decode - Extraire ZKHUNoteData du memo
     *
     * @return Optional<ZKHUNoteData> (None if invalid magic/version)
     */
    static Optional<ZKHUNoteData> Decode(const std::array<unsigned char, MEMO_SIZE>& memo, const uint256& nullifier, const uint256& cm);

    /**
     * Validate - Vérifier magic et version
     */
    bool Validate() const;

    // Accessors
    const unsigned char* raw() const { return data.data(); }
    size_t size() const { return MEMO_SIZE; }

    SERIALIZE_METHODS(ZKHUMemo, obj)
    {
        READWRITE(obj.data);
    }
};

/**
 * Maturity Constants
 *
 * Note: Use GetZKHUMaturityBlocks() from hu_unlock.h for runtime maturity checks.
 * These constants are provided for reference:
 * - MAINNET/TESTNET: 4320 blocks (~3 days)
 * - REGTEST: 1260 blocks (~21 hours for fast testing)
 */
static constexpr uint32_t ZKHU_MATURITY_BLOCKS_DEFAULT = 4320;  // 3 days (60s per block)
static constexpr uint32_t ZKHU_MATURITY_BLOCKS_REGTEST = 1260;  // ~21 hours for regtest

// Note: MIN_LOCK_AMOUNT is defined in hu_lock.h

#endif // HU_HU_NOTES_H
