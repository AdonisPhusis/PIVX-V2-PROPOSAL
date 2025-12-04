// Copyright (c) 2025 The PIVHU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "dao/dao_proposal.h"
#include "hash.h"
#include "logging.h"
#include "evo/deterministicmns.h"
#include "validation.h"
#include "util/system.h"

#include <algorithm>

// Global DAO manager instance
std::unique_ptr<CDAOManager> g_daoManager;

//
// CDAOProposal
//

CDAOProposal::CDAOProposal(const std::string& name, const std::string& desc,
                           CAmount amount, const CTxDestination& addr, uint32_t height)
    : strName(name)
    , strDescription(desc)
    , nAmount(amount)
    , paymentAddress(addr)
    , nSubmitHeight(height)
    , nCycleStart(CDAOManager::GetCycleStart(height))
{
    hash = GetHash();
}

uint256 CDAOProposal::GetHash() const
{
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << strName;
    ss << strDescription;
    ss << nAmount;
    ss << GetScriptForDestination(paymentAddress);
    ss << nCycleStart;
    return ss.GetHash();
}

bool CDAOProposal::IsValid(std::string& strError) const
{
    if (strName.empty() || strName.size() > 20) {
        strError = "Invalid proposal name (1-20 chars)";
        return false;
    }

    if (strDescription.size() > 200) {
        strError = "Description too long (max 200 chars)";
        return false;
    }

    if (nAmount < DAO_MIN_PROPOSAL_AMOUNT) {
        strError = strprintf("Amount too small (min %d PIVHU)", DAO_MIN_PROPOSAL_AMOUNT / COIN);
        return false;
    }

    if (nAmount > DAO_MAX_PROPOSAL_AMOUNT) {
        strError = strprintf("Amount too large (max %d PIVHU)", DAO_MAX_PROPOSAL_AMOUNT / COIN);
        return false;
    }

    if (!IsValidDestination(paymentAddress)) {
        strError = "Invalid payment address";
        return false;
    }

    return true;
}

bool CDAOProposal::IsApproved(int nTotalMNs) const
{
    // Need >50% of total MNs voting YES
    // Abstentions count against (encourages participation)
    int nRequired = (nTotalMNs / 2) + 1;
    return nYesVotes >= nRequired && nYesVotes > nNoVotes;
}

bool CDAOProposal::IsSubmissionOpen(uint32_t nHeight) const
{
    if (nHeight < nCycleStart) return false;
    uint32_t nOffset = nHeight - nCycleStart;
    return nOffset < GetDAOSubmissionEnd();  // Submit window
}

bool CDAOProposal::IsVotingOpen(uint32_t nHeight) const
{
    if (nHeight < nCycleStart) return false;
    uint32_t nOffset = nHeight - nCycleStart;
    return nOffset >= GetDAOVotingStart() && nOffset < GetDAOVotingEnd();  // Vote window
}

bool CDAOProposal::IsPayoutHeight(uint32_t nHeight) const
{
    if (nHeight < nCycleStart) return false;
    uint32_t nOffset = nHeight - nCycleStart;
    return nOffset == GetDAOPayoutHeight() - 1;  // Last block of cycle
}

//
// CDAOVote
//

uint256 CDAOVote::GetSignHash() const
{
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << proposalHash;
    ss << proTxHash;
    ss << static_cast<uint8_t>(vote);
    ss << nHeight;
    return ss.GetHash();
}

bool CDAOVote::CheckSignature(const CPubKey& pubKey) const
{
    uint256 signHash = GetSignHash();
    return pubKey.Verify(signHash, vchSig);
}

bool CDAOVote::Sign(const CKey& secretKey)
{
    uint256 signHash = GetSignHash();
    if (!secretKey.Sign(signHash, vchSig)) {
        return false;
    }
    return true;
}

//
// CDAOManager
//

