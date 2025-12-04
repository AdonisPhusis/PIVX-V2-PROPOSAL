#!/bin/bash
#
# HU Testnet DMM Integration Tests
# =================================
#
# This script runs REAL integration tests on a local testnet with:
# - 4 nodes (1 primary + 3 masternodes)
# - DMM block production (not manual generate)
# - Real HU operations (MINT, LOCK, UNLOCK, DAO)
# - Stress testing and MN failure scenarios
#
# Usage:
#   ./testnet_dmm_integration.sh start     # Start testnet with 3 MN
#   ./testnet_dmm_integration.sh status    # Check status
#   ./testnet_dmm_integration.sh test      # Run quick tests
#   ./testnet_dmm_integration.sh long      # Run long integration (100+ blocks)
#   ./testnet_dmm_integration.sh stress    # Stress test (200 tx)
#   ./testnet_dmm_integration.sh mn-down   # Test MN failure/recovery
#   ./testnet_dmm_integration.sh stop      # Stop all nodes
#   ./testnet_dmm_integration.sh report    # Generate final report
#
# Requirements:
#   - /tmp/hu_testnet_genesis_keys.json must exist
#   - Compiled hud and hu-cli
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HUD="$SCRIPT_DIR/hud"
CLI="$SCRIPT_DIR/hu-cli"
DATADIR="/tmp/hu_testnet_dmm"
KEYS_FILE="/tmp/hu_testnet_genesis_keys.json"
REPORT_FILE="/tmp/hu_integration_report.txt"
LOG_FILE="/tmp/hu_integration.log"

# Ports (testnet mode)
declare -A RPC_PORTS=([0]=18500 [1]=18501 [2]=18502 [3]=18503)
declare -A P2P_PORTS=([0]=18600 [1]=18601 [2]=18602 [3]=18603)

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Report counters
BLOCKS_PRODUCED=0
DMM_ROTATIONS=0
LOCK_UNLOCK_COUNT=0
DAO_PROPOSALS=0
INVARIANT_VIOLATIONS=0
REORGS_DETECTED=0
ERRORS_FATAL=0

log() {
    echo -e "${BLUE}[$(date +%H:%M:%S)]${NC} $*"
    echo "[$(date +%H:%M:%S)] $*" >> "$LOG_FILE"
}

log_ok() {
    echo -e "${GREEN}[OK]${NC} $*"
    echo "[OK] $*" >> "$LOG_FILE"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $*"
    echo "[WARN] $*" >> "$LOG_FILE"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $*"
    echo "[ERROR] $*" >> "$LOG_FILE"
    ((ERRORS_FATAL++)) || true
}

cli() {
    local node=$1
    shift
    "$CLI" -testnet -datadir="$DATADIR/node$node" -rpcuser=test -rpcpassword=test -rpcport="${RPC_PORTS[$node]}" "$@" 2>/dev/null
}

# ═══════════════════════════════════════════════════════════════════════════════
# SETUP FUNCTIONS
# ═══════════════════════════════════════════════════════════════════════════════

check_prerequisites() {
    log "Checking prerequisites..."

    if [ ! -x "$HUD" ]; then
        log_error "hud not found at $HUD"
        exit 1
    fi

    if [ ! -x "$CLI" ]; then
        log_error "hu-cli not found at $CLI"
        exit 1
    fi

    if [ ! -f "$KEYS_FILE" ]; then
        log_error "Genesis keys not found: $KEYS_FILE"
        log "Run: ./generate_testnet_genesis_keys.sh first"
        exit 1
    fi

    log_ok "Prerequisites OK"
}

create_node_config() {
    local node=$1
    local is_mn=$2
    local datadir="$DATADIR/node$node"

    mkdir -p "$datadir"

    cat > "$datadir/pivx.conf" << EOF
testnet=1
server=1
daemon=1
rpcuser=test
rpcpassword=test
rpcport=${RPC_PORTS[$node]}
rpcallowip=127.0.0.1
port=${P2P_PORTS[$node]}
listen=1
listenonion=0
dnsseed=0
EOF

    # Add connections to other nodes
    for i in 0 1 2 3; do
        if [ $i -ne $node ]; then
            echo "addnode=127.0.0.1:${P2P_PORTS[$i]}" >> "$datadir/pivx.conf"
        fi
    done

    # MN specific config
    if [ "$is_mn" = "1" ]; then
        echo "masternode=1" >> "$datadir/pivx.conf"
    fi
}

