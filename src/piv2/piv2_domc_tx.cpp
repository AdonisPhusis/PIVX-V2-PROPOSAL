// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "piv2/piv2_domc_tx.h"

#include "consensus/params.h"
#include "consensus/validation.h"
#include "evo/deterministicmns.h"
#include "hash.h"
#include "piv2/piv2_domcdb.h"
#include "piv2/piv2_state.h"
#include "logging.h"
#include "script/script.h"
#include "streams.h"

// External DMN manager
extern std::unique_ptr<CDeterministicMNManager> deterministicMNManager;

// ============================================================================
// MASTERNODE SIGNATURE VERIFICATION (CONSENSUS-CRITICAL)
// ============================================================================

/**
 * GetDomcCommitSignatureHash - Calculate hash for commit signature verification
 *
 * The signature covers: hashCommit || mnOutpoint || nCycleId || nCommitHeight
 * This prevents replay attacks and ensures the commit is bound to specific context.
 */
static uint256 GetDomcCommitSignatureHash(const khu_domc::DomcCommit& commit)
{
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << commit.hashCommit;
    ss << commit.mnOutpoint;
    ss << commit.nCycleId;
    ss << commit.nCommitHeight;
    return ss.GetHash();
}

/**
 * GetDomcRevealSignatureHash - Calculate hash for reveal signature verification
 *
 * The signature covers: nRProposal || salt || mnOutpoint || nCycleId || nRevealHeight
 * This prevents replay attacks and ensures the reveal is bound to specific context.
 */
static uint256 GetDomcRevealSignatureHash(const khu_domc::DomcReveal& reveal)
{
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << reveal.nRProposal;
    ss << reveal.salt;
    ss << reveal.mnOutpoint;
    ss << reveal.nCycleId;
    ss << reveal.nRevealHeight;
    return ss.GetHash();
}

/**
 * VerifyMasternodeSignature - Verify ECDSA signature from masternode operator
 *
 * @param mnOutpoint  Masternode collateral outpoint (identity)
 * @param msgHash     Message hash to verify
 * @param vchSig      Signature bytes
 * @param strError    Output: error message on failure
 * @return true if signature is valid from the registered operator key
 */
static bool VerifyMasternodeSignature(
    const COutPoint& mnOutpoint,
    const uint256& msgHash,
    const std::vector<unsigned char>& vchSig,
    std::string& strError)
{
    // 1. Check DMN manager is available
    if (!deterministicMNManager) {
        strError = "Deterministic MN manager not initialized";
        return false;
    }

    // 2. Get the masternode from the DMN list by collateral
    CDeterministicMNList mnList = deterministicMNManager->GetListAtChainTip();
    CDeterministicMNCPtr dmn = mnList.GetMNByCollateral(mnOutpoint);

    if (!dmn) {
        strError = strprintf("Masternode not found for collateral %s", mnOutpoint.ToString());
        return false;
    }

    // 3. Check MN is not PoSe banned
    if (dmn->IsPoSeBanned()) {
        strError = strprintf("Masternode %s is PoSe banned", mnOutpoint.ToString());
        return false;
    }

    // 4. Get operator public key (ECDSA)
    const CPubKey& pubKeyOperator = dmn->pdmnState->pubKeyOperator;
    if (!pubKeyOperator.IsValid()) {
        strError = strprintf("Masternode %s has invalid operator key", mnOutpoint.ToString());
        return false;
    }

    // 5. Verify ECDSA signature
    if (vchSig.empty()) {
        strError = "Empty signature";
        return false;
    }

    if (!pubKeyOperator.Verify(msgHash, vchSig)) {
        strError = strprintf("Signature verification failed for MN %s", mnOutpoint.ToString());
        return false;
    }

    return true;
}

// ============================================================================
// EXTRACTION functions (parse DOMC data from transaction)
// ============================================================================

bool ExtractDomcCommitFromTx(const CTransaction& tx, khu_domc::DomcCommit& commit)
{
    // DOMC commit encoded in first vout with OP_RETURN
    // Format: OP_RETURN <serialized DomcCommit>

    if (tx.vout.empty()) {
        return false;
    }

    const CTxOut& txout = tx.vout[0];
    if (!txout.scriptPubKey.IsUnspendable()) {
        return false; // Must be OP_RETURN
    }

    // Extract data after OP_RETURN
    CScript::const_iterator pc = txout.scriptPubKey.begin();
    opcodetype opcode;
    std::vector<unsigned char> data;

    if (!txout.scriptPubKey.GetOp(pc, opcode) || opcode != OP_RETURN) {
        return false;
    }

    if (!txout.scriptPubKey.GetOp(pc, opcode, data)) {
        return false;
    }

    // Deserialize DomcCommit
    try {
        CDataStream ss(data, SER_NETWORK, PROTOCOL_VERSION);
        ss >> commit;
        return true;
    } catch (const std::exception& e) {
        LogPrintf("ERROR: ExtractDomcCommitFromTx: Deserialization failed: %s\n", e.what());
        return false;
    }
}