uint32_t CDAOManager::GetCycleStart(uint32_t nHeight)
{
    // Cycles start at height 0, then every GetDAOCycleBlocks()
    uint32_t cycleBlocks = GetDAOCycleBlocks();
    return (nHeight / cycleBlocks) * cycleBlocks;
}

bool CDAOManager::SubmitProposal(const CDAOProposal& proposal, std::string& strError)
{
    LOCK(cs_dao);

    // Validate proposal
    if (!proposal.IsValid(strError)) {
        return false;
    }

    // Check submission window
    uint32_t nCycleStart = GetCycleStart(proposal.nSubmitHeight);
    uint32_t nOffset = proposal.nSubmitHeight - nCycleStart;
    if (nOffset >= GetDAOSubmissionEnd()) {
        strError = "Submission window closed for this cycle";
        return false;
    }

    // Check for duplicate
    if (mapProposals.count(proposal.hash)) {
        strError = "Proposal already exists";
        return false;
    }

    // Add proposal
    mapProposals[proposal.hash] = proposal;

    LogPrint(BCLog::MASTERNODE, "CDAOManager::SubmitProposal: %s (%s) for %d PIVHU\n",
             proposal.strName, proposal.hash.ToString(), proposal.nAmount / COIN);

    return true;
}

bool CDAOManager::CastVote(const CDAOVote& vote, std::string& strError)
{
    LOCK(cs_dao);

    // Check proposal exists
    auto it = mapProposals.find(vote.proposalHash);
    if (it == mapProposals.end()) {
        strError = "Proposal not found";
        return false;
    }

    // Check voting is open
    if (!it->second.IsVotingOpen(vote.nHeight)) {
        strError = "Voting not open for this proposal";
        return false;
    }

    // Verify MN exists and get pubkey
    auto mnList = deterministicMNManager->GetListAtChainTip();
    auto dmn = mnList.GetMN(vote.proTxHash);
    if (!dmn) {
        strError = "Masternode not found";
        return false;
    }

    // PIVHU: Verify ECDSA signature
    if (!vote.CheckSignature(dmn->pdmnState->pubKeyOperator)) {
        strError = "Invalid vote signature";
        return false;
    }

    // Store vote (overwrites previous vote from same MN)
    mapVotes[vote.proposalHash][vote.proTxHash] = vote;

    LogPrint(BCLog::MASTERNODE, "CDAOManager::CastVote: MN %s voted %s on %s\n",
             vote.proTxHash.ToString().substr(0, 8),
             vote.vote == DAOVote::YES ? "YES" : (vote.vote == DAOVote::NO ? "NO" : "ABSTAIN"),
             vote.proposalHash.ToString().substr(0, 8));

    return true;
}

bool CDAOManager::GetProposal(const uint256& hash, CDAOProposal& proposal) const
{
    LOCK(cs_dao);
    auto it = mapProposals.find(hash);
    if (it == mapProposals.end()) {
        return false;
    }
    proposal = it->second;

    // Count current votes
    int nYes = 0, nNo = 0;
    CountVotes(hash, nYes, nNo);
    proposal.nYesVotes = nYes;
    proposal.nNoVotes = nNo;
    proposal.fPaid = setExecuted.count(hash) > 0;

    return true;
}

std::vector<CDAOProposal> CDAOManager::GetActiveProposals(uint32_t nHeight) const
{
    LOCK(cs_dao);
    std::vector<CDAOProposal> result;
    uint32_t nCycleStart = GetCycleStart(nHeight);

    for (const auto& pair : mapProposals) {
        if (pair.second.nCycleStart == nCycleStart) {
            CDAOProposal prop = pair.second;

            // Count votes
            int nYes = 0, nNo = 0;
            CountVotes(prop.hash, nYes, nNo);
            prop.nYesVotes = nYes;
            prop.nNoVotes = nNo;
            prop.fPaid = setExecuted.count(prop.hash) > 0;

            result.push_back(prop);
        }
    }

    return result;
}

