#!/bin/bash
# ============================================================================
# PIV2 Testnet Bootstrap Script v2.0
# ============================================================================
# This script initializes the PIV2 testnet by:
# 1. Generating Block 1 (Premine: 50M Dev + 50M Faucet)
# 2. Creating 3 collateral outputs via sendmany
# 3. Generating Block 2 to confirm collaterals
# 4. Locking collateral UTXOs
# 5. Registering 3 masternodes with ProRegTx
# 6. Generating blocks to confirm each ProRegTx
# 7. DMM becomes active after 3 MNs are registered
# ============================================================================

set -e

# Configuration
DATADIR="${DATADIR:-/tmp/piv2_bootstrap}"
PIV2_CLI="${PIV2_CLI:-/home/ubuntu/PIV2-Core/src/piv2-cli}"
PIV2D="${PIV2D:-/home/ubuntu/PIV2-Core/src/piv2d}"
CLI="$PIV2_CLI -testnet -datadir=$DATADIR"
DAEMON="$PIV2D -testnet -datadir=$DATADIR"

# Dev wallet key (CHANGE FOR MAINNET!)
DEV_WALLET_KEY="cUHWixpfkqEXpCC2jJHPeTPsXvP3h9m1FtDEH4XJ8RHkKFnqWuGE"

# Masternode IPs
MN1_IP="57.131.33.151:27171"
MN2_IP="57.131.33.152:27171"
MN3_IP="57.131.33.214:27171"

# Masternode keys (GENERATE NEW FOR MAINNET!)
MN1_OWNER="y7L1LfAfdSbMCu9qvvEYd9LHq97FqUPeaM"
MN1_VOTING="yHEGqeGT91oNvcqjkpEi9JweNyHvhEEHhW"
MN1_PAYOUT="y5AxLQc6Wm9GiNs7mNxs1eZJdGtZTFrpnG"
MN1_OP_PUBKEY="02f3ae14dee0a4ba9b1ce436e0cd8e2e30890b509fda174a7d623a39e9bc4acf0d"
MN1_OP_SECRET="cMe84ZuQPK3cpvZsNWiAJU45KdrfX6FPTSno77tWBAyrHfSbCcAL"

MN2_OWNER="yA3MEDZbpDaPPTUqid6AxAbHd7rjiWvWaN"
MN2_VOTING="y8XquediqxGXxNLPdYNSc4BTCLh72r4Pid"
MN2_PAYOUT="yJWrx8WkfQ9idqYEWUCvgQDVygGMmQQVC1"
MN2_OP_PUBKEY="02a7534aa0965e5385c902366c1888869896e7a09b94c4cf36ad1012956517c1e0"
MN2_OP_SECRET="cSbkaQuj1ViyoVPFxyckxa6xZGsipCx2itK8YHyGzktiAchtPtt6"

MN3_OWNER="yAi9Rhh4W7e7SnQ5FkdL2bDS5dDDSLiK9r"
MN3_VOTING="yKDRZHxazjX8gEN8QyLkukLRBupRzcM3Z7"
MN3_PAYOUT="xzGBAK1kpGxpnASP6VQy1Bvwr7i5spWybJ"
MN3_OP_PUBKEY="0312f12f5f4a3d6751de0e651820892a38a3d9f4b0360195b91c5e5490c05f9f5d"
MN3_OP_SECRET="cUuP9odQzC4QDVxCACDgNMMWSPvzMXtgfYACsxiNaY9R7GsyGNsD"

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
    log_step "[1/10] Cleaning up old data..."
    pkill -9 piv2d 2>/dev/null || true
    sleep 2
    rm -rf "$DATADIR"
    mkdir -p "$DATADIR"

    log_step "[2/10] Creating config..."
    cat > "$DATADIR/piv2.conf" << EOF