bool ExtractDomcRevealFromTx(const CTransaction& tx, khu_domc::DomcReveal& reveal)
{
    // DOMC reveal encoded in first vout with OP_RETURN
    // Format: OP_RETURN <serialized DomcReveal>

    if (tx.vout.empty()) {
        return false;
    }

    const CTxOut& txout = tx.vout[0];
    if (!txout.scriptPubKey.IsUnspendable()) {
        return false; // Must be OP_RETURN
    }

    // Extract data after OP_RETURN
    CScript::const_iterator pc = txout.scriptPubKey.begin();
    opcodetype opcode;
    std::vector<unsigned char> data;

    if (!txout.scriptPubKey.GetOp(pc, opcode) || opcode != OP_RETURN) {
        return false;
    }

    if (!txout.scriptPubKey.GetOp(pc, opcode, data)) {
        return false;
    }

    // Deserialize DomcReveal
    try {
        CDataStream ss(data, SER_NETWORK, PROTOCOL_VERSION);
        ss >> reveal;
        return true;
    } catch (const std::exception& e) {
        LogPrintf("ERROR: ExtractDomcRevealFromTx: Deserialization failed: %s\n", e.what());
        return false;
    }
}

// ============================================================================
// VALIDATION functions (consensus-critical)
// ============================================================================

bool ValidateDomcCommitTx(
    const CTransaction& tx,
    CValidationState& state,
    const HuGlobalState& huState,
    uint32_t nHeight,
    const Consensus::Params& consensusParams
)
{
    // Extract commit from transaction
    khu_domc::DomcCommit commit;
    if (!ExtractDomcCommitFromTx(tx, commit)) {
        return state.Invalid(false, REJECT_INVALID, "bad-domc-commit-format",
                            "Failed to extract DOMC commit from transaction");
    }

    // RULE 1: Must be in commit phase
    if (!khu_domc::IsDomcCommitPhase(nHeight, huState.domc_cycle_start)) {
        return state.Invalid(false, REJECT_INVALID, "domc-commit-wrong-phase",
                            strprintf("DOMC commit not allowed outside commit phase (height=%u, cycle_start=%u)",
                                    nHeight, huState.domc_cycle_start));
    }

    // RULE 2: Verify cycle ID matches current cycle
    uint32_t currentCycleId = khu_domc::GetCurrentCycleId(nHeight,
        consensusParams.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight);

    if (commit.nCycleId != currentCycleId) {
        return state.Invalid(false, REJECT_INVALID, "domc-commit-wrong-cycle",
                            strprintf("DOMC commit cycle ID mismatch (commit=%u, expected=%u)",
                                    commit.nCycleId, currentCycleId));
    }

    // RULE 3: Masternode must not have already committed in this cycle
    CKHUDomcDB* domcDB = GetKHUDomcDB();
    if (!domcDB) {
        return state.Invalid(false, REJECT_INVALID, "domc-db-not-initialized",
                            "DOMC database not initialized");
    }

    if (domcDB->HaveCommit(commit.mnOutpoint, commit.nCycleId)) {
        return state.Invalid(false, REJECT_INVALID, "domc-commit-duplicate",
                            strprintf("Masternode %s already committed in cycle %u",
                                    commit.mnOutpoint.ToString(), commit.nCycleId));
    }

    // RULE 4: Verify commit height is current height
    if (commit.nCommitHeight != nHeight) {
        return state.Invalid(false, REJECT_INVALID, "domc-commit-wrong-height",
                            strprintf("Commit height mismatch (commit=%u, expected=%u)",
                                    commit.nCommitHeight, nHeight));
    }

    // RULE 5: Verify masternode signature (CONSENSUS-CRITICAL)
    // The signature must be from the operator key of the masternode identified by mnOutpoint.
    // This prevents unauthorized vote injection and ensures only registered MNs can vote.
    {
        uint256 sigHash = GetDomcCommitSignatureHash(commit);
        std::string strError;
        if (!VerifyMasternodeSignature(commit.mnOutpoint, sigHash, commit.vchSig, strError)) {
            return state.Invalid(false, REJECT_INVALID, "domc-commit-bad-sig",
                                strprintf("DOMC commit signature verification failed: %s", strError));
        }
    }

    LogPrint(BCLog::HU, "ValidateDomcCommitTx: Valid commit from MN %s for cycle %u at height %u (sig verified)\n",
             commit.mnOutpoint.ToString(), commit.nCycleId, nHeight);

    return true;
}

