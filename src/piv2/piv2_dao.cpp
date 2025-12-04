// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "piv2/piv2_dao.h"
#include "piv2/piv2_domc.h"
#include "piv2/piv2_state.h"
#include "consensus/consensus.h"
#include "logging.h"

#include <boost/multiprecision/cpp_int.hpp>
#include <limits>

using namespace boost::multiprecision;

namespace khu_dao {

bool IsDaoCycleBoundary(uint32_t nHeight, uint32_t nActivationHeight)
{
    if (nHeight <= nActivationHeight) {
        return false;
    }

    uint32_t blocks_since_activation = nHeight - nActivationHeight;
    return (blocks_since_activation % DAO_CYCLE_LENGTH) == 0;
}

CAmount CalculateDAOBudget(const HuGlobalState& state)
{
    // Use 128-bit arithmetic to prevent overflow
    int128_t U = state.U;
    int128_t R_annual = state.R_annual;

    if (U < 0 || R_annual < 0) {
        LogPrintf("ERROR: CalculateDAOBudget: negative U=%lld R_annual=%u\n",
                  (long long)state.U, state.R_annual);
        return 0;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // FORMULA:
    //   T_daily = (U × R_annual) / 10000 / T_DIVISOR / 365
    //
    // Where T_DIVISOR = 8 (from khu_domc.h) => Treasury = 1/8 of yield rate
    //
    // At R=40% (4000 bp): T_annual = U × 4000 / 10000 / 8 = U × 0.05 = 5%
    // At R=7% (700 bp):   T_annual = U × 700 / 10000 / 8 = U × 0.00875 = 0.875%
    // As R% decreases (40%→7% over 33 years), T% decreases proportionally.
    //
    // IMPORTANT: T is in PIV satoshis!
    // - Uses U (KHU supply) as index for economic activity
    // - But T itself is PIV, not KHU
    // - DAO payments are in PIV (no impact on C/U/Z invariants)
    // ═══════════════════════════════════════════════════════════════════════
    int128_t budget = (U * R_annual) / 10000 / khu_domc::T_DIVISOR / 365;

    // Check if budget exceeds CAmount limits
    if (budget < 0 || budget > std::numeric_limits<CAmount>::max()) {
        LogPrintf("ERROR: CalculateDAOBudget: overflow budget=%s (U=%lld R_annual=%u)\n",
                  budget.str().c_str(), (long long)state.U, state.R_annual);
        return 0;
    }

    return static_cast<CAmount>(budget);
}

bool AccumulateDaoTreasuryIfNeeded(
    HuGlobalState& state,
    uint32_t nHeight,
    const Consensus::Params& consensusParams
)
{
    if (!IsDaoCycleBoundary(nHeight, consensusParams.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight)) {
        return true; // Not at boundary, nothing to do
    }

    LogPrint(BCLog::HU, "AccumulateDaoTreasury: height=%u, U=%lld, R_annual=%u, T_before=%lld (formula: U*R/10000/20/365)\n",
             nHeight, (long long)state.U, state.R_annual, (long long)state.T);

    CAmount budget = CalculateDAOBudget(state);

    if (budget <= 0) {
        LogPrint(BCLog::HU, "AccumulateDaoTreasury: budget=%lld (skipping)\n", (long long)budget);
        return true; // Nothing to accumulate
    }

    // Add budget to T
    int128_t new_T = static_cast<int128_t>(state.T) + static_cast<int128_t>(budget);

    if (new_T < 0 || new_T > std::numeric_limits<CAmount>::max()) {
        LogPrintf("ERROR: AccumulateDaoTreasury overflow: T=%lld, budget=%lld\n",
                  (long long)state.T, (long long)budget);
        return false;
    }

    state.T = static_cast<CAmount>(new_T);

    LogPrint(BCLog::HU, "AccumulateDaoTreasury: budget=%lld, T_after=%lld\n",
             (long long)budget, (long long)state.T);

    return true;
}

bool UndoDaoTreasuryIfNeeded(
    HuGlobalState& state,
    uint32_t nHeight,
    const Consensus::Params& consensusParams
)
{
    if (!IsDaoCycleBoundary(nHeight, consensusParams.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight)) {
        return true; // Nothing to undo
    }

    LogPrint(BCLog::HU, "UndoDaoTreasury: height=%u, T_before=%lld\n",
             nHeight, (long long)state.T);

    CAmount budget = CalculateDAOBudget(state);

    // Subtract budget from T
    int128_t new_T = static_cast<int128_t>(state.T) - static_cast<int128_t>(budget);

    if (new_T < 0) {
        LogPrintf("ERROR: UndoDaoTreasury underflow: T=%lld, budget=%lld\n",
                  (long long)state.T, (long long)budget);
        return false;
    }

    state.T = static_cast<CAmount>(new_T);

    LogPrint(BCLog::HU, "UndoDaoTreasury: budget=%lld, T_after=%lld\n",
             (long long)budget, (long long)state.T);

    return true;
}

bool DeductBudgetPayment(HuGlobalState& state, CAmount nAmount)
{
    if (nAmount <= 0) {
        return true; // Nothing to deduct
    }

    LogPrint(BCLog::HU, "DeductBudgetPayment: amount=%lld, T_before=%lld\n",
             (long long)nAmount, (long long)state.T);

    // Check if T has sufficient funds
    if (state.T < nAmount) {
        LogPrintf("ERROR: DeductBudgetPayment: insufficient T=%lld for payment=%lld\n",
                  (long long)state.T, (long long)nAmount);
        return false;
    }

    // Deduct from T
    state.T -= nAmount;

    LogPrint(BCLog::HU, "DeductBudgetPayment: T_after=%lld (deducted %lld)\n",
             (long long)state.T, (long long)nAmount);

    return true;
}

bool UndoBudgetPayment(HuGlobalState& state, CAmount nAmount)
{
    if (nAmount <= 0) {
        return true; // Nothing to undo
    }

    LogPrint(BCLog::HU, "UndoBudgetPayment: amount=%lld, T_before=%lld\n",
             (long long)nAmount, (long long)state.T);

    // Check for overflow
    int128_t new_T = static_cast<int128_t>(state.T) + static_cast<int128_t>(nAmount);

    if (new_T < 0 || new_T > std::numeric_limits<CAmount>::max()) {
        LogPrintf("ERROR: UndoBudgetPayment overflow: T=%lld, amount=%lld\n",
                  (long long)state.T, (long long)nAmount);
        return false;
    }

    state.T = static_cast<CAmount>(new_T);

    LogPrint(BCLog::HU, "UndoBudgetPayment: T_after=%lld (restored %lld)\n",
             (long long)state.T, (long long)nAmount);

    return true;
}

} // namespace khu_dao
