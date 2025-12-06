# PIV2 Testnet Installation Guide

This document describes how to set up and operate the PIV2 testnet network.

## Overview

PIV2 uses a pure MN-only consensus (no PoS). The testnet consists of:
- **1 Seed Node**: For peer discovery, explorer, and faucet
- **4 Masternodes (MN1-MN4)**: Block producers with DMM scheduler

Genesis masternodes are defined in consensus parameters - no ProRegTx is needed.

## Architecture

```
                    ┌─────────────────┐
                    │   Seed Node     │
                    │  57.131.33.151  │
                    │  (No MN config) │
                    └────────┬────────┘
                             │
        ┌────────────────────┼────────────────────┐
        │                    │                    │
┌───────┴───────┐   ┌───────┴───────┐   ┌───────┴───────┐
│     MN1       │   │     MN2       │   │     MN3       │
│ 162.19.251.75 │   │ 57.131.33.152 │   │ 57.131.33.214 │
└───────────────┘   └───────────────┘   └───────────────┘
                             │
                    ┌───────┴───────┐
                    │     MN4       │
                    │  51.75.31.44  │
                    └───────────────┘
```

## Quick Start

### 1. Bootstrap a Fresh Testnet

```bash
# Create bootstrap data (block 1 with premine + MN collaterals)
DATADIR=/tmp/piv2_bootstrap ./contrib/testnet/bootstrap_testnet.sh full

# Create tarball for deployment
cd /tmp/piv2_bootstrap/testnet5
tar czvf ~/bootstrap.tar.gz blocks chainstate evodb
```

### 2. Deploy to VPS Nodes

```bash
# Deploy binaries and restart all nodes
./contrib/testnet/deploy_to_vps.sh --clean

# Or with git pull + make on compile nodes
./contrib/testnet/deploy_to_vps.sh --clean --compile
```

### 3. Check Network Status

```bash
./contrib/testnet/deploy_to_vps.sh --status
```

## VPS Node Configuration

### Seed Node (57.131.33.151)

```ini
# ~/.piv2/piv2.conf
testnet=1
server=1
rpcuser=testuser
rpcpassword=testpass123
rpcallowip=127.0.0.1
listen=1
# No masternode config - just a peer discovery node

[test]
addnode=162.19.251.75:27171
addnode=57.131.33.152:27171
addnode=57.131.33.214:27171
addnode=51.75.31.44:27171
```

### Masternode Nodes (MN1-MN4)

Each masternode needs:

```ini
# ~/.piv2/piv2.conf
testnet=1
server=1
rpcuser=testuser
rpcpassword=testpass123
rpcallowip=127.0.0.1
listen=1
masternode=1
mnoperatorprivatekey=<OPERATOR_SECRET>

[test]
addnode=57.131.33.151:27171
addnode=<OTHER_MN_IPS>:27171
```

### Operator Private Keys (TESTNET ONLY)

| Node | Owner Address | Operator Secret |
|------|--------------|-----------------|
| MN1 | y7L1LfAfdSbMCu9qvvEYd9LHq97FqUPeaM | cMe84ZuQPK3cpvZsNWiAJU45KdrfX6FPTSno77tWBAyrHfSbCcAL |
| MN2 | yA3MEDZbpDaPPTUqid6AxAbHd7rjiWvWaN | cSbkaQuj1ViyoVPFxyckxa6xZGsipCx2itK8YHyGzktiAchtPtt6 |
| MN3 | yAi9Rhh4W7e7SnQ5FkdL2bDS5dDDSLiK9r | cUuP9odQzC4QDVxCACDgNMMWSPvzMXtgfYACsxiNaY9R7GsyGNsD |
| MN4 | (4th genesis MN) | cQTtGk5s17H1cQsoji42sw8ACChxW1Q8debYu4gk1GkyFpR7yog6 |

## Block Production (DMM)

