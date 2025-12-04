# PIV2 Testnet - Installation Guide

**Version:** 1.1.0
**Target:** Ubuntu 24.04 / 25.04 LTS
**Date:** December 2025

---

## Quick Start

Copy-paste this entire block on a fresh Ubuntu 24.04+ VPS:

```bash
# === STEP 1: Dependencies ===
sudo apt update && sudo apt install -y \
    build-essential libtool autotools-dev automake autoconf pkg-config \
    autoconf-archive bsdmainutils python3 git curl wget \
    libevent-dev libboost-all-dev libssl-dev libzmq3-dev \
    libminiupnpc-dev libsodium-dev libgmp-dev

# === STEP 2: Rust ===
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
source ~/.cargo/env

# === STEP 3: BerkeleyDB 5.3 ===
sudo apt install -y libdb5.3++-dev

# === STEP 4: PIV2 Core ===
cd ~
git clone https://github.com/AdonisPhusis/PIVX-V2-PROPOSAL.git PIV2-Core
cd PIV2-Core
./autogen.sh
./configure --without-gui --with-incompatible-bdb
make -j$(nproc)

# === STEP 5: Sapling Parameters ===
./params/install-params.sh

# === STEP 6: Verify ===
./src/piv2d --version
```

---

## Prerequisites

- Ubuntu 24.04 or 25.04 LTS
- 2GB RAM minimum (4GB recommended)
- 5GB disk space

---

## Step 1: Install Dependencies

```bash
sudo apt update && sudo apt install -y \
    build-essential libtool autotools-dev automake autoconf pkg-config \
    autoconf-archive bsdmainutils python3 git curl wget \
    libevent-dev libboost-all-dev libssl-dev libzmq3-dev \
    libminiupnpc-dev libsodium-dev libgmp-dev
```

---

## Step 2: Install Rust

```bash
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
source ~/.cargo/env
cargo --version
```

---

## Step 3: Install BerkeleyDB 5.3

```bash
sudo apt install -y libdb5.3++-dev
```

**Note:** BDB 5.3 is recommended. BDB 4.8 has wallet compatibility issues on Ubuntu 24.04+.

---

## Step 4: Build PIV2

```bash
cd ~
git clone https://github.com/AdonisPhusis/PIVX-V2-PROPOSAL.git PIV2-Core
cd PIV2-Core
./autogen.sh
./configure --without-gui --with-incompatible-bdb
make -j$(nproc)
```

---

## Step 5: Sapling Parameters

```bash
./params/install-params.sh
```

---

## Step 6: Configure

```bash
mkdir -p ~/.piv2

# Generate RPC password
RPC_PASS=$(openssl rand -hex 32)

# Create config with [test] section for testnet-specific settings
echo "testnet=1
server=1
daemon=1
listen=1
logtimestamps=1
txindex=1

[test]
port=27171
rpcport=27172
rpcuser=piv2user
rpcpassword=$RPC_PASS
rpcallowip=127.0.0.1" > ~/.piv2/piv2.conf
```

**Note:** The `[test]` section is required for testnet-specific settings like port and rpcport.

---

## Step 7: Run

```bash
# Start daemon
./src/piv2d -testnet -daemon

# Check status
./src/piv2-cli -testnet getblockchaininfo

# Stop daemon
./src/piv2-cli -testnet stop
```

---

## Step 8: Systemd Service (Optional)

```bash
sudo tee /etc/systemd/system/piv2d.service << 'EOF'
[Unit]
Description=PIV2 Testnet Daemon
After=network.target

[Service]
Type=forking
User=ubuntu
ExecStart=/home/ubuntu/PIV2-Core/src/piv2d -daemon
ExecStop=/home/ubuntu/PIV2-Core/src/piv2-cli stop
Restart=on-failure
RestartSec=30

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable piv2d
sudo systemctl start piv2d
```

---

## Masternode Setup

### Step 1: Generate Masternode Key

```bash
./src/piv2-cli -testnet createmasternodekey
```

Save the output - this is your `masternodeprivkey`.

### Step 2: Update Configuration

Add to your `~/.piv2/piv2.conf` in the `[test]` section:

```ini
[test]
# ... existing settings ...
externalip=YOUR_VPS_IP
masternode=1
masternodeprivkey=YOUR_KEY_FROM_STEP_1
```

### Step 3: Add Peer Nodes

Add other testnet nodes to connect:

```ini
[test]
# ... existing settings ...
addnode=PEER_IP_1:27171
addnode=PEER_IP_2:27171
```

### Step 4: Restart Daemon

```bash
./src/piv2-cli -testnet stop
sleep 3
./src/piv2d -daemon
```

### Step 5: Verify Masternode Status

```bash
./src/piv2-cli -testnet getmasternodestatus
./src/piv2-cli -testnet listmasternodes
```

---

## Updates

When a new version is released:

```bash
# Stop node
piv2-cli stop
sleep 5

# Update code
cd ~/PIV2-Core
git pull origin main

# Rebuild
make -j$(nproc)

# Restart
piv2d -testnet -daemon
```

---

## Useful Commands

```bash
# Aliases (add to ~/.bashrc)
alias piv2d='~/PIV2-Core/src/piv2d'
alias piv2-cli='~/PIV2-Core/src/piv2-cli -testnet'

# Status
piv2-cli getblockchaininfo
piv2-cli getnetworkinfo
piv2-cli getpeerinfo

# Wallet
piv2-cli getbalance
piv2-cli getnewaddress
piv2-cli listunspent

# Logs
tail -f ~/.piv2/testnet/debug.log
```

---

## Network Ports

| Network | P2P | RPC |
|---------|-----|-----|
| Testnet | 27171 | 27172 |
| Mainnet | 51472 | 51473 |

---

## Troubleshooting

### BDB Mutex Error
```
configure: error: Unable to find a mutex implementation
```
**Fix:** Add `--with-mutex=POSIX/pthreads/library` to BDB configure.

### Rust Not Found
```bash
source ~/.cargo/env
./configure --without-gui
```

### Port Already in Use
```bash
pkill -9 piv2d
```

### Sapling Parameters Missing
```
Error: Cannot find the Sapling parameters in the following directory:
"/home/ubuntu/.pivx-params"
```
**Fix:** Run `./params/install-params.sh`

### Block Database Corrupted
```
ERROR: LoadBlockIndexGuts : failed to read value
Error loading block database
```
**Fix:** Remove corrupted data and reindex:
```bash
rm -rf ~/.piv2/testnet5/blocks ~/.piv2/testnet5/chainstate ~/.piv2/testnet5/evodb
./src/piv2d -reindex
```

### Port Settings Warning
```
Warning: Config setting for -port only applied on test network when in [test] section.
```
**Fix:** Put port/rpcport settings inside `[test]` section in piv2.conf (see Step 6).

### BDB 4.8 Wallet Crash (HD Wallet Creation)
```
DeriveNewSeed: AddKeyPubKey failed
Segmentation fault
```
**Fix:** Use BDB 5.3 instead of 4.8:
```bash
sudo apt install libdb5.3++-dev
./configure --without-gui --with-incompatible-bdb
make clean && make -j$(nproc)
rm -rf ~/.piv2/testnet5/wallet.dat ~/.piv2/testnet5/database
```

---

## Support

- GitHub: https://github.com/AdonisPhusis/PIVX-V2-PROPOSAL
- Issues: https://github.com/AdonisPhusis/PIVX-V2-PROPOSAL/issues

---

*PIV2 Testnet Installation Guide v1.1.0*
