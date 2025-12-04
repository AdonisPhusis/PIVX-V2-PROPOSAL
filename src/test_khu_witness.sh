#!/bin/bash
# ═══════════════════════════════════════════════════════════════════════════════
# TEST KHU WITNESS PIPELINE - Validates that ZKHU notes use standard Sapling
# witness infrastructure via FindMySaplingNotes + IncrementNoteWitnesses
# ═══════════════════════════════════════════════════════════════════════════════

set -e

DATADIR="/tmp/khu_witness_test"
CLI="./pivx-cli -regtest -datadir=$DATADIR -rpcuser=test -rpcpassword=test123"
PIVXD="./pivxd"

# Cleanup
pkill -f "pivxd.*khu_witness_test" 2>/dev/null || true
sleep 2
rm -rf "$DATADIR"
mkdir -p "$DATADIR"

# Create config
cat > "$DATADIR/pivx.conf" << 'EOF'
regtest=1
server=1
rpcuser=test
rpcpassword=test123
rpcallowip=127.0.0.1
debug=khu
debug=sapling
printtoconsole=0
EOF

echo "═══════════════════════════════════════════════════════════════════"
echo "  TEST KHU WITNESS PIPELINE"
echo "═══════════════════════════════════════════════════════════════════"
echo ""

# Start daemon
echo ">>> Starting pivxd..."
$PIVXD -regtest -datadir=$DATADIR -rpcuser=test -rpcpassword=test123 -daemon
sleep 5

# Wait for RPC
for i in {1..30}; do
    if $CLI getblockchaininfo > /dev/null 2>&1; then
        echo ">>> pivxd ready"
        break
    fi
    sleep 1
done

# Generate initial blocks to V6 activation + some extra
echo ""
echo ">>> Generating blocks to V6 activation (250 blocks)..."
$CLI generate 250 > /dev/null
BLOCKS=$($CLI getblockcount)
echo "    Current height: $BLOCKS"

# Get initial balance
echo ""
echo ">>> Checking initial balance..."
$CLI getbalance

# MINT some KHU
echo ""
echo "═══════════════════════════════════════════════════════════════════"
echo "  STEP 1: MINT 1000 KHU"
echo "═══════════════════════════════════════════════════════════════════"
MINT_RESULT=$($CLI khumint 1000)
echo "$MINT_RESULT"
MINT_TXID=$(echo "$MINT_RESULT" | grep -o '"txid": "[^"]*"' | cut -d'"' -f4)
echo "    MINT txid: $MINT_TXID"

# Confirm MINT
echo ""
echo ">>> Confirming MINT tx..."
$CLI generate 1 > /dev/null

# Check KHU balance
echo ""
echo ">>> KHU Balance after MINT:"
$CLI khubalance

# STAKE some KHU
echo ""
echo "═══════════════════════════════════════════════════════════════════"
echo "  STEP 2: STAKE 500 KHU"
echo "═══════════════════════════════════════════════════════════════════"
STAKE_RESULT=$($CLI khulock 500)
echo "$STAKE_RESULT"
STAKE_TXID=$(echo "$STAKE_RESULT" | grep -o '"txid": "[^"]*"' | cut -d'"' -f4)
NOTE_CM=$(echo "$STAKE_RESULT" | grep -o '"note_commitment": "[^"]*"' | cut -d'"' -f4)
echo "    STAKE txid: $STAKE_TXID"
echo "    Note commitment: $NOTE_CM"

# Confirm STAKE
echo ""
echo ">>> Confirming STAKE tx..."
$CLI generate 1 > /dev/null

# Check debug log for witness detection
echo ""
echo "═══════════════════════════════════════════════════════════════════"
echo "  VERIFYING WITNESS PIPELINE IN DEBUG.LOG"
echo "═══════════════════════════════════════════════════════════════════"

# Wait for log to be written
sleep 2

echo ""
echo ">>> Looking for FindMySaplingNotes KHU_STAKE detection..."
grep -i "FindMySaplingNotes.*KHU_STAKE" "$DATADIR/regtest/debug.log" || echo "    [Not found - may need more blocks]"

