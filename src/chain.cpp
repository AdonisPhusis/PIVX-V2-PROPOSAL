// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2016-2021 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chain.h"


/**
 * CChain implementation
 */
void CChain::SetTip(CBlockIndex* pindex)
{
    if (pindex == nullptr) {
        vChain.clear();
        return;
    }
    vChain.resize(pindex->nHeight + 1);
    while (pindex && vChain[pindex->nHeight] != pindex) {
        vChain[pindex->nHeight] = pindex;
        pindex = pindex->pprev;
    }
}

CBlockLocator CChain::GetLocator(const CBlockIndex* pindex) const
{
    int nStep = 1;
    std::vector<uint256> vHave;
    vHave.reserve(32);

    if (!pindex)
        pindex = Tip();
    while (pindex) {
        vHave.push_back(pindex->GetBlockHash());
        // Stop when we have added the genesis block.
        if (pindex->nHeight == 0)
            break;
        // Exponentially larger steps back, plus the genesis block.
        int nHeight = std::max(pindex->nHeight - nStep, 0);
        if (Contains(pindex)) {
            // Use O(1) CChain index if possible.
            pindex = (*this)[nHeight];
        } else {
            // Otherwise, use O(log n) skiplist.
            pindex = pindex->GetAncestor(nHeight);
        }
        if (vHave.size() > 10)
            nStep *= 2;
    }

    return CBlockLocator(vHave);
}

const CBlockIndex* CChain::FindFork(const CBlockIndex* pindex) const
{
    if (pindex == nullptr)
        return nullptr;
    if (pindex->nHeight > Height())
        pindex = pindex->GetAncestor(Height());
    while (pindex && !Contains(pindex))
        pindex = pindex->pprev;
    return pindex;
}

CBlockIndex* CChain::FindEarliestAtLeast(int64_t nTime) const
{
    std::vector<CBlockIndex*>::const_iterator lower = std::lower_bound(vChain.begin(), vChain.end(), nTime,
        [](CBlockIndex* pBlock, const int64_t& time) -> bool { return pBlock->GetBlockTimeMax() < time; });
    return (lower == vChain.end() ? nullptr : *lower);
}

/** Turn the lowest '1' bit in the binary representation of a number into a '0'. */
int static inline InvertLowestOne(int n) { return n & (n - 1); }

/** Compute what height to jump back to with the CBlockIndex::pskip pointer. */
int static inline GetSkipHeight(int height)
{
    if (height < 2)
        return 0;
    // Determine which height to jump back to. Any number strictly lower than height is acceptable,
    // but the following expression seems to perform well in simulations (max 110 steps to go back
    // up to 2**18 blocks).
    return (height & 1) ? InvertLowestOne(InvertLowestOne(height - 1)) + 1 : InvertLowestOne(height);
}

const CBlockIndex* CBlockIndex::GetAncestor(int height) const
{
    if (height > nHeight || height < 0) {
        return nullptr;
    }

    const CBlockIndex* pindexWalk = this;
    int heightWalk = nHeight;
    while (heightWalk > height) {
        int heightSkip = GetSkipHeight(heightWalk);
        int heightSkipPrev = GetSkipHeight(heightWalk - 1);
        if (heightSkip == height ||
            (heightSkip > height && !(heightSkipPrev < heightSkip - 2 && heightSkipPrev >= height))) {
            // Only follow pskip if pprev->pskip isn't better than pskip->pprev.
            pindexWalk = pindexWalk->pskip;
            heightWalk = heightSkip;
        } else {
            assert(pindexWalk->pprev);
            pindexWalk = pindexWalk->pprev;
            heightWalk--;
        }
    }
    return pindexWalk;
}

CBlockIndex* CBlockIndex::GetAncestor(int height)
{
    return const_cast<CBlockIndex*>(static_cast<const CBlockIndex*>(this)->GetAncestor(height));
}

void CBlockIndex::BuildSkip()
{
    if (pprev)
        pskip = pprev->GetAncestor(GetSkipHeight(nHeight));
}

CBlockIndex::CBlockIndex(const CBlock& block):
        nVersion{block.nVersion},
        hashMerkleRoot{block.hashMerkleRoot},
        hashFinalSaplingRoot(block.hashFinalSaplingRoot),
        nTime{block.nTime},
        nBits{block.nBits},
        nNonce{block.nNonce}
{
    if(block.nVersion > 3 && block.nVersion < 7)
        nAccumulatorCheckpoint = block.nAccumulatorCheckpoint;
    // MN-only consensus
}

