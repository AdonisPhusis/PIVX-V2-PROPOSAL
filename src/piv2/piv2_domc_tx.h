// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef HU_HU_DOMC_TX_H
#define HU_HU_DOMC_TX_H

#include "piv2/piv2_domc.h"
#include "primitives/transaction.h"

#include <string>

// Forward declarations
class CValidationState;
struct HuGlobalState;
namespace Consensus { struct Params; }

/**
 * DOMC Transaction Validation
 *
 * Phase 6.2: Validates DOMC commit/reveal transactions
 *
 * COMMIT RULES:
 * - Must be in commit phase (cycle_start + 132480 → 152640)
 * - One commit per masternode per cycle
 * - Valid masternode signature
 * - Hash is SHA256(R_proposal || salt) - no premature reveal
 *
 * REVEAL RULES:
 * - Must be in reveal phase (cycle_start + 152640 → 172800)
 * - Must have matching commit from same masternode
 * - Hash(R + salt) must match commit hash
 * - Valid masternode signature
 * - R proposal must be ≤ R_MAX (absolute maximum 50.00%)
 */

/**
 * ExtractDomcCommitFromTx - Extract DOMC commit from transaction
 *
 * DOMC commit is encoded in scriptSig of first input (vin[0]).
 * Format: OP_RETURN <serialized DomcCommit>
 *
 * @param tx Transaction to extract from
 * @param commit Output parameter for extracted commit
 * @return true if extraction successful, false otherwise
 */
bool ExtractDomcCommitFromTx(const CTransaction& tx, khu_domc::DomcCommit& commit);

/**
 * ExtractDomcRevealFromTx - Extract DOMC reveal from transaction
 *
 * DOMC reveal is encoded in scriptSig of first input (vin[0]).
 * Format: OP_RETURN <serialized DomcReveal>
 *
 * @param tx Transaction to extract from
 * @param reveal Output parameter for extracted reveal
 * @return true if extraction successful, false otherwise
 */
bool ExtractDomcRevealFromTx(const CTransaction& tx, khu_domc::DomcReveal& reveal);

/**
 * ValidateDomcCommitTx - Validate DOMC commit transaction
 *
 * VALIDATION RULES:
 * 1. Must be in commit phase
 * 2. Masternode must not have already committed in this cycle
 * 3. Masternode signature must be valid
 * 4. Transaction structure must be valid
 *
 * @param tx Transaction to validate
 * @param state Validation state (for error reporting)
 * @param huState Current KHU global state (for cycle info)
 * @param nHeight Current block height
 * @param consensusParams Consensus parameters
 * @return true if valid, false otherwise (sets state.Invalid)
 */
bool ValidateDomcCommitTx(
    const CTransaction& tx,
    CValidationState& state,
    const HuGlobalState& huState,
    uint32_t nHeight,
    const Consensus::Params& consensusParams
);

/**
 * ValidateDomcRevealTx - Validate DOMC reveal transaction
 *
 * VALIDATION RULES:
 * 1. Must be in reveal phase
 * 2. Must have matching commit from same masternode in current cycle
 * 3. Hash(R + salt) must match commit hash
 * 4. R proposal must be ≤ R_MAX (50.00%)
 * 5. Masternode signature must be valid
 * 6. Transaction structure must be valid
 *
 * @param tx Transaction to validate
 * @param state Validation state (for error reporting)
 * @param huState Current KHU global state (for cycle info, R_MAX)
 * @param nHeight Current block height
 * @param consensusParams Consensus parameters
 * @return true if valid, false otherwise (sets state.Invalid)
 */
bool ValidateDomcRevealTx(
    const CTransaction& tx,
    CValidationState& state,
    const HuGlobalState& huState,
    uint32_t nHeight,
    const Consensus::Params& consensusParams
);

/**
 * ApplyDomcCommitTx - Apply DOMC commit to database
 *
 * Stores commit in CKHUDomcDB and adds masternode to cycle index.
 *
 * @param tx Transaction containing commit
 * @param nHeight Current block height
 * @return true on success, false on error
 */
bool ApplyDomcCommitTx(const CTransaction& tx, uint32_t nHeight);

/**
 * ApplyDomcRevealTx - Apply DOMC reveal to database
 *
 * Stores reveal in CKHUDomcDB.
 *
 * @param tx Transaction containing reveal
 * @param nHeight Current block height
 * @return true on success, false on error
 */
bool ApplyDomcRevealTx(const CTransaction& tx, uint32_t nHeight);

/**
 * UndoDomcCommitTx - Undo DOMC commit (for reorg)
 *
 * Removes commit from CKHUDomcDB.
 *
 * @param tx Transaction containing commit
 * @param nHeight Block height being disconnected
 * @return true on success, false on error
 */
bool UndoDomcCommitTx(const CTransaction& tx, uint32_t nHeight);

/**
 * UndoDomcRevealTx - Undo DOMC reveal (for reorg)
 *
 * Removes reveal from CKHUDomcDB.
 *
 * @param tx Transaction containing reveal
 * @param nHeight Block height being disconnected
 * @return true on success, false on error
 */
bool UndoDomcRevealTx(const CTransaction& tx, uint32_t nHeight);

#endif // HU_HU_DOMC_TX_H
