#!/bin/bash
# deploy_to_vps.sh - Deploy PIV2 binaries to all testnet VPS nodes in parallel
#
# Usage:
#   ./deploy_to_vps.sh [options]
#
# Options:
#   --reindex    Start daemons with -reindex flag
#   --clean      Clean wallet and database before starting
#   --stop       Only stop daemons (no start)
#   --status     Only check status (no changes)
#   --compile    Force git pull + make on compile nodes
#   --help       Show this help

set -e

# VPS Configuration
# All testnet nodes:
# - Seed: Non-masternode node for peer discovery and faucet/explorer
# - MN1-MN4: Masternode operators
VPS_NODES=(
    "57.131.33.151"   # Seed + Faucet + Explorer
    "162.19.251.75"   # MN1
    "57.131.33.152"   # MN2
    "57.131.33.214"   # MN3
    "51.75.31.44"     # MN4
)

# Nodes that use ~/PIV2-Core/src/ instead of ~/
# These nodes need git pull + make instead of binary copy
# Note: MN1 (162.19.251.75) has repo but we deploy standalone binaries to ~ for simplicity
COMPILE_NODES=(
    "57.131.33.151"   # Seed - has PIV2-Core repo, uses ~/PIV2-Core/src/
)

SSH_KEY="~/.ssh/id_ed25519_vps"
SSH_OPTS="-o StrictHostKeyChecking=no -o ConnectTimeout=10"
SSH="ssh -i $SSH_KEY $SSH_OPTS"
SCP="scp -i $SSH_KEY $SSH_OPTS"

# Paths
LOCAL_DAEMON="/home/ubuntu/PIV2-Core/src/piv2d"
LOCAL_CLI="/home/ubuntu/PIV2-Core/src/piv2-cli"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Options
REINDEX=false
CLEAN=false
STOP_ONLY=false
STATUS_ONLY=false
COMPILE=false

# Parse arguments
for arg in "$@"; do
    case $arg in
        --reindex) REINDEX=true ;;
        --clean) CLEAN=true ;;
        --stop) STOP_ONLY=true ;;
        --status) STATUS_ONLY=true ;;
        --compile) COMPILE=true ;;
        --help)
            head -20 "$0" | tail -17
            exit 0
            ;;
    esac
done

log() {
    echo -e "${BLUE}[$(date +%H:%M:%S)]${NC} $1"
}

success() {
    echo -e "${GREEN}[$(date +%H:%M:%S)] ✓${NC} $1"
}

warn() {
    echo -e "${YELLOW}[$(date +%H:%M:%S)] !${NC} $1"
}

error() {
    echo -e "${RED}[$(date +%H:%M:%S)] ✗${NC} $1"
}

# Check if IP is in compile nodes list
is_compile_node() {
    local ip=$1
    for node in "${COMPILE_NODES[@]}"; do
        if [[ "$node" == "$ip" ]]; then
            return 0
        fi
    done
    return 1
}

# Get the CLI path for a node
get_cli_path() {
    local ip=$1
    if is_compile_node "$ip"; then
        echo "~/PIV2-Core/src/piv2-cli"
    else
        echo "~/piv2-cli"
    fi
}

# Get the daemon path for a node
get_daemon_path() {
    local ip=$1
    if is_compile_node "$ip"; then
        echo "~/PIV2-Core/src/piv2d"
    else
        echo "~/piv2d"
    fi
}

# Function to stop daemon on a VPS
stop_daemon() {
    local ip=$1
    $SSH ubuntu@$ip "pkill -9 piv2d 2>/dev/null || true; sleep 1" 2>/dev/null
}

# Function to copy binaries to a VPS (for non-compile nodes)
copy_binaries() {
    local ip=$1
    $SCP "$LOCAL_DAEMON" ubuntu@$ip:~/piv2d 2>/dev/null
    $SCP "$LOCAL_CLI" ubuntu@$ip:~/piv2-cli 2>/dev/null
}

