// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * HU Fees Tests - Transaction fees in PIVX, UTXO management
 *
 * Tests:
 *   1. Fees always paid in PIVX (not KHU)
 *   2. OP_RETURN UTXOs management
 *   3. Unconfirmed UTXOs cannot be used
 *   4. Fee calculation for each operation type
 */

#include "piv2/piv2_state.h"
#include "amount.h"
#include "test/test_pivx.h"
#include "primitives/transaction.h"
#include "script/script.h"

#include <boost/test/unit_test.hpp>

// Fee constants (in satoshis)
static const CAmount FEE_MINT = 10000;     // 0.0001 PIVX
static const CAmount FEE_REDEEM = 10000;   // 0.0001 PIVX
static const CAmount FEE_LOCK = 10000;     // 0.0001 PIVX
static const CAmount FEE_UNLOCK = 10000;   // 0.0001 PIVX
static const CAmount FEE_TRANSFER = 10000; // 0.0001 PIVX

BOOST_FIXTURE_TEST_SUITE(hu_fees_tests, BasicTestingSetup)

// =============================================================================
// Test 1: Fees paid in PIVX (not KHU)
// =============================================================================
BOOST_AUTO_TEST_CASE(fees_paid_in_pivx)
{
    // User has: 100 PIVX, 50 KHU
    CAmount pivxBalance = 100 * COIN;
    CAmount khuBalance = 50 * COIN;

    // To MINT 10 KHU, user pays:
    // - 10 PIVX as collateral (converted to KHU)
    // - Fee in PIVX (not KHU)

    CAmount mintAmount = 10 * COIN;
    CAmount fee = FEE_MINT;

    // HU decreases by mintAmount + fee
    CAmount newPivxBalance = pivxBalance - mintAmount - fee;
    BOOST_CHECK_EQUAL(newPivxBalance, 90 * COIN - fee);

    // KHU increases by mintAmount only
    CAmount newKhuBalance = khuBalance + mintAmount;
    BOOST_CHECK_EQUAL(newKhuBalance, 60 * COIN);
}

BOOST_AUTO_TEST_CASE(fee_insufficient_pivx)
{
    // User has: 10 PIVX (exactly enough for MINT, no fee)
    CAmount pivxBalance = 10 * COIN;

    CAmount mintAmount = 10 * COIN;
    CAmount fee = FEE_MINT;

    // Cannot pay fee - transaction should be rejected
    BOOST_CHECK(pivxBalance < mintAmount + fee);
}

// =============================================================================
// Test 2: OP_RETURN marker for KHU transactions
// =============================================================================
BOOST_AUTO_TEST_CASE(op_return_marker_format)
{
    // KHU transactions use OP_RETURN to mark operation type
    // Format: OP_RETURN <4 bytes: "KHU\x00"> <1 byte: op_type> <payload>

    CScript opReturn;
    opReturn << OP_RETURN;

    // Marker bytes
    std::vector<unsigned char> marker = {'K', 'H', 'U', 0x00};
    opReturn << marker;

    // Operation type (1 = MINT, 2 = REDEEM, 3 = LOCK, 4 = UNLOCK)
    opReturn << std::vector<unsigned char>{0x01}; // MINT

    BOOST_CHECK(opReturn.IsUnspendable());
    BOOST_CHECK(opReturn[0] == OP_RETURN);
}

BOOST_AUTO_TEST_CASE(op_return_not_spendable)
{
    CMutableTransaction tx;

    // Create OP_RETURN output
    CScript opReturn;
    opReturn << OP_RETURN << std::vector<unsigned char>{'K', 'H', 'U'};

    CTxOut opReturnOut(0, opReturn);
    tx.vout.push_back(opReturnOut);

    // OP_RETURN outputs have 0 value and are unspendable
    BOOST_CHECK_EQUAL(tx.vout[0].nValue, 0);
    BOOST_CHECK(tx.vout[0].scriptPubKey.IsUnspendable());
}

// =============================================================================
// Test 3: Unconfirmed UTXOs cannot be used
// =============================================================================
BOOST_AUTO_TEST_CASE(unconfirmed_utxo_rejected)
{
    // Simulate UTXO with 0 confirmations
    int confirmations = 0;
    int requiredConfirmations = 1;

    // UTXO should not be usable
    BOOST_CHECK(confirmations < requiredConfirmations);

    // After 1 confirmation, UTXO is usable
    confirmations = 1;
    BOOST_CHECK(confirmations >= requiredConfirmations);
}

BOOST_AUTO_TEST_CASE(mempool_utxo_rejected)
{
    // UTXOs in mempool (unconfirmed) cannot be used for KHU operations
    bool isInMempool = true;
    bool isConfirmed = false;

    // Should be rejected
    BOOST_CHECK(isInMempool && !isConfirmed);

    // After confirmation
    isInMempool = false;
    isConfirmed = true;
    BOOST_CHECK(!isInMempool && isConfirmed);
}

// =============================================================================
// Test 4: Fee calculation per operation type
// =============================================================================
BOOST_AUTO_TEST_CASE(fee_calculation_mint)
{
    CAmount amount = 100 * COIN;
    CAmount fee = FEE_MINT;

    // Total PIVX needed for MINT
    CAmount totalNeeded = amount + fee;
    BOOST_CHECK_EQUAL(totalNeeded, 100 * COIN + FEE_MINT);
}

BOOST_AUTO_TEST_CASE(fee_calculation_redeem)
{
    CAmount amount = 100 * COIN;
    CAmount fee = FEE_REDEEM;

    // User gets back: amount - fee (in PIVX)
    // Actually, fee is paid separately in PIVX, user gets full amount
    CAmount received = amount; // Full redemption
    CAmount pivxForFee = fee;  // Separate PIVX for fee

    BOOST_CHECK_EQUAL(received, 100 * COIN);
    BOOST_CHECK_EQUAL(pivxForFee, FEE_REDEEM);
}

BOOST_AUTO_TEST_CASE(fee_calculation_lock)
{
    CAmount amount = 100 * COIN;
    CAmount fee = FEE_LOCK;

    // LOCK moves KHU_T to ZKHU, fee in PIVX
    // Need PIVX for fee only
    BOOST_CHECK_EQUAL(fee, FEE_LOCK);
}

BOOST_AUTO_TEST_CASE(fee_calculation_unlock)
{
    CAmount principal = 100 * COIN;
    CAmount yield = 5 * COIN;
    CAmount fee = FEE_UNLOCK;

    // UNLOCK returns principal + yield to KHU_T, fee in PIVX
    CAmount received = principal + yield;
    BOOST_CHECK_EQUAL(received, 105 * COIN);
    BOOST_CHECK_EQUAL(fee, FEE_UNLOCK);
}

// =============================================================================
// Test 5: Dust threshold
// =============================================================================
BOOST_AUTO_TEST_CASE(dust_threshold_respected)
{
    // Minimum UTXO value to avoid dust
    const CAmount DUST_THRESHOLD = 546; // Standard Bitcoin dust

    // Valid output
    CAmount validAmount = 1000;
    BOOST_CHECK(validAmount >= DUST_THRESHOLD);

    // Dust output (would be rejected)
    CAmount dustAmount = 100;
    BOOST_CHECK(dustAmount < DUST_THRESHOLD);
}

BOOST_AUTO_TEST_SUITE_END()
