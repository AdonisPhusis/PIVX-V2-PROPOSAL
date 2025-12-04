// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2021 The Bitcoin Core developers
// Copyright (c) 2021 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include "blockassembler.h"

#include "activemasternode.h"
#include "amount.h"
#include "blocksignature.h"
#include "chain.h"
#include "chainparams.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "consensus/mn_validation.h"
#include "consensus/upgrades.h"
#include "consensus/validation.h"
#include "evo/blockproducer.h"
#include "masternode-payments.h"
#include "policy/policy.h"
#include "piv2_chainwork.h"
#include "primitives/transaction.h"
#include "spork.h"
#include "timedata.h"
#include "util/system.h"
#include "util/validation.h"
#include "validationinterface.h"

#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif

#include <algorithm>
#include <boost/thread.hpp>

// Unconfirmed transactions in the memory pool often depend on other
// transactions in the memory pool. When we select transactions from the
// pool, we select by highest priority or fee rate, so we might consider
// transactions that depend on transactions that aren't yet in the block.

uint64_t nLastBlockTx = 0;
uint64_t nLastBlockSize = 0;

int64_t UpdateTime(CBlockHeader* pblock, const Consensus::Params& consensusParams, const CBlockIndex* pindexPrev)
{
    int64_t nOldTime = pblock->nTime;
    int64_t nNewTime = std::max(pindexPrev->GetMedianTimePast()+1, GetAdjustedTime());

    if (nOldTime < nNewTime)
        pblock->nTime = nNewTime;

    // Update nBits for header compatibility
    pblock->nBits = GetBlockDifficultyBits(pindexPrev, pblock);

    return nNewTime - nOldTime;
}

static CMutableTransaction NewCoinbase(const int nHeight, const CScript* pScriptPubKey = nullptr)
{
    CMutableTransaction txCoinbase;
    txCoinbase.vout.emplace_back();
    txCoinbase.vout[0].SetEmpty();
    if (pScriptPubKey) txCoinbase.vout[0].scriptPubKey = *pScriptPubKey;
    txCoinbase.vin.emplace_back();
    txCoinbase.vin[0].scriptSig = CScript() << nHeight << OP_0;
    return txCoinbase;
}

CMutableTransaction CreateCoinbaseTx(const CScript& scriptPubKeyIn, CBlockIndex* pindexPrev)
{
    assert(pindexPrev);
    const int nHeight = pindexPrev->nHeight + 1;

    // Create coinbase tx
    CMutableTransaction txCoinbase = NewCoinbase(nHeight, &scriptPubKeyIn);

    // Masternode payments
    FillBlockPayee(txCoinbase, pindexPrev);

    // If no payee was detected, then the whole block value goes to the first output.
    if (txCoinbase.vout.size() == 1) {
        txCoinbase.vout[0].nValue = GetBlockValue(nHeight);
    }

    return txCoinbase;
}

bool CreateCoinbaseTx(CBlock* pblock, const CScript& scriptPubKeyIn, CBlockIndex* pindexPrev)
{
    pblock->vtx.emplace_back(MakeTransactionRef(CreateCoinbaseTx(scriptPubKeyIn, pindexPrev)));
    return true;
}

BlockAssembler::BlockAssembler(const CChainParams& _chainparams, const bool _defaultPrintPriority)
        : chainparams(_chainparams), defaultPrintPriority(_defaultPrintPriority)
{
    // Largest block you're willing to create:
    nBlockMaxSize = gArgs.GetArg("-blockmaxsize", DEFAULT_BLOCK_MAX_SIZE);
    // Limit to between 1K and MAX_BLOCK_SIZE-1K for sanity:
    nBlockMaxSize = std::max((unsigned int)1000, std::min((unsigned int)(MAX_BLOCK_SIZE_CURRENT - 1000), nBlockMaxSize));
}

void BlockAssembler::resetBlock()
{
    inBlock.clear();

    // Reserve space for coinbase tx
    nBlockSize = 1000;
    nBlockSigOps = 100;

    // These counters do not include coinbase tx
    nBlockTx = 0;
    nFees = 0;
}