# Function to update code via git pull + make (for compile nodes)
compile_update() {
    local ip=$1
    $SSH ubuntu@$ip "
        cd ~/PIV2-Core
        git fetch origin
        git reset --hard origin/main
        echo 'Commit:' \$(git log -1 --oneline)
        make -j2 2>&1 | tail -3
    " 2>/dev/null
}

# Function to deploy to a node (handles both types)
deploy_node() {
    local ip=$1
    if is_compile_node "$ip"; then
        if $COMPILE; then
            compile_update "$ip"
        else
            echo "Skipped (compile node, use --compile to update)"
        fi
    else
        copy_binaries "$ip"
    fi
}

# Function to start daemon on a VPS
start_daemon() {
    local ip=$1
    local daemon_path=$(get_daemon_path "$ip")
    local extra_args=""

    if $CLEAN; then
        $SSH ubuntu@$ip "
            rm -f ~/.piv2/testnet5/wallet.dat ~/.piv2/testnet5/.walletlock
            rm -rf ~/.piv2/testnet5/database ~/.piv2/testnet5/wallets
        " 2>/dev/null
    fi

    if $REINDEX; then
        extra_args="-reindex"
    fi

    $SSH ubuntu@$ip "$daemon_path -testnet -daemon $extra_args" 2>/dev/null
}

# Function to get status of a VPS
get_status() {
    local ip=$1
    local cli_path=$(get_cli_path "$ip")
    local height
    local peers
    local node_type

    if is_compile_node "$ip"; then
        node_type="[repo]"
    else
        node_type="[bin] "
    fi

    height=$($SSH ubuntu@$ip "$cli_path -testnet getblockcount 2>&1" 2>/dev/null || echo "offline")
    peers=$($SSH ubuntu@$ip "$cli_path -testnet getpeerinfo 2>&1 | jq 'length'" 2>/dev/null || echo "?")

    echo "$node_type $ip: height=$height, peers=$peers"
}

# Main execution

if $STATUS_ONLY; then
    log "Checking status of all VPS nodes..."
    echo ""
    for ip in "${VPS_NODES[@]}"; do
        get_status "$ip" &
    done
    wait
    exit 0
fi

if $STOP_ONLY; then
    log "Stopping daemons on all VPS nodes..."
    for ip in "${VPS_NODES[@]}"; do
        stop_daemon "$ip" &
    done
    wait
    success "All daemons stopped"
    exit 0
fi

# Full deployment
echo ""
echo "=================================================="
echo "  PIV2 Testnet VPS Deployment"
echo "=================================================="
echo ""
echo "Options:"
echo "  Reindex: $REINDEX"
echo "  Clean: $CLEAN"
echo "  Compile: $COMPILE"
echo ""
echo "Nodes:"
for ip in "${VPS_NODES[@]}"; do
    if is_compile_node "$ip"; then
        echo "  [repo] $ip (git pull + make)"
    else
        echo "  [bin]  $ip (binary copy)"
    fi
done
echo ""

# Step 1: Stop all daemons in parallel
log "Step 1/4: Stopping all daemons..."
for ip in "${VPS_NODES[@]}"; do
    (stop_daemon "$ip" && success "Stopped $ip") &
done
wait
echo ""

# Step 2: Deploy (copy binaries or compile)
log "Step 2/4: Deploying to all VPS..."
for ip in "${VPS_NODES[@]}"; do
    (deploy_node "$ip" && success "Deployed $ip") &
done
wait
echo ""

# Step 3: Start daemons in parallel
log "Step 3/4: Starting daemons..."
for ip in "${VPS_NODES[@]}"; do
    (start_daemon "$ip" && success "Started $ip") &
done
wait
echo ""

# Step 4: Wait and check status
log "Step 4/4: Waiting 20s for nodes to initialize..."
sleep 20

echo ""
log "Final status:"
echo ""
for ip in "${VPS_NODES[@]}"; do
    get_status "$ip"
done

echo ""
success "Deployment complete!"
