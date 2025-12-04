// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * HU Integration Tests - End-to-end scenarios
 *
 * Tests:
 *   1. Full lifecycle: MINT → LOCK → yield → UNLOCK → REDEEM
 *   2. Multi-user scenario
 *   3. DAO proposal + execution
 *   4. Edge cases and stress tests
 */

#include "piv2/piv2_state.h"
#include "amount.h"
#include "test/test_pivx.h"

#include <boost/test/unit_test.hpp>

// Constants
static const uint32_t BLOCKS_PER_DAY = 1440;
static const uint32_t MATURITY_BLOCKS = 4320; // 3 days
static const uint32_t DAYS_PER_YEAR = 365;
static const uint32_t T_DIVISOR = 8;

BOOST_FIXTURE_TEST_SUITE(hu_integration_tests, BasicTestingSetup)

// =============================================================================
// Test 1: Full lifecycle
// =============================================================================
BOOST_AUTO_TEST_CASE(full_lifecycle_mint_lock_unlock_redeem)
{
    HuGlobalState state;
    state.SetNull();
    state.R_annual = 500; // 5%

    const CAmount userPivx = 1000 * COIN;

    // === Day 0: MINT 1000 PIVX → 1000 KHU_T ===
    state.C += userPivx;
    state.U += userPivx;
    BOOST_CHECK(state.CheckInvariants());
    BOOST_CHECK_EQUAL(state.C, 1000 * COIN);

    // === Day 0: LOCK 1000 KHU_T → 1000 ZKHU ===
    state.U -= 1000 * COIN;
    state.Z += 1000 * COIN;
    BOOST_CHECK(state.CheckInvariants());
    BOOST_CHECK_EQUAL(state.Z, 1000 * COIN);

    // === Day 0-3: Maturity period (no yield) ===
    // Skip 4320 blocks...

    // === Day 3+: Yield accumulation ===
    // Simulate 30 days of yield after maturity
    CAmount dailyYield = (state.Z * state.R_annual) / 10000 / DAYS_PER_YEAR;
    for (int day = 0; day < 30; day++) {
        state.Cr += dailyYield;
        state.Ur += dailyYield;
    }
    CAmount totalYield = dailyYield * 30;
    BOOST_CHECK(state.CheckInvariants());

    // === Day 33: UNLOCK 1000 ZKHU → 1000 + yield KHU_T ===
    CAmount principal = 1000 * COIN;
    state.Z  -= principal;
    state.U  += principal + totalYield;
    state.C  += totalYield;
    state.Cr -= totalYield;
    state.Ur -= totalYield;
    BOOST_CHECK(state.CheckInvariants());
    BOOST_CHECK_EQUAL(state.Z, 0);
    BOOST_CHECK(state.U > 1000 * COIN); // Has yield

    // === Day 33: REDEEM all KHU_T → PIVX ===
    CAmount finalBalance = state.U;
    state.C -= finalBalance;
    state.U -= finalBalance;
    BOOST_CHECK(state.CheckInvariants());
    BOOST_CHECK_EQUAL(state.C, 0);
    BOOST_CHECK_EQUAL(state.U, 0);

    // User received more PIVX than started with (yield earned)
    BOOST_CHECK(finalBalance > userPivx);
}

// =============================================================================
// Test 2: Multi-user scenario
// =============================================================================
BOOST_AUTO_TEST_CASE(multi_user_concurrent_operations)
{
    HuGlobalState state;
    state.SetNull();
    state.R_annual = 500;

    // User A: MINT 500
    state.C += 500 * COIN;
    state.U += 500 * COIN;

    // User B: MINT 300
    state.C += 300 * COIN;
    state.U += 300 * COIN;

    // User C: MINT 200
    state.C += 200 * COIN;
    state.U += 200 * COIN;

    BOOST_CHECK_EQUAL(state.C, 1000 * COIN);
    BOOST_CHECK(state.CheckInvariants());

    // User A: LOCK 400
    state.U -= 400 * COIN;
    state.Z += 400 * COIN;

    // User B: LOCK 300
    state.U -= 300 * COIN;
    state.Z += 300 * COIN;

    BOOST_CHECK_EQUAL(state.Z, 700 * COIN);
    BOOST_CHECK_EQUAL(state.U, 300 * COIN); // 100 (A) + 0 (B) + 200 (C)
    BOOST_CHECK(state.CheckInvariants());

    // Yield accumulation (10 days)
    CAmount dailyYield = (state.Z * state.R_annual) / 10000 / DAYS_PER_YEAR;
    for (int day = 0; day < 10; day++) {
        state.Cr += dailyYield;
        state.Ur += dailyYield;
    }
    BOOST_CHECK(state.CheckInvariants());

    // User A: UNLOCK 400 (with proportional yield)
    CAmount userAYield = (400 * COIN * dailyYield * 10) / state.Z;
    state.Z  -= 400 * COIN;
    state.U  += 400 * COIN + userAYield;
    state.C  += userAYield;
    state.Cr -= userAYield;
    state.Ur -= userAYield;
    BOOST_CHECK(state.CheckInvariants());
}