std::unique_ptr<CBlockTemplate> BlockAssembler::CreateNewBlock(const CScript& scriptPubKeyIn,
                                               CWallet* pwallet,
                                               bool fMNBlock,
                                               void* availableCoins,
                                               bool fNoMempoolTx,
                                               bool fTestValidity,
                                               CBlockIndex* prevBlock,
                                               bool stopOnNewBlock,
                                               bool fIncludeQfc)
{
    resetBlock();

    pblocktemplate.reset(new CBlockTemplate());

    if(!pblocktemplate) return nullptr;
    pblock = &pblocktemplate->block; // pointer for convenience

    pblocktemplate->vTxFees.push_back(-1); // updated at end
    pblocktemplate->vTxSigOps.push_back(-1); // updated at end

    CBlockIndex* pindexPrev = prevBlock ? prevBlock : WITH_LOCK(cs_main, return chainActive.Tip());
    assert(pindexPrev);
    nHeight = pindexPrev->nHeight + 1;

    pblock->nVersion = ComputeBlockVersion(chainparams.GetConsensus(), nHeight);
    // -regtest only: allow overriding block.nVersion with
    // -blockversion=N to test forking scenarios
    if (Params().IsRegTestNet()) {
        pblock->nVersion = gArgs.GetArg("-blockversion", pblock->nVersion);
    }

    // MN-only consensus - always create coinbase
    (void)fMNBlock;  // All blocks are MN blocks
    (void)pwallet;
    (void)availableCoins;
    (void)stopOnNewBlock;
    if (!CreateCoinbaseTx(pblock, scriptPubKeyIn, pindexPrev)) {
        return nullptr;
    }

    (void)fIncludeQfc; // Suppress unused parameter warning

    if (!fNoMempoolTx) {
        // Add transactions from mempool
        LOCK2(cs_main,mempool.cs);
        addPackageTxs();
    }

    // Add fees to coinbase
    {
        CMutableTransaction txCoinbase(*pblock->vtx[0]);
        txCoinbase.vout[0].nValue += nFees;
        pblock->vtx[0] = MakeTransactionRef(txCoinbase);
        pblocktemplate->vTxFees[0] = -nFees;
    }

    nLastBlockTx = nBlockTx;
    nLastBlockSize = nBlockSize;
    LogPrintf("CreateNewBlock(): total size %u txs: %u fees: %ld sigops %d\n", nBlockSize, nBlockTx, nFees, nBlockSigOps);


    // Fill in header
    pblock->hashPrevBlock = pindexPrev->GetBlockHash();
    UpdateTime(pblock, chainparams.GetConsensus(), pindexPrev);
    pblock->nBits = GetBlockDifficultyBits(pindexPrev, pblock);
    pblock->nNonce = 0;
    pblocktemplate->vTxSigOps[0] = GetLegacySigOpCount(*(pblock->vtx[0]));
    appendSaplingTreeRoot();

    {
        LOCK(cs_main);
        if (prevBlock == nullptr && chainActive.Tip() != pindexPrev) return nullptr; // new block came in, move on

        CValidationState state;
        if (fTestValidity &&
            !TestBlockValidity(state, *pblock, pindexPrev, false, false, false)) {
            throw std::runtime_error(
                    strprintf("%s: TestBlockValidity failed: %s", __func__, FormatStateMessage(state)));
        }
    }

    return std::move(pblocktemplate);
}

void BlockAssembler::onlyUnconfirmed(CTxMemPool::setEntries& testSet)
{
    for (CTxMemPool::setEntries::iterator iit = testSet.begin(); iit != testSet.end(); ) {
        // Only test txs not already in the block
        if (inBlock.count(*iit)) {
            testSet.erase(iit++);
        }
        else {
            iit++;
        }
    }
}

bool BlockAssembler::TestPackage(uint64_t packageSize, unsigned int packageSigOps)
{
    if (nBlockSize + packageSize >= nBlockMaxSize)
        return false;
    if (nBlockSigOps + packageSigOps >= MAX_BLOCK_SIGOPS_CURRENT)
        return false;
    return true;
}

// Block size and sigops have already been tested.  Check that all transactions
// are final.
bool BlockAssembler::TestPackageFinality(const CTxMemPool::setEntries& package)
{
    for (const CTxMemPool::txiter& it : package) {
        if (!IsFinalTx(it->GetSharedTx(), nHeight))
            return false;
    }
    return true;
}