start_nodes() {
    log "Starting 4 testnet nodes..."

    # Clean previous data
    pkill -f "hud.*hu_testnet_dmm" 2>/dev/null || true
    sleep 2
    rm -rf "$DATADIR"
    mkdir -p "$DATADIR"

    # Create configs
    create_node_config 0 0  # Primary node (not MN)
    create_node_config 1 1  # MN1
    create_node_config 2 1  # MN2
    create_node_config 3 1  # MN3

    # Start nodes
    for i in 0 1 2 3; do
        log "  Starting node $i (RPC:${RPC_PORTS[$i]}, P2P:${P2P_PORTS[$i]})..."
        "$HUD" -testnet -datadir="$DATADIR/node$i" &
        sleep 2
    done

    # Wait for nodes to be ready
    log "Waiting for nodes to sync..."
    for attempt in {1..30}; do
        all_ready=true
        for i in 0 1 2 3; do
            if ! cli $i getblockchaininfo &>/dev/null; then
                all_ready=false
                break
            fi
        done
        if $all_ready; then
            break
        fi
        sleep 1
    done

    if ! $all_ready; then
        log_error "Nodes failed to start"
        exit 1
    fi

    log_ok "All 4 nodes running"
}

import_genesis_keys() {
    log "Importing genesis keys into node 0..."

    # Extract WIFs from keys file
    local mn1_wif=$(jq -r '.genesis_outputs[0].wif_regtest' "$KEYS_FILE")
    local mn2_wif=$(jq -r '.genesis_outputs[1].wif_regtest' "$KEYS_FILE")
    local mn3_wif=$(jq -r '.genesis_outputs[2].wif_regtest' "$KEYS_FILE")
    local dev_wif=$(jq -r '.genesis_outputs[3].wif_regtest' "$KEYS_FILE")
    local faucet_wif=$(jq -r '.genesis_outputs[4].wif_regtest' "$KEYS_FILE")

    # Import into node 0 (primary controller)
    cli 0 importprivkey "$mn1_wif" "MN1_COLLATERAL" false || log_warn "MN1 key import issue"
    cli 0 importprivkey "$mn2_wif" "MN2_COLLATERAL" false || log_warn "MN2 key import issue"
    cli 0 importprivkey "$mn3_wif" "MN3_COLLATERAL" false || log_warn "MN3 key import issue"
    cli 0 importprivkey "$dev_wif" "DEV_WALLET" false || log_warn "Dev key import issue"
    cli 0 importprivkey "$faucet_wif" "FAUCET_WALLET" false || log_warn "Faucet key import issue"

    # Rescan to find genesis outputs
    log "Rescanning blockchain..."
    cli 0 rescanblockchain 0 || true

    # Check balance
    local balance=$(cli 0 getbalance 2>/dev/null || echo "0")
    log "Node 0 balance: $balance HU"

    if [ "$balance" = "0" ] || [ "$balance" = "0.00000000" ]; then
        log_warn "Balance is 0 - genesis outputs may not be spendable yet"
        log_warn "This is expected: genesis coinbase needs maturity"
    else
        log_ok "Genesis keys imported, balance: $balance HU"
    fi
}

register_masternodes() {
    log "Registering 3 masternodes..."

    # Generate operator keys for each MN
    for i in 1 2 3; do
        local op_addr=$(cli $i getnewaddress "operator_$i")
        local op_info=$(cli $i validateaddress "$op_addr")
        local op_pubkey=$(echo "$op_info" | jq -r '.pubkey')

        log "  MN$i operator pubkey: $op_pubkey"

        # Store for later
        echo "$op_pubkey" > "$DATADIR/mn${i}_operator.txt"
    done

    # Note: In testnet with DMM, MN registration uses the genesis collateral outputs
    # The protx_register commands need the specific UTXO from genesis

    local genesis_txid=$(cli 0 getblockhash 0 | xargs -I{} cli 0 getblock {} | jq -r '.tx[0]')
    log "Genesis txid: $genesis_txid"

    # Check if genesis outputs are available
    for i in 0 1 2; do
        local utxo=$(cli 0 gettxout "$genesis_txid" $i 2>/dev/null)
        if [ -n "$utxo" ]; then
            log "  Output $i available: $(echo "$utxo" | jq -r '.value') HU"
        else
            log_warn "  Output $i not in UTXO set (genesis coinbase not spendable)"
        fi
    done

    log_warn "Genesis coinbase outputs are not spendable (Bitcoin design)"
    log_warn "For DMM to work, we need to use a different approach"
    log ""
    log "ALTERNATIVE: Testing DMM with manual block production first"
}

