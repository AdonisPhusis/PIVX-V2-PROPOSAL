#!/bin/bash
# ============================================================================
# PIV2 Testnet Bootstrap Script
# ============================================================================
# This script initializes the PIV2 testnet by:
# 1. Generating Block 1 (Premine) with MN collateral + Dev + Faucet
# 2. Registering 3 initial masternodes with ProRegTx
# 3. Generating Block 2 to include the ProRegTx
# 4. DMM becomes active at Block 3
# ============================================================================

set -e

# Configuration
PIV2_CLI="${PIV2_CLI:-./src/piv2-cli}"
PIV2D="${PIV2D:-./src/piv2d}"
DATADIR="${DATADIR:-$HOME/.piv2}"
NETWORK="testnet"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_success() { echo -e "${GREEN}[OK]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# RPC wrapper
rpc() {
    $PIV2_CLI -$NETWORK "$@"
}

# Check if daemon is running
check_daemon() {
    if ! rpc getblockchaininfo &>/dev/null; then
        log_error "piv2d is not running. Start it first with: $PIV2D -$NETWORK -daemon"
        exit 1
    fi
    log_success "piv2d is running"
}

# Check current height
check_height() {
    local height=$(rpc getblockcount)
    echo $height
}

# ============================================================================
# STEP 1: Generate Block 1 (Premine)
# ============================================================================
generate_premine() {
    local height=$(check_height)

    if [ "$height" -ge 1 ]; then
        log_warn "Block 1 already exists (height=$height). Skipping premine generation."
        return 0
    fi

    log_info "Generating Block 1 (Premine)..."
    log_info "  - MN1 Collateral: 10,000 PIV2"
    log_info "  - MN2 Collateral: 10,000 PIV2"
    log_info "  - MN3 Collateral: 10,000 PIV2"
    log_info "  - Dev Wallet: 50,000,000 PIV2"
    log_info "  - Faucet: 50,000,000 PIV2"

    local result=$(rpc generatebootstrap 1)
    local block1_hash=$(echo "$result" | jq -r '.[0]')

    log_success "Block 1 generated: $block1_hash"

    # Show the premine outputs
    log_info "Premine outputs:"
    rpc getblock "$block1_hash" 2 | jq '.tx[0].vout[] | {n: .n, value: .value, address: .scriptPubKey.addresses[0]?}'
}

# ============================================================================
# STEP 2: Register Masternodes
# ============================================================================
register_masternodes() {
    local height=$(check_height)

    if [ "$height" -lt 1 ]; then
        log_error "Block 1 not yet generated. Run generate_premine first."
        exit 1
    fi

    if [ "$height" -ge 2 ]; then
        log_warn "Block 2 already exists. MNs should already be registered."
        return 0
    fi

    log_info "Registering 3 bootstrap masternodes..."

    # Get Block 1 coinbase txid (premine)
    local block1_hash=$(rpc getblockhash 1)
    local coinbase_txid=$(rpc getblock "$block1_hash" | jq -r '.tx[0]')

    log_info "Premine coinbase txid: $coinbase_txid"

    # MN1: vout[0] = 10,000 PIV2
    # MN2: vout[1] = 10,000 PIV2
    # MN3: vout[2] = 10,000 PIV2

    # Generate keys for each MN
    log_info "Generating MN keys..."

    for i in 1 2 3; do
        local vout_index=$((i - 1))

        # Generate owner, voting, and operator keys
        local owner_addr=$(rpc getnewaddress "mn${i}_owner")
        local voting_addr=$(rpc getnewaddress "mn${i}_voting")
        local payout_addr=$(rpc getnewaddress "mn${i}_payout")

        # Get operator key (we'll use the owner key for simplicity in testnet)
        local operator_key=$(rpc getnewaddress "mn${i}_operator")
        local operator_pubkey=$(rpc validateaddress "$operator_key" | jq -r '.pubkey')

        log_info "MN$i: Registering with collateral vout[$vout_index]..."
        log_info "  Owner: $owner_addr"
        log_info "  Voting: $voting_addr"
        log_info "  Payout: $payout_addr"
        log_info "  Operator pubkey: $operator_pubkey"

        # For testnet bootstrap, we need to import the premine keys first
        # The premine addresses are hardcoded in blockassembler.cpp
        # This script assumes you have the private keys for those addresses

        # Register the masternode using protx register_fund
        # Since collateral is already in block 1, we use protx register with external collateral

        # TODO: The actual registration requires the private key of the premine address
        # For now, we'll document the manual process

        log_warn "MN$i registration requires manual setup with premine private keys"
    done

    log_info ""
    log_info "=========================================="
    log_info "MANUAL REGISTRATION REQUIRED"
    log_info "=========================================="
    log_info "The premine addresses in Block 1 have hardcoded scripts."
    log_info "To register MNs, you need to:"
    log_info ""
    log_info "1. Import the private keys for premine addresses into your wallet"
    log_info "2. Use protx register to create ProRegTx for each MN"
    log_info "3. The ProRegTx will reference the collateral from Block 1"
    log_info ""
    log_info "Example for MN1 (after importing key):"
    log_info "  piv2-cli -testnet protx register \\"
    log_info "    $coinbase_txid 0 \\"
    log_info "    <ip>:<port> <owner_addr> <operator_pubkey> <voting_addr> \\"
    log_info "    0 <payout_addr>"
    log_info "=========================================="
}

# ============================================================================
# STEP 3: Generate Block 2 (Include ProRegTx)
# ============================================================================
generate_block2() {
    local height=$(check_height)

    if [ "$height" -lt 1 ]; then
        log_error "Block 1 not yet generated. Run generate_premine first."
        exit 1
    fi

    if [ "$height" -ge 2 ]; then
        log_warn "Block 2 already exists (height=$height)."
        return 0
    fi

    # Check if there are ProRegTx in mempool
    local mempool_count=$(rpc getmempoolinfo | jq '.size')
    log_info "Mempool has $mempool_count transactions"

    if [ "$mempool_count" -eq 0 ]; then
        log_warn "No ProRegTx in mempool. MNs will not be registered in Block 2."
        log_warn "You can still generate Block 2, but DMM won't have any MNs."
        read -p "Generate empty Block 2 anyway? (y/n) " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            log_info "Aborted. Add ProRegTx to mempool first."
            exit 0
        fi
    fi

    log_info "Generating Block 2..."
    local result=$(rpc generatebootstrap 1)
    local block2_hash=$(echo "$result" | jq -r '.[0]')

    log_success "Block 2 generated: $block2_hash"

    # Show MN list
    log_info "Masternode list after Block 2:"
    rpc protx list valid || log_warn "No MNs registered yet"
}

# ============================================================================
# STEP 4: Status Check
# ============================================================================
show_status() {
    log_info "=========================================="
    log_info "PIV2 Testnet Bootstrap Status"
    log_info "=========================================="

    local height=$(check_height)
    local mn_count=$(rpc protx list valid 2>/dev/null | jq 'length' 2>/dev/null || echo "0")

    log_info "Current height: $height"
    log_info "Registered MNs: $mn_count"

    if [ "$height" -lt 1 ]; then
        log_warn "Status: Genesis only. Run 'bootstrap_testnet.sh premine' to generate Block 1"
    elif [ "$height" -eq 1 ]; then
        log_warn "Status: Premine done. Register MNs, then run 'bootstrap_testnet.sh block2'"
    elif [ "$height" -eq 2 ]; then
        if [ "$mn_count" -gt 0 ]; then
            log_success "Status: Bootstrap complete! DMM active at Block 3."
        else
            log_warn "Status: Block 2 exists but no MNs. DMM cannot produce blocks."
        fi
    else
        log_success "Status: Chain running. Height=$height, MNs=$mn_count"
    fi

    log_info "=========================================="
}

# ============================================================================
# Main
# ============================================================================
usage() {
    echo "Usage: $0 <command>"
    echo ""
    echo "Commands:"
    echo "  premine   - Generate Block 1 (premine with MN collateral)"
    echo "  register  - Register 3 bootstrap masternodes (requires premine keys)"
    echo "  block2    - Generate Block 2 (includes ProRegTx from mempool)"
    echo "  status    - Show bootstrap status"
    echo "  all       - Run full bootstrap (premine -> register -> block2)"
    echo ""
    echo "Environment variables:"
    echo "  PIV2_CLI  - Path to piv2-cli (default: ./src/piv2-cli)"
    echo "  PIV2D     - Path to piv2d (default: ./src/piv2d)"
    echo "  DATADIR   - Data directory (default: ~/.piv2)"
}

case "${1:-status}" in
    premine)
        check_daemon
        generate_premine
        ;;
    register)
        check_daemon
        register_masternodes
        ;;
    block2)
        check_daemon
        generate_block2
        ;;
    status)
        check_daemon
        show_status
        ;;
    all)
        check_daemon
        generate_premine
        register_masternodes
        generate_block2
        show_status
        ;;
    *)
        usage
        exit 1
        ;;
esac
