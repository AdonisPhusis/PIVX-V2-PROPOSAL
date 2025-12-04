// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2022 The PIVX Core developers
// Copyright (c) 2025 The PIVHU developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "masternode-payments.h"

#include "chainparams.h"
#include "evo/deterministicmns.h"
#include "fs.h"
#include "masternodeman.h"
#include "netmessagemaker.h"
#include "tiertwo/netfulfilledman.h"
#include "spork.h"
#include "sync.h"
#include "tiertwo/tiertwo_sync_state.h"
#include "util/system.h"
#include "utilmoneystr.h"
#include "validation.h"


/** Object for who's going to get paid on which blocks */
CMasternodePayments masternodePayments;

RecursiveMutex cs_vecPayments;
RecursiveMutex cs_mapMasternodeBlocks;
RecursiveMutex cs_mapMasternodePayeeVotes;

static const int MNPAYMENTS_DB_VERSION = 1;

//
// CMasternodePaymentDB
//

CMasternodePaymentDB::CMasternodePaymentDB()
{
    pathDB = GetDataDir() / "mnpayments.dat";
    strMagicMessage = "MasternodePayments";
}

bool CMasternodePaymentDB::Write(const CMasternodePayments& objToSave)
{
    return true;
}

CMasternodePaymentDB::ReadResult CMasternodePaymentDB::Read(CMasternodePayments& objToLoad)
{
    return Ok;
}

uint256 CMasternodePaymentWinner::GetHash() const
{
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << std::vector<unsigned char>(payee.begin(), payee.end());
    ss << nBlockHeight;
    ss << vinMasternode.prevout;
    return ss.GetHash();
}

std::string CMasternodePaymentWinner::GetStrMessage() const
{
    return vinMasternode.prevout.ToStringShort() + std::to_string(nBlockHeight) + HexStr(payee);
}

bool CMasternodePaymentWinner::IsValid(CNode* pnode, CValidationState& state, int chainHeight)
{
    return false;
}

void CMasternodePaymentWinner::Relay()
{
}

void DumpMasternodePayments()
{
}

bool IsBlockValueValid(int nHeight, CAmount& nExpectedValue, CAmount nMinted)
{
    // HU: No budget system - block value is always valid if <= expected
    return true;
}

bool IsBlockPayeeValid(const CBlock& block, const CBlockIndex* pindexPrev)
{
    return true;
}

void FillBlockPayee(CMutableTransaction& txCoinbase, const CBlockIndex* pindexPrev)
{
}

std::string GetRequiredPaymentsString(int nBlockHeight)
{
    return "";
}

bool CMasternodePayments::GetMasternodeTxOuts(const CBlockIndex* pindexPrev, std::vector<CTxOut>& voutMasternodePaymentsRet) const
{
    voutMasternodePaymentsRet.clear();

    if (!deterministicMNManager->LegacyMNObsolete(pindexPrev->nHeight + 1)) {
        return false;
    }

    CAmount masternodeReward = GetMasternodePayment(pindexPrev->nHeight + 1);
    if (masternodeReward <= 0) {
        return true;
    }

    auto dmnPayee = deterministicMNManager->GetListForBlock(pindexPrev).GetMNPayee();
    if (!dmnPayee) {
        return true;
    }

    CAmount operatorReward = 0;
    if (dmnPayee->nOperatorReward != 0 && !dmnPayee->pdmnState->scriptOperatorPayout.empty()) {
        operatorReward = (masternodeReward * dmnPayee->nOperatorReward) / 10000;
        masternodeReward -= operatorReward;
    }
    if (masternodeReward > 0) {
        voutMasternodePaymentsRet.emplace_back(masternodeReward, dmnPayee->pdmnState->scriptPayout);
    }
    if (operatorReward > 0) {
        voutMasternodePaymentsRet.emplace_back(operatorReward, dmnPayee->pdmnState->scriptOperatorPayout);
    }
    return true;
}

bool CMasternodePayments::GetLegacyMasternodeTxOut(int nHeight, std::vector<CTxOut>& voutMasternodePaymentsRet) const
{
    voutMasternodePaymentsRet.clear();
    return false;
}

void CMasternodePayments::FillBlockPayee(CMutableTransaction& txCoinbase, const CBlockIndex* pindexPrev) const
{
}

bool CMasternodePayments::ProcessMessageMasternodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv, CValidationState& state)
{
    return true;
}

bool CMasternodePayments::ProcessMNWinner(CMasternodePaymentWinner& winner, CNode* pfrom, CValidationState& state)
{
    return false;
}

bool CMasternodePayments::GetBlockPayee(int nBlockHeight, CScript& payee) const
{
    return false;
}

bool CMasternodePayments::IsScheduled(const CMasternode& mn, int nNotBlockHeight)
{
    return false;
}

void CMasternodePayments::AddWinningMasternode(CMasternodePaymentWinner& winnerIn)
{
}

bool CMasternodeBlockPayees::IsTransactionValid(const CTransaction& txNew, int nBlockHeight)
{
    return true;
}

std::string CMasternodeBlockPayees::GetRequiredPaymentsString()
{
    return "";
}

std::string CMasternodePayments::GetRequiredPaymentsString(int nBlockHeight)
{
    return "";
}

bool CMasternodePayments::IsTransactionValid(const CTransaction& txNew, const CBlockIndex* pindexPrev)
{
    const int nBlockHeight = pindexPrev->nHeight + 1;

    if (!deterministicMNManager->LegacyMNObsolete(nBlockHeight)) {
        return true;
    }

    std::vector<CTxOut> vecMnOuts;
    if (!GetMasternodeTxOuts(pindexPrev, vecMnOuts)) {
        return true;  // No masternode scheduled to be paid.
    }

    for (const CTxOut& o : vecMnOuts) {
        if (std::find(txNew.vout.begin(), txNew.vout.end(), o) == txNew.vout.end()) {
            CTxDestination mnDest;
            const std::string& payee = ExtractDestination(o.scriptPubKey, mnDest) ? EncodeDestination(mnDest)
                                                                                  : HexStr(o.scriptPubKey);
            LogPrint(BCLog::MASTERNODE, "%s: Failed to find expected payee %s in block at height %d (tx %s)\n",
                                        __func__, payee, pindexPrev->nHeight + 1, txNew.GetHash().ToString());
            return false;
        }
    }
    return true;
}

void CMasternodePayments::CleanPaymentList(int mnCount, int nHeight)
{
}

void CMasternodePayments::UpdatedBlockTip(const CBlockIndex *pindexNew, const CBlockIndex *pindexFork, bool fInitialDownload)
{
}

void CMasternodePayments::ProcessBlock(int nBlockHeight)
{
}

void CMasternodePayments::Sync(CNode* node, int nCountNeeded)
{
}

std::string CMasternodePayments::ToString() const
{
    return "DMN payments active";
}

bool CMasternodePayments::CanVote(const COutPoint& outMasternode, int nBlockHeight) const
{
    return false;
}

void CMasternodePayments::RecordWinnerVote(const COutPoint& outMasternode, int nBlockHeight)
{
}

bool IsCoinbaseValueValid(const CTransactionRef& tx, CValidationState& _state)
{
    // HU: No budget system - coinbase value is always valid
    return true;
}
