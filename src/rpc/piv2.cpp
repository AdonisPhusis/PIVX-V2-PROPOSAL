// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "piv2/piv2_validation.h"

#include "chain.h"
#include "chainparams.h"
#include "evo/deterministicmns.h"
#include "key_io.h"
#include "piv2/piv2_commitment.h"
#include "piv2/piv2_commitmentdb.h"
#include "piv2/piv2_dao.h"
#include "piv2/piv2_domc.h"
#include "piv2/piv2_domc_tx.h"
#include "piv2/piv2_domcdb.h"
#include "piv2/piv2_state.h"
#include "masternodeman.h"
#include "primitives/transaction.h"
#include "rpc/server.h"
#include "sync.h"
#include "txmempool.h"
#include "utilmoneystr.h"
#include "util/validation.h"
#include "validation.h"

#include <univalue.h>

/**
 * gethustate - Get current PIVX V2 global state
 *
 * Nomenclature:
 * - C: Total collateral (= U + Z)
 * - U: PIVX V2 transparent supply (KHU available)
 * - Z: ZKHU shielded staking supply
 * - Cr/Ur: Reward pool / rights
 * - T: DAO Treasury
 *
 * Usage:
 *   gethustate
 *
 * Returns:
 * {
 *   "height": n,           (numeric) Block height
 *   "blockhash": "hash",   (string) Block hash
 *   "C": n,                (numeric) Collateral total (C = U + Z)
 *   "U": n,                (numeric) Transparent supply (KHU)
 *   "Z": n,                (numeric) Shielded staking supply (ZKHU)
 *   "Cr": n,               (numeric) Reward pool
 *   "Ur": n,               (numeric) Reward rights
 *   "T": n,                (numeric) DAO Treasury
 *   "R_annual": n,         (numeric) Annual yield rate (basis points)
 *   "R_annual_pct": x.xx,  (numeric) Annual yield rate (percentage)
 *   "R_MAX_dynamic": n,    (numeric) Maximum R% allowed
 *   "invariants_ok": true|false,  (boolean) Invariants validation
 *   "hashState": "hash",   (string) Hash of this state
 *   "hashPrevState": "hash" (string) Hash of previous state
 * }
 */
static UniValue getkhustate(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(
            "getpiv2state\n"
            "\nReturns the current PIVX V2 global state.\n"
            "\nResult:\n"
            "{\n"
            "  \"height\": n,           (numeric) Block height\n"
            "  \"blockhash\": \"hash\",   (string) Block hash\n"
            "  \"C\": n,                (numeric) Collateral total (C = U + Z)\n"
            "  \"U\": n,                (numeric) Transparent supply (KHU)\n"
            "  \"Z\": n,                (numeric) Shielded staking supply (ZKHU)\n"
            "  \"Cr\": n,               (numeric) Reward pool\n"
            "  \"Ur\": n,               (numeric) Reward rights (accumulated yield)\n"
            "  \"T\": n,                (numeric) DAO Treasury\n"
            "  \"R_annual\": n,         (numeric) Current annual yield rate (basis points: 4000 = 40.00%)\n"
            "  \"R_annual_pct\": x.xx,  (numeric) Current annual yield rate (percentage)\n"
            "  \"R_next\": n,           (numeric) Next R% after DOMC REVEAL\n"
            "  \"R_next_pct\": x.xx,    (numeric) Next R% (percentage)\n"
            "  \"R_MAX_dynamic\": n,    (numeric) Maximum R% allowed by DOMC (decreases yearly)\n"
            "  \"last_yield_update_height\": n, (numeric) Last yield update block\n"
            "  \"domc_cycle_start\": n, (numeric) Current DOMC cycle start\n"
            "  \"invariants_ok\": true|false,  (boolean) Are invariants satisfied (C=U+Z, Cr=Ur)?\n"
            "  \"hashState\": \"hash\",   (string) Hash of this state\n"
            "  \"hashPrevState\": \"hash\" (string) Hash of previous state\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getpiv2state", "")
            + HelpExampleRpc("getpiv2state", "")
        );
    }

    LOCK(cs_main);

    HuGlobalState state;
    bool hasState = GetCurrentKHUState(state);

    // If no state in DB, return default empty state
    if (!hasState) {
        state = HuGlobalState();  // Default: C=U=Z=Cr=Ur=T=0
        state.nHeight = chainActive.Height();
        if (chainActive.Tip()) {
            state.hashBlock = chainActive.Tip()->GetBlockHash();
        }
        // Set default R% values
        state.R_annual = 4000;     // 40.00% default
        state.R_next = 4000;
        state.R_MAX_dynamic = 4000;
    }

    UniValue result(UniValue::VOBJ);

    result.pushKV("height", (int64_t)state.nHeight);
    result.pushKV("blockhash", state.hashBlock.GetHex());
    result.pushKV("C", ValueFromAmount(state.C));
    result.pushKV("U", ValueFromAmount(state.U));
    result.pushKV("Z", ValueFromAmount(state.Z));  // ZKHU shielded supply
    result.pushKV("Cr", ValueFromAmount(state.Cr));
    result.pushKV("Ur", ValueFromAmount(state.Ur));
    result.pushKV("T", ValueFromAmount(state.T));
    result.pushKV("R_annual", (int64_t)state.R_annual);
    result.pushKV("R_annual_pct", state.R_annual / 100.0);
    result.pushKV("R_next", (int64_t)state.R_next);
    result.pushKV("R_next_pct", state.R_next / 100.0);
    result.pushKV("R_MAX_dynamic", (int64_t)state.R_MAX_dynamic);
    result.pushKV("last_yield_update_height", (int64_t)state.last_yield_update_height);
    result.pushKV("domc_cycle_start", (int64_t)state.domc_cycle_start);
    result.pushKV("domc_cycle_length", (int64_t)state.domc_cycle_length);
    result.pushKV("domc_commit_phase_start", (int64_t)state.domc_commit_phase_start);
    result.pushKV("domc_reveal_deadline", (int64_t)state.domc_reveal_deadline);
    result.pushKV("invariants_ok", state.CheckInvariants());
    result.pushKV("hashState", state.GetHash().GetHex());
    result.pushKV("hashPrevState", state.hashPrevState.GetHex());
    result.pushKV("state_initialized", hasState);

    return result;
}