void BlockAssembler::AddToBlock(CTxMemPool::txiter iter)
{
    pblock->vtx.emplace_back(iter->GetSharedTx());
    pblocktemplate->vTxFees.push_back(iter->GetFee());
    pblocktemplate->vTxSigOps.push_back(iter->GetSigOpCount());
    nBlockSize += iter->GetTxSize();
    ++nBlockTx;
    nBlockSigOps += iter->GetSigOpCount();
    nFees += iter->GetFee();
    inBlock.insert(iter);

    bool fPrintPriority = gArgs.GetBoolArg("-printpriority", defaultPrintPriority);
    if (fPrintPriority) {
        LogPrintf("feerate %s txid %s\n",
                  CFeeRate(iter->GetModifiedFee(), iter->GetTxSize()).ToString(),
                  iter->GetTx().GetHash().ToString());
    }
}

void BlockAssembler::UpdatePackagesForAdded(const CTxMemPool::setEntries& alreadyAdded,
                                            indexed_modified_transaction_set& mapModifiedTx)
{
    for (const CTxMemPool::txiter& it : alreadyAdded) {
        CTxMemPool::setEntries descendants;
        mempool.CalculateDescendants(it, descendants);
        // Insert all descendants (not yet in block) into the modified set
        for (CTxMemPool::txiter desc : descendants) {
            if (alreadyAdded.count(desc))
                continue;
            modtxiter mit = mapModifiedTx.find(desc);
            if (mit == mapModifiedTx.end()) {
                CTxMemPoolModifiedEntry modEntry(desc);
                modEntry.nSizeWithAncestors -= it->GetTxSize();
                modEntry.nModFeesWithAncestors -= it->GetModifiedFee();
                modEntry.nSigOpCountWithAncestors -= it->GetSigOpCount();
                mapModifiedTx.insert(modEntry);
            } else {
                mapModifiedTx.modify(mit, update_for_parent_inclusion(it));
            }
        }
    }
}

// Skip entries in mapTx that are already in a block or are present
// in mapModifiedTx (which implies that the mapTx ancestor state is
// stale due to ancestor inclusion in the block)
// Also skip transactions that we've already failed to add. This can happen if
// we consider a transaction in mapModifiedTx and it fails: we can then
// potentially consider it again while walking mapTx.  It's currently
// guaranteed to fail again, but as a belt-and-suspenders check we put it in
// failedTx and avoid re-evaluation, since the re-evaluation would be using
// cached size/sigops/fee values that are not actually correct.
bool BlockAssembler::SkipMapTxEntry(CTxMemPool::txiter it, indexed_modified_transaction_set &mapModifiedTx, CTxMemPool::setEntries &failedTx)
{
    assert (it != mempool.mapTx.end());
    if (mapModifiedTx.count(it) || inBlock.count(it) || failedTx.count(it))
        return true;
    return false;
}

void BlockAssembler::SortForBlock(const CTxMemPool::setEntries& package, CTxMemPool::txiter entry, std::vector<CTxMemPool::txiter>& sortedEntries)
{
    // Sort package by ancestor count
    // If a transaction A depends on transaction B, then A's ancestor count
    // must be greater than B's.  So this is sufficient to validly order the
    // transactions for block inclusion.
    sortedEntries.clear();
    sortedEntries.insert(sortedEntries.begin(), package.begin(), package.end());
    std::sort(sortedEntries.begin(), sortedEntries.end(), CompareTxIterByAncestorCount());
}

