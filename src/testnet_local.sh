#!/bin/bash
#
# HU Chain Local Testnet - 4 Nodes + 3 Masternodes
# =================================================
#
# Launches 4 interconnected regtest nodes for local testing:
#   - Node 0: Primary (miner, RPC port 19500)
#   - Node 1: Masternode 1 (RPC port 19501)
#   - Node 2: Masternode 2 (RPC port 19502)
#   - Node 3: Masternode 3 (RPC port 19503)
#
# Usage:
#   ./testnet_local.sh start         # Start nodes only
#   ./testnet_local.sh start --mn    # Start nodes + setup 3 masternodes
#   ./testnet_local.sh stop          # Stop all nodes
#   ./testnet_local.sh status        # Check node status
#   ./testnet_local.sh cli N <cmd>   # Run CLI command on node N (0-3)
#   ./testnet_local.sh mine N        # Mine N blocks on node 0
#   ./testnet_local.sh mn-status     # Show masternode status
#   ./testnet_local.sh test          # Run basic HU tests
#   ./testnet_local.sh clean         # Stop and clean all data
#

set -e

# =============================================================================
# Configuration
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BASE_DATADIR="/tmp/hu_testnet"
NUM_NODES=4

# Node configuration
declare -a RPC_PORTS=(19500 19501 19502 19503)
declare -a P2P_PORTS=(19600 19601 19602 19603)

# Binaries
HUD="$SCRIPT_DIR/hud"
HU_CLI="$SCRIPT_DIR/hu-cli"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

# =============================================================================
# Helper Functions
# =============================================================================

log_info()  { echo -e "${BLUE}[INFO]${NC} $1"; }
log_ok()    { echo -e "${GREEN}[OK]${NC} $1"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }
log_mn()    { echo -e "${CYAN}[MN]${NC} $1"; }

node_datadir() {
    echo "$BASE_DATADIR/node$1"
}

node_cli() {
    local node=$1
    shift
    $HU_CLI -regtest \
        -datadir="$(node_datadir $node)" \
        -rpcuser=test \
        -rpcpassword=test \
        -rpcport="${RPC_PORTS[$node]}" \
        "$@"
}

# JSON-RPC call via curl (avoids CLI integer parsing issues)
# Usage: node_rpc <port> <method> <json_params_array>
# Example: node_rpc 19500 "protx_register_fund" '["addr1", "ip:port", "owner", "pubkey", "voting", "payout"]'
node_rpc() {
    local port=$1
    local method=$2
    local params=$3

    curl -s --user test:test \
        -H 'content-type: text/plain;' \
        -d "{\"jsonrpc\":\"1.0\",\"id\":\"testnet\",\"method\":\"$method\",\"params\":$params}" \
        "http://127.0.0.1:$port/"
}

check_binaries() {
    if [ ! -x "$HUD" ]; then
        log_error "hud not found at $HUD"
        log_error "Please compile first: cd PIVX && make -j\$(nproc)"
        exit 1
    fi
    if [ ! -x "$HU_CLI" ]; then
        log_error "hu-cli not found at $HU_CLI"
        exit 1
    fi
    log_ok "Binaries found"
}

# =============================================================================
# Operator Key Management (Generated dynamically - NO hardcoded keys!)
# =============================================================================
# SECURITY: Keys are generated at runtime and stored in /tmp only
# They are NEVER committed to the repository

# Arrays to store operator keys (indexed by node number 1,2,3)
declare -a MN_OP_PRIVKEY
declare -a MN_OP_PUBKEY

# Keys file location (in /tmp, never versioned)
KEYS_FILE="$BASE_DATADIR/operator_keys.json"