/**
 * getkhustatecommitment - Get KHU state commitment at a block height
 *
 * PHASE 3: Masternode Finality
 *
 * Returns the ECDSA-signed commitment for KHU state at the specified height.
 * Commitments provide cryptographic finality for KHU state, preventing
 * state divergence across the network.
 *
 * Usage:
 *   getkhustatecommitment height
 *
 * Arguments:
 * 1. height    (numeric, required) The block height
 *
 * Returns:
 * {
 *   "height": n,             (numeric) Block height
 *   "hashState": "hash",     (string) State hash (SHA256 of C, U, Cr, Ur, height)
 *   "quorumHash": "hash",    (string) Quorum identifier
 *   "signature": "hex",      (string) ECDSA aggregate signature
 *   "signers": n,            (numeric) Number of masternodes who signed
 *   "quorumSize": n,         (numeric) Total quorum members
 *   "finalized": true|false, (boolean) Has quorum threshold (>= 60%)
 *   "commitmentHash": "hash" (string) Hash of the commitment itself
 * }
 */
static UniValue getkhustatecommitment(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "getkhustatecommitment height\n"
            "\nReturns KHU state commitment for a given block height (Phase 3: Masternode Finality).\n"
            "\nArguments:\n"
            "1. height    (numeric, required) The block height\n"
            "\nResult:\n"
            "{\n"
            "  \"height\": n,             (numeric) Block height\n"
            "  \"hashState\": \"hash\",     (string) State hash (SHA256 of C, U, Cr, Ur, height)\n"
            "  \"quorumHash\": \"hash\",    (string) Quorum identifier\n"
            "  \"signature\": \"hex\",      (string) ECDSA aggregate signature\n"
            "  \"signers\": n,            (numeric) Number of masternodes who signed\n"
            "  \"quorumSize\": n,         (numeric) Total quorum members\n"
            "  \"finalized\": true|false, (boolean) Has quorum threshold (>= 60%)\n"
            "  \"commitmentHash\": \"hash\" (string) Hash of the commitment itself\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getkhustatecommitment", "1000000")
            + HelpExampleRpc("getkhustatecommitment", "1000000")
        );
    }

    uint32_t nHeight = request.params[0].get_int();

    CHUCommitmentDB* commitmentDB = GetKHUCommitmentDB();
    if (!commitmentDB) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "KHU commitment database not initialized");
    }

    HuStateCommitment commitment;
    if (!commitmentDB->ReadCommitment(nHeight, commitment)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                          strprintf("No commitment found at height %d", nHeight));
    }

    // Count signers
    int signerCount = 0;
    for (bool signer : commitment.signers) {
        if (signer) {
            signerCount++;
        }
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("height", (int)commitment.nHeight);
    result.pushKV("hashState", commitment.hashState.GetHex());
    result.pushKV("quorumHash", commitment.quorumHash.GetHex());
    result.pushKV("signatureCount", (int)commitment.sigs.size());
    result.pushKV("signers", signerCount);
    result.pushKV("quorumSize", (int)commitment.signers.size());
    result.pushKV("finalized", commitment.HasQuorum());
    result.pushKV("commitmentHash", commitment.GetHash().GetHex());

    return result;
}

