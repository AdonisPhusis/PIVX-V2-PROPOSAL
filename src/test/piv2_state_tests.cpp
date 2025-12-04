// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * HU State Tests - Global state machine C/U/Z/Cr/Ur/T
 *
 * Tests:
 *   1. Genesis state initialization (all zeros)
 *   2. Invariant C == U + Z
 *   3. Invariant Cr == Ur
 *   4. Invariant T >= 0
 *   5. State serialization/deserialization
 */

#include "piv2/piv2_state.h"
#include "piv2/piv2_statedb.h"
#include "amount.h"
#include "test/test_pivx.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(hu_state_tests, BasicTestingSetup)

// =============================================================================
// Test 1: Genesis state - all values zero
// =============================================================================
BOOST_AUTO_TEST_CASE(genesis_state_all_zeros)
{
    HuGlobalState state;
    state.SetNull();

    BOOST_CHECK_EQUAL(state.C, 0);
    BOOST_CHECK_EQUAL(state.U, 0);
    BOOST_CHECK_EQUAL(state.Z, 0);
    BOOST_CHECK_EQUAL(state.Cr, 0);
    BOOST_CHECK_EQUAL(state.Ur, 0);
    BOOST_CHECK_EQUAL(state.T, 0);
    BOOST_CHECK(state.CheckInvariants());
}

// =============================================================================
// Test 2: Invariant C == U + Z
// =============================================================================
BOOST_AUTO_TEST_CASE(invariant_c_equals_u_plus_z)
{
    HuGlobalState state;
    state.SetNull();

    // Valid: C = 100, U = 60, Z = 40 => C == U + Z
    state.C = 100 * COIN;
    state.U = 60 * COIN;
    state.Z = 40 * COIN;
    BOOST_CHECK(state.CheckInvariants());

    // Invalid: C = 100, U = 50, Z = 40 => C != U + Z
    state.U = 50 * COIN;
    BOOST_CHECK(!state.CheckInvariants());

    // Valid again: Z = 50 => C == U + Z
    state.Z = 50 * COIN;
    BOOST_CHECK(state.CheckInvariants());
}

// =============================================================================
// Test 3: Invariant Cr == Ur
// =============================================================================
BOOST_AUTO_TEST_CASE(invariant_cr_equals_ur)
{
    HuGlobalState state;
    state.SetNull();

    // Setup valid C/U/Z
    state.C = 100 * COIN;
    state.U = 100 * COIN;
    state.Z = 0;

    // Valid: Cr = Ur = 5
    state.Cr = 5 * COIN;
    state.Ur = 5 * COIN;
    BOOST_CHECK(state.CheckInvariants());

    // Invalid: Cr != Ur
    state.Cr = 6 * COIN;
    BOOST_CHECK(!state.CheckInvariants());

    // Valid again
    state.Ur = 6 * COIN;
    BOOST_CHECK(state.CheckInvariants());
}

// =============================================================================
// Test 4: Invariant T >= 0 (Treasury non-negative)
// =============================================================================
BOOST_AUTO_TEST_CASE(invariant_treasury_non_negative)
{
    HuGlobalState state;
    state.SetNull();

    // Setup valid base state
    state.C = 100 * COIN;
    state.U = 100 * COIN;
    state.Cr = 0;
    state.Ur = 0;

    // Valid: T = 0
    state.T = 0;
    BOOST_CHECK(state.CheckInvariants());

    // Valid: T > 0
    state.T = 10 * COIN;
    BOOST_CHECK(state.CheckInvariants());

    // Note: T is CAmount (int64_t), negative would fail invariant
    // In practice, operations should never allow T < 0
}

// =============================================================================
// Test 5: State serialization round-trip
// =============================================================================
BOOST_AUTO_TEST_CASE(state_serialization_roundtrip)
{
    HuGlobalState original;
    original.C = 1000 * COIN;
    original.U = 600 * COIN;
    original.Z = 400 * COIN;
    original.Cr = 50 * COIN;
    original.Ur = 50 * COIN;
    original.T = 25 * COIN;
    original.nHeight = 12345;
    original.R_annual = 500; // 5%

    // Serialize
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << original;

    // Deserialize
    HuGlobalState loaded;
    ss >> loaded;

    // Verify all fields
    BOOST_CHECK_EQUAL(loaded.C, original.C);
    BOOST_CHECK_EQUAL(loaded.U, original.U);
    BOOST_CHECK_EQUAL(loaded.Z, original.Z);
    BOOST_CHECK_EQUAL(loaded.Cr, original.Cr);
    BOOST_CHECK_EQUAL(loaded.Ur, original.Ur);
    BOOST_CHECK_EQUAL(loaded.T, original.T);
    BOOST_CHECK_EQUAL(loaded.nHeight, original.nHeight);
    BOOST_CHECK_EQUAL(loaded.R_annual, original.R_annual);
    BOOST_CHECK(loaded.CheckInvariants());
}

// =============================================================================
// Test 6: Large values - no overflow
// =============================================================================
BOOST_AUTO_TEST_CASE(large_values_no_overflow)
{
    HuGlobalState state;
    state.SetNull();

    // Max realistic supply: 21M PIVX = 21,000,000 * COIN
    const CAmount MAX_SUPPLY = 21000000LL * COIN;

    state.C = MAX_SUPPLY;
    state.U = MAX_SUPPLY / 2;
    state.Z = MAX_SUPPLY / 2;
    state.Cr = MAX_SUPPLY / 100; // 1% in rewards
    state.Ur = MAX_SUPPLY / 100;
    state.T = MAX_SUPPLY / 1000;

    BOOST_CHECK(state.CheckInvariants());
}

BOOST_AUTO_TEST_SUITE_END()