// =============================================================================
// Test 3: Treasury accumulation and spending
// =============================================================================
BOOST_AUTO_TEST_CASE(treasury_lifecycle)
{
    HuGlobalState state;
    state.SetNull();
    state.R_annual = 500;

    // Setup: 10000 KHU_T
    state.C = 10000 * COIN;
    state.U = 10000 * COIN;

    // Treasury accumulation over 90 days
    CAmount dailyTreasury = (state.U * state.R_annual) / 10000 / T_DIVISOR / DAYS_PER_YEAR;
    for (int day = 0; day < 90; day++) {
        state.T += dailyTreasury;
    }

    CAmount treasuryAfter90Days = state.T;
    BOOST_CHECK(treasuryAfter90Days > 0);
    BOOST_CHECK(state.CheckInvariants());

    // DAO proposal: spend 50% of treasury
    CAmount proposedSpend = treasuryAfter90Days / 2;
    BOOST_CHECK(proposedSpend <= state.T);

    // Execute spending
    state.T -= proposedSpend;
    BOOST_CHECK_EQUAL(state.T, treasuryAfter90Days / 2);
    BOOST_CHECK(state.T >= 0);
}

// =============================================================================
// Test 4: R% change impact
// =============================================================================
BOOST_AUTO_TEST_CASE(r_rate_change_impact)
{
    HuGlobalState state;
    state.SetNull();

    state.C = 1000 * COIN;
    state.U = 0;
    state.Z = 1000 * COIN;
    state.R_annual = 500; // 5%

    // Yield at 5%
    CAmount yieldAt5pct = (state.Z * 500) / 10000 / DAYS_PER_YEAR;

    // Change R to 8%
    state.R_annual = 800;

    // Yield at 8%
    CAmount yieldAt8pct = (state.Z * 800) / 10000 / DAYS_PER_YEAR;

    // 8% yield should be 1.6x of 5%
    BOOST_CHECK(yieldAt8pct > yieldAt5pct);
    BOOST_CHECK_EQUAL(yieldAt8pct * 5, yieldAt5pct * 8); // Proportional
}

// =============================================================================
// Test 5: Edge cases
// =============================================================================
BOOST_AUTO_TEST_CASE(edge_case_zero_amounts)
{
    HuGlobalState state;
    state.SetNull();

    // All zeros should pass invariants
    BOOST_CHECK(state.CheckInvariants());

    // MINT 0 (no-op)
    state.C += 0;
    state.U += 0;
    BOOST_CHECK(state.CheckInvariants());
}

BOOST_AUTO_TEST_CASE(edge_case_minimum_amount)
{
    HuGlobalState state;
    state.SetNull();

    // MINT 1 satoshi
    state.C += 1;
    state.U += 1;
    BOOST_CHECK(state.CheckInvariants());

    // LOCK 1 satoshi
    state.U -= 1;
    state.Z += 1;
    BOOST_CHECK(state.CheckInvariants());
}

BOOST_AUTO_TEST_CASE(edge_case_large_amounts)
{
    HuGlobalState state;
    state.SetNull();

    // Max supply scenario
    const CAmount maxSupply = 21000000LL * COIN;

    state.C = maxSupply;
    state.U = maxSupply / 2;
    state.Z = maxSupply / 2;
    state.R_annual = 1000; // 10% (max rate)

    BOOST_CHECK(state.CheckInvariants());

    // Yield calculation should not overflow
    CAmount dailyYield = (state.Z * state.R_annual) / 10000 / DAYS_PER_YEAR;
    BOOST_CHECK(dailyYield > 0);
    BOOST_CHECK(dailyYield < state.Z); // Sanity check
}

// =============================================================================
// Test 6: Invariant preservation under stress
// =============================================================================
BOOST_AUTO_TEST_CASE(stress_test_random_operations)
{
    HuGlobalState state;
    state.SetNull();
    state.R_annual = 500;

    // Initial MINT
    state.C = 10000 * COIN;
    state.U = 10000 * COIN;

    // Simulate 100 random-ish operations
    for (int i = 0; i < 100; i++) {
        int op = i % 4;
        CAmount amount = ((i % 10) + 1) * COIN;

        switch (op) {
        case 0: // MINT
            state.C += amount;
            state.U += amount;
            break;
        case 1: // LOCK (if U available)
            if (state.U >= amount) {
                state.U -= amount;
                state.Z += amount;
            }
            break;
        case 2: // UNLOCK (if Z available, simplified)
            if (state.Z >= amount) {
                state.Z -= amount;
                state.U += amount;
            }
            break;
        case 3: // REDEEM (if U available)
            if (state.U >= amount) {
                state.C -= amount;
                state.U -= amount;
            }
            break;
        }

        // Invariants MUST hold after every operation
        BOOST_CHECK(state.CheckInvariants());
    }
}

BOOST_AUTO_TEST_SUITE_END()