/**
 * domccommit - Create and broadcast a DOMC commit transaction
 *
 * PHASE 6.2: DOMC Governance
 *
 * Creates a commit transaction for R% voting. The commit contains Hash(R || salt)
 * to prevent front-running. Must be called during commit phase.
 *
 * Usage:
 *   domccommit R_proposal masternode_outpoint
 *
 * Arguments:
 * 1. R_proposal       (numeric, required) Proposed R% in basis points (e.g., 1500 = 15.00%)
 * 2. mn_outpoint      (string, required) Masternode collateral outpoint (txid:n)
 *
 * Returns:
 * {
 *   "txid": "hash",          (string) Transaction ID
 *   "commit_hash": "hash",   (string) Hash(R || salt)
 *   "salt": "hash",          (string) Random salt (SAVE THIS for reveal!)
 *   "cycle_id": n,           (numeric) Cycle ID
 *   "R_proposal": n          (numeric) Proposed R% (basis points)
 * }
 */
static UniValue domccommit(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 2) {
        throw std::runtime_error(
            "domccommit R_proposal mn_outpoint\n"
            "\nCreate and broadcast a DOMC commit transaction (Phase 6.2).\n"
            "\nArguments:\n"
            "1. R_proposal       (numeric, required) Proposed R% in basis points (1500 = 15.00%)\n"
            "2. mn_outpoint      (string, required) Masternode collateral outpoint (txid:n)\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\": \"hash\",          (string) Transaction ID\n"
            "  \"commit_hash\": \"hash\",   (string) Hash(R || salt) - DO NOT SHARE\n"
            "  \"salt\": \"hash\",          (string) Random salt - SAVE THIS FOR REVEAL!\n"
            "  \"cycle_id\": n,           (numeric) Cycle ID\n"
            "  \"R_proposal\": n          (numeric) Proposed R% (basis points)\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("domccommit", "1500 \"abc123...def:0\"")
            + HelpExampleRpc("domccommit", "1500, \"abc123...def:0\"")
        );
    }

    LOCK(cs_main);

    // Parse arguments
    uint16_t nRProposal = request.params[0].get_int();
    std::string mnOutpointStr = request.params[1].get_str();

    // Parse masternode outpoint
    size_t colonPos = mnOutpointStr.find(':');
    if (colonPos == std::string::npos) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid masternode outpoint format (expected txid:n)");
    }
    uint256 txid = ParseHashV(mnOutpointStr.substr(0, colonPos), "txid");
    uint32_t vout = std::stoi(mnOutpointStr.substr(colonPos + 1));
    COutPoint mnOutpoint(txid, vout);

    // Get current state
    HuGlobalState state;
    if (!GetCurrentKHUState(state)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Unable to load KHU state");
    }

    // Check that we're in commit phase
    uint32_t nHeight = chainActive.Height() + 1; // Next block
    uint32_t V6_activation = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight;

    if (!khu_domc::IsDomcCommitPhase(nHeight, state.domc_cycle_start)) {
        throw JSONRPCError(RPC_INVALID_REQUEST,
            strprintf("Not in DOMC commit phase (current height=%u, cycle_start=%u, commit_start=%u, reveal_start=%u)",
                     nHeight, state.domc_cycle_start, state.domc_commit_phase_start, state.domc_reveal_deadline));
    }

    // Validate R proposal
    if (nRProposal > khu_domc::R_MAX) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
            strprintf("R proposal %u exceeds maximum %u (%.2f%%)", nRProposal, khu_domc::R_MAX, khu_domc::R_MAX / 100.0));
    }

    // Generate random salt
    uint256 salt = GetRandHash();

    // Calculate commit hash = Hash(R || salt)
    CHashWriter ss(SER_GETHASH, 0);
    ss << nRProposal;
    ss << salt;
    uint256 hashCommit = ss.GetHash();

    // Create commit structure
    khu_domc::DomcCommit commit;
    commit.hashCommit = hashCommit;
    commit.mnOutpoint = mnOutpoint;
    commit.nCycleId = khu_domc::GetCurrentCycleId(nHeight, V6_activation);
    commit.nCommitHeight = nHeight;
    commit.vchSig.clear(); // TODO: Add masternode signature support

    // Create transaction with OP_RETURN output
    CMutableTransaction tx;
    tx.nVersion = CTransaction::TxVersion::LEGACY;
    tx.nType = CTransaction::TxType::KHU_DOMC_COMMIT;
    tx.nLockTime = 0;

    // Serialize commit to OP_RETURN output
    CDataStream ss_commit(SER_NETWORK, PROTOCOL_VERSION);
    ss_commit << commit;
    std::vector<unsigned char> data(ss_commit.begin(), ss_commit.end());

    CScript scriptPubKey;
    scriptPubKey << OP_RETURN << data;

    tx.vout.push_back(CTxOut(0, scriptPubKey));

    // Broadcast transaction
    CValidationState state_tx;
    CTransactionRef txRef = MakeTransactionRef(tx);

    if (!AcceptToMemoryPool(mempool, state_tx, txRef, false, nullptr)) {
        throw JSONRPCError(RPC_TRANSACTION_REJECTED,
            strprintf("Transaction rejected: %s", FormatStateMessage(state_tx)));
    }

    // Note: Transaction is automatically relayed when accepted to mempool

    // Return result
    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", tx.GetHash().GetHex());
    result.pushKV("commit_hash", hashCommit.GetHex());
    result.pushKV("salt", salt.GetHex());
    result.pushKV("cycle_id", (int64_t)commit.nCycleId);
    result.pushKV("R_proposal", (int64_t)nRProposal);
    result.pushKV("note", "IMPORTANT: Save the 'salt' value - you will need it for domcreveal!");

    return result;
}

