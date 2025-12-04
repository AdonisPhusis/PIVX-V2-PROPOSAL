// Copyright (c) 2025 The PIVHU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PIVHU_DAO_PROPOSAL_H
#define PIVHU_DAO_PROPOSAL_H

#include "serialize.h"
#include "uint256.h"
#include "amount.h"
#include "script/standard.h"
#include "sync.h"

#include <string>
#include <map>
#include <memory>
#include <set>

/**
 * PIVHU DAO Monthly Proposal System
 *
 * - Proposals request PIV from Treasury T
 * - Monthly voting cycle (43200 blocks = 30 days)
 * - Masternodes vote YES/NO
 * - If majority YES and T >= amount, payout executed
 */

// DAO timing - uses consensus params from chainparams.cpp
// Mainnet:  43200 cycle (30d), windows 14400/14400/14400 (10d each)
// Testnet:  1440 cycle (1d), windows 480/480/480 (8h each)
// Regtest:  30 cycle (~30min), windows 10/10/10

// Forward declaration for Params() access
#include "chainparams.h"

// Helper functions to get DAO timing from consensus params
inline uint32_t GetDAOCycleBlocks() { return Params().GetConsensus().nDAOCycleBlocks; }
inline uint32_t GetDAOSubmitWindow() { return Params().GetConsensus().nDAOSubmitWindow; }
inline uint32_t GetDAOStudyWindow() { return Params().GetConsensus().nDAOStudyWindow; }
inline uint32_t GetDAOVoteWindow() { return Params().GetConsensus().nDAOVoteWindow; }

// DAO Phase enum
enum class EDaoPhase : uint8_t {
    DAO_SUBMIT = 0,   // Proposal submission window
    DAO_STUDY = 1,    // Community study/discussion window
    DAO_VOTE = 2      // Masternode voting window
};

// Derived timing offsets (within a cycle)
inline uint32_t GetDAOSubmissionEnd() { return GetDAOSubmitWindow(); }
inline uint32_t GetDAOStudyEnd() { return GetDAOSubmitWindow() + GetDAOStudyWindow(); }
inline uint32_t GetDAOVotingStart() { return GetDAOSubmitWindow() + GetDAOStudyWindow(); }
inline uint32_t GetDAOVotingEnd() { return GetDAOSubmitWindow() + GetDAOStudyWindow() + GetDAOVoteWindow(); }
inline uint32_t GetDAOPayoutHeight() { return GetDAOCycleBlocks(); }

// Get current DAO phase based on cycle offset
inline EDaoPhase GetDAOPhase(uint32_t nCycleOffset) {
    if (nCycleOffset < GetDAOSubmissionEnd()) {
        return EDaoPhase::DAO_SUBMIT;
    } else if (nCycleOffset < GetDAOStudyEnd()) {
        return EDaoPhase::DAO_STUDY;
    } else {
        return EDaoPhase::DAO_VOTE;
    }
}

// Convert phase to string
inline const char* DAOPhaseToString(EDaoPhase phase) {
    switch (phase) {
        case EDaoPhase::DAO_SUBMIT: return "submission";
        case EDaoPhase::DAO_STUDY:  return "study";
        case EDaoPhase::DAO_VOTE:   return "voting";
    }
    return "unknown";
}

// DAO timing constants (mainnet defaults)
static const uint32_t DAO_CYCLE_BLOCKS = 43200;
static const uint32_t DAO_SUBMISSION_END = 10080;         // Mainnet default
static const uint32_t DAO_VOTING_START = 10080;           // Mainnet default
static const uint32_t DAO_VOTING_END = 41760;             // Mainnet default
static const uint32_t DAO_PAYOUT_HEIGHT = 43200;          // Mainnet default

// Proposal limits
static const CAmount DAO_MIN_PROPOSAL_AMOUNT = 100 * COIN;      // Min 100 PIVHU
static const CAmount DAO_MAX_PROPOSAL_AMOUNT = 1000000 * COIN;  // Max 1M PIVHU
static const CAmount DAO_PROPOSAL_FEE = 50 * COIN;              // 50 PIVHU fee (burned)

// Vote types
enum class DAOVote : uint8_t {
    ABSTAIN = 0,
    YES = 1,
    NO = 2
};

/**
 * CDAOProposal - A proposal requesting funds from Treasury T
 */
class CDAOProposal
{
public:
    uint256 hash;                      // Proposal hash (computed from data)
    std::string strName;               // Short name (max 20 chars)
    std::string strDescription;        // Description/URL (max 200 chars)
    CAmount nAmount;                   // Requested amount in satoshis
    CTxDestination paymentAddress;     // Destination for payout
    uint32_t nSubmitHeight;            // Block height when submitted
    uint32_t nCycleStart;              // Start of the voting cycle

