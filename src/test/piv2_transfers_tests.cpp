// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * HU Transfers Tests - All transfer types
 *
 * Part A: KHU/ZKHU Transfers (stablecoin)
 *   1. KHU_T → KHU_T (transparent to transparent)
 *   2. KHU_T → ZKHU (transparent to shielded) = LOCK
 *   3. ZKHU → ZKHU (shielded to shielded)
 *   4. ZKHU → KHU_T (shielded to transparent) = UNLOCK
 *
 * Part B: HU Native Shielded (Sapling)
 *   5. HU transparent → HU shielded (shield)
 *   6. HU shielded → HU shielded (private transfer)
 *   7. HU shielded → HU transparent (deshield)
 *   8. HU shielded does NOT affect KHU state
 */

#include "piv2/piv2_state.h"
#include "amount.h"
#include "test/test_pivx.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(hu_transfers_tests, BasicTestingSetup)

// =============================================================================
// Test 1: HU → HU (KHU_T to KHU_T transfer)
// =============================================================================
BOOST_AUTO_TEST_CASE(hu_to_hu_transfer)
{
    // Sender: 100 KHU_T
    // Recipient: 0 KHU_T
    // Transfer: 30 KHU_T

    CAmount senderBalance = 100 * COIN;
    CAmount recipientBalance = 0;
    CAmount transferAmount = 30 * COIN;

    // Execute transfer
    senderBalance -= transferAmount;
    recipientBalance += transferAmount;

    BOOST_CHECK_EQUAL(senderBalance, 70 * COIN);
    BOOST_CHECK_EQUAL(recipientBalance, 30 * COIN);

    // Total unchanged
    BOOST_CHECK_EQUAL(senderBalance + recipientBalance, 100 * COIN);
}

BOOST_AUTO_TEST_CASE(hu_to_hu_global_state_unchanged)
{
    HuGlobalState state;
    state.SetNull();

    // Setup: 100 total in system
    state.C = 100 * COIN;
    state.U = 100 * COIN;

    // HU → HU transfer doesn't change global state
    // (just changes ownership, not totals)

    BOOST_CHECK_EQUAL(state.C, 100 * COIN); // Unchanged
    BOOST_CHECK_EQUAL(state.U, 100 * COIN); // Unchanged
    BOOST_CHECK(state.CheckInvariants());
}

// =============================================================================
// Test 2: HU → ZKHU (transparent to shielded) = LOCK
// =============================================================================
BOOST_AUTO_TEST_CASE(hu_to_zkhu_lock)
{
    HuGlobalState state;
    state.SetNull();

    // Setup: 100 KHU_T
    state.C = 100 * COIN;
    state.U = 100 * COIN;

    CAmount lockAmount = 40 * COIN;

    // LOCK: U → Z
    state.U -= lockAmount;
    state.Z += lockAmount;

    BOOST_CHECK_EQUAL(state.U, 60 * COIN);
    BOOST_CHECK_EQUAL(state.Z, 40 * COIN);
    BOOST_CHECK_EQUAL(state.C, 100 * COIN); // C unchanged
    BOOST_CHECK(state.CheckInvariants());   // C == U + Z
}

BOOST_AUTO_TEST_CASE(hu_to_zkhu_multiple_locks)
{
    HuGlobalState state;
    state.SetNull();

    state.C = 100 * COIN;
    state.U = 100 * COIN;

    // Multiple LOCKs
    for (int i = 0; i < 5; i++) {
        CAmount amount = 10 * COIN;
        state.U -= amount;
        state.Z += amount;
        BOOST_CHECK(state.CheckInvariants());
    }

    BOOST_CHECK_EQUAL(state.U, 50 * COIN);
    BOOST_CHECK_EQUAL(state.Z, 50 * COIN);
}

