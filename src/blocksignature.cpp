// Copyright (c) 2025 The PIV2 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blocksignature.h"

#include "chainparams.h"
#include "evo/deterministicmns.h"
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

    // Bootstrap phase: Exempt blocks 1-5 on testnet
    // Genesis MNs are injected at block 1, but they can't sign block 1 itself
    // (chicken-and-egg: MNs are loaded after block 1 is connected)
    // Block 1 creates the collaterals, blocks 2-5 allow sync time
    if (Params().IsTestnet()) {
        LOCK(cs_main);
        auto it = mapBlockIndex.find(block.hashPrevBlock);
        if (it != mapBlockIndex.end()) {
            int nHeight = it->second->nHeight + 1;  // Height of this block
            if (nHeight <= 5) {
                LogPrintf("%s: Bootstrap exemption for block %d (testnet)\n", __func__, nHeight);
                return true;
            }
        }
    }

    // DMM blocks: Signature verified by CheckBlockMNOnly in ConnectBlock
    if (block.vchBlockSig.empty()) {
        return error("%s: block has empty vchBlockSig!", __func__);
    }

    return true;
}