/**
 * domcreveal - Create and broadcast a DOMC reveal transaction
 *
 * PHASE 6.2: DOMC Governance
 *
 * Creates a reveal transaction for R% voting. Must match a previous commit
 * and be called during reveal phase.
 *
 * Usage:
 *   domcreveal R_proposal salt masternode_outpoint
 *
 * Arguments:
 * 1. R_proposal       (numeric, required) Proposed R% (must match commit)
 * 2. salt             (string, required) Salt from commit (hex)
 * 3. mn_outpoint      (string, required) Masternode collateral outpoint (txid:n)
 *
 * Returns:
 * {
 *   "txid": "hash",         (string) Transaction ID
 *   "cycle_id": n,          (numeric) Cycle ID
 *   "R_proposal": n,        (numeric) Revealed R%
 *   "commit_hash": "hash"   (string) Matching commit hash
 * }
 */
static UniValue domcreveal(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 3) {
        throw std::runtime_error(
            "domcreveal R_proposal salt mn_outpoint\n"
            "\nCreate and broadcast a DOMC reveal transaction (Phase 6.2).\n"
            "\nArguments:\n"
            "1. R_proposal       (numeric, required) Proposed R% in basis points (must match commit)\n"
            "2. salt             (string, required) Salt from commit (hex)\n"
            "3. mn_outpoint      (string, required) Masternode collateral outpoint (txid:n)\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\": \"hash\",         (string) Transaction ID\n"
            "  \"cycle_id\": n,          (numeric) Cycle ID\n"
            "  \"R_proposal\": n,        (numeric) Revealed R% (basis points)\n"
            "  \"commit_hash\": \"hash\"   (string) Matching commit hash\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("domcreveal", "1500 \"abc123...\" \"def456...:0\"")
            + HelpExampleRpc("domcreveal", "1500, \"abc123...\", \"def456...:0\"")
        );
    }

    LOCK(cs_main);

    // Parse arguments
    uint16_t nRProposal = request.params[0].get_int();
    uint256 salt = ParseHashV(request.params[1], "salt");
    std::string mnOutpointStr = request.params[2].get_str();

    // Parse masternode outpoint
    size_t colonPos = mnOutpointStr.find(':');
    if (colonPos == std::string::npos) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid masternode outpoint format (expected txid:n)");
    }
    uint256 txid = ParseHashV(mnOutpointStr.substr(0, colonPos), "txid");
    uint32_t vout = std::stoi(mnOutpointStr.substr(colonPos + 1));
    COutPoint mnOutpoint(txid, vout);

    // Get current state
    HuGlobalState state;
    if (!GetCurrentKHUState(state)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Unable to load KHU state");
    }

    // Check that we're in reveal phase
    uint32_t nHeight = chainActive.Height() + 1; // Next block
    uint32_t V6_activation = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight;

    if (!khu_domc::IsDomcRevealPhase(nHeight, state.domc_cycle_start)) {
        throw JSONRPCError(RPC_INVALID_REQUEST,
            strprintf("Not in DOMC reveal phase (current height=%u, cycle_start=%u, reveal_start=%u)",
                     nHeight, state.domc_cycle_start, state.domc_reveal_deadline));
    }

    // Validate R proposal
    if (nRProposal > khu_domc::R_MAX) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
            strprintf("R proposal %u exceeds maximum %u", nRProposal, khu_domc::R_MAX));
    }

    // Calculate commit hash (should match previous commit)
    CHashWriter ss(SER_GETHASH, 0);
    ss << nRProposal;
    ss << salt;
    uint256 hashCommit = ss.GetHash();

    // Create reveal structure
    khu_domc::DomcReveal reveal;
    reveal.nRProposal = nRProposal;
    reveal.salt = salt;
    reveal.mnOutpoint = mnOutpoint;
    reveal.nCycleId = khu_domc::GetCurrentCycleId(nHeight, V6_activation);
    reveal.nRevealHeight = nHeight;
    reveal.vchSig.clear(); // TODO: Add masternode signature support

    // Create transaction with OP_RETURN output
    CMutableTransaction tx;
    tx.nVersion = CTransaction::TxVersion::LEGACY;
    tx.nType = CTransaction::TxType::KHU_DOMC_REVEAL;
    tx.nLockTime = 0;

    // Serialize reveal to OP_RETURN output
    CDataStream ss_reveal(SER_NETWORK, PROTOCOL_VERSION);
    ss_reveal << reveal;
    std::vector<unsigned char> data(ss_reveal.begin(), ss_reveal.end());

    CScript scriptPubKey;
    scriptPubKey << OP_RETURN << data;

    tx.vout.push_back(CTxOut(0, scriptPubKey));

    // Broadcast transaction
    CValidationState state_tx;
    CTransactionRef txRef = MakeTransactionRef(tx);

    if (!AcceptToMemoryPool(mempool, state_tx, txRef, false, nullptr)) {
        throw JSONRPCError(RPC_TRANSACTION_REJECTED,
            strprintf("Transaction rejected: %s", FormatStateMessage(state_tx)));
    }

    // Note: Transaction is automatically relayed when accepted to mempool

    // Return result
    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", tx.GetHash().GetHex());
    result.pushKV("cycle_id", (int64_t)reveal.nCycleId);
    result.pushKV("R_proposal", (int64_t)nRProposal);
    result.pushKV("commit_hash", hashCommit.GetHex());

    return result;
}