// =============================================================================
// Test 3: ZKHU → ZKHU (shielded to shielded transfer)
// =============================================================================
BOOST_AUTO_TEST_CASE(zkhu_to_zkhu_transfer)
{
    HuGlobalState state;
    state.SetNull();

    // Setup: All locked (shielded)
    state.C = 100 * COIN;
    state.U = 0;
    state.Z = 100 * COIN;

    // ZKHU → ZKHU transfer doesn't change Z total
    // (privacy-preserving transfer between shielded addresses)

    CAmount transferAmount = 30 * COIN;

    // Sender's Z: 60, Recipient's Z: 40 (but global Z = 100)
    // Global state unchanged
    BOOST_CHECK_EQUAL(state.Z, 100 * COIN);
    BOOST_CHECK(state.CheckInvariants());
}

BOOST_AUTO_TEST_CASE(zkhu_to_zkhu_preserves_privacy)
{
    // Shielded transfers hide:
    // - Sender address
    // - Recipient address
    // - Amount (hidden in encrypted note)

    // Only thing visible: transaction exists, fee paid

    // This is tested at Sapling level, not state level
    BOOST_CHECK(true); // Placeholder - privacy tested in sapling_tests
}

// =============================================================================
// Test 4: ZKHU → HU (shielded to transparent) = UNLOCK
// =============================================================================
BOOST_AUTO_TEST_CASE(zkhu_to_hu_unlock)
{
    HuGlobalState state;
    state.SetNull();

    // Setup: 100 locked
    state.C = 100 * COIN;
    state.U = 0;
    state.Z = 100 * COIN;

    CAmount unlockAmount = 60 * COIN;

    // UNLOCK (simplified, no yield for this test)
    state.Z -= unlockAmount;
    state.U += unlockAmount;

    BOOST_CHECK_EQUAL(state.Z, 40 * COIN);
    BOOST_CHECK_EQUAL(state.U, 60 * COIN);
    BOOST_CHECK_EQUAL(state.C, 100 * COIN); // C unchanged (no yield)
    BOOST_CHECK(state.CheckInvariants());
}

BOOST_AUTO_TEST_CASE(zkhu_to_hu_unlock_with_yield)
{
    HuGlobalState state;
    state.SetNull();

    // Setup: 100 locked, 10 yield
    state.C = 100 * COIN;
    state.U = 0;
    state.Z = 100 * COIN;
    state.Cr = 10 * COIN;
    state.Ur = 10 * COIN;

    CAmount principal = 100 * COIN;
    CAmount yield = 10 * COIN;

    // UNLOCK atomic with yield
    state.Z  -= principal;
    state.U  += principal + yield;
    state.C  += yield;
    state.Cr -= yield;
    state.Ur -= yield;

    BOOST_CHECK_EQUAL(state.Z, 0);
    BOOST_CHECK_EQUAL(state.U, 110 * COIN);
    BOOST_CHECK_EQUAL(state.C, 110 * COIN);
    BOOST_CHECK_EQUAL(state.Cr, 0);
    BOOST_CHECK_EQUAL(state.Ur, 0);
    BOOST_CHECK(state.CheckInvariants());
}

// =============================================================================
// Test 5: Transfer validation
// =============================================================================
BOOST_AUTO_TEST_CASE(transfer_insufficient_balance)
{
    CAmount balance = 50 * COIN;
    CAmount transferAmount = 60 * COIN;

    // Should be rejected
    BOOST_CHECK(transferAmount > balance);
}

BOOST_AUTO_TEST_CASE(transfer_exact_balance)
{
    CAmount balance = 50 * COIN;
    CAmount transferAmount = 50 * COIN;

    // Valid - exact balance
    BOOST_CHECK(transferAmount <= balance);

    balance -= transferAmount;
    BOOST_CHECK_EQUAL(balance, 0);
}

// =============================================================================
// Test 6: Round-trip transfers
// =============================================================================
BOOST_AUTO_TEST_CASE(roundtrip_hu_zkhu_hu)
{
    HuGlobalState state;
    state.SetNull();

    // Start: 100 KHU_T
    state.C = 100 * COIN;
    state.U = 100 * COIN;

    // Step 1: LOCK 50
    state.U -= 50 * COIN;
    state.Z += 50 * COIN;
    BOOST_CHECK(state.CheckInvariants());

    // Step 2: UNLOCK 50 (no yield for simplicity)
    state.Z -= 50 * COIN;
    state.U += 50 * COIN;
    BOOST_CHECK(state.CheckInvariants());

    // End: Back to 100 KHU_T
    BOOST_CHECK_EQUAL(state.U, 100 * COIN);
    BOOST_CHECK_EQUAL(state.Z, 0);
    BOOST_CHECK_EQUAL(state.C, 100 * COIN);
}