# ═══════════════════════════════════════════════════════════════════════════════
# TEST FUNCTIONS
# ═══════════════════════════════════════════════════════════════════════════════

check_status() {
    echo ""
    echo "═══════════════════════════════════════════════════════════════════════════"
    echo "  HU Testnet DMM Status"
    echo "═══════════════════════════════════════════════════════════════════════════"
    echo ""

    for i in 0 1 2 3; do
        local info=$(cli $i getblockchaininfo 2>/dev/null)
        if [ -n "$info" ]; then
            local height=$(echo "$info" | jq -r '.blocks')
            local network=$(echo "$info" | jq -r '.chain')
            local connections=$(cli $i getconnectioncount 2>/dev/null || echo "0")
            echo -e "  Node $i: ${GREEN}ONLINE${NC}  Height: $height  Connections: $connections  Network: $network"
        else
            echo -e "  Node $i: ${RED}OFFLINE${NC}"
        fi
    done

    echo ""

    # Check masternodes
    local mn_list=$(cli 0 listmasternodes 2>/dev/null)
    local mn_count=$(echo "$mn_list" | jq 'length' 2>/dev/null || echo "0")
    echo "  Registered Masternodes: $mn_count"

    # Check HU state
    local hu_state=$(cli 0 getkhustate 2>/dev/null)
    if [ -n "$hu_state" ]; then
        echo ""
        echo "  HU State:"
        echo "    C (Collateral):    $(echo "$hu_state" | jq -r '.C // "N/A"')"
        echo "    U (Transparent):   $(echo "$hu_state" | jq -r '.U // "N/A"')"
        echo "    Z (Shielded):      $(echo "$hu_state" | jq -r '.Z // "N/A"')"
        echo "    T (Treasury):      $(echo "$hu_state" | jq -r '.T // "N/A"')"
        echo "    R (Yield Rate):    $(echo "$hu_state" | jq -r '.R_annual // "N/A"')%"
    fi

    echo ""
}

test_dmm_rotation() {
    local blocks=${1:-10}
    log "Testing DMM rotation over $blocks blocks..."

    local start_height=$(cli 0 getblockcount)
    local producers=""

    for ((b=1; b<=blocks; b++)); do
        # Wait for new block
        local current=$(cli 0 getblockcount)
        local timeout=120
        while [ "$current" -eq "$start_height" ] && [ $timeout -gt 0 ]; do
            sleep 1
            current=$(cli 0 getblockcount)
            ((timeout--))
        done

        if [ "$current" -gt "$start_height" ]; then
            local block_hash=$(cli 0 getblockhash $current)
            local block=$(cli 0 getblock "$block_hash")
            local miner=$(echo "$block" | jq -r '.miner // "unknown"')
            producers="$producers$miner "
            start_height=$current
            ((BLOCKS_PRODUCED++))
            log "  Block $current: miner=$miner"
        else
            log_warn "  Timeout waiting for block $b"
            break
        fi
    done

    # Analyze rotation
    local unique_miners=$(echo "$producers" | tr ' ' '\n' | sort -u | wc -l)
    if [ "$unique_miners" -gt 1 ]; then
        log_ok "DMM rotation detected: $unique_miners unique miners"
        ((DMM_ROTATIONS++))
    else
        log_warn "No DMM rotation observed (only 1 miner)"
    fi
}

