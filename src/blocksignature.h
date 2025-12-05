// Copyright (c) 2025 The PIV2 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PIV2_BLOCKSIGNATURE_H
#define PIV2_BLOCKSIGNATURE_H

#include "key.h"
#include "primitives/block.h"

bool SignBlockWithKey(CBlock& block, const CKey& key);
bool CheckBlockSignature(const CBlock& block);

#endif // PIV2_BLOCKSIGNATURE_H