bool ValidateDomcRevealTx(
    const CTransaction& tx,
    CValidationState& state,
    const HuGlobalState& huState,
    uint32_t nHeight,
    const Consensus::Params& consensusParams
)
{
    // Extract reveal from transaction
    khu_domc::DomcReveal reveal;
    if (!ExtractDomcRevealFromTx(tx, reveal)) {
        return state.Invalid(false, REJECT_INVALID, "bad-domc-reveal-format",
                            "Failed to extract DOMC reveal from transaction");
    }

    // RULE 1: Must be in reveal phase
    if (!khu_domc::IsDomcRevealPhase(nHeight, huState.domc_cycle_start)) {
        return state.Invalid(false, REJECT_INVALID, "domc-reveal-wrong-phase",
                            strprintf("DOMC reveal not allowed outside reveal phase (height=%u, cycle_start=%u)",
                                    nHeight, huState.domc_cycle_start));
    }

    // RULE 2: Verify cycle ID matches current cycle
    uint32_t currentCycleId = khu_domc::GetCurrentCycleId(nHeight,
        consensusParams.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight);

    if (reveal.nCycleId != currentCycleId) {
        return state.Invalid(false, REJECT_INVALID, "domc-reveal-wrong-cycle",
                            strprintf("DOMC reveal cycle ID mismatch (reveal=%u, expected=%u)",
                                    reveal.nCycleId, currentCycleId));
    }

    // RULE 3: Must have matching commit from same masternode
    CKHUDomcDB* domcDB = GetKHUDomcDB();
    if (!domcDB) {
        return state.Invalid(false, REJECT_INVALID, "domc-db-not-initialized",
                            "DOMC database not initialized");
    }

    khu_domc::DomcCommit commit;
    if (!domcDB->ReadCommit(reveal.mnOutpoint, reveal.nCycleId, commit)) {
        return state.Invalid(false, REJECT_INVALID, "domc-reveal-no-commit",
                            strprintf("No commit found for masternode %s in cycle %u",
                                    reveal.mnOutpoint.ToString(), reveal.nCycleId));
    }

    // RULE 4: Hash(R + salt) must match commit hash
    uint256 revealHash = reveal.GetCommitHash();
    if (revealHash != commit.hashCommit) {
        return state.Invalid(false, REJECT_INVALID, "domc-reveal-hash-mismatch",
                            strprintf("Reveal hash does not match commit (expected=%s, got=%s)",
                                    commit.hashCommit.GetHex(), revealHash.GetHex()));
    }

    // RULE 5: R proposal must be ≤ R_MAX (absolute maximum)
    if (reveal.nRProposal > khu_domc::R_MAX) {
        return state.Invalid(false, REJECT_INVALID, "domc-reveal-r-too-high",
                            strprintf("R proposal %u exceeds maximum %u",
                                    reveal.nRProposal, khu_domc::R_MAX));
    }

    // RULE 6: Verify reveal height is current height
    if (reveal.nRevealHeight != nHeight) {
        return state.Invalid(false, REJECT_INVALID, "domc-reveal-wrong-height",
                            strprintf("Reveal height mismatch (reveal=%u, expected=%u)",
                                    reveal.nRevealHeight, nHeight));
    }

    // RULE 7: Masternode must not have already revealed in this cycle
    if (domcDB->HaveReveal(reveal.mnOutpoint, reveal.nCycleId)) {
        return state.Invalid(false, REJECT_INVALID, "domc-reveal-duplicate",
                            strprintf("Masternode %s already revealed in cycle %u",
                                    reveal.mnOutpoint.ToString(), reveal.nCycleId));
    }

    // RULE 8: Verify masternode signature (CONSENSUS-CRITICAL)
    // The signature must be from the operator key of the masternode identified by mnOutpoint.
    // This prevents unauthorized reveal injection and vote manipulation.
    {
        uint256 sigHash = GetDomcRevealSignatureHash(reveal);
        std::string strError;
        if (!VerifyMasternodeSignature(reveal.mnOutpoint, sigHash, reveal.vchSig, strError)) {
            return state.Invalid(false, REJECT_INVALID, "domc-reveal-bad-sig",
                                strprintf("DOMC reveal signature verification failed: %s", strError));
        }
    }

    LogPrint(BCLog::HU, "ValidateDomcRevealTx: Valid reveal from MN %s for cycle %u: R=%u (%.2f%%) at height %u (sig verified)\n",
             reveal.mnOutpoint.ToString(), reveal.nCycleId, reveal.nRProposal,
             reveal.nRProposal / 100.0, nHeight);

    return true;
}

// ============================================================================
// APPLY functions (store to database)
// ============================================================================

