// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "piv2/piv2_notes.h"

#include "utilstrencodings.h"
#include <cstring>

ZKHUMemo ZKHUMemo::Encode(const ZKHUNoteData& noteData)
{
    ZKHUMemo memo;
    size_t offset = 0;

    // [0:4] Magic "ZKHU"
    std::memcpy(&memo.data[offset], MAGIC, MAGIC_SIZE);
    offset += MAGIC_SIZE;

    // [4:5] Version
    memo.data[offset] = VERSION;
    offset += 1;

    // [5:9] nLockStartHeight (4 bytes LE)
    uint32_t height_le = htole32(noteData.nLockStartHeight);
    std::memcpy(&memo.data[offset], &height_le, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    // [9:17] amount (8 bytes LE)
    int64_t amount_le = htole64(noteData.amount);
    std::memcpy(&memo.data[offset], &amount_le, sizeof(int64_t));
    offset += sizeof(int64_t);

    // [17:25] Ur_accumulated (8 bytes LE) — ✅ PAS Ur_at_lock !
    int64_t ur_le = htole64(noteData.Ur_accumulated);
    std::memcpy(&memo.data[offset], &ur_le, sizeof(int64_t));
    offset += sizeof(int64_t);

    // [25:512] Padding zeros (already initialized to 0)
    // Total: 4 + 1 + 4 + 8 + 8 = 25 bytes used, 487 bytes padding

    return memo;
}

Optional<ZKHUNoteData> ZKHUMemo::Decode(const std::array<unsigned char, MEMO_SIZE>& memo, const uint256& nullifier, const uint256& cm)
{
    size_t offset = 0;

    // [0:4] Validate magic
    if (std::memcmp(&memo[offset], MAGIC, MAGIC_SIZE) != 0) {
        return nullopt;  // Invalid magic
    }
    offset += MAGIC_SIZE;

    // [4:5] Validate version
    uint8_t version = memo[offset];
    if (version != VERSION) {
        return nullopt;  // Unsupported version
    }
    offset += 1;

    // [5:9] nLockStartHeight (4 bytes LE)
    uint32_t height_le;
    std::memcpy(&height_le, &memo[offset], sizeof(uint32_t));
    uint32_t nLockStartHeight = le32toh(height_le);
    offset += sizeof(uint32_t);

    // [9:17] amount (8 bytes LE)
    int64_t amount_le;
    std::memcpy(&amount_le, &memo[offset], sizeof(int64_t));
    CAmount amount = le64toh(amount_le);
    offset += sizeof(int64_t);

    // [17:25] Ur_accumulated (8 bytes LE)
    int64_t ur_le;
    std::memcpy(&ur_le, &memo[offset], sizeof(int64_t));
    int64_t Ur_accumulated = le64toh(ur_le);
    offset += sizeof(int64_t);

    // Construct ZKHUNoteData
    ZKHUNoteData noteData(amount, nLockStartHeight, Ur_accumulated, nullifier, cm);
    return noteData;
}

bool ZKHUMemo::Validate() const
{
    // Check magic
    if (std::memcmp(&data[0], MAGIC, MAGIC_SIZE) != 0) {
        return false;
    }

    // Check version
    uint8_t version = data[MAGIC_SIZE];
    if (version != VERSION) {
        return false;
    }

    return true;
}
