# PIV2 Chain - Launch Guide: Regtest & Testnet

**Version:** 1.0
**Date:** 2025-12-03
**Status:** READY FOR USE

---

## Overview

This document describes how to launch PIV2 Chain in different network modes:

| Mode | Purpose | Block Production | Keys |
|------|---------|------------------|------|
| **Regtest** | Local development & testing | `generate` RPC | Dynamic (/tmp) |
| **Testnet** | Public testing network | DMM (automatic) | Persistent |
| **Mainnet** | Production | DMM (automatic) | Secure storage |

---

## 1. REGTEST - Local Development

### Quick Start

```bash
# Build
cd PIVX && make -j$(nproc)

# Start 4 nodes + 3 masternodes
./src/testnet_local.sh start --mn

# Check status
./src/testnet_local.sh status

# Run tests
./src/testnet_local.sh test

# Mine blocks
./src/testnet_local.sh mine 100

# Stop all nodes
./src/testnet_local.sh stop

# Clean all data
./src/testnet_local.sh clean
```

### What Happens

1. **Key Generation** (first run only):
   - Starts temporary node on port 19599
   - Generates 3 operator keypairs via RPC
   - Saves to `/tmp/hu_testnet/operator_keys.json` (never committed)
   - Stops temporary node

2. **Node Startup**:
   - Node 0: Primary (miner, RPC:19500, P2P:19600)
   - Node 1: Masternode 1 (RPC:19501, P2P:19601)
   - Node 2: Masternode 2 (RPC:19502, P2P:19602)
   - Node 3: Masternode 3 (RPC:19503, P2P:19603)

3. **Masternode Setup** (with `--mn`):
   - Mines 11 blocks for coinbase maturity
   - Registers 3 MN via `protx_register_fund`
   - Each MN gets 10000 PIV2 collateral
   - Saves config to `/tmp/hu_testnet/mn_config.txt`

### CLI Commands

```bash
# Query any node
./src/testnet_local.sh cli 0 getblockchaininfo
./src/testnet_local.sh cli 1 getmininginfo

# List masternodes
./src/testnet_local.sh cli 0 listmasternodes

# Send between nodes
ADDR=$(./src/testnet_local.sh cli 1 getnewaddress)
./src/testnet_local.sh cli 0 sendtoaddress "$ADDR" 100
./src/testnet_local.sh mine 1
```

### Data Directories

```
/tmp/hu_testnet/
├── node0/              # Primary node data
├── node1/              # MN1 data
├── node2/              # MN2 data
├── node3/              # MN3 data
├── operator_keys.json  # Generated operator keys (not versioned)
└── mn_config.txt       # MN registration info
```

---

## 2. TESTNET - Public Test Network

### Testnet Genesis (v1 - FINAL)

The testnet genesis was mined on 2025-12-03 with the following parameters:

```
Network ID:   hu-testnet
Timestamp:    1733270400 (Dec 2025)
nNonce:       69256
Genesis Hash: 00000ccd2bb9199976393bdf55f29163ee853fc3cb7dd67e79b838dbfdacfb39
Merkle Root:  c6d69a493603f520dcb27b517cff152db034ff337055677ae3b9df915f100315
```

**Genesis Distribution (100,030,000 PIV2 total):**

| Output | Role | Amount PIV2 | pubKeyHash |
|--------|------|-----------|------------|
| 0 | MN1 Collateral | 10,000 | `687cdc5d06a4ed81b41f4a694ccca26c2b7e66fe` |
| 1 | MN2 Collateral | 10,000 | `f839118dfb45fbd9983833ac52bf5d27047f4d35` |
| 2 | MN3 Collateral | 10,000 | `64497f7f2fe10ff124ec66476ddfd4679d233140` |
| 3 | Dev Wallet | 50,000,000 | `5b1eed0b253e251f2d6e5f592aeac3b891d6d3dd` |
| 4 | Faucet | 50,000,000 | `2771c5c0e59bf7387251eef18c84532551a99296` |

**Private keys:** Stored securely in `/tmp/hu_testnet_genesis_keys.json` (never in repo!)

### Generating New Testnet Keys (if needed)

```bash
# Generate new keys (creates /tmp/hu_testnet_genesis_keys.json)
./src/generate_testnet_genesis_keys.sh

# The script outputs pubKeyHash values to update in chainparams.cpp
# After updating, recompile and the new genesis will be mined on first run
```

### Prerequisites

- Compiled `hud` and `hu-cli` binaries
- Network connectivity
- Seed node addresses (see below)

### Configuration File

Create `~/.huchain/hu.conf`:

```ini
# Network
testnet=1

# RPC
rpcuser=<your-user>
rpcpassword=<your-password>
rpcallowip=127.0.0.1

# Connections
addnode=<seed1-ip>:51474
addnode=<seed2-ip>:51474

# Optional: Run as masternode
# masternode=1
# mnoperatorprivatekey=<your-operator-privkey>
```

### Starting a Node

```bash
# Start daemon
./hud -testnet -daemon

# Check status
./hu-cli -testnet getblockchaininfo

# Stop daemon
./hu-cli -testnet stop
```

### Testnet Parameters

| Parameter | Value |
|-----------|-------|
| Network ID | `hu-testnet` |
| Default Port | 51474 |
| RPC Port | 51475 |
| Magic Bytes | `0xf5, 0xe6, 0xd5, 0xca` |
| Quorum Size | 3 |
| Quorum Threshold | 2 |
| Block Time | 60 seconds |
| Block Reward | 0 PIV2 |
| MN Collateral | 10,000 PIV2 |
| Max Money | 100,030,000 PIV2 |

---

## 3. Differences: Regtest vs Testnet

| Aspect | Regtest | Testnet |
|--------|---------|---------|
| Block production | Manual (`generate`) | DMM automatic |
| Genesis | Local genesis | Shared genesis |
| Difficulty | Fixed (1) | Adjusting |
| Connections | Local only (127.0.0.1) | Public network |
| Persistence | `/tmp/` (temporary) | `~/.huchain/` |
| Purpose | Development, unit tests | Integration, public testing |

---

## 4. Troubleshooting

### Regtest Issues

**"Genesis coinbase not spendable"**
- Normal behavior - Bitcoin design
- Mine 11+ blocks for mature funds

**"Node failed to start"**
- Check if port already in use: `lsof -i :19500`
- Kill orphan processes: `pkill -9 hud`
- Clean data: `./testnet_local.sh clean`

**"Masternode registration failed"**
- Ensure sufficient balance (30100+ PIV2)
- Check operator pubkey format (33 bytes compressed)
- Mine blocks between registrations

### Testnet Issues

**"Connection refused"**
- Check firewall allows port 51474
- Verify seed nodes are online
- Check `hu.conf` syntax

**"Blocks not syncing"**
- Wait for peer connections (getconnectioncount)
- Check debug.log for errors
- Verify genesis hash matches

---

## 5. Security Reminders

```
NEVER commit private keys to the repository
NEVER use production keys on testnet
ALWAYS generate keys dynamically for regtest
ALWAYS use secure storage for mainnet keys
```

See `CLAUDE.md` section "Sécurité — PRIVATE KEYS" for details.

---

## 6. Next Steps

After mastering regtest:

1. **Unit Tests**: `./src/test/test_pivx --run_test=hu_*`
2. **Regtest DMM**: Observe masternode rotation
3. **Testnet**: Join public testnet with real DMM
4. **Mainnet**: Production deployment

---

*LAUNCH-REGTEST-AND-TESTNET.md v1.0*