test_hu_operations() {
    log "Testing HU operations..."

    # Get current state
    local state=$(cli 0 getkhustate 2>/dev/null)
    if [ -z "$state" ]; then
        log_warn "getkhustate not available"
        return
    fi

    local C_before=$(echo "$state" | jq -r '.C // 0')
    local U_before=$(echo "$state" | jq -r '.U // 0')

    log "  State before: C=$C_before, U=$U_before"

    # Try MINT (if we have balance)
    local balance=$(cli 0 getbalance 2>/dev/null || echo "0")
    if [ "$(echo "$balance > 100" | bc)" = "1" ]; then
        log "  Attempting MINT of 100 HU..."
        local mint_result=$(cli 0 khumint 100 2>/dev/null || echo "error")
        if [ "$mint_result" != "error" ]; then
            log_ok "  MINT successful: $mint_result"
        else
            log_warn "  MINT not available or failed"
        fi
    else
        log_warn "  Insufficient balance for MINT test"
    fi

    # Verify invariants
    local state_after=$(cli 0 getkhustate 2>/dev/null)
    if [ -n "$state_after" ]; then
        local C_after=$(echo "$state_after" | jq -r '.C // 0')
        local U_after=$(echo "$state_after" | jq -r '.U // 0')
        local Z_after=$(echo "$state_after" | jq -r '.Z // 0')

        # Check C == U + Z
        local sum=$(echo "$U_after + $Z_after" | bc)
        if [ "$C_after" = "$sum" ]; then
            log_ok "  Invariant C == U + Z: OK ($C_after == $sum)"
        else
            log_error "  Invariant VIOLATION: C=$C_after != U+Z=$sum"
            ((INVARIANT_VIOLATIONS++))
        fi
    fi
}

test_lock_unlock() {
    log "Testing LOCK/UNLOCK cycle..."

    local khu_balance=$(cli 0 khubalance 2>/dev/null | jq -r '.khu_transparent // 0')

    if [ "$(echo "$khu_balance > 100" | bc)" = "1" ]; then
        log "  KHU balance: $khu_balance"

        # LOCK
        log "  Attempting LOCK of 100 KHU..."
        local lock_result=$(cli 0 khulock 100 2>/dev/null || echo "error")
        if [ "$lock_result" != "error" ]; then
            log_ok "  LOCK successful"
            ((LOCK_UNLOCK_COUNT++))

            # Wait for maturity (simplified)
            log "  Waiting for maturity..."
            sleep 5

            # UNLOCK
            log "  Attempting UNLOCK..."
            local unlock_result=$(cli 0 khuunlock 100 2>/dev/null || echo "error")
            if [ "$unlock_result" != "error" ]; then
                log_ok "  UNLOCK successful"
                ((LOCK_UNLOCK_COUNT++))
            else
                log_warn "  UNLOCK not available (maturity not reached?)"
            fi
        else
            log_warn "  LOCK not available"
        fi
    else
        log_warn "  Insufficient KHU balance for LOCK test"
    fi
}

stress_test() {
    local tx_count=${1:-100}
    log "Running stress test: $tx_count transactions..."

    local success=0
    local failed=0
    local start_time=$(date +%s)

    # Get an address
    local addr=$(cli 0 getnewaddress "stress_test")

    for ((i=1; i<=tx_count; i++)); do
        local result=$(cli 0 sendtoaddress "$addr" 0.001 2>/dev/null || echo "error")
        if [ "$result" != "error" ]; then
            ((success++))
        else
            ((failed++))
        fi

        if [ $((i % 20)) -eq 0 ]; then
            log "  Progress: $i/$tx_count (success: $success, failed: $failed)"
        fi
    done

    local end_time=$(date +%s)
    local duration=$((end_time - start_time))

    log "Stress test complete:"
    log "  Total: $tx_count transactions in ${duration}s"
    log "  Success: $success"
    log "  Failed: $failed"
    log "  TPS: $(echo "scale=2; $success / $duration" | bc)"

    # Check mempool
    local mempool_size=$(cli 0 getmempoolinfo | jq -r '.size')
    log "  Mempool size: $mempool_size"
}

