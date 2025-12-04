#!/bin/bash
# =============================================================================
# HU Chain - Regtest DMM Test Suite
# =============================================================================
# Tests automatiques pour le testnet local regtest
#
# Usage: ./test_regtest_dmm.sh
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLI="$SCRIPT_DIR/hu-cli"
TESTNET_SCRIPT="$SCRIPT_DIR/testnet_local.sh"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Counters
TESTS_PASSED=0
TESTS_FAILED=0

# =============================================================================
# Helper functions
# =============================================================================

log_info() {
    echo -e "${YELLOW}[INFO]${NC} $1"
}

log_pass() {
    echo -e "${GREEN}[PASS]${NC} $1"
    ((TESTS_PASSED++))
}

log_fail() {
    echo -e "${RED}[FAIL]${NC} $1"
    ((TESTS_FAILED++))
}

cli_node() {
    local node=$1
    shift
    $CLI -regtest -datadir=/tmp/hu_testnet/node$node -rpcuser=test -rpcpassword=test -rpcport=$((19500+node)) "$@" 2>/dev/null
}

wait_for_sync() {
    local expected_height=$1
    local timeout=30
    local count=0

    while [ $count -lt $timeout ]; do
        local synced=true
        for node in 0 1 2 3; do
            local height=$(cli_node $node getblockcount 2>/dev/null || echo "0")
            if [ "$height" != "$expected_height" ]; then
                synced=false
                break
            fi
        done

        if $synced; then
            return 0
        fi

        sleep 1
        ((count++))
    done

    return 1
}

generate_block() {
    # Try each MN until one succeeds (finds the right producer)
    for mn in 1 2 3; do
        result=$(cli_node $mn generate 1 2>&1)
        if [[ "$result" != *"bad-mn-sig-empty"* ]] && [[ "$result" != *"error"* ]]; then
            echo "$mn"
            return 0
        fi
    done
    echo "0"
    return 1
}

# =============================================================================
# Test cases
# =============================================================================

test_nodes_running() {
    log_info "Test: Checking if all nodes are running..."

    local all_running=true
    for node in 0 1 2 3; do
        if ! cli_node $node getblockcount >/dev/null 2>&1; then
            all_running=false
            log_fail "Node $node is not running"
        fi
    done

    if $all_running; then
        log_pass "All 4 nodes are running"
    fi
}

test_masternodes_registered() {
    log_info "Test: Checking masternode registration..."

    local mn_count=$(cli_node 0 listmasternodes | grep -c "proTxHash" || echo "0")

    if [ "$mn_count" -eq 3 ]; then
        log_pass "3 masternodes registered"
    else
        log_fail "Expected 3 masternodes, found $mn_count"
    fi
}

test_masternodes_enabled() {
    log_info "Test: Checking masternode status..."

    local enabled_count=$(cli_node 0 listmasternodes | grep -c '"status": "ENABLED"' || echo "0")

    if [ "$enabled_count" -eq 3 ]; then
        log_pass "All 3 masternodes are ENABLED"
    else
        log_fail "Expected 3 ENABLED masternodes, found $enabled_count"
    fi
}

test_initial_sync() {
    log_info "Test: Checking initial synchronization..."

    local heights=""
    local all_same=true
    local first_height=""

    for node in 0 1 2 3; do
        local height=$(cli_node $node getblockcount)
        heights="$heights Node$node=$height"

        if [ -z "$first_height" ]; then
            first_height=$height
        elif [ "$height" != "$first_height" ]; then
            all_same=false
        fi
    done

    if $all_same && [ "$first_height" -ge 11 ]; then
        log_pass "All nodes synchronized at height $first_height"
    else
        log_fail "Nodes not synchronized:$heights"
    fi
}

test_dmm_block_production() {
    log_info "Test: DMM block production..."

    local start_height=$(cli_node 0 getblockcount)
    local blocks_to_generate=10
    local producers=""

    for i in $(seq 1 $blocks_to_generate); do
        local producer=$(generate_block)
        if [ "$producer" != "0" ]; then
            producers="$producers MN$producer"
        else
            log_fail "Failed to generate block $i"
            return
        fi
        sleep 0.3
    done

    local end_height=$(cli_node 0 getblockcount)
    local generated=$((end_height - start_height))

    if [ "$generated" -eq "$blocks_to_generate" ]; then
        log_pass "Generated $blocks_to_generate blocks: $producers"
    else
        log_fail "Expected $blocks_to_generate blocks, generated $generated"
    fi
}

