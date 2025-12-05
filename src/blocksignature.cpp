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

    // Bootstrap phase: Exempt until we have registered MNs
    // DMM activation happens AFTER the block containing the first 3 ProRegTx
    // So all blocks during bootstrap (before MN list has 3+ entries) are exempt
    {
        LOCK(cs_main);
        auto it = mapBlockIndex.find(block.hashPrevBlock);
        if (it != mapBlockIndex.end()) {
            // Check if we have enough registered MNs at the parent block
            auto mnList = deterministicMNManager->GetListAtChainTip();
            // If less than 3 MNs, we're still in bootstrap phase
            if (mnList.GetAllMNsCount() < 3) {
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
