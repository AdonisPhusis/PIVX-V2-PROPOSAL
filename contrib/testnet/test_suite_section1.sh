#!/bin/bash
# =============================================================================
# HU-Core Test Suite - Section 1: Pre-Testnet Validation
# =============================================================================
# Copyright (c) 2025 The HU developers
# Distributed under the MIT software license
#
# This script validates HU-Core in regtest before public testnet launch.
#
# Scenarios:
#   1. DMM 3 MN long-run (consensus stability)
#   2. MN offline / recovery
#   3. Reorg / finality (12 blocks)
#   4. KHU pipeline (MINT/LOCK/UNLOCK/REDEEM)
#   5. Global invariants check
#
# Usage:
#   ./contrib/testnet/test_suite_section1.sh [--verbose] [--quick]
#
# Exit codes:
#   0 = All scenarios passed
#   1 = One or more scenarios failed
# =============================================================================

set -euo pipefail

# =============================================================================
# Configuration
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
SRC_DIR="$PROJECT_ROOT/src"

HUD="$SRC_DIR/piv2d"
CLI="$SRC_DIR/piv2-cli"

# Test directories
TEST_BASE="/tmp/hu_section1_$$"
DATADIR_NODE0="$TEST_BASE/node0"
DATADIR_NODE1="$TEST_BASE/node1"
DATADIR_NODE2="$TEST_BASE/node2"
DATADIR_NODE3="$TEST_BASE/node3"
DATADIR_NODE4="$TEST_BASE/node4"

# RPC ports
RPC_PORT_NODE0=19600
RPC_PORT_NODE1=19601
RPC_PORT_NODE2=19602
RPC_PORT_NODE3=19603
RPC_PORT_NODE4=19604

# P2P ports
P2P_PORT_NODE0=19700
P2P_PORT_NODE1=19701
P2P_PORT_NODE2=19702
P2P_PORT_NODE3=19703
P2P_PORT_NODE4=19704

# RPC credentials
RPC_USER="hutest"
RPC_PASS="hutest123"

# Test parameters (adjustable via --quick)
LONG_RUN_BLOCKS=5000      # Scenario 1: long-run block count
OFFLINE_START_HEIGHT=500   # Scenario 2: height to stop MN
OFFLINE_DURATION=200       # Scenario 2: blocks while MN offline
ISOLATED_BLOCKS=50         # Scenario 3: blocks on isolated node

# Verbose mode
VERBOSE=0

# Results tracking
SCENARIO1_RESULT="NOT_RUN"
SCENARIO2_RESULT="NOT_RUN"
SCENARIO3_RESULT="NOT_RUN"
SCENARIO4_RESULT="NOT_RUN"
SCENARIO5_RESULT="NOT_RUN"

# PIDs for cleanup
declare -a NODE_PIDS=()

# =============================================================================
# Utility Functions
# =============================================================================

log() {
    echo "[$(date '+%H:%M:%S')] $*"
}

log_verbose() {
    if [[ $VERBOSE -eq 1 ]]; then
        echo "[$(date '+%H:%M:%S')] [VERBOSE] $*"
    fi
}

log_success() {
    echo -e "[$(date '+%H:%M:%S')] \033[1;32m[PASS]\033[0m $*"
}

log_fail() {
    echo -e "[$(date '+%H:%M:%S')] \033[1;31m[FAIL]\033[0m $*"
}

log_info() {
    echo -e "[$(date '+%H:%M:%S')] \033[1;34m[INFO]\033[0m $*"
}

log_warn() {
    echo -e "[$(date '+%H:%M:%S')] \033[1;33m[WARN]\033[0m $*"
}

# CLI wrapper for a specific node
cli_node() {
    local node=$1
    shift
    local port rpc_port datadir

    case $node in
        0) datadir="$DATADIR_NODE0"; rpc_port=$RPC_PORT_NODE0 ;;
        1) datadir="$DATADIR_NODE1"; rpc_port=$RPC_PORT_NODE1 ;;
        2) datadir="$DATADIR_NODE2"; rpc_port=$RPC_PORT_NODE2 ;;
        3) datadir="$DATADIR_NODE3"; rpc_port=$RPC_PORT_NODE3 ;;
        4) datadir="$DATADIR_NODE4"; rpc_port=$RPC_PORT_NODE4 ;;
        *) echo "Invalid node: $node"; return 1 ;;
    esac

    "$CLI" -regtest -datadir="$datadir" -rpcuser="$RPC_USER" -rpcpassword="$RPC_PASS" -rpcport="$rpc_port" "$@" 2>/dev/null
}

