// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "piv2/zkpiv2_memo.h"

#include "crypto/common.h"
#include <cstring>
#include <stdexcept>

ZKHUMemo::ZKHUMemo()
{
    memcpy(magic, "ZKHU", 4);
    version           = 1;
    nLockStartHeight = 0;
    amount            = 0;
    Ur_accumulated    = 0;
    memset(padding, 0, sizeof(padding));
}

std::array<unsigned char, 512> ZKHUMemo::Serialize() const
{
    std::array<unsigned char, 512> out;
    out.fill(0);

    memcpy(&out[0], magic, 4);
    out[4] = version;
    WriteLE32(&out[5], nLockStartHeight);
    WriteLE64(&out[9], amount);
    WriteLE64(&out[17], Ur_accumulated);
    // padding déjà = 0

    return out;
}

ZKHUMemo ZKHUMemo::Deserialize(const std::array<unsigned char, 512>& data)
{
    ZKHUMemo memo;
    memcpy(memo.magic, &data[0], 4);
    if (memcmp(memo.magic, "ZKHU", 4) != 0) {
        throw std::runtime_error("Invalid ZKHU memo magic");
    }
    memo.version           = data[4];
    memo.nLockStartHeight = ReadLE32(&data[5]);
    memo.amount            = ReadLE64(&data[9]);
    memo.Ur_accumulated    = ReadLE64(&data[17]);
    return memo;
}
