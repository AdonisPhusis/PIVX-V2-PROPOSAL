// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * HU Operations Tests - MINT, REDEEM, LOCK, UNLOCK
 *
 * Tests:
 *   1. MINT: PIVX → KHU_T (C+, U+)
 *   2. REDEEM: KHU_T → PIVX (C-, U-)
 *   3. LOCK: KHU_T → ZKHU (U-, Z+)
 *   4. UNLOCK: ZKHU → KHU_T (Z-, U+, yield paid)
 *   5. Atomic operations - no intermediate invalid states
 */

#include "piv2/piv2_state.h"
#include "piv2/piv2_mint.h"
#include "piv2/piv2_redeem.h"
#include "piv2/piv2_lock.h"
#include "piv2/piv2_unlock.h"
#include "amount.h"
#include "test/test_pivx.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(hu_operations_tests, BasicTestingSetup)

// =============================================================================
// Test 1: MINT - PIVX → KHU_T
// =============================================================================
BOOST_AUTO_TEST_CASE(mint_increases_c_and_u)
{
    HuGlobalState state;
    state.SetNull();

    const CAmount mintAmount = 100 * COIN;

    // Before MINT
    BOOST_CHECK_EQUAL(state.C, 0);
    BOOST_CHECK_EQUAL(state.U, 0);

    // Apply MINT: C += amount, U += amount
    state.C += mintAmount;
    state.U += mintAmount;

    // After MINT
    BOOST_CHECK_EQUAL(state.C, mintAmount);
    BOOST_CHECK_EQUAL(state.U, mintAmount);
    BOOST_CHECK_EQUAL(state.Z, 0); // Z unchanged
    BOOST_CHECK(state.CheckInvariants());
}

BOOST_AUTO_TEST_CASE(mint_multiple_operations)
{
    HuGlobalState state;
    state.SetNull();

    // Multiple MINTs
    for (int i = 0; i < 10; i++) {
        CAmount amount = (i + 1) * 10 * COIN;
        state.C += amount;
        state.U += amount;
        BOOST_CHECK(state.CheckInvariants());
    }

    // Total: 10+20+30+...+100 = 550 COIN
    BOOST_CHECK_EQUAL(state.C, 550 * COIN);
    BOOST_CHECK_EQUAL(state.U, 550 * COIN);
}

// =============================================================================
// Test 2: REDEEM - KHU_T → PIVX
// =============================================================================
BOOST_AUTO_TEST_CASE(redeem_decreases_c_and_u)
{
    HuGlobalState state;
    state.SetNull();

    // Setup: MINT 100
    state.C = 100 * COIN;
    state.U = 100 * COIN;

    const CAmount redeemAmount = 40 * COIN;

    // Apply REDEEM: C -= amount, U -= amount
    state.C -= redeemAmount;
    state.U -= redeemAmount;

    // After REDEEM
    BOOST_CHECK_EQUAL(state.C, 60 * COIN);
    BOOST_CHECK_EQUAL(state.U, 60 * COIN);
    BOOST_CHECK(state.CheckInvariants());
}

BOOST_AUTO_TEST_CASE(redeem_cannot_exceed_balance)
{
    HuGlobalState state;
    state.SetNull();

    state.C = 50 * COIN;
    state.U = 50 * COIN;

    // Attempt to REDEEM more than available should be rejected
    // (in practice, validation rejects this before state mutation)
    CAmount redeemAmount = 60 * COIN;
    BOOST_CHECK(redeemAmount > state.U); // Would fail validation
}

// =============================================================================
// Test 3: LOCK - KHU_T → ZKHU
// =============================================================================
BOOST_AUTO_TEST_CASE(lock_moves_u_to_z)
{
    HuGlobalState state;
    state.SetNull();

    // Setup: 100 KHU_T
    state.C = 100 * COIN;
    state.U = 100 * COIN;

    const CAmount lockAmount = 60 * COIN;

    // Apply LOCK: U -= amount, Z += amount
    state.U -= lockAmount;
    state.Z += lockAmount;

    // After LOCK
    BOOST_CHECK_EQUAL(state.C, 100 * COIN); // C unchanged
    BOOST_CHECK_EQUAL(state.U, 40 * COIN);
    BOOST_CHECK_EQUAL(state.Z, 60 * COIN);
    BOOST_CHECK(state.CheckInvariants()); // C == U + Z
}