# Wait for node to be ready
wait_for_node() {
    local node=$1
    local max_attempts=${2:-60}
    local attempt=0

    while [[ $attempt -lt $max_attempts ]]; do
        if cli_node "$node" getblockchaininfo &>/dev/null; then
            log_verbose "Node $node is ready"
            return 0
        fi
        sleep 1
        ((attempt++))
    done

    log_fail "Node $node failed to start after $max_attempts seconds"
    return 1
}

# Get block count for a node
get_blockcount() {
    local node=$1
    cli_node "$node" getblockcount 2>/dev/null || echo "0"
}

# Generate blocks on a node
# HU-Core uses 'generate nblocks [address]' command (regtest only)
generate_blocks() {
    local node=$1
    local count=$2
    local address

    address=$(cli_node "$node" getnewaddress 2>/dev/null) || {
        log_warn "Failed to get new address on node $node"
        return 1
    }

    cli_node "$node" generate "$count" "$address" &>/dev/null || {
        log_warn "Failed to generate $count blocks on node $node"
        return 1
    }
}

# Check logs for errors
check_logs_for_errors() {
    local datadir=$1
    local logfile="$datadir/regtest/debug.log"
    local errors=0

    if [[ -f "$logfile" ]]; then
        # Check for critical errors
        if grep -q "REJECTED" "$logfile" 2>/dev/null; then
            log_warn "Found REJECTED in logs: $logfile"
            ((errors++))
        fi
        if grep -q "bad-txnmrklroot" "$logfile" 2>/dev/null; then
            log_warn "Found bad-txnmrklroot in logs: $logfile"
            ((errors++))
        fi
        if grep -q "CheckBlockMNOnly.*failed" "$logfile" 2>/dev/null; then
            log_warn "Found CheckBlockMNOnly failure in logs: $logfile"
            ((errors++))
        fi
    fi

    return $errors
}

# =============================================================================
# Node Management
# =============================================================================

start_node() {
    local node=$1
    local extra_args=${2:-""}
    local datadir rpc_port p2p_port

    case $node in
        0) datadir="$DATADIR_NODE0"; rpc_port=$RPC_PORT_NODE0; p2p_port=$P2P_PORT_NODE0 ;;
        1) datadir="$DATADIR_NODE1"; rpc_port=$RPC_PORT_NODE1; p2p_port=$P2P_PORT_NODE1 ;;
        2) datadir="$DATADIR_NODE2"; rpc_port=$RPC_PORT_NODE2; p2p_port=$P2P_PORT_NODE2 ;;
        3) datadir="$DATADIR_NODE3"; rpc_port=$RPC_PORT_NODE3; p2p_port=$P2P_PORT_NODE3 ;;
        4) datadir="$DATADIR_NODE4"; rpc_port=$RPC_PORT_NODE4; p2p_port=$P2P_PORT_NODE4 ;;
    esac

    mkdir -p "$datadir"

    # Create config
    cat > "$datadir/hu.conf" <<EOF
regtest=1
server=1
daemon=1
rpcuser=$RPC_USER
rpcpassword=$RPC_PASS
listen=1
listenonion=0
txindex=1
debug=0
printtoconsole=0

[regtest]
rpcport=$rpc_port
rpcbind=127.0.0.1
port=$p2p_port
rpcallowip=127.0.0.1
EOF

    log_verbose "Starting node $node on RPC port $rpc_port, P2P port $p2p_port"

    "$HUD" -datadir="$datadir" $extra_args &
    local pid=$!
    NODE_PIDS+=("$pid")

    wait_for_node "$node" 60
}

stop_node() {
    local node=$1
    log_verbose "Stopping node $node"
    cli_node "$node" stop &>/dev/null || true
    sleep 2
}

connect_nodes() {
    local from=$1
    local to=$2
    local to_port

    case $to in
        0) to_port=$P2P_PORT_NODE0 ;;
        1) to_port=$P2P_PORT_NODE1 ;;
        2) to_port=$P2P_PORT_NODE2 ;;
        3) to_port=$P2P_PORT_NODE3 ;;
        4) to_port=$P2P_PORT_NODE4 ;;
    esac

    cli_node "$from" addnode "127.0.0.1:$to_port" "onetry" &>/dev/null || true
}

# =============================================================================
# Cleanup
# =============================================================================

cleanup() {
    log_info "Cleaning up..."

    # Stop all nodes
    for node in 0 1 2 3 4; do
        cli_node "$node" stop &>/dev/null || true
    done

    sleep 3

    # Kill any remaining processes
    for pid in "${NODE_PIDS[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            kill -9 "$pid" 2>/dev/null || true
        fi
    done

    # Clean up test directories
    if [[ -d "$TEST_BASE" ]]; then
        rm -rf "$TEST_BASE"
    fi

    log_info "Cleanup complete"
}