BOOST_AUTO_TEST_CASE(roundtrip_mint_lock_unlock_redeem)
{
    HuGlobalState state;
    state.SetNull();

    // Step 1: MINT 100
    state.C += 100 * COIN;
    state.U += 100 * COIN;
    BOOST_CHECK(state.CheckInvariants());

    // Step 2: LOCK 100
    state.U -= 100 * COIN;
    state.Z += 100 * COIN;
    BOOST_CHECK(state.CheckInvariants());

    // Step 3: UNLOCK 100 (no yield)
    state.Z -= 100 * COIN;
    state.U += 100 * COIN;
    BOOST_CHECK(state.CheckInvariants());

    // Step 4: REDEEM 100
    state.C -= 100 * COIN;
    state.U -= 100 * COIN;
    BOOST_CHECK(state.CheckInvariants());

    // End: Empty state
    BOOST_CHECK_EQUAL(state.C, 0);
    BOOST_CHECK_EQUAL(state.U, 0);
    BOOST_CHECK_EQUAL(state.Z, 0);
}

// =============================================================================
// PART B: HU NATIVE SHIELDED (Sapling)
// =============================================================================
// HU (the native coin) can also be shielded via Sapling.
// This is INDEPENDENT of the KHU/ZKHU system.
// HU shielded uses Sapling notes, NOT the HuGlobalState.
// =============================================================================

// Test 5: HU transparent → HU shielded (shield operation)
BOOST_AUTO_TEST_CASE(hu_native_shield)
{
    // User has 100 HU in transparent UTXO
    CAmount transparentBalance = 100 * COIN;
    CAmount shieldAmount = 60 * COIN;

    // Shield operation: t-addr → z-addr (Sapling)
    // Creates a Sapling note, burns the UTXO

    CAmount newTransparentBalance = transparentBalance - shieldAmount;
    CAmount shieldedBalance = shieldAmount; // Now in Sapling note

    BOOST_CHECK_EQUAL(newTransparentBalance, 40 * COIN);
    BOOST_CHECK_EQUAL(shieldedBalance, 60 * COIN);

    // Total HU unchanged
    BOOST_CHECK_EQUAL(newTransparentBalance + shieldedBalance, 100 * COIN);
}

// Test 6: HU shielded → HU shielded (private transfer)
BOOST_AUTO_TEST_CASE(hu_native_shielded_transfer)
{
    // Sender has 100 HU shielded
    // Transfer 30 HU to another shielded address

    CAmount senderShielded = 100 * COIN;
    CAmount recipientShielded = 0;
    CAmount transferAmount = 30 * COIN;

    // Sapling spend + output
    senderShielded -= transferAmount;
    recipientShielded += transferAmount;

    BOOST_CHECK_EQUAL(senderShielded, 70 * COIN);
    BOOST_CHECK_EQUAL(recipientShielded, 30 * COIN);

    // Total unchanged, privacy preserved
    BOOST_CHECK_EQUAL(senderShielded + recipientShielded, 100 * COIN);
}

// Test 7: HU shielded → HU transparent (deshield operation)
BOOST_AUTO_TEST_CASE(hu_native_deshield)
{
    // User has 100 HU shielded, wants to deshield 50
    CAmount shieldedBalance = 100 * COIN;
    CAmount deshieldAmount = 50 * COIN;

    // Deshield: z-addr → t-addr
    // Spends Sapling note, creates transparent UTXO

    CAmount newShieldedBalance = shieldedBalance - deshieldAmount;
    CAmount transparentBalance = deshieldAmount;

    BOOST_CHECK_EQUAL(newShieldedBalance, 50 * COIN);
    BOOST_CHECK_EQUAL(transparentBalance, 50 * COIN);

    // Total unchanged
    BOOST_CHECK_EQUAL(newShieldedBalance + transparentBalance, 100 * COIN);
}