    // Voting state (not serialized on-chain, computed from votes)
    int nYesVotes{0};
    int nNoVotes{0};
    bool fPaid{false};
    uint256 txidPayout;                // Txid if paid

    CDAOProposal() : nAmount(0), nSubmitHeight(0), nCycleStart(0) {}

    CDAOProposal(const std::string& name, const std::string& desc,
                 CAmount amount, const CTxDestination& addr, uint32_t height);

    // Compute proposal hash
    uint256 GetHash() const;

    // Check if proposal is valid
    bool IsValid(std::string& strError) const;

    // Get vote count needed for approval (>50% of voting MNs)
    bool IsApproved(int nTotalMNs) const;

    // Get cycle end height
    uint32_t GetCycleEnd() const { return nCycleStart + GetDAOCycleBlocks(); }

    // Check if submission open at given height (days 1-7)
    bool IsSubmissionOpen(uint32_t nHeight) const;

    // Check if voting is open at given height (days 8-29)
    bool IsVotingOpen(uint32_t nHeight) const;

    // Check if payout height (day 30)
    bool IsPayoutHeight(uint32_t nHeight) const;

    SERIALIZE_METHODS(CDAOProposal, obj) {
        READWRITE(obj.strName);
        READWRITE(obj.strDescription);
        READWRITE(obj.nAmount);
        // CTxDestination serialization via script
        CScript scriptPubKey;
        SER_WRITE(obj, scriptPubKey = GetScriptForDestination(obj.paymentAddress));
        READWRITE(scriptPubKey);
        SER_READ(obj, ExtractDestination(scriptPubKey, obj.paymentAddress));
        READWRITE(obj.nSubmitHeight);
        READWRITE(obj.nCycleStart);
    }
};

/**
 * CDAOVote - A masternode's vote on a proposal
 */
class CDAOVote
{
public:
    uint256 proposalHash;              // Which proposal
    uint256 proTxHash;                 // Voting masternode
    DAOVote vote;                      // YES/NO/ABSTAIN
    uint32_t nHeight;                  // Block height when cast
    std::vector<unsigned char> vchSig; // ECDSA signature

    CDAOVote() : vote(DAOVote::ABSTAIN), nHeight(0) {}

    CDAOVote(const uint256& propHash, const uint256& mnHash, DAOVote v, uint32_t h)
        : proposalHash(propHash), proTxHash(mnHash), vote(v), nHeight(h) {}

    // Get hash for signing
    uint256 GetSignHash() const;

    // Verify ECDSA signature
    bool CheckSignature(const CPubKey& pubKey) const;

    // PIVHU: Sign with MN operator key (ECDSA)
    bool Sign(const CKey& secretKey);

    SERIALIZE_METHODS(CDAOVote, obj) {
        READWRITE(obj.proposalHash);
        READWRITE(obj.proTxHash);
        uint8_t nVote;
        SER_WRITE(obj, nVote = static_cast<uint8_t>(obj.vote));
        READWRITE(nVote);
        SER_READ(obj, obj.vote = static_cast<DAOVote>(nVote));
        READWRITE(obj.nHeight);
        READWRITE(obj.vchSig);
    }
};

/**
 * CDAOManager - Manages proposals and votes
 */
class CDAOManager
{
private:
    mutable RecursiveMutex cs_dao;

    // Active proposals (by hash)
    std::map<uint256, CDAOProposal> mapProposals;

    // Votes per proposal
    std::map<uint256, std::map<uint256, CDAOVote>> mapVotes;  // proposalHash -> (proTxHash -> vote)

    // Executed proposals (for history)
    std::set<uint256> setExecuted;

public:
    CDAOManager() = default;

    // Submit a new proposal
    bool SubmitProposal(const CDAOProposal& proposal, std::string& strError);

    // Cast a vote
    bool CastVote(const CDAOVote& vote, std::string& strError);

    // Get proposal by hash
    bool GetProposal(const uint256& hash, CDAOProposal& proposal) const;

    // Get all active proposals for current cycle
    std::vector<CDAOProposal> GetActiveProposals(uint32_t nHeight) const;

    // Count votes for a proposal
    void CountVotes(const uint256& proposalHash, int& nYes, int& nNo) const;

    // Execute approved proposals (called during block connection)
    bool ExecutePayouts(int nHeight, CAmount nTreasuryBalance, std::vector<std::pair<CTxDestination, CAmount>>& payouts);

    // Get current cycle start for a given height
    static uint32_t GetCycleStart(uint32_t nHeight);

    // Clean old proposals
    void CleanOldProposals(uint32_t nHeight);

    // Persistence
    bool Load();
    bool Save() const;
};

// Global DAO manager
extern std::unique_ptr<CDAOManager> g_daoManager;

#endif // PIVHU_DAO_PROPOSAL_H
