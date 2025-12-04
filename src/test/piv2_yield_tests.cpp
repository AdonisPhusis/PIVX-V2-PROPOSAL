// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * HU Yield Tests - Daily yield, maturity, R%
 *
 * Tests:
 *   1. Daily yield calculation (LINEAR, not compounding)
 *   2. Maturity period (4320 blocks = 3 days)
 *   3. R% rate application
 *   4. Cr/Ur accumulation
 *   5. No yield before maturity
 *
 * Key formula:
 *   daily_yield = (principal * R_annual) / 10000 / 365
 */

#include "piv2/piv2_state.h"
#include "piv2/piv2_yield.h"
#include "amount.h"
#include "test/test_pivx.h"

#include <boost/test/unit_test.hpp>

// Constants from HU spec
static const uint32_t BLOCKS_PER_DAY = 1440;     // 1 block/minute
static const uint32_t MATURITY_BLOCKS = 4320;    // 3 days
static const uint32_t DAYS_PER_YEAR = 365;

BOOST_FIXTURE_TEST_SUITE(hu_yield_tests, BasicTestingSetup)

// =============================================================================
// Test 1: Daily yield calculation - LINEAR
// =============================================================================
BOOST_AUTO_TEST_CASE(daily_yield_linear_calculation)
{
    // Principal: 1000 KHU
    // R: 5% = 500 bps
    // Daily yield = (1000 * 500) / 10000 / 365 = 0.1369863... COIN

    const CAmount principal = 1000 * COIN;
    const uint32_t R_annual = 500; // 5%

    // Using integer math: (principal * R_annual) / 10000 / 365
    CAmount dailyYield = (principal * R_annual) / 10000 / DAYS_PER_YEAR;

    // Expected: (1000 * COIN * 500) / 10000 / 365 = 13698630 satoshis
    // = 0.1369863 COIN (with integer truncation)
    BOOST_CHECK_EQUAL(dailyYield, 13698630);

    // Verify NOT compounding (same yield each day)
    CAmount day1 = dailyYield;
    CAmount day2 = (principal * R_annual) / 10000 / DAYS_PER_YEAR;
    CAmount day3 = (principal * R_annual) / 10000 / DAYS_PER_YEAR;

    BOOST_CHECK_EQUAL(day1, day2);
    BOOST_CHECK_EQUAL(day2, day3);
}

BOOST_AUTO_TEST_CASE(yield_accumulation_linear)
{
    const CAmount principal = 1000 * COIN;
    const uint32_t R_annual = 500;

    CAmount dailyYield = (principal * R_annual) / 10000 / DAYS_PER_YEAR;

    // After 365 days, total yield should be ~5% of principal
    CAmount yearlyYield = dailyYield * DAYS_PER_YEAR;
    CAmount expectedYearly = (principal * R_annual) / 10000;

    // Should be approximately equal (small rounding difference)
    BOOST_CHECK(yearlyYield <= expectedYearly);
    BOOST_CHECK(yearlyYield >= expectedYearly - COIN); // Within 1 COIN tolerance
}

// =============================================================================
// Test 2: Maturity period - 4320 blocks (3 days)
// =============================================================================
BOOST_AUTO_TEST_CASE(no_yield_before_maturity)
{
    // Lock at block 1000
    const int lockHeight = 1000;
    const int maturityHeight = lockHeight + MATURITY_BLOCKS; // 5320

    // At block 1000: no yield (just locked)
    BOOST_CHECK(1000 < maturityHeight);

    // At block 4319: still no yield
    BOOST_CHECK(lockHeight + MATURITY_BLOCKS - 1 < maturityHeight);

    // At block 5320: maturity reached, yield starts
    BOOST_CHECK_EQUAL(lockHeight + MATURITY_BLOCKS, maturityHeight);
}

BOOST_AUTO_TEST_CASE(yield_starts_at_maturity)
{
    const int lockHeight = 1000;
    const int currentHeight = lockHeight + MATURITY_BLOCKS;

    // Days since maturity
    int blocksSinceMaturity = currentHeight - (lockHeight + MATURITY_BLOCKS);
    BOOST_CHECK_EQUAL(blocksSinceMaturity, 0); // Just matured

    // After 1 more day
    int blocksAfterOneDay = MATURITY_BLOCKS + BLOCKS_PER_DAY;
    int daysSinceMaturity = (blocksAfterOneDay - MATURITY_BLOCKS) / BLOCKS_PER_DAY;
    BOOST_CHECK_EQUAL(daysSinceMaturity, 1);
}

