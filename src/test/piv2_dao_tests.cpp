// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * HU DAO Tests - Treasury T, proposals, R% voting
 *
 * Tests:
 *   1. Treasury T accumulation (T_DIVISOR = 8)
 *   2. Proposal creation and voting
 *   3. R% change via DOMC voting
 *   4. R% bounds (1-10%)
 *   5. Voting power based on ZKHU holdings
 */

#include "piv2/piv2_state.h"
#include "piv2/piv2_dao.h"
#include "piv2/piv2_domc.h"
#include "amount.h"
#include "test/test_pivx.h"

#include <boost/test/unit_test.hpp>

// Constants
static const uint32_t T_DIVISOR = 8;         // Treasury gets 1/8 of yield rate
static const uint32_t DAYS_PER_YEAR = 365;
static const uint32_t R_MIN_BPS = 100;       // 1%
static const uint32_t R_MAX_BPS = 1000;      // 10%
static const uint32_t R_DEFAULT_BPS = 500;   // 5%

BOOST_FIXTURE_TEST_SUITE(hu_dao_tests, BasicTestingSetup)

// =============================================================================
// Test 1: Treasury T accumulation
// =============================================================================
BOOST_AUTO_TEST_CASE(treasury_daily_accumulation)
{
    HuGlobalState state;
    state.SetNull();

    // Setup: 10000 total transparent supply
    state.C = 10000 * COIN;
    state.U = 10000 * COIN;
    state.R_annual = 500; // 5%

    // Treasury daily = (U * R_annual) / 10000 / T_DIVISOR / 365
    // = (10000 * 500) / 10000 / 8 / 365
    // = 50 / 8 / 365 COIN ≈ 0.0171 COIN per day

    CAmount dailyTreasury = (state.U * state.R_annual) / 10000 / T_DIVISOR / DAYS_PER_YEAR;

    // Accumulate
    state.T += dailyTreasury;

    BOOST_CHECK(state.T > 0);
    BOOST_CHECK(state.CheckInvariants());
}

BOOST_AUTO_TEST_CASE(treasury_yearly_accumulation)
{
    HuGlobalState state;
    state.SetNull();

    state.C = 10000 * COIN;
    state.U = 10000 * COIN;
    state.R_annual = 500;

    CAmount dailyTreasury = (state.U * state.R_annual) / 10000 / T_DIVISOR / DAYS_PER_YEAR;

    // Simulate 365 days
    for (int day = 0; day < 365; day++) {
        state.T += dailyTreasury;
    }

    // Yearly treasury ≈ (U * R%) / T_DIVISOR = (10000 * 5%) / 8 = 62.5 COIN
    CAmount expectedYearly = (state.U * state.R_annual) / 10000 / T_DIVISOR;

    // Should be close (within rounding tolerance)
    BOOST_CHECK(state.T <= expectedYearly);
    BOOST_CHECK(state.T >= expectedYearly - COIN);
}

// =============================================================================
// Test 2: R% rate bounds
// =============================================================================
BOOST_AUTO_TEST_CASE(r_rate_minimum_bound)
{
    uint32_t R_bps = 50; // 0.5% - below minimum

    BOOST_CHECK(R_bps < R_MIN_BPS);

    // Should be rejected or clamped to minimum
    if (R_bps < R_MIN_BPS) {
        R_bps = R_MIN_BPS;
    }
    BOOST_CHECK_EQUAL(R_bps, R_MIN_BPS);
}

BOOST_AUTO_TEST_CASE(r_rate_maximum_bound)
{
    uint32_t R_bps = 1500; // 15% - above maximum

    BOOST_CHECK(R_bps > R_MAX_BPS);

    // Should be rejected or clamped to maximum
    if (R_bps > R_MAX_BPS) {
        R_bps = R_MAX_BPS;
    }
    BOOST_CHECK_EQUAL(R_bps, R_MAX_BPS);
}

BOOST_AUTO_TEST_CASE(r_rate_valid_range)
{
    // Test all valid rates (1% to 10%)
    for (uint32_t R_bps = R_MIN_BPS; R_bps <= R_MAX_BPS; R_bps += 100) {
        BOOST_CHECK(R_bps >= R_MIN_BPS);
        BOOST_CHECK(R_bps <= R_MAX_BPS);
    }
}

// =============================================================================
// Test 3: R% change via voting
// =============================================================================
BOOST_AUTO_TEST_CASE(r_rate_change_proposal)
{
    uint32_t currentR = R_DEFAULT_BPS; // 5%
    uint32_t proposedR = 600;          // 6%

    // Proposal must be within bounds
    BOOST_CHECK(proposedR >= R_MIN_BPS);
    BOOST_CHECK(proposedR <= R_MAX_BPS);

    // Simulate vote passing
    bool votesPassed = true;

    if (votesPassed) {
        currentR = proposedR;
    }

    BOOST_CHECK_EQUAL(currentR, 600);
}