// This transaction selection algorithm orders the mempool based
// on feerate of a transaction including all unconfirmed ancestors.
// Since we don't remove transactions from the mempool as we select them
// for block inclusion, we need an alternate method of updating the feerate
// of a transaction with its not-yet-selected ancestors as we go.
// This is accomplished by walking the in-mempool descendants of selected
// transactions and storing a temporary modified state in mapModifiedTxs.
// Each time through the loop, we compare the best transaction in
// mapModifiedTxs with the next transaction in the mempool to decide what
// transaction package to work on next.
void BlockAssembler::addPackageTxs()
{
    // mapModifiedTx will store sorted packages after they are modified
    // because some of their txs are already in the block
    indexed_modified_transaction_set mapModifiedTx;
    // Keep track of entries that failed inclusion, to avoid duplicate work
    CTxMemPool::setEntries failedTx;

    // Start by adding all descendants of previously added txs to mapModifiedTx
    // and modifying them for their already included ancestors
    UpdatePackagesForAdded(inBlock, mapModifiedTx);

    LogPrint(BCLog::HU, "BlockAssembler::addPackageTxs - mempool size=%u\n", mempool.size());

    CTxMemPool::indexed_transaction_set::index<ancestor_score>::type::iterator mi = mempool.mapTx.get<ancestor_score>().begin();
    CTxMemPool::txiter iter;
    while (mi != mempool.mapTx.get<ancestor_score>().end() || !mapModifiedTx.empty())
    {
        // First try to find a new transaction in mapTx to evaluate.
        if (mi != mempool.mapTx.get<ancestor_score>().end() &&
                SkipMapTxEntry(mempool.mapTx.project<0>(mi), mapModifiedTx, failedTx)) {
            ++mi;
            continue;
        }

        // Now that mi is not stale, determine which transaction to evaluate:
        // the next entry from mapTx, or the best from mapModifiedTx?
        bool fUsingModified = false;

        modtxscoreiter modit = mapModifiedTx.get<ancestor_score>().begin();
        if (mi == mempool.mapTx.get<ancestor_score>().end()) {
            // We're out of entries in mapTx; use the entry from mapModifiedTx
            iter = modit->iter;
            fUsingModified = true;
        } else {
            // Try to compare the mapTx entry to the mapModifiedTx entry
            iter = mempool.mapTx.project<0>(mi);
            if (modit != mapModifiedTx.get<ancestor_score>().end() &&
                    CompareModifiedEntry()(*modit, CTxMemPoolModifiedEntry(iter))) {
                // The best entry in mapModifiedTx has higher score
                // than the one from mapTx.
                // Switch which transaction (package) to consider
                iter = modit->iter;
                fUsingModified = true;
            } else {
                // Either no entry in mapModifiedTx, or it's worse than mapTx.
                // Increment mi for the next loop iteration.
                ++mi;
            }
        }

        // We skip mapTx entries that are inBlock, and mapModifiedTx shouldn't
        // contain anything that is inBlock.
        assert(!inBlock.count(iter));

        uint64_t packageSize = iter->GetSizeWithAncestors();
        CAmount packageFees = iter->GetModFeesWithAncestors();
        unsigned int packageSigOps = iter->GetSigOpCountWithAncestors();
        if (fUsingModified) {
            packageSize = modit->nSizeWithAncestors;
            packageFees = modit->nModFeesWithAncestors;
            packageSigOps = modit->nSigOpCountWithAncestors;
        }

        const CTransaction& txCheck = iter->GetTx();
        LogPrint(BCLog::HU, "BlockAssembler: Evaluating tx %s type=%d size=%lu fees=%lld\n",
                 txCheck.GetHash().ToString().substr(0, 16), (int)txCheck.nType, packageSize, packageFees);

        CAmount minFee = ::minRelayTxFee.GetFee(packageSize);
        if (packageFees < minFee) {
            LogPrint(BCLog::HU, "BlockAssembler: SKIP tx %s - low fees (%lld < %lld)\n",
                     txCheck.GetHash().ToString().substr(0, 16), packageFees, minFee);
            // Everything else we might consider has a lower fee rate
            return;
        }

        if (!TestPackage(packageSize, packageSigOps)) {
            LogPrint(BCLog::HU, "BlockAssembler: SKIP tx %s - failed TestPackage\n",
                     txCheck.GetHash().ToString().substr(0, 16));
            if (fUsingModified) {
                // Since we always look at the best entry in mapModifiedTx,
                // we must erase failed entries so that we can consider the
                // next best entry on the next loop iteration
                mapModifiedTx.get<ancestor_score>().erase(modit);
                failedTx.insert(iter);
            }
            continue;
        }

        CTxMemPool::setEntries ancestors;
        uint64_t nNoLimit = std::numeric_limits<uint64_t>::max();
        std::string dummy;
        mempool.CalculateMemPoolAncestors(*iter, ancestors, nNoLimit, nNoLimit, nNoLimit, nNoLimit, dummy, false);

        onlyUnconfirmed(ancestors);
        ancestors.insert(iter);

        // Test if all tx's are Final
        if (!TestPackageFinality(ancestors)) {
            LogPrint(BCLog::HU, "BlockAssembler: SKIP tx %s - failed TestPackageFinality\n",
                     txCheck.GetHash().ToString().substr(0, 16));
            if (fUsingModified) {
                mapModifiedTx.get<ancestor_score>().erase(modit);
                failedTx.insert(iter);
            }
            continue;
        }

        LogPrint(BCLog::HU, "BlockAssembler: tx %s PASSED all checks, adding package\n",
                 txCheck.GetHash().ToString().substr(0, 16));

        // Package can be added. Sort the entries in a valid order.
        std::vector<CTxMemPool::txiter> sortedEntries;
        SortForBlock(ancestors, iter, sortedEntries);

        for (size_t i = 0; i < sortedEntries.size(); ++i) {
            CTxMemPool::txiter& iterSortedEntries = sortedEntries[i];
            const CTransaction& tx = iterSortedEntries->GetTx();
            bool isShielded = iterSortedEntries->IsShielded();

            // KHU_LOCK and KHU_UNLOCK use Sapling for privacy but are NOT regular Sapling transfers
            // They should NOT be blocked by SPORK_20_SAPLING_MAINTENANCE or shielded size limits
            bool isKHUSaplingType = (tx.nType == CTransaction::TxType::KHU_LOCK ||
                                     tx.nType == CTransaction::TxType::KHU_UNLOCK);

            LogPrint(BCLog::HU, "BlockAssembler: Considering tx %s (type=%d, isShielded=%d, isKHUSapling=%d)\n",
                     tx.GetHash().ToString().substr(0, 16), (int)tx.nType, isShielded, isKHUSaplingType);

            // Apply Sapling restrictions only to regular shielded txes, NOT to KHU Sapling types
            if (isShielded && !isKHUSaplingType) {
                // Don't add SHIELD transactions if in maintenance (SPORK_20)
                bool spork20Active = sporkManager.IsSporkActive(SPORK_20_SAPLING_MAINTENANCE);
                LogPrint(BCLog::HU, "BlockAssembler: Regular shielded tx - SPORK_20 active=%d\n", spork20Active);
                if (spork20Active) {
                    LogPrint(BCLog::HU, "BlockAssembler: SKIPPING shielded tx due to SPORK_20\n");
                    break;
                }
                // Don't add SHIELD transactions if there's no reserved space left in the block
                unsigned int txSize = iterSortedEntries->GetTxSize();
                LogPrint(BCLog::HU, "BlockAssembler: Shielded size check: current=%u + tx=%u = %u vs max=%u\n",
                         nSizeShielded, txSize, nSizeShielded + txSize, MAX_BLOCK_SHIELDED_TXES_SIZE);
                if (nSizeShielded + txSize > MAX_BLOCK_SHIELDED_TXES_SIZE) {
                    LogPrint(BCLog::HU, "BlockAssembler: SKIPPING shielded tx due to size limit\n");
                    break;
                }
                // Update cumulative size of SHIELD transactions in this block
                nSizeShielded += txSize;
            } else if (isKHUSaplingType) {
                LogPrint(BCLog::HU, "BlockAssembler: KHU Sapling tx - EXEMPT from Sapling restrictions\n");
            }

            LogPrint(BCLog::HU, "BlockAssembler: Adding tx %s to block\n", tx.GetHash().ToString().substr(0, 16));
            AddToBlock(iterSortedEntries);
            // Erase from the modified set, if present
            mapModifiedTx.erase(iterSortedEntries);
        }

        // Update transactions that depend on each of these
        UpdatePackagesForAdded(ancestors, mapModifiedTx);
    }
}

