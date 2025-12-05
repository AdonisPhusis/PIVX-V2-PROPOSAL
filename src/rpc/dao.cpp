// Copyright (c) 2025 The PIVHU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "dao/dao_proposal.h"
#include "rpc/server.h"
#include "key_io.h"
#include "tinyformat.h"
#include "util/system.h"
#include "validation.h"
#include "evo/deterministicmns.h"
#include "activemasternode.h"

/**
 * PIVHU DAO RPC Commands
 *
 * - daosubmit: Submit a new proposal
 * - daovote: Vote on a proposal (YES/NO)
 * - daolist: List active proposals
 * - daoinfo: Get info about a specific proposal
 * - daostatus: Get current DAO cycle status
 */

static UniValue daosubmit(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 3)
        throw std::runtime_error(
            "daosubmit \"name\" \"address\" amount\n"
            "\nSubmit a DAO proposal requesting funds from Treasury T.\n"
            "\nArguments:\n"
            "1. \"name\"      (string, required) Proposal name (max 20 chars)\n"
            "2. \"address\"   (string, required) PIVHU address to receive funds\n"
            "3. amount        (numeric, required) Amount in PIVHU\n"
            "\nResult:\n"
            "{\n"
            "  \"hash\": \"xxx\",       (string) Proposal hash\n"
            "  \"name\": \"xxx\",       (string) Proposal name\n"
            "  \"cycle_start\": n,      (numeric) Cycle start height\n"
            "  \"cycle_end\": n         (numeric) Cycle end height\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("daosubmit", "\"Marketing Q1\" \"D1abc...\" 10000")
        );

    if (!g_daoManager) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "DAO manager not initialized");
    }

    std::string strName = request.params[0].get_str();
    CTxDestination dest = DecodeDestination(request.params[1].get_str());
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid PIVHU address");
    }

    CAmount nAmount = AmountFromValue(request.params[2]);
    if (nAmount < DAO_MIN_PROPOSAL_AMOUNT) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
            strprintf("Amount too small (min %d PIVHU)", DAO_MIN_PROPOSAL_AMOUNT / COIN));
    }

    // Get current height
    int nHeight;
    {
        LOCK(cs_main);
        nHeight = chainActive.Height();
    }

    // Create proposal
    CDAOProposal proposal(strName, "", nAmount, dest, nHeight);

    std::string strError;
    if (!g_daoManager->SubmitProposal(proposal, strError)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strError);
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("hash", proposal.hash.ToString());
    result.pushKV("name", proposal.strName);
    result.pushKV("amount", ValueFromAmount(proposal.nAmount));
    result.pushKV("cycle_start", (int)proposal.nCycleStart);
    result.pushKV("cycle_end", (int)proposal.GetCycleEnd());
    result.pushKV("payout_height", (int)(proposal.nCycleStart + GetDAOPayoutHeight() - 1));

    return result;
}

static UniValue daovote(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 2)
        throw std::runtime_error(
            "daovote \"proposal_hash\" \"yes|no\"\n"
            "\nVote on a DAO proposal. Requires running masternode.\n"
            "\nArguments:\n"
            "1. \"proposal_hash\"  (string, required) Proposal hash\n"
            "2. \"vote\"           (string, required) Vote: \"yes\" or \"no\"\n"
            "\nResult:\n"
            "{\n"
            "  \"result\": \"success\"\n"
            "}\n"
        );

    if (!g_daoManager) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "DAO manager not initialized");
    }

    // Check masternode
    if (!activeMasternodeManager) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "This is not a masternode");
    }

    uint256 proposalHash = ParseHashV(request.params[0], "proposal_hash");
    std::string strVote = request.params[1].get_str();

    DAOVote vote;
    if (strVote == "yes" || strVote == "YES") {
        vote = DAOVote::YES;
    } else if (strVote == "no" || strVote == "NO") {
        vote = DAOVote::NO;
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Vote must be 'yes' or 'no'");
    }

    // Get current height
    int nHeight;
    {
        LOCK(cs_main);
        nHeight = chainActive.Height();
    }

    CKey ecdsaKey;
    CDeterministicMNCPtr dmn;
    auto result = activeMasternodeManager->GetOperatorKey(ecdsaKey, dmn);
    if (!result) {
        throw JSONRPCError(RPC_INVALID_REQUEST, strprintf("Could not get masternode operator key: %s", result.getError()));
    }

    // PIVHU: Create and sign vote with ECDSA
    CDAOVote daoVote(proposalHash, dmn->proTxHash, vote, nHeight);
    if (!daoVote.Sign(ecdsaKey)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to sign vote");
    }

    std::string strError;
    if (!g_daoManager->CastVote(daoVote, strError)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strError);
    }

    UniValue resultObj(UniValue::VOBJ);
    resultObj.pushKV("result", "success");
    resultObj.pushKV("proposal", proposalHash.ToString());
    resultObj.pushKV("vote", strVote);
    return resultObj;
}