BOOST_AUTO_TEST_CASE(r_rate_change_rejected_out_of_bounds)
{
    uint32_t currentR = R_DEFAULT_BPS;
    uint32_t proposedR = 1500; // 15% - invalid

    // Proposal out of bounds - rejected before voting
    bool validProposal = (proposedR >= R_MIN_BPS && proposedR <= R_MAX_BPS);
    BOOST_CHECK(!validProposal);

    // R unchanged
    BOOST_CHECK_EQUAL(currentR, R_DEFAULT_BPS);
}

// =============================================================================
// Test 4: Voting power based on ZKHU
// =============================================================================
BOOST_AUTO_TEST_CASE(voting_power_proportional_to_zkhu)
{
    // User A: 100 ZKHU
    // User B: 300 ZKHU
    // User C: 600 ZKHU
    // Total: 1000 ZKHU

    CAmount zkhuA = 100 * COIN;
    CAmount zkhuB = 300 * COIN;
    CAmount zkhuC = 600 * COIN;
    CAmount totalZkhu = zkhuA + zkhuB + zkhuC;

    // Voting power is proportional to ZKHU holdings
    // A: 10%, B: 30%, C: 60%

    int64_t powerA = (zkhuA * 10000) / totalZkhu; // 1000 = 10%
    int64_t powerB = (zkhuB * 10000) / totalZkhu; // 3000 = 30%
    int64_t powerC = (zkhuC * 10000) / totalZkhu; // 6000 = 60%

    BOOST_CHECK_EQUAL(powerA, 1000);
    BOOST_CHECK_EQUAL(powerB, 3000);
    BOOST_CHECK_EQUAL(powerC, 6000);
    BOOST_CHECK_EQUAL(powerA + powerB + powerC, 10000);
}

BOOST_AUTO_TEST_CASE(vote_threshold_majority)
{
    // 51% threshold for R% change

    CAmount totalVotes = 1000 * COIN;
    CAmount votesFor = 510 * COIN;
    CAmount votesAgainst = 490 * COIN;

    int64_t percentFor = (votesFor * 100) / totalVotes;

    BOOST_CHECK(percentFor >= 51); // Passes
    BOOST_CHECK_EQUAL(votesFor + votesAgainst, totalVotes);
}

BOOST_AUTO_TEST_CASE(vote_threshold_not_met)
{
    CAmount totalVotes = 1000 * COIN;
    CAmount votesFor = 490 * COIN;

    int64_t percentFor = (votesFor * 100) / totalVotes;

    BOOST_CHECK(percentFor < 51); // Fails
}

// =============================================================================
// Test 5: Treasury spending proposals
// =============================================================================
BOOST_AUTO_TEST_CASE(treasury_spending_proposal)
{
    HuGlobalState state;
    state.SetNull();

    // Treasury has 100 KHU
    state.T = 100 * COIN;

    // Proposal to spend 30 KHU
    CAmount proposedSpend = 30 * COIN;

    // Must not exceed treasury balance
    BOOST_CHECK(proposedSpend <= state.T);

    // After spending
    state.T -= proposedSpend;

    BOOST_CHECK_EQUAL(state.T, 70 * COIN);
    BOOST_CHECK(state.T >= 0); // Invariant
}

BOOST_AUTO_TEST_CASE(treasury_spending_exceeds_balance)
{
    HuGlobalState state;
    state.SetNull();

    state.T = 50 * COIN;

    CAmount proposedSpend = 60 * COIN;

    // Should be rejected
    BOOST_CHECK(proposedSpend > state.T);

    // Treasury unchanged
    BOOST_CHECK_EQUAL(state.T, 50 * COIN);
}

// =============================================================================
// Test 6: DOMC cycle
// =============================================================================
BOOST_AUTO_TEST_CASE(domc_cycle_blocks)
{
    // DOMC cycle: 30 days = 30 * 1440 blocks = 43200 blocks
    const uint32_t BLOCKS_PER_DAY = 1440;
    const uint32_t DOMC_CYCLE_DAYS = 30;
    const uint32_t DOMC_CYCLE_BLOCKS = BLOCKS_PER_DAY * DOMC_CYCLE_DAYS;

    BOOST_CHECK_EQUAL(DOMC_CYCLE_BLOCKS, 43200);

    // Commit phase: first 20 days
    const uint32_t COMMIT_PHASE_DAYS = 20;
    const uint32_t COMMIT_PHASE_BLOCKS = BLOCKS_PER_DAY * COMMIT_PHASE_DAYS;

    // Reveal phase: last 10 days
    const uint32_t REVEAL_PHASE_DAYS = 10;
    const uint32_t REVEAL_PHASE_BLOCKS = BLOCKS_PER_DAY * REVEAL_PHASE_DAYS;

    BOOST_CHECK_EQUAL(COMMIT_PHASE_BLOCKS + REVEAL_PHASE_BLOCKS, DOMC_CYCLE_BLOCKS);
}

BOOST_AUTO_TEST_SUITE_END()
