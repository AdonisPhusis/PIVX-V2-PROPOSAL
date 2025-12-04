// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef HU_HU_DAO_H
#define HU_HU_DAO_H

#include "amount.h"
#include "consensus/params.h"

// Forward declarations
struct HuGlobalState;

namespace khu_dao {

// ============================================================================
// Constants (consensus-critical)
// ============================================================================

/**
 * DAO Treasury accumulates ~5% annual (at R=40%), calculated daily (same trigger as yield).
 *
 * FORMULA:
 *   T_daily = (U × R_annual) / 10000 / T_DIVISOR / 365
 *
 * Where T_DIVISOR = 8 (from khu_domc.h) => Treasury = 1/8 of yield rate
 *
 * Example at R=40% (4000 bp):
 *   T_annual = U × R / 10000 / T_DIVISOR = U × 4000 / 10000 / 8 = U × 0.05 = 5%
 *
 * Example at R=7% (700 bp, year 33):
 *   T_annual = U × 700 / 10000 / 8 = U × 0.00875 = 0.875%
 *
 * As R% decreases over 33 years (40%→7%), T% also decreases proportionally.
 */
static const uint32_t DAO_CYCLE_LENGTH = 1440;    // Daily (same as yield)

/**
 * Check if current height is a DAO cycle boundary (daily)
 *
 * DAO Treasury accumulation happens every 1440 blocks (daily, same as yield).
 * Unified with ApplyDailyUpdatesIfNeeded() in ConnectBlock.
 *
 * @param nHeight Current block height
 * @param nActivationHeight KHU V6.0 activation height
 * @return true if daily yield should be applied
 */
bool IsDaoCycleBoundary(
    uint32_t nHeight,
    uint32_t nActivationHeight
);

/**
 * Calculate daily DAO Treasury budget from current global state
 *
 * FORMULA:
 *   T_daily = (U × R_annual) / 10000 / T_DIVISOR / 365
 *
 * Where T_DIVISOR = 8 => Treasury = 1/8 of yield rate (~5% at R=40%)
 *
 * Example at R=40% (4000 bp):
 * - U = 1,000,000 KHU (1e14 satoshis)
 * - T_daily = 1e14 × 4000 / 10000 / 8 / 365 = 1,369,863,013 satoshis (~1369.9 KHU/day)
 * - Annual: 1369.9 × 365 = 500,000 KHU ≈ 5% of 1M
 *
 * Note: T% decreases proportionally as R% decreases over 33 years.
 *
 * Uses 128-bit arithmetic to prevent overflow.
 *
 * @param state Current global state (uses U and R_annual)
 * @return Daily DAO budget in satoshis (0 on overflow)
 */
CAmount CalculateDAOBudget(
    const HuGlobalState& state
);

/**
 * Accumulate DAO treasury if at cycle boundary
 *
 * CONSENSUS CRITICAL: Must be called FIRST in ConnectBlock (before yield).
 * Budget is calculated on INITIAL state (U+Ur before any modifications).
 *
 * @param state [IN/OUT] Global state to modify (T increases)
 * @param nHeight Current block height
 * @param consensusParams Consensus parameters (for activation height)
 * @return true on success, false on overflow
 */
bool AccumulateDaoTreasuryIfNeeded(
    HuGlobalState& state,
    uint32_t nHeight,
    const Consensus::Params& consensusParams
);

/**
 * Undo DAO treasury accumulation (for DisconnectBlock)
 *
 * Reverses AccumulateDaoTreasuryIfNeeded by subtracting the budget from T.
 *
 * @param state [IN/OUT] Global state to restore
 * @param nHeight Current block height
 * @param consensusParams Consensus parameters
 * @return true on success, false on underflow
 */
bool UndoDaoTreasuryIfNeeded(
    HuGlobalState& state,
    uint32_t nHeight,
    const Consensus::Params& consensusParams
);

/**
 * Deduct budget payment from DAO Treasury (T)
 *
 * Called when a budget proposal is paid (post-V6).
 * Proposals are funded from T instead of block inflation.
 *
 * CONSENSUS CRITICAL: Must be called in ProcessHUBlock after all transactions
 * when a budget payment is detected in the coinbase.
 *
 * @param state [IN/OUT] Global state to modify (T decreases)
 * @param nAmount Amount to deduct (in satoshis)
 * @return true if successful, false if T < nAmount (insufficient funds)
 */
bool DeductBudgetPayment(
    HuGlobalState& state,
    CAmount nAmount
);

/**
 * Undo budget payment deduction (for DisconnectBlock)
 *
 * Reverses DeductBudgetPayment by adding back the amount to T.
 *
 * @param state [IN/OUT] Global state to restore
 * @param nAmount Amount to restore (in satoshis)
 * @return true on success, false on overflow
 */
bool UndoBudgetPayment(
    HuGlobalState& state,
    CAmount nAmount
);

} // namespace khu_dao

#endif // HU_HU_DAO_H
