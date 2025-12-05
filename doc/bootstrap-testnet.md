# PIV2 Testnet/Mainnet Bootstrap Guide

## Overview

This document describes the complete bootstrap process for PIV2 network initialization with DMM (Deterministic Masternode Minting).

## Architecture

PIV2 uses DMM instead of Proof-of-Stake. Block production requires:
- Minimum 3 registered masternodes
- Each masternode needs 10,000 PIV collateral
- Masternodes take turns producing blocks based on deterministic selection

## Prerequisites

### Build Requirements
```bash
# On each VPS/node
cd ~/PIV2-Core
git pull origin main
./autogen.sh
./configure --without-gui
make -j$(nproc)
```

### Network Configuration
- Testnet port: 27171
- Mainnet port: 51472
- RPC port (testnet): 27172
- RPC port (mainnet): 51473

## Bootstrap Workflow

### Phase 1: Genesis and Premine (Block 0-1)

#### Block 0: Genesis
- Automatically created by the network
- Contains no spendable coins

#### Block 1: Premine
Generated via `generatebootstrap` RPC command:
- Output 0: 50,000,000 PIV to Dev wallet
- Output 1: 50,000,000 PIV to Faucet wallet

```bash
# Start bootstrap node
piv2d -testnet -datadir=/path/to/datadir -daemon

# Generate Block 1 (premine)
piv2-cli -testnet generatebootstrap 1
```

### Phase 2: Collateral Setup (Block 2)

#### Import Dev Wallet Key
```bash
# Import the dev wallet private key
piv2-cli -testnet importprivkey "DEV_WALLET_PRIVKEY" "dev_wallet" false

# Rescan to detect coins
piv2-cli -testnet rescanblockchain 0
```

#### Create Collateral Addresses
```bash
# Generate 3 addresses for collaterals
COLL1=$(piv2-cli -testnet getnewaddress "coll_mn1")
COLL2=$(piv2-cli -testnet getnewaddress "coll_mn2")
COLL3=$(piv2-cli -testnet getnewaddress "coll_mn3")
```

#### Send Collaterals via sendmany
```bash
# Single transaction with 3 outputs of 10,000 PIV each
COLL_TXID=$(piv2-cli -testnet sendmany "" "{\"$COLL1\":10000,\"$COLL2\":10000,\"$COLL3\":10000}")
echo "Collateral TXID: $COLL_TXID"
```

#### Confirm Collaterals (Block 2)
```bash
piv2-cli -testnet generatebootstrap 1
```

#### Lock Collateral UTXOs
**CRITICAL**: Lock collaterals to prevent wallet from using them for fees!

```bash
# Find the vout indices for each collateral (check listunspent output)
piv2-cli -testnet listunspent

# Lock all 3 collateral UTXOs (false=lock, true=transparent)
piv2-cli -testnet lockunspent false true '[{"txid":"COLL_TXID","vout":0},{"txid":"COLL_TXID","vout":1},{"txid":"COLL_TXID","vout":3}]'

# Verify locks
piv2-cli -testnet listlockunspent
```

### Phase 3: Masternode Registration (Blocks 3-5)

#### Masternode Key Generation (do once per MN)
For each masternode, generate these keys:
- Owner address (controls MN, votes on proposals)
- Operator public key (signs blocks)
- Voting address
- Payout address

```bash
# Generate operator key pair
piv2-cli -testnet bls_generate

# Output:
# {
#   "public": "02f3ae14dee0a4ba9b1ce436e0cd8e2e30890b509fda174a7d623a39e9bc4acf0d",
#   "secret": "cMe84ZuQPK3cpvZsNWiAJU45KdrfX6FPTSno77tWBAyrHfSbCcAL"
# }
```

#### Register MN1 (Block 3)
```bash
# protx_register collateralHash collateralIndex ipAndPort ownerAddress operatorPubKey votingAddress payoutAddress
piv2-cli -testnet protx_register "$COLL_TXID" 0 "IP1:27171" "$MN1_OWNER" "$MN1_OP_PUBKEY" "$MN1_VOTING" "$MN1_PAYOUT"

# Confirm with block
piv2-cli -testnet generatebootstrap 1
```

#### Register MN2 (Block 4)
```bash
piv2-cli -testnet protx_register "$COLL_TXID" 1 "IP2:27171" "$MN2_OWNER" "$MN2_OP_PUBKEY" "$MN2_VOTING" "$MN2_PAYOUT"
piv2-cli -testnet generatebootstrap 1
```

#### Register MN3 (Block 5) - DMM Activation!
```bash
piv2-cli -testnet protx_register "$COLL_TXID" 3 "IP3:27171" "$MN3_OWNER" "$MN3_OP_PUBKEY" "$MN3_VOTING" "$MN3_PAYOUT"
piv2-cli -testnet generatebootstrap 1
```

