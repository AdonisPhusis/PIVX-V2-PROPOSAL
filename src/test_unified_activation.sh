#!/bin/bash
#
# PIVHU Unified Activation Test
# Tests DOMC + DAO synchronization (DOMC=90, DAO=30 blocks)
#
# Block 89: UNIFIED ACTIVATION (R_next → R_annual + DAO3 payout)
# Block 90: New DOMC cycle START
#

set -e

pkill -9 pivxd 2>/dev/null || true
sleep 2

rm -rf /tmp/pivhu_test && mkdir -p /tmp/pivhu_test
./pivxd -regtest -datadir=/tmp/pivhu_test -daemon -rpcuser=test -rpcpassword=test -rpcport=19501
sleep 8

CLI="./pivx-cli -regtest -datadir=/tmp/pivhu_test -rpcuser=test -rpcpassword=test -rpcport=19501"

echo "======================================================================="
echo "   UNIFIED ACTIVATION TEST (DOMC=90, DAO=30 blocks)"
echo "======================================================================="
echo ""

# Generate 89 blocks (V6 activation at block 1, so block 89 = DOMC offset 88)
echo "=== Generate 89 blocks (to reach block 89) ==="
$CLI generate 89 > /dev/null
BLOCK=$($CLI getblockcount)
echo "Block count: $BLOCK"
echo ""

echo "=== KHU State at block 89 ==="
$CLI getkhustate
echo ""

echo "=== DAO Status at block 89 ==="
$CLI daostatus
echo ""

echo "=== Generate 1 block (block 90 = new DOMC cycle START) ==="
$CLI generate 1 > /dev/null
BLOCK=$($CLI getblockcount)
echo "Block count: $BLOCK"
echo ""

echo "=== KHU State at block 90 (new cycle) ==="
$CLI getkhustate
echo ""

echo "=== DAO Status at block 90 (new cycle) ==="
$CLI daostatus
echo ""

echo "======================================================================="
echo "   VERIFICATION"
echo "======================================================================="
echo ""
echo "Expected:"
echo "  Block 89: Last block of DOMC cycle 1 (offset 88)"
echo "            + DAO3 payout height (offset 29)"
echo "            = UNIFIED ACTIVATION"
echo ""
echo "  Block 90: First block of DOMC cycle 2 (offset 0)"
echo "            + DAO4 start (offset 0)"
echo "            = New cycle START"
echo ""

# Cleanup
$CLI stop 2>/dev/null || true
sleep 2

echo "Test completed!"
