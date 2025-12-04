// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef HU_SCRIPT_CONDITIONAL_H
#define HU_SCRIPT_CONDITIONAL_H

#include "script/script.h"
#include "pubkey.h"
#include "uint256.h"

#include <vector>

/**
 * Conditional Script (hash + timelock)
 * BIP-199 compatible P2SH standard
 * Compatible: BTC, LTC, DASH, ZEC, BCH, DOGE
 */

/**
 * Create conditional script (P2SH redeemScript)
 *
 * @param hashlock   SHA256(secret), 32 bytes
 * @param timelock   Block height (absolute)
 * @param destA      Destination if secret revealed
 * @param destB      Destination if timeout
 * @return           redeemScript for P2SH
 */
CScript CreateConditionalScript(
    const uint256& hashlock,
    uint32_t timelock,
    const CKeyID& destA,
    const CKeyID& destB
);

/**
 * Decode conditional script parameters
 *
 * @param script     redeemScript to decode
 * @param hashlock   Output: extracted hashlock
 * @param timelock   Output: extracted timelock
 * @param destA      Output: destination A
 * @param destB      Output: destination B
 * @return           true if valid conditional script
 */
bool DecodeConditionalScript(
    const CScript& script,
    uint256& hashlock,
    uint32_t& timelock,
    CKeyID& destA,
    CKeyID& destB
);

/**
 * Check if script is a conditional script
 */
bool IsConditionalScript(const CScript& script);

/**
 * Create scriptSig for spending via branch A (with secret)
 */
CScript CreateConditionalSpendA(
    const std::vector<unsigned char>& sig,
    const CPubKey& pubkey,
    const std::vector<unsigned char>& secret,
    const CScript& redeemScript
);

/**
 * Create scriptSig for spending via branch B (timeout)
 */
CScript CreateConditionalSpendB(
    const std::vector<unsigned char>& sig,
    const CPubKey& pubkey,
    const CScript& redeemScript
);

#endif // HU_SCRIPT_CONDITIONAL_H