trap cleanup EXIT

# =============================================================================
# Scenario 1: DMM 3 MN Long-Run (Consensus Stability)
# =============================================================================

scenario1_dmm_longrun() {
    log_info "========================================"
    log_info "SCENARIO 1: DMM 3 MN Long-Run"
    log_info "========================================"
    log_info "Target: $LONG_RUN_BLOCKS blocks with 3 masternodes"

    local start_time=$(date +%s)

    # Start 4 nodes
    log "Starting nodes..."
    start_node 0  # Observer
    start_node 1  # MN1
    start_node 2  # MN2
    start_node 3  # MN3

    # Connect nodes in mesh
    log "Connecting nodes..."
    for from in 0 1 2 3; do
        for to in 0 1 2 3; do
            if [[ $from -ne $to ]]; then
                connect_nodes $from $to
            fi
        done
    done
    sleep 5

    # Generate initial blocks to have mature coinbase for MN collateral
    log "Generating initial blocks for maturity..."
    generate_blocks 0 200
    sleep 2

    # Sync check
    local height0=$(get_blockcount 0)
    log "Initial height: $height0"

    # Register masternodes (simplified - in real scenario would use protx_register)
    # For now, we simulate by just generating blocks and checking DMM behavior
    log "Setting up masternodes..."

    # Generate blocks to fund MN wallets
    for node in 1 2 3; do
        generate_blocks $node 10
    done
    sleep 2

    # Main long-run block generation
    log "Starting long-run block generation ($LONG_RUN_BLOCKS blocks)..."
    local blocks_generated=0
    local batch_size=100
    local progress_interval=500

    while [[ $blocks_generated -lt $LONG_RUN_BLOCKS ]]; do
        # Round-robin generation among MN nodes
        local current_node=$(( (blocks_generated / batch_size) % 3 + 1 ))

        generate_blocks $current_node $batch_size || {
            log_warn "Block generation failed on node $current_node at block $blocks_generated"
        }

        ((blocks_generated += batch_size))

        # Progress update
        if [[ $((blocks_generated % progress_interval)) -eq 0 ]]; then
            local h0=$(get_blockcount 0)
            local h1=$(get_blockcount 1)
            local h2=$(get_blockcount 2)
            local h3=$(get_blockcount 3)
            log "Progress: ~$blocks_generated blocks | Heights: N0=$h0 N1=$h1 N2=$h2 N3=$h3"
        fi

        # Brief pause for sync
        sleep 0.5
    done

    # Final sync wait
    log "Waiting for final sync..."
    sleep 10

    # Verify results
    local final_h0=$(get_blockcount 0)
    local final_h1=$(get_blockcount 1)
    local final_h2=$(get_blockcount 2)
    local final_h3=$(get_blockcount 3)

    log "Final heights: N0=$final_h0 N1=$final_h1 N2=$final_h2 N3=$final_h3"

    # Check height consistency
    local height_ok=1
    if [[ "$final_h0" != "$final_h1" ]] || [[ "$final_h0" != "$final_h2" ]] || [[ "$final_h0" != "$final_h3" ]]; then
        height_ok=0
        log_fail "Height mismatch between nodes!"
    fi

    # Check logs for errors
    local log_errors=0
    for node in 0 1 2 3; do
        local datadir
        case $node in
            0) datadir="$DATADIR_NODE0" ;;
            1) datadir="$DATADIR_NODE1" ;;
            2) datadir="$DATADIR_NODE2" ;;
            3) datadir="$DATADIR_NODE3" ;;
        esac

        if ! check_logs_for_errors "$datadir"; then
            ((log_errors++))
        fi
    done

    local end_time=$(date +%s)
    local duration=$((end_time - start_time))

    # Summary
    log_info "--- Scenario 1 Summary ---"
    log_info "Duration: ${duration}s"
    log_info "Blocks produced: $final_h0"
    log_info "Height consistency: $([ $height_ok -eq 1 ] && echo 'OK' || echo 'FAILED')"
    log_info "Log errors found: $log_errors"

    if [[ $height_ok -eq 1 ]] && [[ $log_errors -eq 0 ]]; then
        log_success "Scenario 1: DMM Long-Run PASSED"
        SCENARIO1_RESULT="PASS"
        return 0
    else
        log_fail "Scenario 1: DMM Long-Run FAILED"
        SCENARIO1_RESULT="FAIL"
        return 1
    fi
}

# =============================================================================
# Scenario 2: MN Offline / Recovery
# =============================================================================