bool ApplyDomcCommitTx(const CTransaction& tx, uint32_t nHeight)
{
    khu_domc::DomcCommit commit;
    if (!ExtractDomcCommitFromTx(tx, commit)) {
        LogPrintf("ERROR: ApplyDomcCommitTx: Failed to extract commit from tx %s\n",
                  tx.GetHash().ToString());
        return false;
    }

    CKHUDomcDB* domcDB = GetKHUDomcDB();
    if (!domcDB) {
        LogPrintf("ERROR: ApplyDomcCommitTx: DOMC DB not initialized\n");
        return false;
    }

    // Write commit to database
    if (!domcDB->WriteCommit(commit)) {
        LogPrintf("ERROR: ApplyDomcCommitTx: Failed to write commit to DB (MN=%s, cycle=%u)\n",
                  commit.mnOutpoint.ToString(), commit.nCycleId);
        return false;
    }

    // Add masternode to cycle index (for GetRevealsForCycle)
    if (!domcDB->AddMasternodeToCycleIndex(commit.nCycleId, commit.mnOutpoint)) {
        LogPrintf("ERROR: ApplyDomcCommitTx: Failed to add MN to cycle index (MN=%s, cycle=%u)\n",
                  commit.mnOutpoint.ToString(), commit.nCycleId);
        return false;
    }

    LogPrint(BCLog::HU, "ApplyDomcCommitTx: Stored commit from MN %s for cycle %u\n",
             commit.mnOutpoint.ToString(), commit.nCycleId);

    return true;
}

bool ApplyDomcRevealTx(const CTransaction& tx, uint32_t nHeight)
{
    khu_domc::DomcReveal reveal;
    if (!ExtractDomcRevealFromTx(tx, reveal)) {
        LogPrintf("ERROR: ApplyDomcRevealTx: Failed to extract reveal from tx %s\n",
                  tx.GetHash().ToString());
        return false;
    }

    CKHUDomcDB* domcDB = GetKHUDomcDB();
    if (!domcDB) {
        LogPrintf("ERROR: ApplyDomcRevealTx: DOMC DB not initialized\n");
        return false;
    }

    // Write reveal to database
    if (!domcDB->WriteReveal(reveal)) {
        LogPrintf("ERROR: ApplyDomcRevealTx: Failed to write reveal to DB (MN=%s, cycle=%u)\n",
                  reveal.mnOutpoint.ToString(), reveal.nCycleId);
        return false;
    }

    LogPrint(BCLog::HU, "ApplyDomcRevealTx: Stored reveal from MN %s for cycle %u: R=%u (%.2f%%)\n",
             reveal.mnOutpoint.ToString(), reveal.nCycleId, reveal.nRProposal,
             reveal.nRProposal / 100.0);

    return true;
}

// ============================================================================
// UNDO functions (reorg support)
// ============================================================================

bool UndoDomcCommitTx(const CTransaction& tx, uint32_t nHeight)
{
    khu_domc::DomcCommit commit;
    if (!ExtractDomcCommitFromTx(tx, commit)) {
        LogPrintf("ERROR: UndoDomcCommitTx: Failed to extract commit from tx %s\n",
                  tx.GetHash().ToString());
        return false;
    }

    CKHUDomcDB* domcDB = GetKHUDomcDB();
    if (!domcDB) {
        LogPrintf("ERROR: UndoDomcCommitTx: DOMC DB not initialized\n");
        return false;
    }

    // Erase commit from database
    if (!domcDB->EraseCommit(commit.mnOutpoint, commit.nCycleId)) {
        LogPrintf("ERROR: UndoDomcCommitTx: Failed to erase commit from DB (MN=%s, cycle=%u)\n",
                  commit.mnOutpoint.ToString(), commit.nCycleId);
        return false;
    }

    LogPrint(BCLog::HU, "UndoDomcCommitTx: Erased commit from MN %s for cycle %u\n",
             commit.mnOutpoint.ToString(), commit.nCycleId);

    return true;
}

bool UndoDomcRevealTx(const CTransaction& tx, uint32_t nHeight)
{
    khu_domc::DomcReveal reveal;
    if (!ExtractDomcRevealFromTx(tx, reveal)) {
        LogPrintf("ERROR: UndoDomcRevealTx: Failed to extract reveal from tx %s\n",
                  tx.GetHash().ToString());
        return false;
    }

    CKHUDomcDB* domcDB = GetKHUDomcDB();
    if (!domcDB) {
        LogPrintf("ERROR: UndoDomcRevealTx: DOMC DB not initialized\n");
        return false;
    }

    // Erase reveal from database
    if (!domcDB->EraseReveal(reveal.mnOutpoint, reveal.nCycleId)) {
        LogPrintf("ERROR: UndoDomcRevealTx: Failed to erase reveal from DB (MN=%s, cycle=%u)\n",
                  reveal.mnOutpoint.ToString(), reveal.nCycleId);
        return false;
    }

    LogPrint(BCLog::HU, "UndoDomcRevealTx: Erased reveal from MN %s for cycle %u\n",
             reveal.mnOutpoint.ToString(), reveal.nCycleId);

    return true;
}