std::string CBlockIndex::ToString() const
{
    return strprintf("CBlockIndex(pprev=%p, nHeight=%d, merkle=%s, hashBlock=%s)",
        pprev, nHeight,
        hashMerkleRoot.ToString(),
        GetBlockHash().ToString());
}

FlatFilePos CBlockIndex::GetBlockPos() const
{
    FlatFilePos ret;
    if (nStatus & BLOCK_HAVE_DATA) {
        ret.nFile = nFile;
        ret.nPos = nDataPos;
    }
    return ret;
}

FlatFilePos CBlockIndex::GetUndoPos() const
{
    FlatFilePos ret;
    if (nStatus & BLOCK_HAVE_UNDO) {
        ret.nFile = nFile;
        ret.nPos = nUndoPos;
    }
    return ret;
}

CBlockHeader CBlockIndex::GetBlockHeader() const
{
    CBlockHeader block;
    block.nVersion = nVersion;
    if (pprev) block.hashPrevBlock = pprev->GetBlockHash();
    block.hashMerkleRoot = hashMerkleRoot;
    block.nTime = nTime;
    block.nBits = nBits;
    block.nNonce = nNonce;
    if (nVersion > 3 && nVersion < 7) block.nAccumulatorCheckpoint = nAccumulatorCheckpoint;
    if (nVersion >= 8) block.hashFinalSaplingRoot = hashFinalSaplingRoot;
    return block;
}

int64_t CBlockIndex::MaxFutureBlockTime() const
{
    return GetAdjustedTime() + Params().GetConsensus().FutureBlockTimeDrift(nHeight+1);
}

int64_t CBlockIndex::MinPastBlockTime() const
{
    const Consensus::Params& consensus = Params().GetConsensus();
    // Time Protocol v1: pindexPrev->MedianTimePast + 1
    if (!consensus.IsTimeProtocolV2(nHeight+1))
        return GetMedianTimePast();

    // on the transition from Time Protocol v1 to v2
    // pindexPrev->nTime might be in the future (up to the allowed drift)
    // so we allow the nBlockTimeProtocolV2 (PIVX v4.0) to be at most (180-14) seconds earlier than previous block
    if (nHeight + 1 == consensus.vUpgrades[Consensus::UPGRADE_V4_0].nActivationHeight)
        return GetBlockTime() - consensus.FutureBlockTimeDrift(nHeight) + consensus.FutureBlockTimeDrift(nHeight + 1);

    // Time Protocol v2: pindexPrev->nTime
    return GetBlockTime();
}

enum { nMedianTimeSpan = 11 };

int64_t CBlockIndex::GetMedianTimePast() const
{
    int64_t pmedian[nMedianTimeSpan];
    int64_t* pbegin = &pmedian[nMedianTimeSpan];
    int64_t* pend = &pmedian[nMedianTimeSpan];

    const CBlockIndex* pindex = this;
    for (int i = 0; i < nMedianTimeSpan && pindex; i++, pindex = pindex->pprev)
        *(--pbegin) = pindex->GetBlockTime();

    std::sort(pbegin, pend);
    return pbegin[(pend - pbegin) / 2];
}

unsigned int CBlockIndex::GetEntropyBit() const
{
    unsigned int nEntropyBit = ((GetBlockHash().GetCheapHash()) & 1);
    return nEntropyBit;
}

bool CBlockIndex::SetEntropyBit(unsigned int nEntropyBit)
{
    if (nEntropyBit > 1)
        return false;
    nFlags |= (nEntropyBit ? BLOCK_ENTROPY : 0);
    return true;
}

// Sets V1 block modifier (uint64_t)
void CBlockIndex::SetBlockModifier(const uint64_t nBlockModifier, bool fGeneratedBlockModifier)
{
    vBlockModifier.clear();
    const size_t modSize = sizeof(nBlockModifier);
    vBlockModifier.resize(modSize);
    std::memcpy(vBlockModifier.data(), &nBlockModifier, modSize);
    if (fGeneratedBlockModifier)
        nFlags |= BLOCK_MODIFIER;

}