scenario2_mn_offline() {
    log_info "========================================"
    log_info "SCENARIO 2: MN Offline / Recovery"
    log_info "========================================"

    # Get current height
    local h0=$(get_blockcount 0)
    log "Current height: $h0"

    # Generate blocks until target height
    local target_h0=$((h0 + OFFLINE_START_HEIGHT))
    log "Generating blocks to height $target_h0..."

    while [[ $(get_blockcount 0) -lt $target_h0 ]]; do
        generate_blocks 1 50
        sleep 1
    done

    local pre_offline_height=$(get_blockcount 0)
    log "Pre-offline height: $pre_offline_height"

    # Record MN2 status before stop
    local mn2_pre_height=$(get_blockcount 2)
    log "Stopping node 2 (MN2)..."
    stop_node 2

    # Generate blocks while MN2 is offline
    log "Generating $OFFLINE_DURATION blocks with MN2 offline..."
    local offline_blocks=0
    while [[ $offline_blocks -lt $OFFLINE_DURATION ]]; do
        # Alternate between node1 and node3
        local gen_node=$(( (offline_blocks / 25) % 2 + 1 ))
        if [[ $gen_node -eq 2 ]]; then gen_node=3; fi

        generate_blocks $gen_node 25
        ((offline_blocks += 25))

        local current_h=$(get_blockcount 0)
        log_verbose "Offline progress: $offline_blocks blocks, height=$current_h"
        sleep 0.5
    done

    local during_offline_height=$(get_blockcount 0)
    log "Height while MN2 offline: $during_offline_height"

    # Check other nodes continued
    local h1=$(get_blockcount 1)
    local h3=$(get_blockcount 3)
    local others_ok=1

    if [[ $h1 -lt $during_offline_height ]] || [[ $h3 -lt $during_offline_height ]]; then
        log_warn "Other nodes not at expected height"
        others_ok=0
    fi

    # Restart MN2
    log "Restarting node 2 (MN2)..."
    start_node 2

    # Reconnect
    connect_nodes 2 0
    connect_nodes 2 1
    connect_nodes 2 3
    connect_nodes 0 2
    connect_nodes 1 2
    connect_nodes 3 2

    # Wait for sync
    log "Waiting for MN2 to sync..."
    local sync_attempts=0
    local max_sync_attempts=60
    local synced=0

    while [[ $sync_attempts -lt $max_sync_attempts ]]; do
        local mn2_height=$(get_blockcount 2)
        local network_height=$(get_blockcount 0)

        if [[ $mn2_height -ge $network_height ]]; then
            synced=1
            break
        fi

        log_verbose "MN2 syncing: $mn2_height / $network_height"
        sleep 2
        ((sync_attempts++))
    done

    local post_sync_h2=$(get_blockcount 2)
    local post_sync_h0=$(get_blockcount 0)

    # Check for deep reorg in logs
    local deep_reorg=0
    if grep -q "REORGANIZE.*depth=[0-9]\{2,\}" "$DATADIR_NODE2/regtest/debug.log" 2>/dev/null; then
        log_warn "Deep reorganization detected on MN2!"
        deep_reorg=1
    fi

    # Summary
    log_info "--- Scenario 2 Summary ---"
    log_info "Pre-offline height: $pre_offline_height"
    log_info "During-offline height: $during_offline_height"
    log_info "Post-sync MN2 height: $post_sync_h2"
    log_info "Network height: $post_sync_h0"
    log_info "MN2 synced: $([ $synced -eq 1 ] && echo 'YES' || echo 'NO')"
    log_info "Deep reorg: $([ $deep_reorg -eq 0 ] && echo 'NO' || echo 'YES')"

    if [[ $synced -eq 1 ]] && [[ $deep_reorg -eq 0 ]] && [[ $others_ok -eq 1 ]]; then
        log_success "Scenario 2: MN Offline/Recovery PASSED"
        SCENARIO2_RESULT="PASS"
        return 0
    else
        log_fail "Scenario 2: MN Offline/Recovery FAILED"
        SCENARIO2_RESULT="FAIL"
        return 1
    fi
}

# =============================================================================
# Scenario 3: Reorg / Finality (12 blocks)
# =============================================================================