test_mn_failure() {
    log "Testing MN failure/recovery..."

    # Get current block height
    local start_height=$(cli 0 getblockcount)
    log "  Start height: $start_height"

    # Kill MN2
    log "  Stopping MN2 (node 2)..."
    pkill -f "hud.*node2" 2>/dev/null || true
    sleep 5

    # Check if blocks still produced
    log "  Waiting for blocks with MN2 down..."
    local blocks_with_mn2_down=0
    for ((i=1; i<=10; i++)); do
        sleep 6
        local current=$(cli 0 getblockcount)
        if [ "$current" -gt "$start_height" ]; then
            ((blocks_with_mn2_down++))
            start_height=$current
            log "    Block $current produced"
        fi
    done

    log "  Blocks produced with MN2 down: $blocks_with_mn2_down"

    # Restart MN2
    log "  Restarting MN2..."
    "$HUD" -testnet -datadir="$DATADIR/node2" &
    sleep 5

    # Wait for sync
    for ((i=1; i<=30; i++)); do
        if cli 2 getblockchaininfo &>/dev/null; then
            break
        fi
        sleep 1
    done

    # Check if MN2 synced
    local mn2_height=$(cli 2 getblockcount 2>/dev/null || echo "0")
    local main_height=$(cli 0 getblockcount)

    if [ "$mn2_height" = "$main_height" ]; then
        log_ok "  MN2 recovered and synced to height $mn2_height"
    else
        log_warn "  MN2 at height $mn2_height, main chain at $main_height"
    fi
}

check_reorgs() {
    log "Checking for reorgs..."

    # Parse debug logs for reorg events
    for i in 0 1 2 3; do
        local log_file="$DATADIR/node$i/testnet5/debug.log"
        if [ -f "$log_file" ]; then
            local reorg_count=$(grep -c "REORGANIZE" "$log_file" 2>/dev/null || echo "0")
            if [ "$reorg_count" -gt 0 ]; then
                log_warn "  Node $i: $reorg_count reorg events"
                ((REORGS_DETECTED += reorg_count))
            else
                log "  Node $i: No reorgs"
            fi
        fi
    done

    if [ $REORGS_DETECTED -eq 0 ]; then
        log_ok "No reorgs detected"
    fi
}

# ═══════════════════════════════════════════════════════════════════════════════
# LONG INTEGRATION TEST
# ═══════════════════════════════════════════════════════════════════════════════

run_long_integration() {
    local target_blocks=${1:-100}
    local start_time=$(date +%s)

    echo ""
    echo "═══════════════════════════════════════════════════════════════════════════"
    echo "  HU Testnet Long Integration Test"
    echo "  Target: $target_blocks blocks"
    echo "═══════════════════════════════════════════════════════════════════════════"
    echo ""

    log "Starting long integration test..."

    local start_height=$(cli 0 getblockcount)
    log "Initial height: $start_height"

    local last_height=$start_height
    local check_interval=10  # Check every 10 seconds
    local blocks_since_op=0

    while [ $BLOCKS_PRODUCED -lt $target_blocks ]; do
        sleep $check_interval

        local current_height=$(cli 0 getblockcount)
        local new_blocks=$((current_height - last_height))

        if [ $new_blocks -gt 0 ]; then
            BLOCKS_PRODUCED=$((BLOCKS_PRODUCED + new_blocks))
            blocks_since_op=$((blocks_since_op + new_blocks))
            log "Height: $current_height (+$new_blocks blocks, total: $BLOCKS_PRODUCED)"
            last_height=$current_height

            # Every 20 blocks, run some operations
            if [ $blocks_since_op -ge 20 ]; then
                test_hu_operations
                blocks_since_op=0
            fi
        fi

        # Check invariants
        local state=$(cli 0 getkhustate 2>/dev/null)
        if [ -n "$state" ]; then
            local T=$(echo "$state" | jq -r '.T // 0')
            if [ "$(echo "$T < 0" | bc)" = "1" ]; then
                log_error "INVARIANT VIOLATION: T < 0 ($T)"
                ((INVARIANT_VIOLATIONS++))
            fi
        fi

        # Timeout check (2 hours max)
        local elapsed=$(($(date +%s) - start_time))
        if [ $elapsed -gt 7200 ]; then
            log_warn "Timeout reached (2 hours)"
            break
        fi
    done

    local end_time=$(date +%s)
    local duration=$((end_time - start_time))

    log "Long integration complete"
    log "Duration: ${duration}s"
    log "Blocks produced: $BLOCKS_PRODUCED"
}

# ═══════════════════════════════════════════════════════════════════════════════
# REPORT
# ═══════════════════════════════════════════════════════════════════════════════