PIV2 uses Deterministic Masternode Miner (DMM) for block production:

- **Block Interval**: ~60 seconds
- **Producer Selection**: Hash-based scoring with fallback rotation
- **HU Finality**: 2/3 MN signatures required for finality

### Producer Selection Algorithm

```
score(MN) = SHA256(prevBlockHash || height || proTxHash)
```

The MN with the highest score is the primary producer. If they don't produce within the leader timeout, fallback producers take over.

### Consensus Parameters

| Parameter | Value | Description |
|-----------|-------|-------------|
| nTimeSlotLength | 15s | Block timestamp granularity |
| nHuLeaderTimeoutSeconds | 30s | Primary producer timeout |
| nHuFallbackRecoverySeconds | 10s | Window per fallback producer |
| nHuQuorumSize | 2/3 | Signatures needed for finality |

## Scripts Reference

### `bootstrap_testnet.sh`

Creates a fresh testnet from genesis.

```bash
# Full bootstrap
DATADIR=/tmp/piv2_test ./contrib/testnet/bootstrap_testnet.sh full

# Check status
DATADIR=/tmp/piv2_test ./contrib/testnet/bootstrap_testnet.sh status
```

### `deploy_to_vps.sh`

Deploys binaries to all VPS nodes.

```bash
# Deploy with clean restart
./contrib/testnet/deploy_to_vps.sh --clean

# Deploy with reindex
./contrib/testnet/deploy_to_vps.sh --clean --reindex

# Update compile nodes (git pull + make)
./contrib/testnet/deploy_to_vps.sh --compile

# Check status only
./contrib/testnet/deploy_to_vps.sh --status

# Stop all daemons
./contrib/testnet/deploy_to_vps.sh --stop
```

## Troubleshooting

### Nodes Not Producing Blocks

1. Check MN sync status:
   ```bash
   piv2-cli -testnet mnsync status
   ```

2. Check DMM scheduler logs:
   ```bash
   grep "DMM-SCHEDULER" ~/.piv2/testnet5/debug.log | tail -20
   ```

3. Verify MN is in the active list:
   ```bash
   piv2-cli -testnet getmasternodecount
   piv2-cli -testnet listmasternodes
   ```

### Sync Issues

1. Verify peers are connected:
   ```bash
   piv2-cli -testnet getpeerinfo | jq 'length'
   ```

2. Add peers manually:
   ```bash
   piv2-cli -testnet addnode "57.131.33.151:27171" "onetry"
   ```

3. Check for chain split (compare block hashes):
   ```bash
   piv2-cli -testnet getblockhash 6
   ```

### Signature Verification Failures

Check the block producer slot calculation in logs:
```bash
grep "GetExpectedProducer\|GetProducerSlot" ~/.piv2/testnet5/debug.log | tail -20
```

## Premine Distribution

Block 1 creates the following outputs:

| Recipient | Amount | Purpose |
|-----------|--------|---------|
| Dev Wallet | 98,850,000 PIV2 | Development/Testing |
| MN1 Owner | 10,000 PIV2 | Collateral |
| MN2 Owner | 10,000 PIV2 | Collateral |
| MN3 Owner | 10,000 PIV2 | Collateral |

**Dev Wallet Private Key (TESTNET ONLY)**:
```
cUHWixpfkqEXpCC2jJHPeTPsXvP3h9m1FtDEH4XJ8RHkKFnqWuGE
```

## SSH Access

All VPS nodes use SSH key authentication:
```bash
SSH_KEY=~/.ssh/id_ed25519_vps
ssh -i $SSH_KEY ubuntu@<IP>
```

## See Also

- [BLUEPRINT_DMM_HU.md](./BLUEPRINT_DMM_HU.md) - DMM and HU Finality architecture (includes nDMMBootstrapHeight documentation)
- [01-ARCHITECTURE.md](./01-ARCHITECTURE.md) - Overall PIV2 architecture