void BlockAssembler::appendSaplingTreeRoot()
{
    // Update header
    pblock->hashFinalSaplingRoot = CalculateSaplingTreeRoot(pblock, nHeight, chainparams);
}

uint256 CalculateSaplingTreeRoot(CBlock* pblock, int nHeight, const CChainParams& chainparams)
{
    if (NetworkUpgradeActive(nHeight, chainparams.GetConsensus(), Consensus::UPGRADE_V5_0)) {
        SaplingMerkleTree sapling_tree;
        uint256 saplingAnchor = pcoinsTip->GetBestAnchor();
        if (!pcoinsTip->GetSaplingAnchorAt(saplingAnchor, sapling_tree)) {
            // Anchor not found - use empty tree
            LogPrintf("%s: Sapling anchor %s not found, using empty tree\n", __func__, saplingAnchor.ToString());
            sapling_tree = SaplingMerkleTree();
        }

        // Update the Sapling commitment tree.
        for (const auto &tx : pblock->vtx) {
            if (tx->IsShieldedTx()) {
                for (const OutputDescription &odesc : tx->sapData->vShieldedOutput) {
                    sapling_tree.append(odesc.cmu);
                }
            }
        }
        return sapling_tree.root();
    }
    return UINT256_ZERO;
}

bool SolveBlock(std::shared_ptr<CBlock>& pblock, int nHeight)
{
    unsigned int extraNonce = 0;
    IncrementExtraNonce(pblock, nHeight, extraNonce);

    pblock->nNonce = GetRand(std::numeric_limits<uint32_t>::max());
    LogPrint(BCLog::MASTERNODE, "SolveBlock: MN-only mode, height=%d\n", nHeight);
    return true;
}

