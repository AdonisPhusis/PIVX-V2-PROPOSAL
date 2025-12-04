# PIV2 Testnet - Installation Guide

**Version:** 1.0.0
**Target:** Ubuntu 22.04 / 24.04 / 25.04 LTS
**Date:** December 2025

---

## Quick Start (Copy-Paste)

Complete installation on a fresh Ubuntu VPS. Copy-paste this entire block:

```bash
#!/bin/bash
set -e

echo "=== PIV2 Testnet Installation ==="

# === STEP 1: Install system dependencies ===
echo "[1/5] Installing system dependencies..."
sudo apt update && sudo apt install -y \
    build-essential libtool autotools-dev automake autoconf pkg-config \
    autoconf-archive bsdmainutils python3 git curl wget jq ufw \
    libevent-dev libboost-all-dev libssl-dev libzmq3-dev \
    libminiupnpc-dev libsodium-dev libgmp-dev

# === STEP 2: Install Rust (required for Sapling) ===
echo "[2/5] Installing Rust..."
if ! command -v cargo &> /dev/null; then
    curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
    source ~/.cargo/env
fi
cargo --version

# === STEP 3: Install BerkeleyDB 4.8 (wallet support) ===
echo "[3/5] Installing BerkeleyDB 4.8..."
cd /tmp
wget -q http://download.oracle.com/berkeley-db/db-4.8.30.NC.tar.gz
tar -xzf db-4.8.30.NC.tar.gz
cd db-4.8.30.NC
sed -i 's/__atomic_compare_exchange/__atomic_compare_exchange_db/g' dbinc/atomic.h
cd build_unix
../dist/configure --enable-cxx --disable-shared --with-pic --prefix=/usr/local/BerkeleyDB.4.8
make -j$(nproc)
sudo make install
echo "[OK] BerkeleyDB 4.8 installed"

# === STEP 4: Clone and build PIV2 ===
echo "[4/5] Building PIV2..."
cd ~
git clone https://github.com/AdonisPhusis/PIVX-V2-PROPOSAL.git PIV2-Core
cd PIV2-Core
./autogen.sh
./configure --without-gui \
    BDB_LIBS="-L/usr/local/BerkeleyDB.4.8/lib -ldb_cxx-4.8" \
    BDB_CFLAGS="-I/usr/local/BerkeleyDB.4.8/include"
make -j$(nproc)

# === STEP 5: Verify ===
echo "[5/5] Verifying installation..."
./src/piv2d --version

echo ""
echo "=== PIV2 Installation Complete ==="
echo "Run: ./src/piv2d -testnet -daemon"
```

Or follow the detailed steps below.

---

## Prerequisites

- Ubuntu 24.04 or 25.04 LTS
- Minimum 2GB RAM (4GB recommended for compilation)
- 20GB disk space
- Root access or sudo privileges

---

## Step 1: Install Dependencies

```bash
# Update system
sudo apt update && sudo apt upgrade -y

# Install build tools
sudo apt install -y \
    build-essential \
    libtool \
    autotools-dev \
    automake \
    autoconf \
    pkg-config \
    autoconf-archive \
    bsdmainutils \
    python3

# Install libraries
sudo apt install -y \
    libevent-dev \
    libboost-dev \
    libboost-system-dev \
    libboost-filesystem-dev \
    libboost-test-dev \
    libboost-thread-dev \
    libssl-dev \
    libzmq3-dev \
    libminiupnpc-dev \
    libsodium-dev \
    libgmp-dev

# NOTE: BerkeleyDB 4.8 is installed separately in Step 2
# Do NOT install libdb5.3 - we use BDB 4.8 for wallet compatibility

# Install utilities
sudo apt install -y git curl jq ufw

# Install Rust (required for Sapling/shielded transactions)
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
source ~/.cargo/env

# Verify Rust installation
cargo --version
```

### Verify Dependencies

```bash
# Check boost version (need >= 1.70)
dpkg -l | grep libboost

# Check OpenSSL version
openssl version

# Check Rust
cargo --version
```

---

## Step 2: Install BerkeleyDB 4.8

PIV2 wallet requires BerkeleyDB 4.8 for compatibility. Ubuntu 24.04+ doesn't include it, so we compile from source.

### Option A: Use the install script (recommended)

```bash
cd ~/PIV2-Core
./contrib/wallet/install_bdb48.sh
```

### Option B: Manual installation

```bash
cd /tmp

# Download BDB 4.8
wget http://download.oracle.com/berkeley-db/db-4.8.30.NC.tar.gz
tar -xzf db-4.8.30.NC.tar.gz
cd db-4.8.30.NC

# Patch for modern GCC (required for GCC 10+)
sed -i 's/__atomic_compare_exchange/__atomic_compare_exchange_db/g' dbinc/atomic.h

# Build
cd build_unix
../dist/configure --enable-cxx --disable-shared --with-pic --prefix=/usr/local/BerkeleyDB.4.8
make -j$(nproc)
sudo make install

# Verify
ls /usr/local/BerkeleyDB.4.8/lib/libdb_cxx*
```

