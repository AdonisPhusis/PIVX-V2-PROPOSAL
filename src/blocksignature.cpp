// Copyright (c) 2025 The PIV2 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blocksignature.h"

#include "chainparams.h"
#include "validation.h"

bool SignBlockWithKey(CBlock& block, const CKey& key)
{
    if (!key.Sign(block.GetHash(), block.vchBlockSig))
        return error("%s: failed to sign block hash with key", __func__);

    return true;
}

bool CheckBlockSignature(const CBlock& block)
{
    // Genesis block - no signature required
    if (block.hashPrevBlock.IsNull()) {
        return true;
    }

    // Regtest: Skip signature verification
    if (Params().IsRegTestNet()) {
        return true;
    }

    // Bootstrap phase: Blocks 1 and 2 exempt (no MNs active yet)
    {
        LOCK(cs_main);
        auto it = mapBlockIndex.find(block.hashPrevBlock);
        if (it != mapBlockIndex.end() && it->second->nHeight < 2) {
            return true;
        }
    }

    // DMM blocks: Signature verified by CheckBlockMNOnly in ConnectBlock
    if (block.vchBlockSig.empty()) {
        return error("%s: block has empty vchBlockSig!", __func__);
    }

    return true;
}
