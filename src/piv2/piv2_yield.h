// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef HU_HU_YIELD_H
#define HU_HU_YIELD_H

#include "amount.h"
#include "piv2/piv2_state.h"

#include <stdint.h>

/**
 * KHU Phase 6.1 — Daily Yield Engine
 *
 * RÈGLES ARCHITECTURALES (consensus-critical):
 * - Aucune note stockée dans HuGlobalState
 * - Lecture streaming LevelDB via curseur (ordre déterministe)
 * - Yield pour notes stakées + matures (≥ 4320 blocks)
 * - Intervalle 1440 blocks
 * - Formule: daily = (amount × R_annual / 10000) / 365
 * - Total yield → Cr += daily_total, Ur += daily_total (invariant Cr==Ur)
 * - Undo par recalcul et soustraction
 * - Protection overflow avec int128_t
 */

namespace khu_yield {

// ============================================================================
// Constants (consensus-critical)
// ============================================================================

/** Yield distribution interval (1 day = 1440 blocks at 1 min/block) */
static constexpr uint32_t YIELD_INTERVAL = 1440;

/** Note maturity period - PRODUCTION (3 days = 4320 blocks) */
static constexpr uint32_t MATURITY_BLOCKS = 4320;

/** Note maturity period - REGTEST (21 hours = 1260 blocks for fast testing) */
static constexpr uint32_t MATURITY_BLOCKS_REGTEST = 1260;

/** Days per year for yield calculation */
static constexpr uint32_t DAYS_PER_YEAR = 365;

/**
 * GetMaturityBlocks - Get network-aware maturity period
 *
 * Returns nZKHUMaturityBlocks from consensus params (per-network).
 * - Mainnet: 4320 blocks (~3 days)
 * - Testnet: 60 blocks (~1 hour)
 * - Regtest: 10 blocks (~10 minutes)
 *
 * @return Maturity blocks for current network
 */
uint32_t GetMaturityBlocks();

/**
 * GetYieldInterval - Get network-aware yield distribution interval
 *
 * Returns nBlocksPerDay from consensus params (per-network).
 * - Mainnet/Testnet: 1440 blocks (1 day @ 1 min/block)
 * - Regtest: 10 blocks (for fast testing)
 *
 * @return Yield interval blocks for current network
 */
uint32_t GetYieldInterval();

// ============================================================================
// Public Functions
// ============================================================================

/**
 * ShouldApplyDailyYield - Determine if yield should be applied at this height
 *
 * RÈGLE CONSENSUS:
 * - Yield applied every YIELD_INTERVAL blocks (1440)
 * - Starting from V6_0 activation height
 *
 * @param nHeight Current block height
 * @param nV6ActivationHeight V6_0 activation height
 * @param nLastYieldHeight Height of last yield application (0 if first)
 * @return true if yield should be applied at nHeight
 */
bool ShouldApplyDailyYield(uint32_t nHeight, uint32_t nV6ActivationHeight, uint32_t nLastYieldHeight);

/**
 * ApplyDailyYield - Apply daily yield to all mature locked notes
 *
 * ALGORITHME CONSENSUS-CRITICAL:
 * 1. Iterate all ZKHU notes via LevelDB cursor (deterministic order)
 * 2. For each note:
 *    - Check maturity: height_current - note.nLockStartHeight >= MATURITY_BLOCKS
 *    - Calculate daily yield: (amount × R_annual / 10000) / 365
 *    - Accumulate total_yield (with overflow protection)
 * 3. Update global state: Cr += total_yield, Ur += total_yield
 *    (BOTH must be updated to maintain invariant Cr == Ur)
 *
 * @param state KHU global state (will be modified: Cr += total_yield, Ur += total_yield)
 * @param nHeight Current block height
 * @param nV6ActivationHeight V6_0 activation height
 * @return true on success, false on error
 */
bool ApplyDailyYield(HuGlobalState& state, uint32_t nHeight, uint32_t nV6ActivationHeight);

/**
 * UndoDailyYield - Undo daily yield (for reorg support)
 *
 * ALGORITHME:
 * 1. Use stored yield amount from state.last_yield_amount
 * 2. Subtract from BOTH: Cr -= total_yield, Ur -= total_yield
 *    (BOTH must be updated to maintain invariant Cr == Ur)
 *
 * @param state KHU global state (will be modified: Cr -= total_yield, Ur -= total_yield)
 * @param nHeight Block height being disconnected
 * @param nV6ActivationHeight V6_0 activation height
 * @return true on success, false on error
 */
bool UndoDailyYield(HuGlobalState& state, uint32_t nHeight, uint32_t nV6ActivationHeight);

/**
 * CalculateDailyYieldForNote - Calculate daily yield for a single note
 *
 * FORMULE CONSENSUS (basis points):
 * daily = (amount × R_annual / 10000) / 365
 *
 * Exemple:
 * - amount = 1000 KHU (100000000000 satoshis)
 * - R_annual = 1500 (15.00%)
 * - annual_yield = 100000000000 × 1500 / 10000 = 15000000000
 * - daily_yield = 15000000000 / 365 = 41095890 satoshis (~0.41 KHU/day)
 *
 * PROTECTION OVERFLOW:
 * - Uses int128_t for intermediate calculation
 * - Returns CAmount (int64_t) clamped to safe range
 *
 * @param amount Note amount (satoshis)
 * @param R_annual Annual yield rate (basis points, e.g., 1500 = 15.00%)
 * @return Daily yield amount (satoshis), 0 on overflow
 */
CAmount CalculateDailyYieldForNote(CAmount amount, uint16_t R_annual);

/**
 * IsNoteMature - Check if a note is mature for yield distribution
 *
 * RÈGLE CONSENSUS:
 * - Note must be locked for at least MATURITY_BLOCKS (4320 blocks = 3 days)
 *
 * @param noteHeight Height when note was created (nLockStartHeight)
 * @param currentHeight Current block height
 * @return true if note is mature (currentHeight - noteHeight >= MATURITY_BLOCKS)
 */
bool IsNoteMature(uint32_t noteHeight, uint32_t currentHeight);

} // namespace khu_yield

#endif // HU_HU_YIELD_H