Expected output:
```
/usr/local/BerkeleyDB.4.8/lib/libdb_cxx-4.8.a
/usr/local/BerkeleyDB.4.8/lib/libdb_cxx.a
```

---

## Step 3: Clone and Build PIV2

```bash
# Create user (optional but recommended)
sudo useradd -m -s /bin/bash piv2
sudo su - piv2

# Clone repository
cd ~
git clone https://github.com/AdonisPhusis/PIVX-V2-PROPOSAL.git PIV2-Core
cd PIV2-Core

# Generate configure script
./autogen.sh

# Configure with BDB 4.8
./configure --without-gui \
    BDB_LIBS="-L/usr/local/BerkeleyDB.4.8/lib -ldb_cxx-4.8" \
    BDB_CFLAGS="-I/usr/local/BerkeleyDB.4.8/include"

# Compile
make -j$(nproc)

# Verify build
./src/piv2d --version
# Expected: PIV2 Core Daemon version v1.0.0
```

> **Note:** `autogen.sh` automatically initializes git submodules and fixes incomplete leveldb if needed.

### Build Options

| Option | Description |
|--------|-------------|
| `--without-gui` | No Qt GUI (server only) |
| `--with-incompatible-bdb` | Allow any Berkeley DB version |
| `--disable-tests` | Skip test compilation (faster) |
| `--disable-bench` | Skip benchmark compilation |

---

## Step 4: Configure

### Create Data Directory

```bash
mkdir -p ~/.piv2
```

### Create Configuration File

```bash
cat > ~/.piv2/piv2.conf << 'EOF'
# =============================================================================
# PIV2 Testnet Configuration
# =============================================================================

# Network
testnet=1
server=1
daemon=1
listen=1

# RPC (generate secure password!)
rpcuser=piv2user
rpcpassword=CHANGE_THIS_TO_SECURE_PASSWORD
rpcport=27172
rpcallowip=127.0.0.1

# P2P
port=27171
# externalip=YOUR_PUBLIC_IP:27171

# Logging
debug=dmm
debug=masternode
logips=1
logtimestamps=1

# Performance
maxconnections=125
dbcache=512

# Masternode (uncomment for MN)
# masternode=1
EOF
```

### Generate RPC Password

```bash
# Generate secure password
RPC_PASS=$(openssl rand -hex 32)
sed -i "s/CHANGE_THIS_TO_SECURE_PASSWORD/$RPC_PASS/" ~/.piv2/piv2.conf
echo "RPC Password: $RPC_PASS"
```

---

## Step 5: Configure Firewall

```bash
# Allow SSH
sudo ufw allow ssh

# Allow PIV2 P2P port
sudo ufw allow 27171/tcp comment 'PIV2 P2P'

# Enable firewall
sudo ufw --force enable

# Verify
sudo ufw status
```

---

## Step 6: Create Systemd Service

```bash
sudo cat > /etc/systemd/system/piv2d.service << 'EOF'
[Unit]
Description=PIV2 Testnet Daemon
After=network.target

[Service]
Type=forking
User=piv2
Group=piv2
WorkingDirectory=/home/piv2

ExecStart=/home/piv2/PIV2-Core/src/piv2d -daemon -conf=/home/piv2/.piv2/piv2.conf -datadir=/home/piv2/.piv2
ExecStop=/home/piv2/PIV2-Core/src/piv2-cli -conf=/home/piv2/.piv2/piv2.conf -datadir=/home/piv2/.piv2 stop

Restart=on-failure
RestartSec=30
TimeoutStartSec=60
TimeoutStopSec=120

# Hardening
PrivateTmp=true
ProtectSystem=full
NoNewPrivileges=true

[Install]
WantedBy=multi-user.target
EOF

# Enable and start
sudo systemctl daemon-reload
sudo systemctl enable piv2d
sudo systemctl start piv2d
```

---

## Step 7: Verify Installation

```bash
# Check service status
sudo systemctl status piv2d

# Check blockchain info
~/PIV2-Core/src/piv2-cli -testnet getblockchaininfo

# Check network info
~/PIV2-Core/src/piv2-cli -testnet getnetworkinfo

# Check PIV2 state
~/PIV2-Core/src/piv2-cli -testnet getpiv2state
```

---

## Useful Commands

### CLI Shortcuts

```bash
# Add to ~/.bashrc
alias piv2-cli='~/PIV2-Core/src/piv2-cli -testnet'
alias piv2d='~/PIV2-Core/src/piv2d'

# Reload
source ~/.bashrc
```

### Common Operations

```bash
# Get blockchain info
piv2-cli getblockchaininfo

# Get network info
piv2-cli getnetworkinfo

# Get PIV2/KHU state
piv2-cli getpiv2state

# List masternodes
piv2-cli listmasternodes

# Get masternode count
piv2-cli getmasternodecount

# Get wallet balance
piv2-cli getbalance

# Generate new address
piv2-cli getnewaddress

# Stop daemon
piv2-cli stop
```