// Generates and sets new V1 block modifier - DISABLED
void CBlockIndex::SetNewBlockModifier()
{
    // V1 modifier not used, this function should never be called
    LogPrintf("%s : WARNING: V1 block modifier called (should not happen)\n", __func__);
    SetBlockModifier(uint64_t(0), false);
}

// Sets V2 block modifiers (uint256)
void CBlockIndex::SetBlockModifier(const uint256& nBlockModifier)
{
    vBlockModifier.clear();
    vBlockModifier.insert(vBlockModifier.begin(), nBlockModifier.begin(), nBlockModifier.end());
}

// Generates and sets new V2 block modifier
void CBlockIndex::SetNewBlockModifier(const uint256& prevoutId)
{
    // Shouldn't be called on V1 modifier's blocks (or before setting pprev)
    if (!Params().GetConsensus().NetworkUpgradeActive(nHeight, Consensus::UPGRADE_V3_4)) return;
    if (!pprev) throw std::runtime_error(strprintf("%s : ERROR: null pprev", __func__));

    // Generate Hash(prevoutId | prevModifier) - switch with genesis modifier (0) on upgrade block
    CHashWriter ss(SER_GETHASH, 0);
    ss << prevoutId;
    ss << pprev->GetBlockModifierV2();
    SetBlockModifier(ss.GetHash());
}

// Returns V1 block modifier (uint64_t)
uint64_t CBlockIndex::GetBlockModifierV1() const
{
    if (vBlockModifier.empty() || Params().GetConsensus().NetworkUpgradeActive(nHeight, Consensus::UPGRADE_V3_4))
        return 0;
    uint64_t nBlockModifier;
    std::memcpy(&nBlockModifier, vBlockModifier.data(), vBlockModifier.size());
    return nBlockModifier;
}

// Returns V2 block modifier (uint256)
uint256 CBlockIndex::GetBlockModifierV2() const
{
    if (vBlockModifier.empty() || !Params().GetConsensus().NetworkUpgradeActive(nHeight, Consensus::UPGRADE_V3_4))
        return UINT256_ZERO;
    uint256 nBlockModifier;
    std::memcpy(nBlockModifier.begin(), vBlockModifier.data(), vBlockModifier.size());
    return nBlockModifier;
}

void CBlockIndex::SetChainSaplingValue()
{
    // Sapling, update chain value
    if (pprev) {
        if (pprev->nChainSaplingValue) {
            nChainSaplingValue = *pprev->nChainSaplingValue + nSaplingValue;
        } else {
            nChainSaplingValue = nullopt;
        }
    } else {
        nChainSaplingValue = nSaplingValue;
    }
}

//! Check whether this block index entry is valid up to the passed validity level.
bool CBlockIndex::IsValid(enum BlockStatus nUpTo) const
{
    assert(!(nUpTo & ~BLOCK_VALID_MASK)); // Only validity flags allowed.
    if (nStatus & BLOCK_FAILED_MASK)
        return false;
    return ((nStatus & BLOCK_VALID_MASK) >= nUpTo);
}

//! Raise the validity level of this block index entry.
//! Returns true if the validity was changed.
bool CBlockIndex::RaiseValidity(enum BlockStatus nUpTo)
{
    assert(!(nUpTo & ~BLOCK_VALID_MASK)); // Only validity flags allowed.
    if (nStatus & BLOCK_FAILED_MASK)
        return false;
    if ((nStatus & BLOCK_VALID_MASK) < nUpTo) {
        nStatus = (nStatus & ~BLOCK_VALID_MASK) | nUpTo;
        return true;
    }
    return false;
}

/** Find the last common ancestor two blocks have.
 *  Both pa and pb must be non-nullptr. */
const CBlockIndex* LastCommonAncestor(const CBlockIndex* pa, const CBlockIndex* pb)
{
    if (pa->nHeight > pb->nHeight) {
        pa = pa->GetAncestor(pb->nHeight);
    } else if (pb->nHeight > pa->nHeight) {
        pb = pb->GetAncestor(pa->nHeight);
    }

    while (pa != pb && pa && pb) {
        pa = pa->pprev;
        pb = pb->pprev;
    }

    // Eventually all chain branches meet at the genesis block.
    assert(pa == pb);
    return pa;
}