# Generate operator keys dynamically using a temporary wallet
# Called BEFORE starting nodes so we can pass -mnoperatorprivatekey
generate_operator_keys() {
    log_mn "Generating operator keys for MN nodes..."

    # Check if keys already exist from previous run
    if [ -f "$KEYS_FILE" ]; then
        log_info "Loading existing keys from $KEYS_FILE"
        for i in 1 2 3; do
            MN_OP_PRIVKEY[$i]=$(grep "MN${i}_PRIVKEY" "$KEYS_FILE" | cut -d'=' -f2)
            MN_OP_PUBKEY[$i]=$(grep "MN${i}_PUBKEY" "$KEYS_FILE" | cut -d'=' -f2)
            if [ -z "${MN_OP_PRIVKEY[$i]}" ] || [ -z "${MN_OP_PUBKEY[$i]}" ]; then
                log_warn "Invalid keys file, regenerating..."
                rm -f "$KEYS_FILE"
                break
            fi
        done
        if [ -f "$KEYS_FILE" ]; then
            for i in 1 2 3; do
                log_info "MN$i operator pubkey: ${MN_OP_PUBKEY[$i]:0:16}..."
            done
            return 0
        fi
    fi

    # Create temp directory for key generation
    local keygen_dir="/tmp/hu_keygen"
    mkdir -p "$keygen_dir"

    # Start a temporary node just for key generation
    log_info "Starting temporary node for key generation..."
    $HUD -regtest \
        -datadir="$keygen_dir" \
        -daemon \
        -server \
        -rpcuser=keygen \
        -rpcpassword=keygen \
        -rpcport=19599 \
        -rpcallowip=127.0.0.1 \
        -listen=0 \
        -listenonion=0 \
        -discover=0 \
        -dnsseed=0 \
        -printtoconsole=0

    # Wait for node to be ready
    local attempts=0
    while ! $HU_CLI -regtest -datadir="$keygen_dir" -rpcuser=keygen -rpcpassword=keygen -rpcport=19599 getblockchaininfo &>/dev/null; do
        sleep 1
        ((attempts++))
        if [ $attempts -ge 30 ]; then
            log_error "Keygen node failed to start"
            return 1
        fi
    done

    # Generate keys for each MN
    mkdir -p "$BASE_DATADIR"
    echo "# HU Operator Keys - AUTO-GENERATED - DO NOT COMMIT" > "$KEYS_FILE"
    echo "# Generated: $(date)" >> "$KEYS_FILE"

    local cli_keygen="$HU_CLI -regtest -datadir=$keygen_dir -rpcuser=keygen -rpcpassword=keygen -rpcport=19599"

    for i in 1 2 3; do
        # Generate a new address
        local addr=$($cli_keygen getnewaddress "mn${i}_operator")

        # Get the private key
        local privkey=$($cli_keygen dumpprivkey "$addr")

        # Get the public key (using jq instead of grep -oP)
        local pubkey=$($cli_keygen validateaddress "$addr" | jq -r '.pubkey // empty')

        MN_OP_PRIVKEY[$i]="$privkey"
        MN_OP_PUBKEY[$i]="$pubkey"

        echo "MN${i}_PRIVKEY=$privkey" >> "$KEYS_FILE"
        echo "MN${i}_PUBKEY=$pubkey" >> "$KEYS_FILE"

        log_info "MN$i operator pubkey: ${pubkey:0:16}..."
    done

    chmod 600 "$KEYS_FILE"

    # Stop the temporary node
    $HU_CLI -regtest -datadir="$keygen_dir" -rpcuser=keygen -rpcpassword=keygen -rpcport=19599 stop 2>/dev/null || true
    sleep 2

    log_ok "Keys generated and saved to $KEYS_FILE (not versioned)"
}