void IncrementExtraNonce(std::shared_ptr<CBlock>& pblock, int nHeight, unsigned int& nExtraNonce)
{
    // Update nExtraNonce
    static uint256 hashPrevBlock;
    if (hashPrevBlock != pblock->hashPrevBlock) {
        nExtraNonce = 0;
        hashPrevBlock = pblock->hashPrevBlock;
    }
    ++nExtraNonce;
    CMutableTransaction txCoinbase(*pblock->vtx[0]);
    txCoinbase.vin[0].scriptSig = (CScript() << nHeight << CScriptNum(nExtraNonce)) + COINBASE_FLAGS;
    assert(txCoinbase.vin[0].scriptSig.size() <= 100);

    pblock->vtx[0] = MakeTransactionRef(txCoinbase);
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
}

int32_t ComputeBlockVersion(const Consensus::Params& consensus, int nHeight)
{
    if (NetworkUpgradeActive(nHeight, consensus, Consensus::UPGRADE_V5_0)) {
        return CBlockHeader::CURRENT_VERSION;       // v11 (since 5.2.99)
    } else if (consensus.NetworkUpgradeActive(nHeight, Consensus::UPGRADE_V4_0)) {
        return 7;
    } else if (consensus.NetworkUpgradeActive(nHeight, Consensus::UPGRADE_V3_4)) {
        return 6;
    } else if (consensus.NetworkUpgradeActive(nHeight, Consensus::UPGRADE_BIP65)) {
        return 5;
    } else {
        return 3;
    }
}

// MN-only block production functions

bool SignBlockWithMN(CBlock& block)
{
    if (!activeMasternodeManager || !activeMasternodeManager->IsReady()) {
        LogPrintf("%s: Active masternode not ready\n", __func__);
        return false;
    }

    CKey ecdsaKey;
    CDeterministicMNCPtr dmn;
    auto result = activeMasternodeManager->GetOperatorKey(ecdsaKey, dmn);
    if (!result) {
        LogPrintf("%s: Failed to get operator key: %s\n", __func__, result.getError());
        return false;
    }

    // PIVHU: Sign the block with ECDSA
    return mn_consensus::SignBlockMNOnly(block, ecdsaKey);
}

std::unique_ptr<CBlockTemplate> CreateMNOnlyBlock(
    const CScript& scriptPubKeyIn,
    CBlockIndex* prevBlock,
    bool fNoMempoolTx,
    bool fTestValidity,
    bool fIncludeQfc)
{
    // Check if this node is the expected block producer
    CBlockIndex* pindexPrev = prevBlock ? prevBlock : WITH_LOCK(cs_main, return chainActive.Tip());
    if (!pindexPrev) {
        LogPrintf("%s: No previous block\n", __func__);
        return nullptr;
    }

    // Verify this node is the expected producer
    if (!IsLocalMNBlockProducer(pindexPrev)) {
        LogPrint(BCLog::MASTERNODE, "%s: This node is not the expected block producer\n", __func__);
        return nullptr;
    }

    // Use BlockAssembler to create the block template
    BlockAssembler assembler(Params(), false);
    auto pblocktemplate = assembler.CreateNewBlock(
        scriptPubKeyIn,
        nullptr,        // no wallet needed
        false,          // MN-only block
        nullptr,        // no available coins
        fNoMempoolTx,
        false,          // don't test validity yet (we'll sign first)
        prevBlock,
        true,
        fIncludeQfc
    );

    if (!pblocktemplate) {
        LogPrintf("%s: CreateNewBlock failed\n", __func__);
        return nullptr;
    }

    CBlock& block = pblocktemplate->block;

    // Finalize the block (merkle root, etc.)
    block.hashMerkleRoot = BlockMerkleRoot(block);

    // NOTE: Block is NOT signed here - caller must sign AFTER SolveBlock (which changes nonce/hash)
    // The signature is done in generateBlocks() after SolveBlock()

    LogPrint(BCLog::MASTERNODE, "%s: Created MN-only block template at height %d (unsigned)\n",
              __func__, pindexPrev->nHeight + 1);

    return pblocktemplate;
}

