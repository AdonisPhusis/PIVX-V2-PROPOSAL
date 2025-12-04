#!/bin/bash
#
# Generate Testnet Genesis Keys for PIV2
# =======================================
#
# This script generates 5 key pairs for the testnet genesis block:
#   - MN1_OWNER: Masternode 1 collateral (10,000 PIV2)
#   - MN2_OWNER: Masternode 2 collateral (10,000 PIV2)
#   - MN3_OWNER: Masternode 3 collateral (10,000 PIV2)
#   - DEV_WALLET: Development wallet (50,000,000 PIV2)
#   - FAUCET: Public faucet (50,000,000 PIV2)
#
# Output: /tmp/piv2_testnet_genesis_keys.json (NOT in repo!)
#
# Note: Uses regtest to generate keys (pubKeyHash is network-agnostic)
# The WIF will need conversion for testnet use, but pubKeyHash is universal.
#
# Usage:
#   ./generate_testnet_genesis_keys.sh
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PIV2D="$SCRIPT_DIR/piv2d"
CLI="$SCRIPT_DIR/piv2-cli"
DATADIR="/tmp/piv2_keygen_regtest"
OUTPUT_FILE="/tmp/piv2_testnet_genesis_keys.json"

echo "═══════════════════════════════════════════════════════════════════════════"
echo "  PIV2 Testnet Genesis Key Generator"
echo "  (Using regtest for key generation - pubKeyHash is network-agnostic)"
echo "═══════════════════════════════════════════════════════════════════════════"

# Check binaries
if [ ! -x "$PIV2D" ]; then
    echo "ERROR: piv2d not found at $PIV2D"
    echo "Please compile first: cd PIV2-Core && make -j\$(nproc)"
    exit 1
fi

if [ ! -x "$CLI" ]; then
    echo "ERROR: piv2-cli not found at $CLI"
    exit 1
fi

# Kill any existing regtest processes
pkill -f "piv2d.*keygen" 2>/dev/null || true
sleep 1

# Clean up any previous keygen data
rm -rf "$DATADIR"
mkdir -p "$DATADIR"

# Create minimal regtest config for key generation
cat > "$DATADIR/piv2.conf" << 'EOF'
regtest=1
server=1
rpcuser=keygen
rpcpassword=keygen123
rpcallowip=127.0.0.1
rpcport=18998
listen=0
dnsseed=0
listenonion=0
EOF

echo ""
echo "[1/5] Starting temporary regtest node for key generation..."

# Start piv2d in background (regtest mode) with explicit RPC params
"$PIV2D" -regtest -datadir="$DATADIR" -rpcuser=keygen -rpcpassword=keygen123 -rpcport=18998 -daemon -printtoconsole=0

# CLI command with explicit credentials
CLI_CMD="$CLI -regtest -datadir=$DATADIR -rpcuser=keygen -rpcpassword=keygen123 -rpcport=18998"

# Wait for node to be ready
echo "    Waiting for node to start..."
for i in {1..30}; do
    if $CLI_CMD getblockchaininfo &>/dev/null; then
        echo "    Node ready!"
        break
    fi
    sleep 1
    if [ $i -eq 30 ]; then
        echo "ERROR: Node failed to start"
        cat "$DATADIR/regtest/debug.log" 2>/dev/null | tail -20 || echo "No debug log"
        exit 1
    fi
done

echo ""
echo "[2/5] Creating wallet..."

# Create a new wallet
$CLI_CMD createwallet "genesis_keys" false false "" false false 2>/dev/null || true

# Function to generate a key and extract info
generate_key() {
    local name=$1
    local addr=$($CLI_CMD getnewaddress "$name")
    local info=$($CLI_CMD validateaddress "$addr")
    local wif=$($CLI_CMD dumpprivkey "$addr")

    # Extract pubkey hash from scriptPubKey (P2PKH format)
    # scriptPubKey: OP_DUP OP_HASH160 <20-byte-hash> OP_EQUALVERIFY OP_CHECKSIG
    local scriptPubKey=$(echo "$info" | jq -r '.scriptPubKey')

    # Extract the 40-char hex (20 bytes) - P2PKH format: 76a914<hash>88ac
    local pubKeyHash=$(echo "$scriptPubKey" | sed -n 's/.*76a914\([a-f0-9]\{40\}\)88ac.*/\1/p')

    if [ -z "$pubKeyHash" ]; then
        echo "ERROR: Could not extract pubKeyHash for $name" >&2
        echo "Address: $addr" >&2
        echo "scriptPubKey: $scriptPubKey" >&2
        exit 1
    fi

    # Output as JSON (to stdout)
    echo "{\"name\": \"$name\", \"address\": \"$addr\", \"wif\": \"$wif\", \"pubKeyHash\": \"$pubKeyHash\"}"
}

