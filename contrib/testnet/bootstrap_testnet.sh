#!/bin/bash
# ============================================================================
# PIV2 Testnet Bootstrap Script v3.0
# ============================================================================
# This script initializes the PIV2 testnet by:
# 1. Starting the daemon
# 2. Generating Block 1 (Premine with 3 MN collaterals)
# 3. Done! Genesis MNs are automatically active (no ProRegTx needed)
#
# Like ETH2/Cosmos, genesis MNs are defined in the consensus params.
# Their collaterals are created at block 1, and they can start producing
# blocks immediately (after announcing their IP via P2P).
# ============================================================================

set -e

# Configuration
DATADIR="${DATADIR:-/tmp/piv2_bootstrap}"
PIV2_CLI="${PIV2_CLI:-/home/ubuntu/PIV2-Core/src/piv2-cli}"
PIV2D="${PIV2D:-/home/ubuntu/PIV2-Core/src/piv2d}"
CLI="$PIV2_CLI -testnet -datadir=$DATADIR"
DAEMON="$PIV2D -testnet -datadir=$DATADIR"

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
log_step() { echo -e "\n${GREEN}========================================${NC}"; echo -e "${GREEN}$1${NC}"; echo -e "${GREEN}========================================${NC}"; }

# ============================================================================
# MAIN BOOTSTRAP FUNCTION
# ============================================================================
full_bootstrap() {
    log_step "[1/4] Cleaning up old data..."
    pkill -9 piv2d 2>/dev/null || true
    sleep 2
    rm -rf "$DATADIR"
    mkdir -p "$DATADIR"

    log_step "[2/4] Creating config..."
    cat > "$DATADIR/piv2.conf" << EOF
testnet=1
server=1
rpcuser=testuser
rpcpassword=testpass123
rpcallowip=127.0.0.1
listen=0
EOF

    log_step "[3/4] Starting daemon..."
    $DAEMON -daemon
    sleep 10

    if ! $CLI getblockcount > /dev/null 2>&1; then
        log_error "Daemon failed to start"
        exit 1
    fi
    log_success "Daemon started. Block count: $($CLI getblockcount)"

    # Check genesis MNs are loaded
    MN_COUNT=$($CLI getmasternodecount 2>/dev/null | jq '.total' 2>/dev/null || echo "0")
    log_info "Genesis MNs loaded: $MN_COUNT"

    log_step "[4/4] Generating Block 1 (premine + collaterals)..."
    # Block 1 creates:
    # - 500,000 PIV2 to Dev Wallet
    # - 10,000 PIV2 to each of 3 genesis MN owner addresses
    BLOCK1=$($CLI generatebootstrap 1)
    BLOCK1_HASH=$(echo $BLOCK1 | jq -r '.[0]')
    log_success "Block 1 hash: $BLOCK1_HASH"
    log_success "Block count: $($CLI getblockcount)"

    # Verify MNs are active
    MN_COUNT=$($CLI getmasternodecount | jq '.total')
    log_success "Active Masternodes: $MN_COUNT"

    log_step "BOOTSTRAP COMPLETE!"
    echo ""
    log_info "Genesis MNs are now active with their collaterals."
    log_info "No ProRegTx was needed - they are defined in consensus params."
    echo ""
    log_info "Bootstrap data saved to: $DATADIR"
    echo ""
    log_info "Next steps:"
    log_info "1. Copy blocks & chainstate to each VPS:"
    log_info "   cd $DATADIR/testnet5 && tar czvf ~/bootstrap.tar.gz blocks chainstate evodb"
    log_info ""
    log_info "2. On each VPS, extract and configure:"
    log_info "   tar xzvf bootstrap.tar.gz -C ~/.piv2/testnet5/"
    log_info ""
    log_info "3. Add to piv2.conf on each VPS:"
    log_info "   masternode=1"
    log_info "   mnoperatorprivatekey=<OPERATOR_SECRET>"
    log_info ""
    log_info "Operator secrets (TESTNET ONLY - keep secure!):"
    log_info "  MN1 (y7L1...): cMe84ZuQPK3cpvZsNWiAJU45KdrfX6FPTSno77tWBAyrHfSbCcAL"
    log_info "  MN2 (yA3M...): cSbkaQuj1ViyoVPFxyckxa6xZGsipCx2itK8YHyGzktiAchtPtt6"
    log_info "  MN3 (yAi9...): cUuP9odQzC4QDVxCACDgNMMWSPvzMXtgfYACsxiNaY9R7GsyGNsD"
}

# ============================================================================
# SHOW STATUS
# ============================================================================
show_status() {
    log_info "PIV2 Testnet Status"
    log_info "==================="

    if ! $CLI getblockcount > /dev/null 2>&1; then
        log_error "Daemon not running"
        exit 1
    fi

    local height=$($CLI getblockcount)
    local mn_count=$($CLI getmasternodecount | jq '.total' 2>/dev/null || echo "0")

    log_info "Block height: $height"
    log_info "Masternode count: $mn_count"

    if [ "$mn_count" -ge 3 ]; then
        log_success "DMM should be active (3 MNs)"
    else
        log_warn "DMM requires 3 MNs, currently have $mn_count"
    fi
}

# ============================================================================
# USAGE
# ============================================================================
usage() {
    echo "PIV2 Testnet Bootstrap Script v3.0"
    echo ""
    echo "Usage: $0 <command>"
    echo ""
    echo "Commands:"
    echo "  full     - Run full bootstrap (clean start)"
    echo "  status   - Show current status"
    echo "  help     - Show this help"
    echo ""
    echo "Environment variables:"
    echo "  DATADIR  - Data directory (default: /tmp/piv2_bootstrap)"
    echo "  PIV2_CLI - Path to piv2-cli"
    echo "  PIV2D    - Path to piv2d"
    echo ""
    echo "Genesis MNs (defined in consensus, no ProRegTx needed):"
    echo "  MN1: y7L1LfAfdSbMCu9qvvEYd9LHq97FqUPeaM"
    echo "  MN2: yA3MEDZbpDaPPTUqid6AxAbHd7rjiWvWaN"
    echo "  MN3: yAi9Rhh4W7e7SnQ5FkdL2bDS5dDDSLiK9r"
}

# ============================================================================
# MAIN
# ============================================================================
case "${1:-help}" in
    full)
        full_bootstrap
        ;;
    status)
        show_status
        ;;
    help|--help|-h)
        usage
        ;;
    *)
        usage
        exit 1
        ;;
esac
