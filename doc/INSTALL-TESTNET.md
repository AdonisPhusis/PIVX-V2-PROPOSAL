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

# === STEP 3: BerkeleyDB 4.8 ===
cd /tmp
wget http://download.oracle.com/berkeley-db/db-4.8.30.NC.tar.gz
tar -xzf db-4.8.30.NC.tar.gz && cd db-4.8.30.NC
sed -i 's/__atomic_compare_exchange/__atomic_compare_exchange_db/g' dbinc/atomic.h
cd build_unix
../dist/configure --enable-cxx --disable-shared --with-pic --with-mutex=POSIX/pthreads/library --prefix=/usr/local/BerkeleyDB.4.8
make -j$(nproc) && sudo make install

# === STEP 4: PIV2 Core ===
cd ~
git clone https://github.com/AdonisPhusis/PIVX-V2-PROPOSAL.git PIV2-Core
cd PIV2-Core
./autogen.sh
./configure --without-gui
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

## Step 3: Install BerkeleyDB 4.8

```bash
cd /tmp
wget http://download.oracle.com/berkeley-db/db-4.8.30.NC.tar.gz
tar -xzf db-4.8.30.NC.tar.gz
cd db-4.8.30.NC

# Patch for modern GCC
sed -i 's/__atomic_compare_exchange/__atomic_compare_exchange_db/g' dbinc/atomic.h

# Build & Install
cd build_unix
../dist/configure --enable-cxx --disable-shared --with-pic --with-mutex=POSIX/pthreads/library --prefix=/usr/local/BerkeleyDB.4.8
make -j$(nproc)
sudo make install

# Verify
ls /usr/local/BerkeleyDB.4.8/lib/libdb_cxx*
```

---

## Step 4: Build PIV2

```bash
cd ~
git clone https://github.com/AdonisPhusis/PIVX-V2-PROPOSAL.git PIV2-Core
cd PIV2-Core
./autogen.sh
./configure --without-gui
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

echo "testnet=1
server=1
daemon=1
listen=1
rpcuser=piv2user
rpcpassword=$(openssl rand -hex 32)
rpcport=27172
rpcallowip=127.0.0.1
port=27171
logtimestamps=1" > ~/.piv2/piv2.conf
```

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

---

## Support

- GitHub: https://github.com/AdonisPhusis/PIVX-V2-PROPOSAL
- Issues: https://github.com/AdonisPhusis/PIVX-V2-PROPOSAL/issues

---

*PIV2 Testnet Installation Guide v1.1.0*