echo ""
echo "[3/5] Generating 5 key pairs..."

# Generate all 5 keys
MN1=$(generate_key "MN1_COLLATERAL")
echo "    Generated MN1"
MN2=$(generate_key "MN2_COLLATERAL")
echo "    Generated MN2"
MN3=$(generate_key "MN3_COLLATERAL")
echo "    Generated MN3"
DEV=$(generate_key "DEV_WALLET")
echo "    Generated DEV"
FAUCET=$(generate_key "FAUCET_WALLET")
echo "    Generated FAUCET"

echo ""
echo "[4/5] Stopping temporary node..."
$CLI_CMD stop 2>/dev/null || true
sleep 2

# Extract pubKeyHashes
MN1_HASH=$(echo "$MN1" | jq -r '.pubKeyHash')
MN2_HASH=$(echo "$MN2" | jq -r '.pubKeyHash')
MN3_HASH=$(echo "$MN3" | jq -r '.pubKeyHash')
DEV_HASH=$(echo "$DEV" | jq -r '.pubKeyHash')
FAUCET_HASH=$(echo "$FAUCET" | jq -r '.pubKeyHash')

echo ""
echo "[5/5] Writing output to $OUTPUT_FILE..."

# Create JSON output
cat > "$OUTPUT_FILE" << EOF
{
  "network": "piv2-testnet",
  "generated": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "note": "Keys generated via regtest - pubKeyHash is network-agnostic",
  "warning": "NEVER commit this file to git! Keep private keys secure!",
  "genesis_outputs": [
    {
      "index": 0,
      "role": "MN1 Collateral",
      "amount_hu": 10000,
      "regtest_address": "$(echo "$MN1" | jq -r '.address')",
      "wif_regtest": "$(echo "$MN1" | jq -r '.wif')",
      "pubKeyHash": "$MN1_HASH"
    },
    {
      "index": 1,
      "role": "MN2 Collateral",
      "amount_hu": 10000,
      "regtest_address": "$(echo "$MN2" | jq -r '.address')",
      "wif_regtest": "$(echo "$MN2" | jq -r '.wif')",
      "pubKeyHash": "$MN2_HASH"
    },
    {
      "index": 2,
      "role": "MN3 Collateral",
      "amount_hu": 10000,
      "regtest_address": "$(echo "$MN3" | jq -r '.address')",
      "wif_regtest": "$(echo "$MN3" | jq -r '.wif')",
      "pubKeyHash": "$MN3_HASH"
    },
    {
      "index": 3,
      "role": "Dev Wallet",
      "amount_hu": 50000000,
      "regtest_address": "$(echo "$DEV" | jq -r '.address')",
      "wif_regtest": "$(echo "$DEV" | jq -r '.wif')",
      "pubKeyHash": "$DEV_HASH"
    },
    {
      "index": 4,
      "role": "Faucet",
      "amount_hu": 50000000,
      "regtest_address": "$(echo "$FAUCET" | jq -r '.address')",
      "wif_regtest": "$(echo "$FAUCET" | jq -r '.wif')",
      "pubKeyHash": "$FAUCET_HASH"
    }
  ],
  "chainparams_snippet": {
    "output_0_mn1": "ParseHex(\"$MN1_HASH\")",
    "output_1_mn2": "ParseHex(\"$MN2_HASH\")",
    "output_2_mn3": "ParseHex(\"$MN3_HASH\")",
    "output_3_dev": "ParseHex(\"$DEV_HASH\")",
    "output_4_faucet": "ParseHex(\"$FAUCET_HASH\")"
  }
}
EOF

# Clean up temp datadir
rm -rf "$DATADIR"

echo ""
echo "═══════════════════════════════════════════════════════════════════════════"
echo "  SUCCESS! Keys generated and saved to:"
echo "  $OUTPUT_FILE"
echo ""
echo "  ⚠️  IMPORTANT: This file contains PRIVATE KEYS!"
echo "  ⚠️  NEVER commit to git! Keep secure backup!"
echo "═══════════════════════════════════════════════════════════════════════════"
echo ""
echo "  pubKeyHash values for chainparams.cpp:"
echo "  ──────────────────────────────────────────────────────────────────────────"
echo "  MN1:    $MN1_HASH"
echo "  MN2:    $MN2_HASH"
echo "  MN3:    $MN3_HASH"
echo "  Dev:    $DEV_HASH"
echo "  Faucet: $FAUCET_HASH"
echo ""
echo "  Next steps:"
echo "  1. Copy pubKeyHash values to CreatePIV2TestnetGenesisBlock() in chainparams.cpp"
echo "  2. Recompile: make -j\$(nproc)"
echo "  3. The genesis will be auto-mined on first testnet start"
echo "  4. Import WIF keys into testnet wallet to access funds"
echo ""