// ============================================================================
// DAO TREASURY INFO COMMAND
// ============================================================================

/**
 * getdaoinfo - Get DAO system info (Treasury T and proposals)
 *
 * Budget proposals are funded from DAO Treasury (T). Use budget commands:
 *   - preparebudget: Create proposal collateral tx
 *   - submitbudget: Submit proposal to network
 *   - mnbudgetvote: Vote on proposal (masternode)
 *   - getbudgetinfo: List proposals
 *
 * This command shows Treasury status and proposal integration.
 */
static UniValue khudaoinfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(
            "getdaoinfo\n"
            "\nGet PIVX V2 DAO Treasury information and proposal status.\n"
            "\nBudget proposals are funded from DAO Treasury (T).\n"
            "Use budget commands to manage proposals:\n"
            "  - preparebudget: Create proposal collateral tx\n"
            "  - submitbudget: Submit proposal to network\n"
            "  - mnbudgetvote: Vote on proposal (masternode)\n"
            "  - getbudgetinfo: List proposals\n"
            "\nResult:\n"
            "{\n"
            "  \"treasury_balance\": n,       (numeric) Current DAO Treasury balance (HU)\n"
            "  \"daily_accumulation\": n,     (numeric) Daily treasury accumulation\n"
            "  \"total_budget\": n,           (numeric) Available budget for proposals\n"
            "  \"proposal_count\": n,         (numeric) Number of proposals\n"
            "  \"next_dao_cycle\": n,         (numeric) Next DAO cycle height\n"
            "  \"blocks_until_dao_cycle\": n, (numeric) Blocks until next DAO cycle\n"
            "  \"masternode_count\": n,       (numeric) Active masternodes\n"
            "  \"current_height\": n,         (numeric) Current block height\n"
            "  \"r_annual_pct\": n,           (numeric) Current R% (affects T accumulation)\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getdaoinfo", "")
            + HelpExampleRpc("getdaoinfo", "")
        );
    }

    LOCK(cs_main);

    HuGlobalState state;
    if (!GetCurrentKHUState(state)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Unable to load KHU state");
    }

    int nHeight = chainActive.Height();
    int mnCount = mnodeman.CountEnabled();

    // Calculate daily accumulation using KHU DAO function
    CAmount dailyAccum = khu_dao::CalculateDAOBudget(state);

    // Next DAO cycle boundary
    uint32_t v6Height = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight;
    uint32_t blocksSinceV6 = (nHeight > (int)v6Height) ? (nHeight - v6Height) : 0;
    uint32_t nextDaoCycle = v6Height + ((blocksSinceV6 / khu_dao::DAO_CYCLE_LENGTH) + 1) * khu_dao::DAO_CYCLE_LENGTH;
    int blocksUntil = nextDaoCycle - nHeight;

    UniValue result(UniValue::VOBJ);
    result.pushKV("treasury_balance", ValueFromAmount(state.T));
    result.pushKV("daily_accumulation", ValueFromAmount(dailyAccum));
    result.pushKV("next_dao_cycle", (int)nextDaoCycle);
    result.pushKV("blocks_until_dao_cycle", blocksUntil);
    result.pushKV("masternode_count", mnCount);
    result.pushKV("current_height", nHeight);
    result.pushKV("r_annual_pct", state.R_annual / 100.0);

    result.pushKV("note", "PIVHU uses KHU DAO T for treasury management");

    return result;
}