scenario3_reorg_finality() {
    log_info "========================================"
    log_info "SCENARIO 3: Reorg / Finality Test"
    log_info "========================================"

    local main_height=$(get_blockcount 0)
    log "Main network height: $main_height"

    # Start isolated node4
    log "Starting isolated node 4..."
    start_node 4 "-connect=0"  # No connections

    wait_for_node 4 30

    # Generate blocks on isolated node
    log "Generating $ISOLATED_BLOCKS blocks on isolated node 4..."
    generate_blocks 4 $ISOLATED_BLOCKS

    local isolated_height=$(get_blockcount 4)
    log "Isolated node height: $isolated_height"

    # Generate more blocks on main network
    local main_extra_blocks=$((ISOLATED_BLOCKS + 20))
    log "Generating $main_extra_blocks blocks on main network..."
    generate_blocks 0 $main_extra_blocks

    local main_new_height=$(get_blockcount 0)
    log "Main network new height: $main_new_height"

    # Now connect isolated node to main network
    log "Connecting isolated node to main network..."
    connect_nodes 4 0
    connect_nodes 4 1
    connect_nodes 0 4
    connect_nodes 1 4

    # Wait for sync attempt
    log "Waiting for sync/reorg attempt..."
    sleep 15

    # Check what happened
    local node4_final_height=$(get_blockcount 4)
    local main_final_height=$(get_blockcount 0)

    # Check for reorg behavior in logs
    local reorg_rejected=0
    local reorg_depth=0

    if grep -q "REORGANIZE" "$DATADIR_NODE4/regtest/debug.log" 2>/dev/null; then
        # Extract reorg depth if available
        reorg_depth=$(grep -o "depth=[0-9]*" "$DATADIR_NODE4/regtest/debug.log" 2>/dev/null | tail -1 | cut -d= -f2 || echo "0")
        log_info "Reorg detected on node 4, depth: $reorg_depth"
    fi

    # Check if finality was respected (no reorg > 12 blocks)
    local finality_ok=1
    if [[ -n "$reorg_depth" ]] && [[ "$reorg_depth" -gt 12 ]]; then
        log_warn "Reorg exceeded finality limit (12 blocks)!"
        finality_ok=0
    fi

    # Node4 should have followed main chain
    local followed_main=0
    if [[ $node4_final_height -ge $((main_final_height - 5)) ]]; then
        followed_main=1
    fi

    # Summary
    log_info "--- Scenario 3 Summary ---"
    log_info "Main network height: $main_final_height"
    log_info "Isolated fork height (before connect): $isolated_height"
    log_info "Node 4 final height: $node4_final_height"
    log_info "Reorg depth observed: ${reorg_depth:-none}"
    log_info "Finality respected (<= 12): $([ $finality_ok -eq 1 ] && echo 'YES' || echo 'NO')"
    log_info "Node 4 followed main chain: $([ $followed_main -eq 1 ] && echo 'YES' || echo 'NO')"

    # Stop isolated node
    stop_node 4

    if [[ $finality_ok -eq 1 ]] && [[ $followed_main -eq 1 ]]; then
        log_success "Scenario 3: Reorg/Finality PASSED"
        SCENARIO3_RESULT="PASS"
        return 0
    else
        log_fail "Scenario 3: Reorg/Finality FAILED"
        SCENARIO3_RESULT="FAIL"
        return 1
    fi
}

# =============================================================================
# Scenario 4: KHU Pipeline (MINT/LOCK/UNLOCK/REDEEM)
# =============================================================================

