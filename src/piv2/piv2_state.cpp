// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "piv2/piv2_state.h"

#include "hash.h"
#include "streams.h"

uint256 HuGlobalState::GetHash() const
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << *this;
    return ss.GetHash();
}
