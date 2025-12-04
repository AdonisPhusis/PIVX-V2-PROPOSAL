# PIV2 Public Testnet

**Version:** 0.1
**Genesis Date:** December 2025
**Status:** ACTIVE

---

## Overview

PIV2 (PIVX V2 / Knowledge Hedge Unit) is a cryptocurrency with:
- **DMM Consensus**: Deterministic Masternode Management (no PoW/PoS)
- **Zero Block Reward**: All inflation via R% yield on ZKHU (shielded staking)
- **KHU State Machine**: MINT → LOCK → UNLOCK → REDEEM operations
- **3 MN Quorum**: 2/3 threshold for block finality

---

## Network Parameters

| Parameter | Value |
|-----------|-------|
| **Chain ID** | `piv2-testnet` |
| **P2P Port** | `27171` |
| **RPC Port** | `27172` |
| **Magic Bytes** | `0xfa 0xbf 0xb5 0xda` |
| **Address Prefix** | `y` (testnet) |
| **Block Time** | 60 seconds |
| **MN Collateral** | 10,000 PIV2 |

---

## Genesis Block

```
Hash:        000001a025bee548de2afe598046e04dfbffd26180207558b65104c4cc7b626d
Merkle Root: f14fac7a43eff3c44336a76109ac95717075785e4c48c496c384f8aa3198b5a3
nNonce:      575173
nTime:       1733270400 (Dec 4, 2025)
nBits:       0x1e0ffff0
```

### Genesis Distribution (100,030,000 PIV2)

| Output | Amount | Purpose |
|--------|--------|---------|
| 0 | 10,000 PIV2 | MN1 Collateral |
| 1 | 10,000 PIV2 | MN2 Collateral |
| 2 | 10,000 PIV2 | MN3 Collateral |
| 3 | 50,000,000 PIV2 | Development |
| 4 | 50,000,000 PIV2 | Faucet |

---

## Quick Start

### 1. Download Binaries

```bash
# Build from source
git clone https://github.com/AdonisPhusis/PIVX-V2-PROPOSAL
cd PIV2-Core
./autogen.sh
./configure --without-gui --with-incompatible-bdb
make -j$(nproc)
```

### 2. Create Configuration

```bash
mkdir -p ~/.piv2
cat > ~/.piv2/piv2.conf << 'EOF'
# PIV2 Testnet Configuration
testnet=1
server=1
daemon=1

# RPC
rpcuser=piv2user
rpcpassword=YOUR_SECURE_PASSWORD
rpcport=27172
rpcallowip=127.0.0.1

# P2P
port=27171

# Logging
debug=0
printtoconsole=0

# Performance
maxconnections=32
dbcache=256
EOF
```

### 3. Start Node

```bash
# Start daemon
./src/piv2d -testnet

# Check status
./src/piv2-cli -testnet getblockchaininfo
./src/piv2-cli -testnet getpiv2state
```

---

## RPC Commands

### PIV2/KHU State

```bash
# Get global KHU state (C, U, Z, Cr, Ur, T, R%)
piv2-cli -testnet getpiv2state

# Alias
piv2-cli -testnet getkhustate
```

### KHU Operations (when implemented)

```bash
# MINT: PIV → HU (transparent)
piv2-cli -testnet piv2mint <amount>

# LOCK: HU → ZKHU (shielded staking)
piv2-cli -testnet piv2lock <amount>

# UNLOCK: ZKHU → HU + yield
piv2-cli -testnet piv2unlock

# REDEEM: HU → PIV
piv2-cli -testnet piv2redeem <amount>
```

### Masternode

```bash
# List all masternodes
piv2-cli -testnet listmasternodes

# Get MN count
piv2-cli -testnet getmasternodecount

# Get MN status (if running as MN)
piv2-cli -testnet getmasternodestatus

# Initialize masternode
piv2-cli -testnet initmasternode "<operator_privkey>"
```

### Wallet

```bash
# Get balance
piv2-cli -testnet piv2balance

# List unspent
piv2-cli -testnet piv2listunspent

# Send PIV2
piv2-cli -testnet piv2send "<address>" <amount>
```

---

## Running a Masternode

### Requirements

- 10,000 PIV2 collateral
- Public IP address
- VPS with 2GB+ RAM

### Setup

1. **Get collateral from faucet** (testnet only)

2. **Create ProRegTx**
```bash
# Generate operator key
piv2-cli -testnet getnewaddress "mn_operator"
piv2-cli -testnet dumpprivkey "<operator_address>"

# Register masternode
piv2-cli -testnet protx_register_fund \
  "<collateral_address>" \
  "<ip>:27171" \
  "<owner_address>" \
  "<operator_pubkey>" \
  "<voting_address>" \
  0 \
  "<payout_address>"
```

3. **Start masternode daemon**
```bash
piv2d -testnet -masternode=1
piv2-cli -testnet initmasternode "<operator_privkey>"
```

4. **Verify**
```bash
piv2-cli -testnet getmasternodestatus
piv2-cli -testnet listmasternodes
```

---

## Faucet

**Testnet faucet will be available at:** (TBD)

For now, contact developers for testnet PIV2.

---

## Monitoring Invariants

The PIV2 state machine must always satisfy:

```
C == U + Z       # Collateral = Transparent + Shielded
Cr == Ur         # Reward pool = Reward rights
T >= 0           # DAO Treasury non-negative
```

Check with:
```bash
piv2-cli -testnet getpiv2state | jq '.invariants_ok'
```

---

## Known Seeds

Seeds will be added once VPS infrastructure is deployed.

For now, use `addnode` in config:
```
addnode=<mn1_ip>:27171
addnode=<mn2_ip>:27171
addnode=<mn3_ip>:27171
```

---

## Red Team / Auditors

We welcome security researchers to:

1. **Test consensus**: Try to produce invalid blocks
2. **Test finality**: Attempt reorgs > 6 blocks
3. **Test KHU operations**: Find edge cases in MINT/LOCK/UNLOCK/REDEEM
4. **Test invariants**: Try to break C == U + Z
5. **Test DMM**: Attempt to become block producer without being in quorum

Report issues to: (TBD)

---

## Test Suite

Run the pre-testnet validation suite:

```bash
# Quick mode (faster, fewer blocks)
./contrib/testnet/test_suite_section1.sh --quick

# Full mode
./contrib/testnet/test_suite_section1.sh
```

### Test Scenarios

1. **DMM Long-Run**: 50+ blocks with 3 MN quorum
2. **MN Offline/Recovery**: MN goes offline, network continues
3. **Reorg/Finality**: Test 6-block reorg limit
4. **KHU Pipeline**: MINT → LOCK → UNLOCK → REDEEM cycle
5. **Invariants**: Verify C == U + Z after all operations

---

## Support

- **GitHub Issues**: https://github.com/AdonisPhusis/PIVX-V2-PROPOSAL/issues
- **Documentation**: `/docs/` directory

---

*PIV2 Testnet v0.1 - December 2025*