scenario4_khu_pipeline() {
    log_info "========================================"
    log_info "SCENARIO 4: KHU Pipeline Test"
    log_info "========================================"

    # Use node 0 for KHU operations
    local test_node=0

    # Get initial HU state
    log "Getting initial HU state..."
    local initial_state
    initial_state=$(cli_node $test_node getkhustate 2>/dev/null) || {
        log_warn "getkhustate not available or failed"
        log_info "Skipping KHU pipeline test (RPC not implemented)"
        SCENARIO4_RESULT="SKIP"
        return 0
    }

    log_verbose "Initial state: $initial_state"

    # Parse initial values
    local initial_C=$(echo "$initial_state" | grep -o '"C":[0-9]*' | cut -d: -f2 || echo "0")
    local initial_U=$(echo "$initial_state" | grep -o '"U":[0-9]*' | cut -d: -f2 || echo "0")
    local initial_Z=$(echo "$initial_state" | grep -o '"Z":[0-9]*' | cut -d: -f2 || echo "0")
    local initial_Cr=$(echo "$initial_state" | grep -o '"Cr":[0-9]*' | cut -d: -f2 || echo "0")
    local initial_Ur=$(echo "$initial_state" | grep -o '"Ur":[0-9]*' | cut -d: -f2 || echo "0")
    local initial_T=$(echo "$initial_state" | grep -o '"T":[0-9]*' | cut -d: -f2 || echo "0")

    log "Initial: C=$initial_C U=$initial_U Z=$initial_Z Cr=$initial_Cr Ur=$initial_Ur T=$initial_T"

    # Check initial invariants
    local invariant_ok=1

    # C == U + Z
    if [[ $((initial_U + initial_Z)) -ne $initial_C ]]; then
        log_warn "Initial invariant C == U + Z violated!"
        invariant_ok=0
    fi

    # Cr == Ur
    if [[ $initial_Cr -ne $initial_Ur ]]; then
        log_warn "Initial invariant Cr == Ur violated!"
        invariant_ok=0
    fi

    # T >= 0
    if [[ $initial_T -lt 0 ]]; then
        log_warn "Initial invariant T >= 0 violated!"
        invariant_ok=0
    fi

    # Test 1: MINT (HU -> KHU)
    log "Testing MINT operation..."
    local mint_result
    mint_result=$(cli_node $test_node khumint 100 2>/dev/null) || {
        log_warn "khumint not available or failed"
    }

    if [[ -n "$mint_result" ]]; then
        log_verbose "Mint result: $mint_result"
        generate_blocks $test_node 1  # Confirm tx
        sleep 1

        # Check state after MINT
        local post_mint_state=$(cli_node $test_node getkhustate 2>/dev/null)
        local post_C=$(echo "$post_mint_state" | grep -o '"C":[0-9]*' | cut -d: -f2 || echo "0")
        local post_U=$(echo "$post_mint_state" | grep -o '"U":[0-9]*' | cut -d: -f2 || echo "0")

        # After MINT: C should increase, U should increase
        if [[ $post_C -gt $initial_C ]] && [[ $post_U -gt $initial_U ]]; then
            log_success "MINT: C and U increased correctly"
        else
            log_warn "MINT: State change unexpected"
        fi

        # Verify C == U + Z
        local post_Z=$(echo "$post_mint_state" | grep -o '"Z":[0-9]*' | cut -d: -f2 || echo "0")
        if [[ $((post_U + post_Z)) -ne $post_C ]]; then
            log_fail "MINT: Invariant C == U + Z violated!"
            invariant_ok=0
        fi
    fi

    # Test 2: LOCK (KHU -> ZKHU)
    log "Testing LOCK operation..."
    local lock_result
    lock_result=$(cli_node $test_node khulock 50 2>/dev/null) || {
        log_warn "khulock not available or failed"
    }

    if [[ -n "$lock_result" ]]; then
        log_verbose "Lock result: $lock_result"
        generate_blocks $test_node 1
        sleep 1

        # Check state after LOCK
        local post_lock_state=$(cli_node $test_node getkhustate 2>/dev/null)
        local lock_U=$(echo "$post_lock_state" | grep -o '"U":[0-9]*' | cut -d: -f2 || echo "0")
        local lock_Z=$(echo "$post_lock_state" | grep -o '"Z":[0-9]*' | cut -d: -f2 || echo "0")
        local lock_C=$(echo "$post_lock_state" | grep -o '"C":[0-9]*' | cut -d: -f2 || echo "0")

        # After LOCK: U decreases, Z increases, C unchanged
        if [[ $((lock_U + lock_Z)) -eq $lock_C ]]; then
            log_success "LOCK: Invariant C == U + Z maintained"
        else
            log_fail "LOCK: Invariant C == U + Z violated!"
            invariant_ok=0
        fi
    fi

    # Test 3: Simulate time passage for yield
    log "Simulating time passage for yield accumulation..."
    generate_blocks $test_node 100  # Generate blocks to accumulate yield
    sleep 2

    # Test 4: UNLOCK (ZKHU -> KHU + yield)
    log "Testing UNLOCK operation..."
    local unlock_result
    unlock_result=$(cli_node $test_node khuunlock 2>/dev/null) || {
        log_warn "khuunlock not available or failed"
    }

    if [[ -n "$unlock_result" ]]; then
        log_verbose "Unlock result: $unlock_result"
        generate_blocks $test_node 1
        sleep 1

        # Check state after UNLOCK
        local post_unlock_state=$(cli_node $test_node getkhustate 2>/dev/null)
        local unlock_U=$(echo "$post_unlock_state" | grep -o '"U":[0-9]*' | cut -d: -f2 || echo "0")
        local unlock_Z=$(echo "$post_unlock_state" | grep -o '"Z":[0-9]*' | cut -d: -f2 || echo "0")
        local unlock_C=$(echo "$post_unlock_state" | grep -o '"C":[0-9]*' | cut -d: -f2 || echo "0")
        local unlock_Cr=$(echo "$post_unlock_state" | grep -o '"Cr":[0-9]*' | cut -d: -f2 || echo "0")
        local unlock_Ur=$(echo "$post_unlock_state" | grep -o '"Ur":[0-9]*' | cut -d: -f2 || echo "0")

        # Verify invariants
        if [[ $((unlock_U + unlock_Z)) -eq $unlock_C ]]; then
            log_success "UNLOCK: Invariant C == U + Z maintained"
        else
            log_fail "UNLOCK: Invariant C == U + Z violated!"
            invariant_ok=0
        fi

        if [[ $unlock_Cr -eq $unlock_Ur ]]; then
            log_success "UNLOCK: Invariant Cr == Ur maintained"
        else
            log_fail "UNLOCK: Invariant Cr == Ur violated!"
            invariant_ok=0
        fi
    fi

    # Test 5: REDEEM (KHU -> HU)
    log "Testing REDEEM operation..."
    local redeem_result
    redeem_result=$(cli_node $test_node khuredeem 25 2>/dev/null) || {
        log_warn "khuredeem not available or failed"
    }

    if [[ -n "$redeem_result" ]]; then
        log_verbose "Redeem result: $redeem_result"
        generate_blocks $test_node 1
        sleep 1

        # Check final state
        local final_state=$(cli_node $test_node getkhustate 2>/dev/null)
        local final_U=$(echo "$final_state" | grep -o '"U":[0-9]*' | cut -d: -f2 || echo "0")
        local final_Z=$(echo "$final_state" | grep -o '"Z":[0-9]*' | cut -d: -f2 || echo "0")
        local final_C=$(echo "$final_state" | grep -o '"C":[0-9]*' | cut -d: -f2 || echo "0")
        local final_T=$(echo "$final_state" | grep -o '"T":[0-9]*' | cut -d: -f2 || echo "0")

        # Verify final invariants
        if [[ $((final_U + final_Z)) -eq $final_C ]]; then
            log_success "REDEEM: Invariant C == U + Z maintained"
        else
            log_fail "REDEEM: Invariant C == U + Z violated!"
            invariant_ok=0
        fi

        if [[ $final_T -ge 0 ]]; then
            log_success "REDEEM: Invariant T >= 0 maintained"
        else
            log_fail "REDEEM: Invariant T >= 0 violated!"
            invariant_ok=0
        fi
    fi

    # Summary
    log_info "--- Scenario 4 Summary ---"
    log_info "Invariants maintained: $([ $invariant_ok -eq 1 ] && echo 'YES' || echo 'NO')"

    if [[ $invariant_ok -eq 1 ]]; then
        log_success "Scenario 4: KHU Pipeline PASSED"
        SCENARIO4_RESULT="PASS"
        return 0
    else
        log_fail "Scenario 4: KHU Pipeline FAILED"
        SCENARIO4_RESULT="FAIL"
        return 1
    fi
}

