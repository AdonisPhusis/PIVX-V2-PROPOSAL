# PIV2-Core GitHub Setup Guide

**Date:** December 2025
**Version:** 0.1.0

---

## Overview

This guide explains how to push the PIV2-Core repository to GitHub.

The repository is located at: `/home/ubuntu/PIV2-Core`

---

## Prerequisites

1. A GitHub account
2. SSH key configured on GitHub (or HTTPS token)
3. Git installed locally

---

## Step 1: Create GitHub Repository

1. Go to https://github.com/new
2. Repository name: `PIV2-Core` (or `PIVX-V2-Core`)
3. Description: "PIV2 Core - PIVX V2 with DMM Consensus and KHU State Machine"
4. **DO NOT** initialize with README (we already have one)
5. **DO NOT** add .gitignore (we have one)
6. Click "Create repository"

---

## Step 2: Add Remote and Push

```bash
cd /home/ubuntu/PIV2-Core

# Verify we're on main branch with clean commit
git status
git log --oneline -3

# Add GitHub remote (replace YOUR_USERNAME)
git remote add origin git@github.com:YOUR_USERNAME/PIV2-Core.git

# Or use HTTPS:
# git remote add origin https://github.com/YOUR_USERNAME/PIV2-Core.git

# Push to GitHub
git push -u origin main
```

---

## Step 3: Verify

After pushing, verify on GitHub:
- [ ] Initial commit visible
- [ ] `src/` directory present
- [ ] `doc/` directory with documentation
- [ ] `contrib/testnet/` with scripts and configs
- [ ] README.md displayed on repo page

---

## Repository Structure

```
PIV2-Core/
├── src/                          # C++ source code
│   ├── piv2/                     # PIV2/KHU specific code
│   ├── rpc/                      # RPC commands
│   ├── wallet/                   # Wallet code
│   └── ...
├── contrib/
│   ├── genesis/                  # Genesis block tools
│   │   └── genesis_piv2.py
│   └── testnet/                  # Testnet infrastructure
│       ├── test_suite_section1.sh
│       ├── piv2_testnet_deploy.sh
│       └── piv2_testnet_mn*.conf
├── doc/                          # Documentation
│   ├── TESTNET-PIV2-PUBLIC.md
│   ├── GITHUB-SETUP.md
│   ├── 01-ARCHITECTURE.md
│   ├── 02-SPEC.md
│   └── ...
├── test/                         # Test framework
├── README.md
├── LICENSE
└── configure.ac
```

---

## Network Parameters

| Network | P2P Port | RPC Port | Prefix |
|---------|----------|----------|--------|
| Mainnet | 51472    | 51473    | P      |
| Testnet | 27171    | 27172    | y      |
| Regtest | 19501    | 19500    | y      |

---

## Build Instructions

```bash
cd PIV2-Core

# Install dependencies (Ubuntu/Debian)
sudo apt-get update
sudo apt-get install -y build-essential libtool autotools-dev automake \
  pkg-config bsdmainutils python3 libevent-dev libboost-dev \
  libboost-system-dev libboost-filesystem-dev libboost-test-dev \
  libboost-thread-dev libssl-dev libdb5.3-dev libdb5.3++-dev \
  libzmq3-dev libminiupnpc-dev libnatpmp-dev libsodium-dev

# Build
./autogen.sh
./configure --without-gui --with-incompatible-bdb
make -j$(nproc)

# Verify
./src/piv2d --version
./src/piv2-cli --version
```

---

## Test

```bash
# Run test suite
./contrib/testnet/test_suite_section1.sh --quick

# Expected: FULL PASS
```

---

## Next Steps After GitHub Push

1. **Create Releases**:
   ```bash
   git tag -a v0.1.0-testnet -m "PIV2-Core Testnet Release v0.1.0"
   git push origin v0.1.0-testnet
   ```

2. **Deploy Testnet VPS** (3 masternodes):
   ```bash
   # On each VPS:
   ./contrib/testnet/piv2_testnet_deploy.sh install mn1  # or mn2, mn3
   ```

3. **Configure MN IPs** in config files after VPS deployment

4. **Register Masternodes** via ProRegTx

---

## Contact

- **GitHub Issues**: https://github.com/AdonisPhusis/PIVX-V2-PROPOSAL/issues

---

*PIV2-Core - PIVX V2 with DMM Consensus*
