// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef HU_HU_ZKHU_MEMO_H
#define HU_HU_ZKHU_MEMO_H

#include "amount.h"
#include <array>
#include <stdint.h>

/**
 * ZKHUMemo - 512-byte Sapling memo for ZKHU notes
 *
 * Format (512 bytes total):
 * - [0:4]   Magic "ZKHU" (4 bytes)
 * - [4:5]   Version (1 byte, currently 1)
 * - [5:9]   nLockStartHeight (4 bytes LE)
 * - [9:17]  amount (8 bytes LE)
 * - [17:25] Ur_accumulated (8 bytes LE) — PAS Ur_at_lock !
 * - [25:512] Padding zeros (487 bytes)
 *
 * Total: 4 + 1 + 4 + 8 + 8 + 487 = 512 bytes
 */
struct ZKHUMemo
{
    char     magic[4];              // "ZKHU"
    uint8_t  version;               // 1
    uint32_t nLockStartHeight;     // LE
    CAmount  amount;                // LE
    CAmount  Ur_accumulated;        // LE (Phase 4: 0)
    uint8_t  padding[512 - 4 - 1 - 4 - 8 - 8];  // 487 bytes

    ZKHUMemo();

    std::array<unsigned char, 512> Serialize() const;
    static ZKHUMemo Deserialize(const std::array<unsigned char, 512>& data);
};

#endif // HU_HU_ZKHU_MEMO_H