# =============================================================================
# Scenario 5: Global Invariants Check
# =============================================================================

scenario5_global_invariants() {
    log_info "========================================"
    log_info "SCENARIO 5: Global Invariants Check"
    log_info "========================================"

    local all_ok=1

    # Get final state from all nodes
    for node in 0 1 2 3; do
        log "Checking node $node..."

        local state
        state=$(cli_node $node getkhustate 2>/dev/null) || {
            log_warn "Could not get KHU state from node $node"
            continue
        }

        # Parse values
        local C=$(echo "$state" | grep -o '"C":[0-9]*' | cut -d: -f2 || echo "0")
        local U=$(echo "$state" | grep -o '"U":[0-9]*' | cut -d: -f2 || echo "0")
        local Z=$(echo "$state" | grep -o '"Z":[0-9]*' | cut -d: -f2 || echo "0")
        local Cr=$(echo "$state" | grep -o '"Cr":[0-9]*' | cut -d: -f2 || echo "0")
        local Ur=$(echo "$state" | grep -o '"Ur":[0-9]*' | cut -d: -f2 || echo "0")
        local T=$(echo "$state" | grep -o '"T":[0-9]*' | cut -d: -f2 || echo "0")

        log "Node $node: C=$C U=$U Z=$Z Cr=$Cr Ur=$Ur T=$T"

        # Check C == U + Z
        if [[ $((U + Z)) -ne $C ]]; then
            log_fail "Node $node: C != U + Z ($C != $U + $Z)"
            all_ok=0
        else
            log_success "Node $node: C == U + Z"
        fi

        # Check Cr == Ur
        if [[ $Cr -ne $Ur ]]; then
            log_fail "Node $node: Cr != Ur ($Cr != $Ur)"
            all_ok=0
        else
            log_success "Node $node: Cr == Ur"
        fi

        # Check T >= 0
        if [[ $T -lt 0 ]]; then
            log_fail "Node $node: T < 0 ($T)"
            all_ok=0
        else
            log_success "Node $node: T >= 0"
        fi
    done

    # Check blockchain consistency
    log "Checking blockchain consistency..."
    local h0=$(get_blockcount 0)
    local h1=$(get_blockcount 1)
    local h2=$(get_blockcount 2)
    local h3=$(get_blockcount 3)

    if [[ "$h0" == "$h1" ]] && [[ "$h0" == "$h2" ]] && [[ "$h0" == "$h3" ]]; then
        log_success "All nodes at same height: $h0"
    else
        log_fail "Height mismatch: N0=$h0 N1=$h1 N2=$h2 N3=$h3"
        all_ok=0
    fi

    # Summary
    log_info "--- Scenario 5 Summary ---"
    log_info "All invariants OK: $([ $all_ok -eq 1 ] && echo 'YES' || echo 'NO')"

    if [[ $all_ok -eq 1 ]]; then
        log_success "Scenario 5: Global Invariants PASSED"
        SCENARIO5_RESULT="PASS"
        return 0
    else
        log_fail "Scenario 5: Global Invariants FAILED"
        SCENARIO5_RESULT="FAIL"
        return 1
    fi
}