// ============================================================================
// RPC Command Registration
// ============================================================================

// NOTE: Wallet RPCs (khubalance, khulistunspent, khumint, khuredeem) are in
// wallet/rpc_hu.cpp and registered via RegisterHUWalletRPCCommands.

// RPC command table (to be registered in rpc/register.cpp)
// NOTE: Wallet RPCs moved to wallet/rpc_hu.cpp
// NOTE: DAO proposals use existing budget system (preparebudget, submitbudget, etc.)
//       Budget is funded from DAO Treasury T (GetTotalBudget returns T)
//
// Nomenclature: HU (transparent), sHU (shielded), KHU (locked), ZKHU (staked)
static const CRPCCommand commands[] = {
    //  category    name                      actor (function)            okSafe  argNames
    //  ----------- ------------------------  ------------------------    ------  ----------
    // HU State info
    { "piv2",         "getpiv2state",           &getkhustate,               true,   {} },
    { "piv2",         "getkhustate",            &getkhustate,               true,   {} },  // Alias for compatibility
    { "piv2",         "getstatecommitment",     &getkhustatecommitment,     true,   {"height"} },
    // DOMC governance (R% voting)
    { "piv2",         "domccommit",             &domccommit,                false,  {"R_proposal", "mn_outpoint"} },
    { "piv2",         "domcreveal",             &domcreveal,                false,  {"R_proposal", "salt", "mn_outpoint"} },
    // DAO Treasury info (integrates with existing budget system)
    { "piv2",         "getdaoinfo",             &khudaoinfo,                true,   {} },
};

void RegisterHURPCCommands(CRPCTable& t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