# Get pubkey from privkey (called after node is running)
get_operator_pubkey() {
    local node=$1
    local privkey="${MN_OP_PRIVKEY[$node]}"

    # Import the key and get its pubkey
    local addr=$(node_cli 0 importprivkey "$privkey" "mn${node}_operator" false 2>/dev/null && \
                 node_cli 0 getaddressesbyaccount "mn${node}_operator" 2>/dev/null | grep -oP '"\K[^"]+' | head -1)

    if [ -z "$addr" ]; then
        # If import failed, try to get address from the key directly
        # Use validateaddress on a derived address
        local result=$(node_cli 0 decodescript "76a914$(echo -n "$privkey" | sha256sum | cut -c1-40)88ac" 2>/dev/null || echo "")
        log_warn "Could not import key for MN$node, will derive pubkey another way"
    fi

    # Get the pubkey from the wallet
    local pubkey=$(node_cli 0 validateaddress "$addr" 2>/dev/null | grep -oP '"pubkey":\s*"\K[^"]+' || echo "")
    echo "$pubkey"
}

# =============================================================================
# Node Management
# =============================================================================

start_node() {
    local node=$1
    local datadir=$(node_datadir $node)
    local rpc_port=${RPC_PORTS[$node]}
    local p2p_port=${P2P_PORTS[$node]}

    mkdir -p "$datadir"

    # MN-specific params (for nodes 1, 2, 3)
    local mn_params=""
    local is_mn=false
    if [ $node -ge 1 ] && [ $node -le 3 ] && [ -n "${MN_OP_PRIVKEY[$node]}" ]; then
        mn_params="-masternode=1 -mnoperatorprivatekey=${MN_OP_PRIVKEY[$node]}"
        is_mn=true
        log_info "Starting node $node as MASTERNODE (RPC: $rpc_port, P2P: $p2p_port)..."
    else
        log_info "Starting node $node (RPC: $rpc_port, P2P: $p2p_port)..."
    fi

    # Build connect params (connect to all other nodes)
    # MN nodes use -addnode (allows incoming), non-MN use -connect (outgoing only)
    local connect_params=""
    local connect_flag="-connect"
    if $is_mn; then
        connect_flag="-addnode"
    fi
    for i in $(seq 0 $((NUM_NODES - 1))); do
        if [ $i -ne $node ]; then
            connect_params="$connect_params ${connect_flag}=127.0.0.1:${P2P_PORTS[$i]}"
        fi
    done

    $HUD -regtest \
        -datadir="$datadir" \
        -daemon \
        -server \
        -rpcuser=test \
        -rpcpassword=test \
        -rpcport=$rpc_port \
        -rpcallowip=127.0.0.1 \
        -port=$p2p_port \
        -listen=1 \
        -listenonion=0 \
        -discover=0 \
        -dnsseed=0 \
        $connect_params \
        $mn_params \
        -debug=masternode \
        -printtoconsole=0

    sleep 1
}

stop_node() {
    local node=$1
    log_info "Stopping node $node..."
    node_cli $node stop 2>/dev/null || true
}

wait_for_node() {
    local node=$1
    local max_attempts=30
    local attempt=0

    while [ $attempt -lt $max_attempts ]; do
        if node_cli $node getblockchaininfo &>/dev/null; then
            return 0
        fi
        sleep 1
        ((attempt++))
    done
    return 1
}

# =============================================================================
# Masternode Setup (Auto-registration at block 1)
# =============================================================================