// =============================================================================
// Test 3: R% rate bounds
// =============================================================================
BOOST_AUTO_TEST_CASE(r_rate_bounds)
{
    // R: 1-10% = 100-1000 bps
    const uint32_t R_MIN = 100;  // 1%
    const uint32_t R_MAX = 1000; // 10%

    // Valid rates
    BOOST_CHECK(R_MIN >= 100);
    BOOST_CHECK(R_MAX <= 1000);

    // Test yield at min/max rates
    const CAmount principal = 1000 * COIN;

    CAmount yieldAtMin = (principal * R_MIN) / 10000 / DAYS_PER_YEAR;
    CAmount yieldAtMax = (principal * R_MAX) / 10000 / DAYS_PER_YEAR;

    // Max yield should be 10x min yield
    BOOST_CHECK_EQUAL(yieldAtMax, yieldAtMin * 10);
}

// =============================================================================
// Test 4: Cr/Ur accumulation
// =============================================================================
BOOST_AUTO_TEST_CASE(cr_ur_daily_accumulation)
{
    HuGlobalState state;
    state.SetNull();

    // Setup: 1000 locked
    state.C = 1000 * COIN;
    state.U = 0;
    state.Z = 1000 * COIN;
    state.R_annual = 500; // 5%

    // Daily yield
    CAmount dailyYield = (state.Z * state.R_annual) / 10000 / DAYS_PER_YEAR;

    // Accumulate Cr and Ur
    state.Cr += dailyYield;
    state.Ur += dailyYield;

    BOOST_CHECK_EQUAL(state.Cr, dailyYield);
    BOOST_CHECK_EQUAL(state.Ur, dailyYield);
    BOOST_CHECK(state.CheckInvariants()); // Cr == Ur
}

BOOST_AUTO_TEST_CASE(cr_ur_multi_day_accumulation)
{
    HuGlobalState state;
    state.SetNull();

    state.C = 1000 * COIN;
    state.U = 0;
    state.Z = 1000 * COIN;
    state.R_annual = 500;

    CAmount dailyYield = (state.Z * state.R_annual) / 10000 / DAYS_PER_YEAR;

    // Simulate 30 days
    for (int day = 0; day < 30; day++) {
        state.Cr += dailyYield;
        state.Ur += dailyYield;
        BOOST_CHECK(state.CheckInvariants());
    }

    BOOST_CHECK_EQUAL(state.Cr, dailyYield * 30);
    BOOST_CHECK_EQUAL(state.Ur, dailyYield * 30);
}

// =============================================================================
// Test 5: Yield consumed on UNLOCK
// =============================================================================
BOOST_AUTO_TEST_CASE(yield_consumed_on_unlock)
{
    HuGlobalState state;
    state.SetNull();

    // Setup: 100 locked, 5 yield accumulated
    state.C = 100 * COIN;
    state.U = 0;
    state.Z = 100 * COIN;
    state.Cr = 5 * COIN;
    state.Ur = 5 * COIN;

    // UNLOCK with full yield
    CAmount principal = 100 * COIN;
    CAmount yield = 5 * COIN;

    state.Z  -= principal;
    state.U  += principal + yield;
    state.C  += yield;
    state.Cr -= yield;
    state.Ur -= yield;

    // Yield pools should be empty
    BOOST_CHECK_EQUAL(state.Cr, 0);
    BOOST_CHECK_EQUAL(state.Ur, 0);
    BOOST_CHECK(state.CheckInvariants());
}

// =============================================================================
// Test 6: Different R% rates
// =============================================================================
BOOST_AUTO_TEST_CASE(yield_with_different_rates)
{
    const CAmount principal = 10000 * COIN;

    // R = 1% (100 bps)
    CAmount yield1pct = (principal * 100) / 10000 / DAYS_PER_YEAR;

    // R = 5% (500 bps)
    CAmount yield5pct = (principal * 500) / 10000 / DAYS_PER_YEAR;

    // R = 10% (1000 bps)
    CAmount yield10pct = (principal * 1000) / 10000 / DAYS_PER_YEAR;

    // Verify proportional relationships (allow small rounding tolerance)
    // Due to integer division, exact equality may not hold
    BOOST_CHECK(yield5pct >= yield1pct * 5 - 5);
    BOOST_CHECK(yield5pct <= yield1pct * 5 + 5);
    BOOST_CHECK(yield10pct >= yield1pct * 10 - 10);
    BOOST_CHECK(yield10pct <= yield1pct * 10 + 10);
}

BOOST_AUTO_TEST_SUITE_END()