### Phase 4: Verify DMM Activation

```bash
# Check masternode count
piv2-cli -testnet getmasternodecount
# Expected: {"total": 3, "enabled": 3, ...}

# List registered masternodes
piv2-cli -testnet protx_list

# Verify generate is disabled (DMM active)
piv2-cli -testnet generate 1
# Expected error: "generate is only available in regtest mode. PIV2 testnet/mainnet uses DMM for block production."
```

## VPS Deployment

### Copy Bootstrap Data to VPS

```bash
# Create archive of bootstrap blocks
cd /tmp/piv2_bootstrap3/testnet5
tar czvf ~/bootstrap_blocks.tar.gz blocks/ chainstate/

# Copy to each VPS
scp ~/bootstrap_blocks.tar.gz ubuntu@VPS_IP:~/
```

### Configure Each VPS

#### VPS 1 (MN1 - 57.131.33.151)
```bash
# piv2.conf
testnet=1
server=1
listen=1
daemon=1
rpcuser=piv2user
rpcpassword=SECURE_PASSWORD
rpcallowip=127.0.0.1
port=27171
rpcport=27172

# Masternode settings
masternode=1
masternodeoperatorprivatekey=MN1_OP_SECRET

# Peers
addnode=57.131.33.152:27171
addnode=57.131.33.214:27171
```

#### VPS 2 (MN2 - 57.131.33.152)
```bash
# Same as VPS1, but with:
masternodeoperatorprivatekey=MN2_OP_SECRET
addnode=57.131.33.151:27171
addnode=57.131.33.214:27171
```

#### VPS 3 (MN3 - 57.131.33.214)
```bash
# Same as VPS1, but with:
masternodeoperatorprivatekey=MN3_OP_SECRET
addnode=57.131.33.151:27171
addnode=57.131.33.152:27171
```

### Start Masternodes

On each VPS:
```bash
# Extract bootstrap data
mkdir -p ~/.piv2/testnet5
cd ~/.piv2/testnet5
tar xzvf ~/bootstrap_blocks.tar.gz

# Start daemon
~/PIV2-Core/src/piv2d -testnet -daemon

# Check sync status
~/PIV2-Core/src/piv2-cli -testnet getblockcount
~/PIV2-Core/src/piv2-cli -testnet getmasternodecount
~/PIV2-Core/src/piv2-cli -testnet masternode status
```

## Troubleshooting

### Common Errors

#### "bad-protx-collateral"
- Collateral UTXO doesn't exist or wrong amount
- Solution: Verify collateral TX is confirmed and has exactly 10,000 PIV

#### "bad-block-sig"
- Block signature validation failed
- Solution: Ensure bootstrap phase extends until 3 MNs registered

#### "Input 0 not found or already spent"
- Wallet used collateral UTXO for fees
- Solution: Lock collateral UTXOs with `lockunspent` before creating ProRegTx

#### "JSON value is not an integer as expected"
- RPC parameter conversion missing
- Solution: Ensure `src/rpc/client.cpp` has proper vRPCConvertParams entries

### Verification Commands

```bash
# Check blockchain state
piv2-cli -testnet getblockchaininfo

# Check masternode list
piv2-cli -testnet protx_list valid

# Check peer connections
piv2-cli -testnet getpeerinfo

# Check mempool
piv2-cli -testnet getrawmempool

# Debug log
tail -f ~/.piv2/testnet5/debug.log
```

## Mainnet Checklist

Before mainnet launch:

1. [ ] Fresh genesis block (new timestamp, nonce)
2. [ ] Update chainparams.cpp with mainnet values
3. [ ] Generate new dev/faucet wallet keys
4. [ ] Generate new MN operator keys for each node
5. [ ] Update DNS seeds
6. [ ] Update checkpoint blocks
7. [ ] Full test of bootstrap workflow on fresh testnet
8. [ ] Verify all 3 MNs can produce blocks
9. [ ] Test block propagation between nodes
10. [ ] Test HU (Hierarchical Unanimity) finalization

## Key Files Modified for Bootstrap

- `src/blockassembler.cpp` - Block 1 premine structure
- `src/blocksignature.cpp` - Bootstrap phase signature validation
- `src/rpc/mining.cpp` - generatebootstrap RPC command
- `src/rpc/client.cpp` - RPC parameter conversions

## Security Notes

- **NEVER** share operator private keys
- **NEVER** commit private keys to git
- Store collateral wallet keys securely (hardware wallet recommended for mainnet)
- Use unique, strong RPC passwords on each node
- Firewall: Only allow P2P port (27171/51472) from public