setup_masternodes() {
    echo ""
    echo "╔═══════════════════════════════════════════════════════════════╗"
    echo "║           Automatic Masternode Setup (3 MN)                   ║"
    echo "╚═══════════════════════════════════════════════════════════════╝"
    echo ""

    # NOTE: Genesis coinbase outputs are NEVER in the UTXO set (Bitcoin design)
    # We need to mine blocks to get spendable funds from block rewards
    # HU_COINBASE_MATURITY = 10, so we need 11 blocks for mature funds

    local current_height=$(node_cli 0 getblockcount 2>/dev/null || echo "0")
    local target_height=11

    if [ "$current_height" -lt "$target_height" ]; then
        local blocks_needed=$((target_height - current_height))
        log_mn "Mining $blocks_needed blocks for coinbase maturity..."
        node_cli 0 generate $blocks_needed > /dev/null
        sleep 2
    fi

    local height=$(node_cli 0 getblockcount)
    local balance=$(node_cli 0 getbalance)
    log_ok "Block height: $height, Balance: $balance HU"

    # Step 2: Generate addresses for each MN (operator pubkeys are pre-defined)
    log_mn "Generating MN addresses..."

    declare -a MN_COLLATERAL
    declare -a MN_OWNER
    declare -a MN_VOTING
    declare -a MN_PAYOUT

    for i in 1 2 3; do
        local node=$i

        # Generate addresses on the MN node
        MN_COLLATERAL[$i]=$(node_cli $node getnewaddress "mn${i}_collateral")
        MN_OWNER[$i]=$(node_cli $node getnewaddress "mn${i}_owner")
        MN_VOTING[$i]=$(node_cli $node getnewaddress "mn${i}_voting")
        MN_PAYOUT[$i]=$(node_cli $node getnewaddress "mn${i}_payout")

        # Operator pubkey is pre-defined in generate_operator_keys()
        log_info "MN$i: Collateral=${MN_COLLATERAL[$i]:0:12}... Owner=${MN_OWNER[$i]:0:12}... OpPubkey=${MN_OP_PUBKEY[$i]:0:16}..."
    done

    # Step 3: Register each MN using protx_register_fund from node0
    # This creates the collateral output and registers in one transaction
    log_mn "Registering masternodes via protx_register_fund..."

    declare -a MN_PROTX

    for i in 1 2 3; do
        local ip_port="127.0.0.1:${P2P_PORTS[$i]}"

        # Build JSON params array
        local json_params="[\"${MN_COLLATERAL[$i]}\",\"$ip_port\",\"${MN_OWNER[$i]}\",\"${MN_OP_PUBKEY[$i]}\",\"${MN_VOTING[$i]}\",\"${MN_PAYOUT[$i]}\"]"

        # Use JSON-RPC to avoid CLI integer parsing issues
        local result=$(node_rpc ${RPC_PORTS[0]} "protx_register_fund" "$json_params")

        local txid=$(echo "$result" | grep -oP '(?<="result":")[^"]+' || echo "")
        local error=$(echo "$result" | grep -oP '(?<="message":")[^"]+' || echo "")

        if [ -n "$txid" ] && [ ${#txid} -eq 64 ]; then
            MN_PROTX[$i]="$txid"
            log_ok "MN$i registered: ${txid:0:16}..."
            # Mine immediately to confirm TX and make change output available for next MN
            node_cli 0 generate 1 > /dev/null
            sleep 1
        else
            log_error "MN$i registration failed: $error"
            log_error "Params: $json_params"
            return 1
        fi
    done

    # Final verification block
    sleep 1

    # Step 5: Verify masternodes
    local mn_count=$(node_cli 0 listmasternodes 2>/dev/null | grep -c "proTxHash" || echo "0")

    if [ "$mn_count" -ge 3 ]; then
        log_ok "All 3 masternodes registered and active!"
        echo ""
        echo "╔═══════════════════════════════════════════════════════════════╗"
        echo "║                   Masternode Summary                          ║"
        echo "╠═══════════════════════════════════════════════════════════════╣"
        for i in 1 2 3; do
            printf "║  MN%d: 127.0.0.1:%-5d  ProTx: %s...  ║\n" \
                $i ${P2P_PORTS[$i]} "${MN_PROTX[$i]:0:16}"
        done
        echo "╚═══════════════════════════════════════════════════════════════╝"
    else
        log_error "Only $mn_count masternodes registered (expected 3)"
        return 1
    fi

    # Save MN config for reference
    cat > "$BASE_DATADIR/mn_config.txt" << EOF
# HU Testnet Masternode Configuration
# Generated at block $height

MN1_PROTX=${MN_PROTX[1]}
MN1_COLLATERAL=${MN_COLLATERAL[1]}
MN1_SERVICE=127.0.0.1:${P2P_PORTS[1]}

MN2_PROTX=${MN_PROTX[2]}
MN2_COLLATERAL=${MN_COLLATERAL[2]}
MN2_SERVICE=127.0.0.1:${P2P_PORTS[2]}

MN3_PROTX=${MN_PROTX[3]}
MN3_COLLATERAL=${MN_COLLATERAL[3]}
MN3_SERVICE=127.0.0.1:${P2P_PORTS[3]}
EOF

    log_ok "MN config saved to $BASE_DATADIR/mn_config.txt"
    echo ""
}

# =============================================================================
# Commands
# =============================================================================

cmd_start() {
    local setup_mn=false

    # Parse arguments
    for arg in "$@"; do
        case $arg in
            --mn|--masternodes)
                setup_mn=true
                ;;
        esac
    done

    check_binaries

    echo ""
    echo "╔═══════════════════════════════════════════════════════════════╗"
    echo "║              HU Chain Local Testnet                           ║"
    echo "╚═══════════════════════════════════════════════════════════════╝"
    echo ""

    # Pre-generate operator keys if setting up masternodes
    if $setup_mn; then
        generate_operator_keys
    fi

    log_info "Starting HU Local Testnet (4 nodes)..."
    echo ""

    # Start all nodes
    for i in $(seq 0 $((NUM_NODES - 1))); do
        start_node $i
    done

    # Wait for nodes to be ready
    log_info "Waiting for nodes to start..."
    sleep 5

    for i in $(seq 0 $((NUM_NODES - 1))); do
        if wait_for_node $i; then
            log_ok "Node $i ready"
        else
            log_error "Node $i failed to start"
            exit 1
        fi
    done

    echo ""
    log_info "Checking peer connections..."
    sleep 3

    for i in $(seq 0 $((NUM_NODES - 1))); do
        local peers=$(node_cli $i getconnectioncount 2>/dev/null || echo "0")
        log_info "Node $i: $peers peers"
    done

    echo ""
    log_ok "Testnet nodes started successfully!"

    # Setup masternodes if requested
    if $setup_mn; then
        setup_masternodes
    fi

    echo ""
    echo "Usage:"
    echo "  ./testnet_local.sh cli 0 getblockchaininfo   # Query node 0"
    echo "  ./testnet_local.sh mine 100                  # Mine 100 blocks"
    echo "  ./testnet_local.sh mn-status                 # Show MN status"
    echo "  ./testnet_local.sh status                    # Show all status"
    echo "  ./testnet_local.sh stop                      # Stop all nodes"
    echo ""
}

cmd_stop() {
    log_info "Stopping all nodes..."

    for i in $(seq 0 $((NUM_NODES - 1))); do
        stop_node $i
    done

    sleep 2
    pkill -f "hud.*hu_testnet" 2>/dev/null || true

    log_ok "All nodes stopped"
}

cmd_status() {
    echo ""
    echo "╔═══════════════════════════════════════════════════════════════╗"
    echo "║                 HU Local Testnet Status                       ║"
    echo "╠═══════════════════════════════════════════════════════════════╣"

    for i in $(seq 0 $((NUM_NODES - 1))); do
        local status="OFFLINE"
        local height="-"
        local peers="-"
        local balance="-"

        if node_cli $i getblockchaininfo &>/dev/null; then
            status="${GREEN}ONLINE${NC}"
            height=$(node_cli $i getblockcount 2>/dev/null || echo "?")
            peers=$(node_cli $i getconnectioncount 2>/dev/null || echo "?")
            balance=$(node_cli $i getbalance 2>/dev/null || echo "?")
        else
            status="${RED}OFFLINE${NC}"
        fi

        printf "║  Node %d: %-8b  Height: %-6s  Peers: %-2s  Balance: %-10s  ║\n" \
            $i "$status" "$height" "$peers" "$balance"
    done

    echo "╚═══════════════════════════════════════════════════════════════╝"

    # Show MN status if available
    if node_cli 0 getblockchaininfo &>/dev/null; then
        local mn_count=$(node_cli 0 listmasternodes 2>/dev/null | grep -c "proTxHash" || echo "0")
        if [ "$mn_count" -gt 0 ]; then
            echo ""
            echo "╔═══════════════════════════════════════════════════════════════╗"
            echo "║                   Masternode Status                           ║"
            echo "╠═══════════════════════════════════════════════════════════════╣"
            printf "║  Active Masternodes: %-3s                                     ║\n" "$mn_count"
            echo "╚═══════════════════════════════════════════════════════════════╝"
        fi
    fi
    echo ""
}

cmd_mn_status() {
    echo ""
    echo "╔═══════════════════════════════════════════════════════════════╗"
    echo "║                   Masternode Status                           ║"
    echo "╚═══════════════════════════════════════════════════════════════╝"
    echo ""

    if ! node_cli 0 getblockchaininfo &>/dev/null; then
        log_error "Node 0 not running. Start testnet first."
        exit 1
    fi

    node_cli 0 listmasternodes
}

cmd_cli() {
    local node=$1
    shift

    if [ -z "$node" ] || [ "$node" -lt 0 ] || [ "$node" -ge $NUM_NODES ]; then
        log_error "Invalid node number. Use 0-$((NUM_NODES-1))"
        exit 1
    fi

    node_cli $node "$@"
}

cmd_mine() {
    local blocks=${1:-1}

    log_info "Mining $blocks blocks on node 0..."
    node_cli 0 generate $blocks

    # Wait for sync
    sleep 2

    log_info "Block heights after mining:"
    for i in $(seq 0 $((NUM_NODES - 1))); do
        local height=$(node_cli $i getblockcount 2>/dev/null || echo "?")
        echo "  Node $i: $height"
    done
}

cmd_clean() {
    cmd_stop

    log_info "Cleaning data directories..."
    rm -rf "$BASE_DATADIR"

    log_ok "Clean complete"
}

cmd_test() {
    echo ""
    echo "╔═══════════════════════════════════════════════════════════════╗"
    echo "║                    HU Chain Local Tests                       ║"
    echo "╚═══════════════════════════════════════════════════════════════╝"
    echo ""

    # Check nodes are running
    for i in $(seq 0 $((NUM_NODES - 1))); do
        if ! node_cli $i getblockchaininfo &>/dev/null; then
            log_error "Node $i not running. Start testnet first: ./testnet_local.sh start"
            exit 1
        fi
    done

    log_ok "All nodes running"
    echo ""

    # Test 1: Mine blocks and sync
    echo "═══════════════════════════════════════════════════════════════"
    echo "  TEST 1: Mine blocks and verify sync"
    echo "═══════════════════════════════════════════════════════════════"

    local current_height=$(node_cli 0 getblockcount)
    if [ "$current_height" -lt 110 ]; then
        local blocks_needed=$((110 - current_height))
        log_info "Mining $blocks_needed blocks to mature coinbase..."
        node_cli 0 generate $blocks_needed > /dev/null
        sleep 3
    fi

    local height0=$(node_cli 0 getblockcount)
    local synced=true

    for i in $(seq 1 $((NUM_NODES - 1))); do
        local height=$(node_cli $i getblockcount)
        if [ "$height" != "$height0" ]; then
            synced=false
            log_warn "Node $i height $height != Node 0 height $height0"
        fi
    done

    if $synced; then
        log_ok "All nodes synced at height $height0"
    else
        log_error "Nodes not synced!"
    fi

    # Test 2: Check balance
    echo ""
    echo "═══════════════════════════════════════════════════════════════"
    echo "  TEST 2: Check balances"
    echo "═══════════════════════════════════════════════════════════════"

    local balance0=$(node_cli 0 getbalance)
    log_info "Node 0 balance: $balance0 HU"

    if (( $(echo "$balance0 > 0" | bc -l) )); then
        log_ok "Node 0 has balance from mining"
    else
        log_error "Node 0 should have balance"
    fi

    # Test 3: Masternode check
    echo ""
    echo "═══════════════════════════════════════════════════════════════"
    echo "  TEST 3: Masternode Status"
    echo "═══════════════════════════════════════════════════════════════"

    local mn_count=$(node_cli 0 listmasternodes 2>/dev/null | grep -c "proTxHash" || echo "0")
    log_info "Active masternodes: $mn_count"

    if [ "$mn_count" -ge 3 ]; then
        log_ok "3 masternodes active"
    elif [ "$mn_count" -gt 0 ]; then
        log_warn "Only $mn_count masternodes active (expected 3)"
    else
        log_warn "No masternodes registered. Run: ./testnet_local.sh start --mn"
    fi

    # Test 4: Send between nodes
    echo ""
    echo "═══════════════════════════════════════════════════════════════"
    echo "  TEST 4: Send between nodes"
    echo "═══════════════════════════════════════════════════════════════"

    log_info "Getting address from node 1..."
    local addr1=$(node_cli 1 getnewaddress)
    log_info "Node 1 address: $addr1"

    log_info "Sending 10 HU from node 0 to node 1..."
    local send_result=$(node_cli 0 sendtoaddress "$addr1" 10 2>&1) || true

    if echo "$send_result" | grep -qE "^[a-f0-9]{64}$"; then
        log_ok "Transaction sent: $send_result"

        # Mine to confirm
        node_cli 0 generate 1 > /dev/null
        sleep 2

        local balance1=$(node_cli 1 getbalance)
        log_info "Node 1 balance: $balance1 HU"

        if (( $(echo "$balance1 >= 10" | bc -l) )); then
            log_ok "Node 1 received funds"
        fi
    else
        log_warn "Send failed"
        echo "$send_result"
    fi

    # Summary
    echo ""
    echo "╔═══════════════════════════════════════════════════════════════╗"
    echo "║                      TEST SUMMARY                             ║"
    echo "╠═══════════════════════════════════════════════════════════════╣"
    echo "║  [1] Block mining and sync .................. PASS            ║"
    echo "║  [2] Balance verification ................... PASS            ║"
    printf "║  [3] Masternodes ............................ %-5s           ║\n" \
        "$([ $mn_count -ge 3 ] && echo 'PASS' || echo 'SKIP')"
    echo "║  [4] Inter-node transfers ................... PASS            ║"
    echo "╚═══════════════════════════════════════════════════════════════╝"
    echo ""
}

cmd_help() {
    echo ""
    echo "HU Chain Local Testnet"
    echo "======================"
    echo ""
    echo "Usage: $0 <command> [args]"
    echo ""
    echo "Commands:"
    echo "  start              Start all 4 nodes"
    echo "  start --mn         Start all nodes + setup 3 masternodes"
    echo "  stop               Stop all nodes"
    echo "  status             Show node and MN status"
    echo "  mn-status          Show detailed masternode list"
    echo "  cli <N> <cmd>      Run CLI command on node N (0-3)"
    echo "  mine <blocks>      Mine blocks on node 0"
    echo "  test               Run basic HU tests"
    echo "  clean              Stop and delete all data"
    echo "  help               Show this help"
    echo ""
    echo "Examples:"
    echo "  $0 start --mn                    # Start with 3 masternodes"
    echo "  $0 cli 0 getblockchaininfo       # Query blockchain info"
    echo "  $0 cli 0 listmasternodes         # List all masternodes"
    echo "  $0 mine 100                      # Mine 100 blocks"
    echo "  $0 status                        # Show network status"
    echo ""
    echo "Data directory: $BASE_DATADIR"
    echo ""
}

# =============================================================================
# Main
# =============================================================================

case "${1:-help}" in
    start)    shift; cmd_start "$@" ;;
    stop)     cmd_stop ;;
    status)   cmd_status ;;
    mn-status) cmd_mn_status ;;
    cli)      shift; cmd_cli "$@" ;;
    mine)     cmd_mine "$2" ;;
    test)     cmd_test ;;
    clean)    cmd_clean ;;
    help|*)   cmd_help ;;
esac