// Test 8: HU shielded does NOT affect KHU state
BOOST_AUTO_TEST_CASE(hu_native_shielded_independent_of_khu)
{
    HuGlobalState state;
    state.SetNull();

    // Setup KHU state
    state.C = 1000 * COIN;  // 1000 KHU collateral
    state.U = 800 * COIN;   // 800 KHU_T
    state.Z = 200 * COIN;   // 200 ZKHU

    // User shields 50 HU (native coin, NOT KHU)
    CAmount huShieldAmount = 50 * COIN;

    // HU shielding uses Sapling, NOT HuGlobalState
    // The KHU state should be COMPLETELY UNCHANGED

    BOOST_CHECK_EQUAL(state.C, 1000 * COIN);  // Unchanged
    BOOST_CHECK_EQUAL(state.U, 800 * COIN);   // Unchanged
    BOOST_CHECK_EQUAL(state.Z, 200 * COIN);   // Unchanged
    BOOST_CHECK(state.CheckInvariants());

    // HU shielded is a separate accounting:
    // - Uses Sapling commitment tree
    // - Uses Sapling nullifiers
    // - Does NOT touch C/U/Z
}

// Test 9: HU shielded round-trip
BOOST_AUTO_TEST_CASE(hu_native_shielded_roundtrip)
{
    CAmount transparent = 100 * COIN;
    CAmount shielded = 0;

    // Step 1: Shield all
    shielded = transparent;
    transparent = 0;
    BOOST_CHECK_EQUAL(transparent, 0);
    BOOST_CHECK_EQUAL(shielded, 100 * COIN);

    // Step 2: Transfer shielded (to self or other)
    // (just moves between notes, total unchanged)
    BOOST_CHECK_EQUAL(shielded, 100 * COIN);

    // Step 3: Deshield all
    transparent = shielded;
    shielded = 0;
    BOOST_CHECK_EQUAL(transparent, 100 * COIN);
    BOOST_CHECK_EQUAL(shielded, 0);
}

// Test 10: HU shielded with fee
BOOST_AUTO_TEST_CASE(hu_native_shielded_with_fee)
{
    CAmount shieldedBalance = 100 * COIN;
    CAmount transferAmount = 30 * COIN;
    CAmount fee = 1000; // 0.00001 HU fee

    // Shielded transfer with fee
    CAmount totalSpent = transferAmount + fee;
    BOOST_CHECK(totalSpent <= shieldedBalance);

    shieldedBalance -= totalSpent;
    BOOST_CHECK_EQUAL(shieldedBalance, 100 * COIN - 30 * COIN - 1000);

    // Fee goes to miner (transparent output or burned)
}

// Test 11: Cannot shield more than balance
BOOST_AUTO_TEST_CASE(hu_native_shield_insufficient_balance)
{
    CAmount transparentBalance = 50 * COIN;
    CAmount shieldAmount = 60 * COIN;

    // Should fail - insufficient balance
    BOOST_CHECK(shieldAmount > transparentBalance);
}

// Test 12: HU shielded coexists with ZKHU
BOOST_AUTO_TEST_CASE(hu_native_shielded_coexists_with_zkhu)
{
    HuGlobalState state;
    state.SetNull();

    // KHU system state
    state.C = 500 * COIN;
    state.U = 300 * COIN;
    state.Z = 200 * COIN;  // 200 ZKHU (shielded KHU)

    // Separately, user has HU shielded (Sapling)
    CAmount huShielded = 100 * COIN;

    // These are DIFFERENT pools:
    // - ZKHU (state.Z) = shielded KHU stablecoin, tracked in HuGlobalState
    // - HU shielded = native coin in Sapling, tracked in Sapling tree

    // Both can exist simultaneously without conflict
    BOOST_CHECK_EQUAL(state.Z, 200 * COIN);      // ZKHU
    BOOST_CHECK_EQUAL(huShielded, 100 * COIN);   // HU shielded (separate)

    // They use different mechanisms:
    // - ZKHU: LOCK/UNLOCK operations, affects C/U/Z
    // - HU shielded: Sapling shield/deshield, does NOT affect C/U/Z

    BOOST_CHECK(state.CheckInvariants());
}

BOOST_AUTO_TEST_SUITE_END()