void CDAOManager::CountVotes(const uint256& proposalHash, int& nYes, int& nNo) const
{
    // Note: cs_dao should already be held
    nYes = 0;
    nNo = 0;

    auto it = mapVotes.find(proposalHash);
    if (it == mapVotes.end()) {
        return;
    }

    // Only count votes from currently valid MNs
    auto mnList = deterministicMNManager->GetListAtChainTip();

    for (const auto& votePair : it->second) {
        // Check MN still exists
        if (!mnList.GetMN(votePair.first)) {
            continue;
        }

        if (votePair.second.vote == DAOVote::YES) {
            nYes++;
        } else if (votePair.second.vote == DAOVote::NO) {
            nNo++;
        }
    }
}

bool CDAOManager::ExecutePayouts(int nHeight, CAmount nTreasuryBalance,
                                  std::vector<std::pair<CTxDestination, CAmount>>& payouts)
{
    LOCK(cs_dao);
    payouts.clear();

    uint32_t nCycleStart = GetCycleStart(nHeight);

    // Get total MN count for approval threshold
    auto mnList = deterministicMNManager->GetListAtChainTip();
    int nTotalMNs = mnList.GetAllMNsCount();

    if (nTotalMNs == 0) {
        return true; // No MNs, no payouts
    }

    // Collect approved proposals sorted by votes (highest first)
    std::vector<CDAOProposal> approved;

    for (auto& pair : mapProposals) {
        CDAOProposal& prop = pair.second;

        // Only this cycle's proposals
        if (prop.nCycleStart != nCycleStart) continue;

        // Only at payout height (day 30)
        if (!prop.IsPayoutHeight(nHeight)) continue;

        // Already paid?
        if (setExecuted.count(prop.hash)) continue;

        // Count votes
        int nYes = 0, nNo = 0;
        CountVotes(prop.hash, nYes, nNo);
        prop.nYesVotes = nYes;
        prop.nNoVotes = nNo;

        // Check approval
        if (prop.IsApproved(nTotalMNs)) {
            approved.push_back(prop);
        }
    }

    // Sort by YES votes (descending) - prioritize most popular proposals
    std::sort(approved.begin(), approved.end(),
              [](const CDAOProposal& a, const CDAOProposal& b) {
                  return a.nYesVotes > b.nYesVotes;
              });

    // Execute payouts while treasury has funds
    CAmount nRemaining = nTreasuryBalance;

    for (const auto& prop : approved) {
        if (prop.nAmount <= nRemaining) {
            payouts.push_back({prop.paymentAddress, prop.nAmount});
            nRemaining -= prop.nAmount;
            setExecuted.insert(prop.hash);

            LogPrintf("CDAOManager: Approved payout %s (%s) for %d PIVHU\n",
                      prop.strName, prop.hash.ToString().substr(0, 8), prop.nAmount / COIN);
        } else {
            LogPrintf("CDAOManager: Insufficient treasury for %s (need %d, have %d)\n",
                      prop.strName, prop.nAmount / COIN, nRemaining / COIN);
        }
    }

    return true;
}

void CDAOManager::CleanOldProposals(uint32_t nHeight)
{
    LOCK(cs_dao);

    uint32_t nCycleStart = GetCycleStart(nHeight);

    // Remove proposals from 2+ cycles ago
    uint32_t cycleBlocks = GetDAOCycleBlocks();
    for (auto it = mapProposals.begin(); it != mapProposals.end(); ) {
        if (it->second.nCycleStart + (2 * cycleBlocks) < nCycleStart) {
            mapVotes.erase(it->first);
            it = mapProposals.erase(it);
        } else {
            ++it;
        }
    }
}

bool CDAOManager::Load()
{
    // TODO: Load from LevelDB
    return true;
}

bool CDAOManager::Save() const
{
    // TODO: Save to LevelDB
    return true;
}
