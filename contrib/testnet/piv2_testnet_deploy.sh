#!/bin/bash
# =============================================================================
# PIV2 Testnet - VPS Deployment Script
# =============================================================================
# Copyright (c) 2025 The PIV2/PIVHU developers
# Usage: ./piv2_testnet_deploy.sh [mn1|mn2|mn3|node]
# =============================================================================

set -e

# -----------------------------------------------------------------------------
# Configuration
# -----------------------------------------------------------------------------
PIV2_VERSION="0.1.0"
PIV2_USER="piv2"
PIV2_HOME="/home/${PIV2_USER}"
PIV2_DATADIR="${PIV2_HOME}/.piv2"
PIV2_BINDIR="/usr/local/bin"
PIV2_SERVICE="piv2d"

# Network ports
P2P_PORT=27171
RPC_PORT=27172

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# -----------------------------------------------------------------------------
# Functions
# -----------------------------------------------------------------------------

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[OK]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

check_root() {
    if [[ $EUID -ne 0 ]]; then
        log_error "This script must be run as root"
        exit 1
    fi
}

print_banner() {
    echo ""
    echo "=============================================="
    echo "  PIV2 Testnet Deployment Script v${PIV2_VERSION}"
    echo "=============================================="
    echo ""
}

# -----------------------------------------------------------------------------
# System Setup
# -----------------------------------------------------------------------------

setup_system() {
    log_info "Updating system packages..."
    apt-get update -qq
    apt-get upgrade -y -qq

    log_info "Installing dependencies..."
    apt-get install -y -qq \
        build-essential \
        libtool \
        autotools-dev \
        automake \
        autoconf \
        pkg-config \
        autoconf-archive \
        bsdmainutils \
        python3 \
        libevent-dev \
        libboost-system-dev \
        libboost-filesystem-dev \
        libboost-test-dev \
        libboost-thread-dev \
        libssl-dev \
        libdb5.3-dev \
        libdb5.3++-dev \
        libzmq3-dev \
        libminiupnpc-dev \
        libnatpmp-dev \
        libqrencode-dev \
        libsodium-dev \
        libgmp-dev \
        git \
        curl \
        jq \
        ufw \
        fail2ban

    log_success "System dependencies installed"
}

setup_firewall() {
    log_info "Configuring firewall..."

    ufw default deny incoming
    ufw default allow outgoing
    ufw allow ssh
    ufw allow ${P2P_PORT}/tcp comment 'PIV2 P2P'

    # Only allow RPC from localhost by default
    # Uncomment next line to allow external RPC (not recommended)
    # ufw allow ${RPC_PORT}/tcp comment 'PIV2 RPC'

    ufw --force enable

    log_success "Firewall configured (P2P: ${P2P_PORT}, RPC: localhost only)"
}

setup_fail2ban() {
    log_info "Configuring fail2ban..."

    cat > /etc/fail2ban/jail.local << 'EOF'
[DEFAULT]
bantime = 3600
findtime = 600
maxretry = 5

[sshd]
enabled = true
port = ssh
filter = sshd
logpath = /var/log/auth.log
maxretry = 3
EOF

    systemctl enable fail2ban
    systemctl restart fail2ban

    log_success "fail2ban configured"
}

# -----------------------------------------------------------------------------
# User Setup
# -----------------------------------------------------------------------------

setup_user() {
    log_info "Creating PIV2 user..."

    if id "${PIV2_USER}" &>/dev/null; then
        log_warn "User ${PIV2_USER} already exists"
    else
        useradd -m -s /bin/bash ${PIV2_USER}
        log_success "User ${PIV2_USER} created"
    fi

    mkdir -p ${PIV2_DATADIR}
    chown -R ${PIV2_USER}:${PIV2_USER} ${PIV2_HOME}
}

# -----------------------------------------------------------------------------
# Build PIV2
# -----------------------------------------------------------------------------

build_piv2() {
    log_info "Building PIV2 from source..."

    cd /tmp

    if [[ -d "PIVX-V2-PROPOSAL" ]]; then
        log_info "Updating existing repository..."
        cd PIVX-V2-PROPOSAL
        git fetch origin
        git checkout main
        git pull
    else
        log_info "Cloning repository..."
        git clone https://github.com/AdonisPhusis/PIVX-V2-PROPOSAL.git
        cd PIVX-V2-PROPOSAL
        git checkout main
    fi

    log_info "Initializing submodules..."
    git submodule update --init --recursive

    log_info "Running autogen..."
    ./autogen.sh

    log_info "Configuring..."
    ./configure --without-gui --with-incompatible-bdb --disable-tests

    log_info "Compiling (this may take a while)..."
    make -j$(nproc)

    log_info "Installing binaries..."
    cp src/piv2d ${PIV2_BINDIR}/
    cp src/piv2-cli ${PIV2_BINDIR}/
    chmod +x ${PIV2_BINDIR}/piv2d
    chmod +x ${PIV2_BINDIR}/piv2-cli

    log_success "PIV2 binaries installed to ${PIV2_BINDIR}"
}

