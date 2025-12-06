# BLUEPRINT: DMM + HU Finality Architecture

**Version:** 1.0
**Date:** December 2025
**Status:** Production Ready

---

## 1. Overview

PIV2 uses a two-layer consensus architecture:

1. **DMM (Deterministic Masternode Miner)**: Block production layer
2. **HU Finality**: Block finalization layer (ECDSA 12/8 quorum)

This design is inspired by ETH2/Tendermint separation of concerns:
- Block production continues independently of finality
- Finality provides economic security guarantees
- Neither layer blocks the other

---

## 2. DMM Block Production

### 2.1 Producer Selection

```cpp
score(MN) = SHA256(prevBlockHash || height || proTxHash)
```

MNs are sorted by descending score. The MN with highest score is the primary producer.

### 2.2 Fallback Rotation

If the primary producer doesn't produce within the leader timeout:

| Time Window | Producer |
|-------------|----------|
| `[0, leaderTimeout)` | Primary (slot 0) |
| `[leaderTimeout, leaderTimeout + fallbackWindow)` | Fallback 1 (slot 1) |
| `[leaderTimeout + fallbackWindow, leaderTimeout + 2*fallbackWindow)` | Fallback 2 (slot 2) |
| ... | ... |

```cpp
int GetProducerSlot(const CBlockIndex* pindexPrev, int64_t nBlockTime)
{
    int64_t dt = nBlockTime - pindexPrev->GetBlockTime();

    if (dt < nHuLeaderTimeoutSeconds) return 0;  // Primary

    int64_t extra = dt - nHuLeaderTimeoutSeconds;
    int slot = 1 + (extra / nHuFallbackRecoverySeconds);
    return slot % numMNs;  // Wrap around
}
```

### 2.3 Consensus Parameters

| Parameter | Mainnet | Testnet | Regtest |
|-----------|---------|---------|---------|
| `nHuLeaderTimeoutSeconds` | 45s | 30s | 5s |
| `nHuFallbackRecoverySeconds` | 15s | 10s | 3s |
| `nHuQuorumSize` | 12 | 3 | 1 |
| `nHuQuorumThreshold` | 8 | 2 | 1 |
| `nDMMBootstrapHeight` | 10 | 5 | 2 |

---

## 3. Bootstrap Phase (nDMMBootstrapHeight)

### 3.1 The Cold Start Problem

When syncing a fresh chain from genesis, the genesis block timestamp may be far in the past (days, weeks, or months). Standard fallback slot calculation would produce very high slot numbers, causing:

1. Producer slot = `(now - genesisTime) / fallbackWindow` = potentially 100,000+
2. Modulo wrap-around cycles through all MNs many times
3. Block timestamps would need to align to a slot grid based on ancient genesis time

### 3.2 Bootstrap Solution

During the bootstrap phase (height <= `nDMMBootstrapHeight`):

1. **Producer**: Always primary (`scores[0]`), no fallback slot calculation
2. **nTime**: `max(prevTime + 1, now)` instead of slot-aligned time
3. **Effect**: Blocks are produced immediately without timestamp issues

```cpp
// In GetProducerSlot():
int nextHeight = pindexPrev->nHeight + 1;
if (nextHeight <= consensus.nDMMBootstrapHeight) {
    return 0;  // Always primary during bootstrap
}
```

### 3.3 After Bootstrap

Once `height > nDMMBootstrapHeight`:

1. **Producer**: Calculated via fallback slots based on time elapsed since prev block
2. **nTime**: Aligned to slot grid for deterministic verification
3. **Full DMM rotation**: All MNs can produce via fallback mechanism

The transition is seamless because:
- Bootstrap blocks use simple `prevTime + 1` timestamps
- Post-bootstrap blocks calculate slots relative to their previous block (not genesis)
- No "big jump" in slot calculation occurs

### 3.4 Values Per Network

| Network | nDMMBootstrapHeight | Rationale |
|---------|---------------------|-----------|
| **Testnet** | 5 | Quick bootstrap, allows testing fallback early |
| **Mainnet** | 10 | More blocks before fallback logic kicks in |
| **Regtest** | 2 | Minimal bootstrap for unit tests |

---

## 4. HU Finality Layer

### 4.1 Quorum Selection

At each block height, a quorum of `nHuQuorumSize` MNs is selected deterministically:

```cpp
std::vector<CDeterministicMNCPtr> GetHuQuorum(int nHeight, const CDeterministicMNList& mnList)
{
    // Score all valid MNs
    // Sort by score descending
    // Return top nHuQuorumSize
}
```

Quorum rotates every `nHuQuorumRotationBlocks` blocks.

### 4.2 Signature Collection

1. Block is produced by DMM leader
2. Quorum members sign the block hash (ECDSA)
3. Signatures are broadcast via P2P
4. Block reaches finality when `signatures >= nHuQuorumThreshold`

### 4.3 Finality Persistence

Finality data is stored in a separate LevelDB namespace ('F'):

```
'F' + blockHash → CHuFinality {
    uint256 blockHash;
    int nHeight;
    std::map<uint256, std::vector<unsigned char>> mapSignatures;
}
```

---

## 5. Decoupled Design

### 5.1 Independence

DMM and HU operate independently:

- **DMM doesn't wait for finality** to produce the next block
- **HU doesn't block** if a producer is slow
- **Finality is retroactive**: blocks are valid before finality, economically secure after

### 5.2 Failure Modes

| Scenario | DMM Behavior | HU Behavior |
|----------|--------------|-------------|
| Primary producer offline | Fallback after timeout | Continues signing blocks |
| Quorum member offline | Continues producing | Finality delayed but not blocked |
| Network partition | Each partition produces | Finality helps resolve on merge |

### 5.3 Reorg Protection

Once a block has finality (`>= nHuQuorumThreshold` signatures), it cannot be reorged beyond `nHuMaxReorgDepth` blocks.

---

## 6. Code References

| Component | File | Key Functions |
|-----------|------|---------------|
| Block Producer | `src/evo/blockproducer.cpp` | `GetProducerSlot()`, `GetExpectedProducer()`, `GetBlockProducerWithFallback()` |
| DMM Scheduler | `src/activemasternode.cpp` | `CActiveMasternodeManager::TryProducingBlock()` |
| HU Finality | `src/hu/hu_finality.cpp` | `CHuFinalityHandler::ProcessSignature()` |
| Consensus Params | `src/consensus/params.h` | `nDMMBootstrapHeight`, `nHuLeaderTimeoutSeconds` |
| Signature Verify | `src/evo/blockproducer.cpp` | `VerifyBlockProducerSignature()` |

---

## 7. Verification Formula

Both production and verification use the **same** `GetProducerSlot()` formula:

```
slot = 0                                        if height <= nDMMBootstrapHeight
slot = 0                                        if dt < nHuLeaderTimeoutSeconds
slot = 1 + (dt - nHuLeaderTimeoutSeconds) / nHuFallbackRecoverySeconds   otherwise
```

`GetExpectedProducer()` wraps the slot with modulo:
```cpp
producerIndex = slot % numConfirmedMNs;
expectedMN = sortedScores[producerIndex];
```

This ensures deterministic consensus on all nodes.

---

*BLUEPRINT_DMM_HU.md - DMM + HU Finality Architecture Documentation*