static UniValue daolist(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "daolist ( \"filter\" )\n"
            "\nList active DAO proposals for current cycle.\n"
            "\nArguments:\n"
            "1. \"filter\"  (string, optional) Filter: \"all\", \"active\", \"approved\"\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"hash\": \"xxx\",\n"
            "    \"name\": \"xxx\",\n"
            "    \"amount\": n,\n"
            "    \"yes_votes\": n,\n"
            "    \"no_votes\": n,\n"
            "    \"status\": \"voting|approved|rejected|paid\"\n"
            "  }\n"
            "]\n"
        );

    if (!g_daoManager) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "DAO manager not initialized");
    }

    int nHeight;
    {
        LOCK(cs_main);
        nHeight = chainActive.Height();
    }

    auto mnList = deterministicMNManager->GetListAtChainTip();
    int nTotalMNs = mnList.GetAllMNsCount();

    std::vector<CDAOProposal> proposals = g_daoManager->GetActiveProposals(nHeight);

    UniValue result(UniValue::VARR);
    for (const auto& prop : proposals) {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("hash", prop.hash.ToString());
        obj.pushKV("name", prop.strName);
        obj.pushKV("amount", ValueFromAmount(prop.nAmount));
        obj.pushKV("address", EncodeDestination(prop.paymentAddress));
        obj.pushKV("yes_votes", prop.nYesVotes);
        obj.pushKV("no_votes", prop.nNoVotes);

        std::string status;
        if (prop.fPaid) {
            status = "paid";
        } else if (prop.IsApproved(nTotalMNs)) {
            status = "approved";
        } else if (prop.IsVotingOpen(nHeight)) {
            status = "voting";
        } else {
            status = "rejected";
        }
        obj.pushKV("status", status);
        obj.pushKV("payout_height", (int)(prop.nCycleStart + DAO_PAYOUT_HEIGHT - 1));

        result.push_back(obj);
    }

    return result;
}

static UniValue daoinfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "daoinfo \"proposal_hash\"\n"
            "\nGet detailed info about a DAO proposal.\n"
            "\nArguments:\n"
            "1. \"proposal_hash\"  (string, required) Proposal hash\n"
        );

    if (!g_daoManager) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "DAO manager not initialized");
    }

    uint256 proposalHash = ParseHashV(request.params[0], "proposal_hash");

    CDAOProposal proposal;
    if (!g_daoManager->GetProposal(proposalHash, proposal)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Proposal not found");
    }

    int nHeight;
    {
        LOCK(cs_main);
        nHeight = chainActive.Height();
    }

    auto mnList = deterministicMNManager->GetListAtChainTip();
    int nTotalMNs = mnList.GetAllMNsCount();

    UniValue result(UniValue::VOBJ);
    result.pushKV("hash", proposal.hash.ToString());
    result.pushKV("name", proposal.strName);
    result.pushKV("description", proposal.strDescription);
    result.pushKV("amount", ValueFromAmount(proposal.nAmount));
    result.pushKV("address", EncodeDestination(proposal.paymentAddress));
    result.pushKV("submit_height", (int)proposal.nSubmitHeight);
    result.pushKV("cycle_start", (int)proposal.nCycleStart);
    result.pushKV("cycle_end", (int)proposal.GetCycleEnd());
    result.pushKV("payout_height", (int)(proposal.nCycleStart + GetDAOPayoutHeight() - 1));
    result.pushKV("yes_votes", proposal.nYesVotes);
    result.pushKV("no_votes", proposal.nNoVotes);
    result.pushKV("total_mns", nTotalMNs);
    result.pushKV("votes_needed", (nTotalMNs / 2) + 1);
    result.pushKV("is_approved", proposal.IsApproved(nTotalMNs));
    result.pushKV("is_paid", proposal.fPaid);
    result.pushKV("submission_open", proposal.IsSubmissionOpen(nHeight));
    result.pushKV("voting_open", proposal.IsVotingOpen(nHeight));

    return result;
}

static UniValue daostatus(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "daostatus\n"
            "\nGet current DAO cycle status.\n"
        );

    int nHeight;
    {
        LOCK(cs_main);
        nHeight = chainActive.Height();
    }

    uint32_t nCycleStart = CDAOManager::GetCycleStart(nHeight);
    uint32_t nOffset = nHeight - nCycleStart;

    // Use the new GetDAOPhase helper
    EDaoPhase phase = GetDAOPhase(nOffset);

    UniValue result(UniValue::VOBJ);
    result.pushKV("current_height", nHeight);
    result.pushKV("cycle_start", (int)nCycleStart);
    result.pushKV("cycle_end", (int)(nCycleStart + GetDAOCycleBlocks()));
    result.pushKV("cycle_offset", (int)nOffset);
    result.pushKV("phase", DAOPhaseToString(phase));
    result.pushKV("submission_ends", (int)(nCycleStart + GetDAOSubmissionEnd()));
    result.pushKV("study_ends", (int)(nCycleStart + GetDAOStudyEnd()));
    result.pushKV("voting_starts", (int)(nCycleStart + GetDAOVotingStart()));
    result.pushKV("voting_ends", (int)(nCycleStart + GetDAOVotingEnd()));
    result.pushKV("payout_height", (int)(nCycleStart + GetDAOPayoutHeight() - 1));

    return result;
}

// Register DAO RPC commands
static const CRPCCommand commands[] =
{   //  category      name           actor            okSafe    argNames
    { "dao",        "daosubmit",    &daosubmit,       false,    {"name", "address", "amount"} },
    { "dao",        "daovote",      &daovote,         false,    {"proposal_hash", "vote"} },
    { "dao",        "daolist",      &daolist,         true,     {"filter"} },
    { "dao",        "daoinfo",      &daoinfo,         true,     {"proposal_hash"} },
    { "dao",        "daostatus",    &daostatus,       true,     {} },
};

void RegisterDAORPCCommands(CRPCTable &t)
{
    for (unsigned int i = 0; i < ARRAYLEN(commands); i++) {
        t.appendCommand(commands[i].name, &commands[i]);
    }
}