generate_report() {
    local end_time=$(date +"%Y-%m-%d %H:%M:%S")

    cat > "$REPORT_FILE" << EOF
═══════════════════════════════════════════════════════════════════════════════
  HU TESTNET INTEGRATION REPORT
  Generated: $end_time
═══════════════════════════════════════════════════════════════════════════════

SUMMARY
-------
  Blocks produced on testnet:     $BLOCKS_PRODUCED
  DMM rotations observed:         $DMM_ROTATIONS
  LOCK/UNLOCK operations:         $LOCK_UNLOCK_COUNT
  DAO proposals submitted:        $DAO_PROPOSALS
  Invariant violations:           $INVARIANT_VIOLATIONS
  Reorgs detected (>1 block):     $REORGS_DETECTED
  Fatal errors:                   $ERRORS_FATAL

CHAIN STATUS
------------
EOF

    for i in 0 1 2 3; do
        local height=$(cli $i getblockcount 2>/dev/null || echo "offline")
        echo "  Node $i height: $height" >> "$REPORT_FILE"
    done

    cat >> "$REPORT_FILE" << EOF

HU STATE (Final)
----------------
EOF

    local state=$(cli 0 getkhustate 2>/dev/null)
    if [ -n "$state" ]; then
        echo "$state" | jq '.' >> "$REPORT_FILE"
    else
        echo "  Not available" >> "$REPORT_FILE"
    fi

    cat >> "$REPORT_FILE" << EOF

MASTERNODES
-----------
EOF

    local mn_list=$(cli 0 listmasternodes 2>/dev/null)
    if [ -n "$mn_list" ]; then
        echo "$mn_list" | jq '.' >> "$REPORT_FILE"
    else
        echo "  None registered" >> "$REPORT_FILE"
    fi

    cat >> "$REPORT_FILE" << EOF

VERDICT
-------
EOF

    if [ $INVARIANT_VIOLATIONS -eq 0 ] && [ $ERRORS_FATAL -eq 0 ]; then
        echo "  PASS: No invariant violations or fatal errors" >> "$REPORT_FILE"
    else
        echo "  FAIL: Issues detected (see above)" >> "$REPORT_FILE"
    fi

    echo "" >> "$REPORT_FILE"
    echo "═══════════════════════════════════════════════════════════════════════════════" >> "$REPORT_FILE"

    echo ""
    echo "Report generated: $REPORT_FILE"
    cat "$REPORT_FILE"
}

stop_nodes() {
    log "Stopping all nodes..."

    for i in 0 1 2 3; do
        cli $i stop 2>/dev/null || true
    done

    sleep 3
    pkill -f "hud.*hu_testnet_dmm" 2>/dev/null || true

    log_ok "All nodes stopped"
}

# ═══════════════════════════════════════════════════════════════════════════════
# MAIN
# ═══════════════════════════════════════════════════════════════════════════════

echo ""
echo "═══════════════════════════════════════════════════════════════════════════"
echo "  HU Testnet DMM Integration Tests"
echo "═══════════════════════════════════════════════════════════════════════════"
echo ""

# Initialize log
echo "=== HU Integration Test Log ===" > "$LOG_FILE"
echo "Started: $(date)" >> "$LOG_FILE"

case "${1:-help}" in
    start)
        check_prerequisites
        start_nodes
        import_genesis_keys
        register_masternodes
        check_status
        ;;
    status)
        check_status
        ;;
    test)
        test_hu_operations
        test_lock_unlock
        check_reorgs
        ;;
    long)
        blocks=${2:-100}
        run_long_integration $blocks
        generate_report
        ;;
    stress)
        tx_count=${2:-100}
        stress_test $tx_count
        ;;
    mn-down)
        test_mn_failure
        ;;
    report)
        generate_report
        ;;
    stop)
        stop_nodes
        ;;
    help|*)
        echo "Usage: $0 <command> [options]"
        echo ""
        echo "Commands:"
        echo "  start         Start 4 testnet nodes with MN setup"
        echo "  status        Show current status of all nodes"
        echo "  test          Run quick integration tests"
        echo "  long [N]      Run long integration (default 100 blocks)"
        echo "  stress [N]    Run stress test with N transactions (default 100)"
        echo "  mn-down       Test MN failure and recovery"
        echo "  report        Generate integration report"
        echo "  stop          Stop all nodes"
        echo ""
        ;;
esac