# -----------------------------------------------------------------------------
# Configuration
# -----------------------------------------------------------------------------

generate_rpc_password() {
    openssl rand -hex 32
}

create_config() {
    local NODE_TYPE=$1
    local RPC_USER="piv2${NODE_TYPE}"
    local RPC_PASS=$(generate_rpc_password)

    log_info "Creating configuration for ${NODE_TYPE}..."

    # Get external IP
    EXTERNAL_IP=$(curl -s https://api.ipify.org || curl -s https://ifconfig.me || echo "YOUR_IP")

    cat > ${PIV2_DATADIR}/piv2.conf << EOF
# =============================================================================
# PIV2 Testnet Configuration - ${NODE_TYPE^^}
# Generated: $(date -u +"%Y-%m-%d %H:%M:%S UTC")
# =============================================================================

# Network
testnet=1
server=1
daemon=1
listen=1

# RPC
rpcuser=${RPC_USER}
rpcpassword=${RPC_PASS}
rpcport=${RPC_PORT}
rpcallowip=127.0.0.1

# P2P
port=${P2P_PORT}
externalip=${EXTERNAL_IP}:${P2P_PORT}

# Logging
debug=dmm
debug=masternode
logips=1
logtimestamps=1

# Performance
maxconnections=125
dbcache=512

# Peers (add known testnet nodes)
# addnode=<MN1_IP>:${P2P_PORT}
# addnode=<MN2_IP>:${P2P_PORT}
# addnode=<MN3_IP>:${P2P_PORT}
EOF

    # Add masternode config if this is a MN
    if [[ "${NODE_TYPE}" == mn* ]]; then
        cat >> ${PIV2_DATADIR}/piv2.conf << EOF

# Masternode
masternode=1
# mnoperatorprivatekey will be set via initmasternode RPC after registration
EOF
    fi

    chown ${PIV2_USER}:${PIV2_USER} ${PIV2_DATADIR}/piv2.conf
    chmod 600 ${PIV2_DATADIR}/piv2.conf

    log_success "Configuration created at ${PIV2_DATADIR}/piv2.conf"
    log_info "RPC User: ${RPC_USER}"
    log_warn "RPC Password saved in config file (keep secure!)"
}

# -----------------------------------------------------------------------------
# Systemd Service
# -----------------------------------------------------------------------------

create_systemd_service() {
    log_info "Creating systemd service..."

    cat > /etc/systemd/system/${PIV2_SERVICE}.service << EOF
[Unit]
Description=PIV2 Testnet Daemon
After=network.target

[Service]
Type=forking
User=${PIV2_USER}
Group=${PIV2_USER}
WorkingDirectory=${PIV2_HOME}

ExecStart=${PIV2_BINDIR}/piv2d -daemon -conf=${PIV2_DATADIR}/piv2.conf -datadir=${PIV2_DATADIR}
ExecStop=${PIV2_BINDIR}/piv2-cli -conf=${PIV2_DATADIR}/piv2.conf -datadir=${PIV2_DATADIR} stop

Restart=on-failure
RestartSec=30
TimeoutStartSec=60
TimeoutStopSec=120

# Hardening
PrivateTmp=true
ProtectSystem=full
NoNewPrivileges=true
PrivateDevices=true

[Install]
WantedBy=multi-user.target
EOF

    systemctl daemon-reload
    systemctl enable ${PIV2_SERVICE}

    log_success "Systemd service created and enabled"
}

# -----------------------------------------------------------------------------
# Health Check
# -----------------------------------------------------------------------------

health_check() {
    log_info "Running health check..."

    # Check if service is running
    if systemctl is-active --quiet ${PIV2_SERVICE}; then
        log_success "Service is running"
    else
        log_error "Service is not running"
        return 1
    fi

    # Wait for RPC to be available
    sleep 5

    # Get blockchain info
    local RPC_USER=$(grep rpcuser ${PIV2_DATADIR}/piv2.conf | cut -d= -f2)
    local RPC_PASS=$(grep rpcpassword ${PIV2_DATADIR}/piv2.conf | cut -d= -f2)

    local INFO=$(${PIV2_BINDIR}/piv2-cli -testnet -rpcuser=${RPC_USER} -rpcpassword=${RPC_PASS} getblockchaininfo 2>/dev/null || echo "")

    if [[ -n "${INFO}" ]]; then
        local BLOCKS=$(echo "${INFO}" | jq -r '.blocks')
        local CHAIN=$(echo "${INFO}" | jq -r '.chain')
        log_success "Blockchain: ${CHAIN}, Blocks: ${BLOCKS}"
    else
        log_warn "RPC not yet available (node may still be starting)"
    fi

    # Check P2P port
    if ss -tlnp | grep -q ":${P2P_PORT}"; then
        log_success "P2P port ${P2P_PORT} is listening"
    else
        log_warn "P2P port ${P2P_PORT} not yet listening"
    fi
}

# -----------------------------------------------------------------------------
# Start/Stop
# -----------------------------------------------------------------------------

start_node() {
    log_info "Starting PIV2 node..."
    systemctl start ${PIV2_SERVICE}
    sleep 5
    health_check
}

stop_node() {
    log_info "Stopping PIV2 node..."
    systemctl stop ${PIV2_SERVICE}
    log_success "Node stopped"
}

# -----------------------------------------------------------------------------
# Logs
# -----------------------------------------------------------------------------

show_logs() {
    log_info "Showing PIV2 logs (Ctrl+C to exit)..."
    tail -f ${PIV2_DATADIR}/testnet/debug.log
}

# -----------------------------------------------------------------------------
# Status
# -----------------------------------------------------------------------------

show_status() {
    echo ""
    echo "=== PIV2 Node Status ==="
    echo ""

    # Service status
    if systemctl is-active --quiet ${PIV2_SERVICE}; then
        echo -e "Service: ${GREEN}RUNNING${NC}"
    else
        echo -e "Service: ${RED}STOPPED${NC}"
    fi

    # Get RPC credentials
    if [[ -f "${PIV2_DATADIR}/piv2.conf" ]]; then
        local RPC_USER=$(grep rpcuser ${PIV2_DATADIR}/piv2.conf | cut -d= -f2)
        local RPC_PASS=$(grep rpcpassword ${PIV2_DATADIR}/piv2.conf | cut -d= -f2)
        local CLI="${PIV2_BINDIR}/piv2-cli -testnet -rpcuser=${RPC_USER} -rpcpassword=${RPC_PASS}"

        # Blockchain info
        local INFO=$(${CLI} getblockchaininfo 2>/dev/null || echo "")
        if [[ -n "${INFO}" ]]; then
            echo "Chain: $(echo ${INFO} | jq -r '.chain')"
            echo "Blocks: $(echo ${INFO} | jq -r '.blocks')"
            echo "Headers: $(echo ${INFO} | jq -r '.headers')"
            echo "Difficulty: $(echo ${INFO} | jq -r '.difficulty')"
        fi

        # Network info
        local NETINFO=$(${CLI} getnetworkinfo 2>/dev/null || echo "")
        if [[ -n "${NETINFO}" ]]; then
            echo "Connections: $(echo ${NETINFO} | jq -r '.connections')"
            echo "Version: $(echo ${NETINFO} | jq -r '.subversion')"
        fi

        # Masternode status
        local MNSTATUS=$(${CLI} getmasternodestatus 2>/dev/null || echo "")
        if [[ -n "${MNSTATUS}" && "${MNSTATUS}" != *"error"* ]]; then
            echo ""
            echo "=== Masternode Status ==="
            echo "${MNSTATUS}" | jq .
        fi

        # PIV2 state
        local PIV2STATE=$(${CLI} getpiv2state 2>/dev/null || echo "")
        if [[ -n "${PIV2STATE}" ]]; then
            echo ""
            echo "=== PIV2/KHU State ==="
            echo "${PIV2STATE}" | jq .
        fi
    fi

    echo ""
}

# -----------------------------------------------------------------------------
# Main
# -----------------------------------------------------------------------------

main() {
    print_banner

    case "${1:-}" in
        install)
            check_root
            local NODE_TYPE="${2:-node}"
            log_info "Installing PIV2 as ${NODE_TYPE}..."
            setup_system
            setup_firewall
            setup_fail2ban
            setup_user
            build_piv2
            create_config "${NODE_TYPE}"
            create_systemd_service
            start_node
            echo ""
            log_success "Installation complete!"
            echo ""
            echo "Useful commands:"
            echo "  systemctl status ${PIV2_SERVICE}  - Check service status"
            echo "  systemctl restart ${PIV2_SERVICE} - Restart node"
            echo "  $0 status                         - Show node status"
            echo "  $0 logs                           - Show live logs"
            echo ""
            ;;

        start)
            start_node
            ;;

        stop)
            stop_node
            ;;

        restart)
            stop_node
            sleep 2
            start_node
            ;;

        status)
            show_status
            ;;

        logs)
            show_logs
            ;;

        health)
            health_check
            ;;

        config)
            local NODE_TYPE="${2:-node}"
            create_config "${NODE_TYPE}"
            ;;

        *)
            echo "Usage: $0 {install|start|stop|restart|status|logs|health|config} [node_type]"
            echo ""
            echo "Commands:"
            echo "  install [mn1|mn2|mn3|node]  - Full installation"
            echo "  start                       - Start the node"
            echo "  stop                        - Stop the node"
            echo "  restart                     - Restart the node"
            echo "  status                      - Show node status"
            echo "  logs                        - Show live logs"
            echo "  health                      - Run health check"
            echo "  config [mn1|mn2|mn3|node]   - Generate config only"
            echo ""
            echo "Node types:"
            echo "  mn1, mn2, mn3  - Masternode configurations"
            echo "  node           - Regular node (default)"
            echo ""
            echo "Examples:"
            echo "  $0 install mn1    - Install as Masternode 1"
            echo "  $0 install node   - Install as regular node"
            echo "  $0 status         - Check current status"
            echo ""
            exit 1
            ;;
    esac
}

main "$@"