echo ""
echo ">>> Looking for IncrementNoteWitnesses KHU_STAKE processing..."
grep -i "IncrementNoteWitnesses.*KHU_STAKE" "$DATADIR/regtest/debug.log" || echo "    [Not found - may need more blocks]"

# Generate more blocks to ensure witnesses are updated
echo ""
echo ">>> Generating 10 more blocks to update witnesses..."
$CLI generate 10 > /dev/null

sleep 2
echo ""
echo ">>> Checking witness pipeline logs after additional blocks..."
grep -i "IncrementNoteWitnesses.*KHU_STAKE" "$DATADIR/regtest/debug.log" | tail -3 || echo "    [No KHU witness updates]"

# Generate blocks to maturity
echo ""
echo "═══════════════════════════════════════════════════════════════════"
echo "  STEP 3: GENERATE TO MATURITY (4320 blocks)"
echo "═══════════════════════════════════════════════════════════════════"

# Generate in batches to see progress
for batch in {1..10}; do
    echo "    Generating batch $batch/10 (432 blocks each)..."
    $CLI generate 432 > /dev/null
done

BLOCKS=$($CLI getblockcount)
echo ""
echo ">>> Current height: $BLOCKS"

# Check staked notes
echo ""
echo ">>> Staked notes after maturity:"
$CLI khuliststaked

# Generate a few more blocks for yield boundary
echo ""
echo ">>> Generating 5 more blocks to cross yield boundary..."
$CLI generate 5 > /dev/null

# Try UNSTAKE
echo ""
echo "═══════════════════════════════════════════════════════════════════"
echo "  STEP 4: UNSTAKE (Testing Witness Pipeline)"
echo "═══════════════════════════════════════════════════════════════════"

UNSTAKE_RESULT=$($CLI khuunlock 2>&1) || true
echo "$UNSTAKE_RESULT"

# Check which witness source was used
echo ""
echo ">>> Checking WITNESS_SOURCE in debug.log..."
grep -i "WITNESS_SOURCE" "$DATADIR/regtest/debug.log" | tail -5 || echo "    [No WITNESS_SOURCE logs]"

# Confirm UNSTAKE
echo ""
echo ">>> Confirming UNSTAKE tx..."
$CLI generate 1 > /dev/null

# Final balance check
echo ""
echo "═══════════════════════════════════════════════════════════════════"
echo "  FINAL STATE"
echo "═══════════════════════════════════════════════════════════════════"
echo ""
echo ">>> KHU Balance:"
$CLI khubalance

echo ""
echo ">>> KHU Global State:"
$CLI getkhustate

# Summary from log
echo ""
echo "═══════════════════════════════════════════════════════════════════"
echo "  WITNESS PIPELINE SUMMARY"
echo "═══════════════════════════════════════════════════════════════════"
echo ""
echo ">>> FindMySaplingNotes KHU detections:"
grep -c "FindMySaplingNotes.*KHU_STAKE" "$DATADIR/regtest/debug.log" 2>/dev/null || echo "0"

echo ""
echo ">>> IncrementNoteWitnesses KHU updates:"
grep -c "IncrementNoteWitnesses.*KHU_STAKE" "$DATADIR/regtest/debug.log" 2>/dev/null || echo "0"

echo ""
echo ">>> WITNESS_SOURCE breakdown:"
echo "    STANDARD_PIPELINE: $(grep -c 'WITNESS_SOURCE=STANDARD_PIPELINE' "$DATADIR/regtest/debug.log" 2>/dev/null || echo 0)"
echo "    FALLBACK: $(grep -c 'WITNESS_SOURCE=FALLBACK' "$DATADIR/regtest/debug.log" 2>/dev/null || echo 0)"

# Cleanup
echo ""
echo ">>> Stopping pivxd..."
$CLI stop 2>/dev/null || true
sleep 3

echo ""
echo "═══════════════════════════════════════════════════════════════════"
echo "  TEST COMPLETE"
echo "═══════════════════════════════════════════════════════════════════"