testnet=1
server=1
rpcuser=testuser
rpcpassword=testpass123
rpcallowip=127.0.0.1
listen=0
EOF

    log_step "[3/10] Starting daemon..."
    $DAEMON -daemon
    sleep 10

    if ! $CLI getblockcount > /dev/null 2>&1; then
        log_error "Daemon failed to start"
        exit 1
    fi
    log_success "Daemon started. Block count: $($CLI getblockcount)"

    log_step "[4/10] Generating Block 1 (premine)..."
    BLOCK1=$($CLI generatebootstrap 1)
    log_success "Block 1 hash: $(echo $BLOCK1 | jq -r '.[0]')"

    log_step "[5/10] Importing dev wallet..."
    $CLI importprivkey "$DEV_WALLET_KEY" "dev_wallet" false
    $CLI rescanblockchain 0 > /dev/null
    BALANCE=$($CLI getbalance)
    log_success "Balance: $BALANCE PIV"

    log_step "[6/10] Creating collaterals via sendmany..."
    COLL1=$($CLI getnewaddress "coll_mn1")
    COLL2=$($CLI getnewaddress "coll_mn2")
    COLL3=$($CLI getnewaddress "coll_mn3")

    log_info "Collateral addresses:"
    log_info "  MN1: $COLL1"
    log_info "  MN2: $COLL2"
    log_info "  MN3: $COLL3"

    COLL_TXID=$($CLI sendmany "" "{\"$COLL1\":10000,\"$COLL2\":10000,\"$COLL3\":10000}")
    log_success "Collateral TXID: $COLL_TXID"

    # Generate Block 2 to confirm collaterals
    log_info "Generating Block 2 (confirm collaterals)..."
    $CLI generatebootstrap 1
    log_success "Block count: $($CLI getblockcount)"

    log_step "[7/10] Finding and locking collateral UTXOs..."
    UTXOS=$($CLI listunspent)

    # Find collateral vouts by label
    VOUT_MN1=$(echo "$UTXOS" | jq -r ".[] | select(.amount == 10000 and .label == \"coll_mn1\") | .vout")
    VOUT_MN2=$(echo "$UTXOS" | jq -r ".[] | select(.amount == 10000 and .label == \"coll_mn2\") | .vout")
    VOUT_MN3=$(echo "$UTXOS" | jq -r ".[] | select(.amount == 10000 and .label == \"coll_mn3\") | .vout")

    log_info "Collateral outputs:"
    log_info "  MN1: $COLL_TXID:$VOUT_MN1"
    log_info "  MN2: $COLL_TXID:$VOUT_MN2"
    log_info "  MN3: $COLL_TXID:$VOUT_MN3"

    # Lock collaterals (CRITICAL - prevents wallet from using them for fees)
    $CLI lockunspent false true "[{\"txid\":\"$COLL_TXID\",\"vout\":$VOUT_MN1},{\"txid\":\"$COLL_TXID\",\"vout\":$VOUT_MN2},{\"txid\":\"$COLL_TXID\",\"vout\":$VOUT_MN3}]"
    log_success "Collaterals locked"

    log_step "[8/10] Registering MN1..."
    MN1_PROTX=$($CLI protx_register "$COLL_TXID" $VOUT_MN1 "$MN1_IP" "$MN1_OWNER" "$MN1_OP_PUBKEY" "$MN1_VOTING" "$MN1_PAYOUT")
    log_success "MN1 ProRegTx: $MN1_PROTX"
    $CLI generatebootstrap 1
    log_info "Block count: $($CLI getblockcount)"

    log_info "Registering MN2..."
    MN2_PROTX=$($CLI protx_register "$COLL_TXID" $VOUT_MN2 "$MN2_IP" "$MN2_OWNER" "$MN2_OP_PUBKEY" "$MN2_VOTING" "$MN2_PAYOUT")
    log_success "MN2 ProRegTx: $MN2_PROTX"
    $CLI generatebootstrap 1
    log_info "Block count: $($CLI getblockcount)"

    log_info "Registering MN3..."
    MN3_PROTX=$($CLI protx_register "$COLL_TXID" $VOUT_MN3 "$MN3_IP" "$MN3_OWNER" "$MN3_OP_PUBKEY" "$MN3_VOTING" "$MN3_PAYOUT")
    log_success "MN3 ProRegTx: $MN3_PROTX"
    $CLI generatebootstrap 1
    log_info "Block count: $($CLI getblockcount)"

    log_step "[9/10] Verifying bootstrap..."
    MN_COUNT=$($CLI getmasternodecount | jq '.total')
    BLOCK_COUNT=$($CLI getblockcount)

    log_info "Masternode count: $MN_COUNT"
    log_info "Block count: $BLOCK_COUNT"

    # Test DMM is active
    if $CLI generate 1 2>&1 | grep -q "DMM"; then
        log_success "DMM is active!"
    else
        log_warn "DMM may not be active"
    fi

    log_step "[10/10] Saving bootstrap info..."
    cat > "$DATADIR/bootstrap_info.txt" << EOF
PIV2 Testnet Bootstrap Info
===========================
Generated: $(date)

Block Count: $BLOCK_COUNT
Masternode Count: $MN_COUNT

Collateral TXID: $COLL_TXID

MN1:
  IP: $MN1_IP
  Collateral: $COLL_TXID:$VOUT_MN1
  ProRegTx: $MN1_PROTX
  Operator Secret: $MN1_OP_SECRET

MN2:
  IP: $MN2_IP
  Collateral: $COLL_TXID:$VOUT_MN2
  ProRegTx: $MN2_PROTX
  Operator Secret: $MN2_OP_SECRET

MN3:
  IP: $MN3_IP
  Collateral: $COLL_TXID:$VOUT_MN3
  ProRegTx: $MN3_PROTX
  Operator Secret: $MN3_OP_SECRET
EOF

    log_step "BOOTSTRAP COMPLETE!"
    echo ""
    log_info "Bootstrap data saved to: $DATADIR"
    log_info "Info file: $DATADIR/bootstrap_info.txt"
    echo ""
    log_info "Next steps:"
    log_info "1. Copy blocks & chainstate to each VPS:"
    log_info "   cd $DATADIR/testnet5 && tar czvf ~/bootstrap.tar.gz blocks chainstate"
    log_info ""
    log_info "2. On each VPS, extract and configure:"
    log_info "   tar xzvf bootstrap.tar.gz -C ~/.piv2/testnet5/"
    log_info ""
    log_info "3. Add to piv2.conf on each VPS:"
    log_info "   masternode=1"
    log_info "   masternodeoperatorprivatekey=<OPERATOR_SECRET>"
    log_info ""
    log_info "Operator secrets:"
    log_info "  MN1 (57.131.33.151): $MN1_OP_SECRET"
    log_info "  MN2 (57.131.33.152): $MN2_OP_SECRET"
    log_info "  MN3 (57.131.33.214): $MN3_OP_SECRET"
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
        log_success "DMM should be active"
    else
        log_warn "DMM requires 3 MNs, currently have $mn_count"
    fi

    log_info ""
    log_info "Registered masternodes:"
    $CLI protx_list valid | jq '.[].dmnstate | {service, registeredHeight}'
}

# ============================================================================
# USAGE
# ============================================================================
usage() {
    echo "PIV2 Testnet Bootstrap Script v2.0"
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