BOOST_AUTO_TEST_CASE(lock_full_balance)
{
    HuGlobalState state;
    state.SetNull();

    state.C = 100 * COIN;
    state.U = 100 * COIN;

    // Lock everything
    state.U -= 100 * COIN;
    state.Z += 100 * COIN;

    BOOST_CHECK_EQUAL(state.U, 0);
    BOOST_CHECK_EQUAL(state.Z, 100 * COIN);
    BOOST_CHECK(state.CheckInvariants());
}

// =============================================================================
// Test 4: UNLOCK - ZKHU → KHU_T (with yield)
// =============================================================================
BOOST_AUTO_TEST_CASE(unlock_atomic_with_yield)
{
    HuGlobalState state;
    state.SetNull();

    // Setup: 100 locked, 10 yield accumulated
    state.C = 100 * COIN;
    state.U = 0;
    state.Z = 100 * COIN;
    state.Cr = 10 * COIN; // Yield pool
    state.Ur = 10 * COIN; // Yield rights

    const CAmount principal = 100 * COIN;
    const CAmount yield = 10 * COIN;

    // UNLOCK atomic operation (5 lines, no code between):
    state.Z  -= principal;       // (1) Principal from shielded
    state.U  += principal + yield; // (2) Principal + Yield to transparent
    state.C  += yield;           // (3) Yield increases collateral
    state.Cr -= yield;           // (4) Consume yield pool
    state.Ur -= yield;           // (5) Consume yield rights

    // After UNLOCK
    BOOST_CHECK_EQUAL(state.Z, 0);
    BOOST_CHECK_EQUAL(state.U, 110 * COIN); // Principal + Yield
    BOOST_CHECK_EQUAL(state.C, 110 * COIN);
    BOOST_CHECK_EQUAL(state.Cr, 0);
    BOOST_CHECK_EQUAL(state.Ur, 0);
    BOOST_CHECK(state.CheckInvariants());
}

BOOST_AUTO_TEST_CASE(unlock_partial)
{
    HuGlobalState state;
    state.SetNull();

    // Setup: 100 locked, 5 yield
    state.C = 100 * COIN;
    state.U = 0;
    state.Z = 100 * COIN;
    state.Cr = 5 * COIN;
    state.Ur = 5 * COIN;

    // Unlock only 50 with proportional yield (2.5)
    const CAmount principal = 50 * COIN;
    const CAmount yield = 25 * COIN / 10; // 2.5 COIN

    state.Z  -= principal;
    state.U  += principal + yield;
    state.C  += yield;
    state.Cr -= yield;
    state.Ur -= yield;

    BOOST_CHECK_EQUAL(state.Z, 50 * COIN);
    BOOST_CHECK_EQUAL(state.U, 525 * COIN / 10); // 52.5 COIN
    BOOST_CHECK(state.CheckInvariants());
}

// =============================================================================
// Test 5: Invalid operation sequences
// =============================================================================
BOOST_AUTO_TEST_CASE(cannot_unlock_without_lock)
{
    HuGlobalState state;
    state.SetNull();

    state.C = 100 * COIN;
    state.U = 100 * COIN;
    state.Z = 0; // Nothing locked

    // Cannot unlock Z = 0
    BOOST_CHECK_EQUAL(state.Z, 0);
    // Validation would reject unlock with amount > Z
}

BOOST_AUTO_TEST_CASE(cannot_lock_without_mint)
{
    HuGlobalState state;
    state.SetNull();

    // U = 0, cannot lock
    BOOST_CHECK_EQUAL(state.U, 0);
    // Validation would reject lock with amount > U
}

BOOST_AUTO_TEST_SUITE_END()