test_dmm_rotation() {
    log_info "Test: DMM producer rotation..."

    local mn1_count=0
    local mn2_count=0
    local mn3_count=0

    # Generate 15 blocks and track producers
    for i in $(seq 1 15); do
        local producer=$(generate_block)
        case $producer in
            1) ((mn1_count++)) ;;
            2) ((mn2_count++)) ;;
            3) ((mn3_count++)) ;;
        esac
        sleep 0.2
    done

    log_info "  MN1: $mn1_count blocks, MN2: $mn2_count blocks, MN3: $mn3_count blocks"

    # All MNs should have produced at least 1 block
    if [ "$mn1_count" -gt 0 ] && [ "$mn2_count" -gt 0 ] && [ "$mn3_count" -gt 0 ]; then
        log_pass "All 3 MNs produced blocks (rotation working)"
    else
        log_fail "Not all MNs produced blocks - rotation may be broken"
    fi
}

test_block_propagation() {
    log_info "Test: Block propagation across network..."

    local start_height=$(cli_node 0 getblockcount)

    # Generate 5 blocks
    for i in $(seq 1 5); do
        generate_block >/dev/null
        sleep 0.3
    done

    local expected_height=$((start_height + 5))

    sleep 2  # Wait for propagation

    local all_synced=true
    for node in 0 1 2 3; do
        local height=$(cli_node $node getblockcount)
        if [ "$height" != "$expected_height" ]; then
            all_synced=false
            log_info "  Node $node at height $height (expected $expected_height)"
        fi
    done

    if $all_synced; then
        log_pass "All nodes synced to height $expected_height"
    else
        log_fail "Block propagation failed"
    fi
}

test_signature_verification() {
    log_info "Test: ECDSA signature verification..."

    # Check logs for successful verifications
    local verify_count=$(grep -c "VerifyBlockProducerSignature.*verified (ECDSA)" /tmp/hu_testnet/node0/regtest/debug.log 2>/dev/null || echo "0")

    if [ "$verify_count" -gt 10 ]; then
        log_pass "Found $verify_count successful ECDSA signature verifications"
    else
        log_fail "Only $verify_count signature verifications found"
    fi
}

test_no_signature_failures() {
    log_info "Test: No signature verification failures..."

    local fail_count=$(grep -c "Signature verification FAILED" /tmp/hu_testnet/node0/regtest/debug.log 2>/dev/null || echo "0")

    if [ "$fail_count" -eq 0 ]; then
        log_pass "No signature verification failures"
    else
        log_fail "Found $fail_count signature verification failures"
    fi
}

# =============================================================================
# Main
# =============================================================================

main() {
    echo "=============================================="
    echo " HU Chain - Regtest DMM Test Suite"
    echo "=============================================="
    echo ""

    # Check if testnet is running
    if ! pgrep -f "hud.*regtest" >/dev/null; then
        log_info "Starting testnet..."
        pkill -9 hud 2>/dev/null || true
        sleep 2
        rm -rf /tmp/hu_testnet /tmp/hu_keygen
        $TESTNET_SCRIPT start --mn >/dev/null 2>&1 &

        # Wait for startup
        log_info "Waiting for nodes to start (30s)..."
        sleep 30
    fi

    echo ""
    echo "Running tests..."
    echo ""

    # Run all tests
    test_nodes_running
    test_masternodes_registered
    test_masternodes_enabled
    test_initial_sync
    test_dmm_block_production
    test_dmm_rotation
    test_block_propagation
    test_signature_verification
    test_no_signature_failures

    echo ""
    echo "=============================================="
    echo " Results: ${GREEN}$TESTS_PASSED passed${NC}, ${RED}$TESTS_FAILED failed${NC}"
    echo "=============================================="

    if [ "$TESTS_FAILED" -eq 0 ]; then
        echo -e "${GREEN}All tests passed!${NC}"
        exit 0
    else
        echo -e "${RED}Some tests failed!${NC}"
        exit 1
    fi
}

main "$@"