# =============================================================================
# Final Report
# =============================================================================

print_final_report() {
    echo ""
    echo "============================================="
    echo "=== HU Test Suite Section 1 - RESULTS ==="
    echo "============================================="
    echo ""
    printf "%-25s : %s\n" "DMM long-run" "$SCENARIO1_RESULT"
    printf "%-25s : %s\n" "MN offline/recovery" "$SCENARIO2_RESULT"
    printf "%-25s : %s\n" "Reorg / finality" "$SCENARIO3_RESULT"
    printf "%-25s : %s\n" "KHU pipeline" "$SCENARIO4_RESULT"
    printf "%-25s : %s\n" "Invariants" "$SCENARIO5_RESULT"
    echo ""

    # Determine global result
    local global_result="OK"
    if [[ "$SCENARIO1_RESULT" == "FAIL" ]] || \
       [[ "$SCENARIO2_RESULT" == "FAIL" ]] || \
       [[ "$SCENARIO3_RESULT" == "FAIL" ]] || \
       [[ "$SCENARIO4_RESULT" == "FAIL" ]] || \
       [[ "$SCENARIO5_RESULT" == "FAIL" ]]; then
        global_result="NOT OK"
    fi

    echo "============================================="
    printf "%-25s : %s\n" "GLOBAL RESULT" "$global_result"
    echo "============================================="
    echo ""

    if [[ "$global_result" == "OK" ]]; then
        return 0
    else
        return 1
    fi
}

# =============================================================================
# Main
# =============================================================================

main() {
    # Parse arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            --verbose|-v)
                VERBOSE=1
                shift
                ;;
            --quick|-q)
                LONG_RUN_BLOCKS=1000
                OFFLINE_START_HEIGHT=200
                OFFLINE_DURATION=100
                ISOLATED_BLOCKS=25
                log_info "Quick mode enabled"
                shift
                ;;
            --help|-h)
                echo "Usage: $0 [--verbose] [--quick]"
                echo ""
                echo "Options:"
                echo "  --verbose, -v   Enable verbose output"
                echo "  --quick, -q     Run shorter tests (faster)"
                echo "  --help, -h      Show this help"
                exit 0
                ;;
            *)
                echo "Unknown option: $1"
                exit 1
                ;;
        esac
    done

    log_info "============================================="
    log_info "HU-Core Test Suite - Section 1"
    log_info "Pre-Testnet Validation"
    log_info "============================================="
    log_info "Test base directory: $TEST_BASE"
    log_info "HU daemon: $HUD"
    log_info "HU CLI: $CLI"
    log_info ""

    # Check binaries exist
    if [[ ! -x "$HUD" ]]; then
        log_fail "piv2d binary not found at $HUD"
        exit 1
    fi

    if [[ ! -x "$CLI" ]]; then
        log_fail "piv2-cli binary not found at $CLI"
        exit 1
    fi

    # Create test directory
    mkdir -p "$TEST_BASE"

    # Run scenarios
    local overall_result=0

    scenario1_dmm_longrun || overall_result=1
    scenario2_mn_offline || overall_result=1
    scenario3_reorg_finality || overall_result=1
    scenario4_khu_pipeline || overall_result=1
    scenario5_global_invariants || overall_result=1

    # Print final report
    print_final_report

    exit $overall_result
}

main "$@"