### Log Monitoring

```bash
# Follow debug log
tail -f ~/.piv2/testnet/debug.log

# Search for errors
grep -i error ~/.piv2/testnet/debug.log

# Search for masternode activity
grep -i masternode ~/.piv2/testnet/debug.log
```

---

## Masternode Setup

### 1. Enable Masternode Mode

Edit `~/.piv2/piv2.conf`:

```ini
masternode=1
```

### 2. Get Masternode Info

After starting the daemon:

```bash
# Get operator public key
piv2-cli getnewaddress "mn-operator"
piv2-cli validateaddress <address>
# Note the "pubkey" field
```

### 3. Register Masternode

On a node with funds (10,000 PIV2 collateral):

```bash
# Register masternode (from funding wallet)
piv2-cli protx_register_fund \
    "<collateral_address>" \
    "<mn_ip>:27171" \
    "<owner_address>" \
    "<operator_pubkey>" \
    "<voting_address>" \
    0 \
    "<payout_address>"
```

### 4. Initialize on MN

```bash
# Set operator key on masternode VPS
piv2-cli initmasternode "<operator_privkey>"

# Restart
sudo systemctl restart piv2d
```

### 5. Verify

```bash
# Check status
piv2-cli getmasternodestatus

# List all masternodes
piv2-cli listmasternodes
```

---

## Network Ports

| Network | P2P Port | RPC Port |
|---------|----------|----------|
| Mainnet | 51472    | 51473    |
| Testnet | 27171    | 27172    |
| Regtest | 19501    | 19500    |

---

## Troubleshooting

### Build Fails: Berkeley DB (undefined reference to `Dbt::~Dbt()`)

If you see linker errors like:
```
undefined reference to `Dbt::~Dbt()'
undefined reference to `Db::~Db()'
collect2: error: ld returned 1 exit status
```

This means BerkeleyDB is not properly linked. Fix:

```bash
# 1. Install BDB 5.3 (the only version available on Ubuntu 24.04+)
sudo apt install -y libdb5.3-dev libdb5.3++-dev

# 2. Clean and reconfigure with --with-incompatible-bdb flag
make clean
./configure --without-gui --with-incompatible-bdb
make -j$(nproc)
```

**Why `--with-incompatible-bdb`?**
- Ubuntu 24.04+ only provides BerkeleyDB 5.3, not 4.8
- The `--with-incompatible-bdb` flag tells the build system to accept BDB 5.3
- The wallet will work normally, but won't be portable to systems using BDB 4.8
- For testnet nodes, this is perfectly fine

### Build Fails: Boost Not Found

```bash
# Install all boost components
sudo apt install libboost-all-dev
```

### Build Fails: Cargo/Rust Not Found

If you see error: `/bin/bash: line 1: build: command not found` at `cargo-build`:

```bash
# Install Rust if not installed
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
source ~/.cargo/env

# IMPORTANT: Reconfigure to detect cargo
make clean
./configure --without-gui --with-incompatible-bdb
make -j$(nproc)
```

The key is to run `./configure` AFTER installing Rust so it detects `cargo`.

### Build Fails: Leveldb builder.cc Not Found

If `autogen.sh` didn't fix leveldb automatically:

```bash
# Manual fix
rm -rf src/leveldb
git clone --depth 1 --branch 1.22 https://github.com/google/leveldb.git src/leveldb

# Then rebuild
./autogen.sh
./configure --without-gui --with-incompatible-bdb
make -j$(nproc)
```

### Daemon Won't Start

```bash
# Check logs
tail -100 ~/.piv2/testnet/debug.log

# Check if already running
pgrep -a piv2d

# Kill if stuck
pkill -9 piv2d
```

### Connection Issues

```bash
# Check firewall
sudo ufw status

# Check if port is listening
ss -tlnp | grep 27171

# Check peers
piv2-cli getpeerinfo
```

### Sync Issues

```bash
# Get current block height
piv2-cli getblockcount

# Check if syncing
piv2-cli getblockchaininfo | grep -E "blocks|headers|verificationprogress"

# Add peer manually
piv2-cli addnode "<ip>:27171" "add"
```

---

## Testnet Seed Nodes

Add to `piv2.conf` for faster initial sync:

```ini
# Seed nodes (update with actual IPs after deployment)
# addnode=mn1.piv2.testnet:27171
# addnode=mn2.piv2.testnet:27171
# addnode=mn3.piv2.testnet:27171
```

---

## Updates

### Update PIV2

```bash
# Stop daemon
piv2-cli stop
sleep 5

# Update code
cd ~/PIV2-Core
git pull origin main

# Rebuild
make -j$(nproc)

# Restart
sudo systemctl start piv2d
```

---

## Support

- GitHub: https://github.com/AdonisPhusis/PIVX-V2-PROPOSAL
- Issues: https://github.com/AdonisPhusis/PIVX-V2-PROPOSAL/issues

---

*PIV2-Core Testnet Installation Guide - v0.9.0*
