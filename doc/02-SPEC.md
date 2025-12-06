# SPEC - PIV2 Technical Specification

**Version:** 2.0
**Date:** December 2025

---

## 0. Nomenclature

| Symbole | Nom | Description |
|---------|-----|-------------|
| **PIV2** | PIV2 | Coin natif transparent de la chain |
| **sHU** | Shielded PIV2 | PIV2 en mode privé (Sapling Z-to-Z) |
| **KHU** | Locked PIV2 | PIV2 locké (préparation au staking) |
| **ZKHU** | Staked KHU | KHU staké avec yield (shielded Sapling) |

### Opérations

| Commande | De → Vers | Description |
|----------|-----------|-------------|
| `shield` | PIV2 → sHU | Rendre privé (Z-to-Z) |
| `unshield` | sHU → PIV2 | Rendre transparent |
| `mint` | PIV2 → KHU | Locker pour préparer staking |
| `redeem` | KHU → PIV2 | Délocker (sortir du lock) |
| `lock` | KHU → ZKHU | Staker (yield commence) |
| `unlock` | ZKHU → KHU + Yield | Récupérer + rendement |

### Flow

```
                    shield
        ┌─────────────────────────┐
        │                         ▼
       PIV2 ◄─────────────────────► sHU
  (transparent)    unshield    (privé Z-to-Z)
        │
        │ mint
        ▼
       KHU ◄────────────────────► ZKHU
   (locké L1)      lock/unlock   (staké + yield)
        │
        │ redeem
        ▼
       PIV2
```

---

## 1. State Variables

```cpp
struct HuGlobalState {
    int64_t C;       // Collateral total (satoshis)
    int64_t U;       // PIV2 transparent supply (satoshis)
    int64_t Z;       // ZKHU staked supply (satoshis)
    int64_t Cr;      // Reward pool (satoshis)
    int64_t Ur;      // Reward rights (satoshis)
    int64_t T;       // DAO Treasury (satoshis)

    uint32_t R_annual;       // Current R% (basis points)
    uint32_t R_next;         // Next R% after reveal
    uint32_t R_MAX_dynamic;  // Plafond R%

    uint32_t nHeight;
    uint256 hashBlock;
    uint256 hashPrevState;
};
```

---

## 2. Invariants

```
INVARIANT_1:  C == U + Z
INVARIANT_2:  Cr == Ur  (ou Ur == 0)
INVARIANT_3:  T >= 0
```

Ces invariants sont vérifiés après chaque bloc.

---

## 3. State Transitions

| Operation | C | U | Z | Cr | Ur | T |
|-----------|---|---|---|----|----|----|
| MINT(a) | +a | +a | | | | |
| REDEEM(a) | -a | -a | | | | |
| LOCK(a) | | -a | +a | | | |
| UNLOCK(P,Y) | +Y | +(P+Y) | -P | -Y | -Y | |
| DAILY_YIELD | | | | +y | +y | +t |

---

## 4. Formulas

### Yield (ZKHU)
```
daily_yield = (principal × R_annual) / 10000 / 365
```

### Treasury T
```
T_daily = (U × R_annual) / 10000 / 8 / 365
```

### R_MAX Decay
```
R_MAX = max(700, 4000 - year × 100)
```

---

## 5. Constants

```cpp
// Timing
const uint32_t BLOCKS_PER_DAY = 1440;
const uint32_t BLOCKS_PER_YEAR = 525600;
const uint32_t MATURITY_BLOCKS = 4320;        // 3 jours
const uint32_t FINALITY_DEPTH = 12;

// DOMC
const uint32_t DOMC_CYCLE_LENGTH = 172800;    // 4 mois
const uint32_t DOMC_COMMIT_OFFSET = 132480;
const uint32_t DOMC_REVEAL_OFFSET = 152640;

// R%
const uint16_t R_INITIAL = 4000;              // 40%
const uint16_t R_FLOOR = 700;                 // 7%
const uint16_t R_DECAY_PER_YEAR = 100;        // -1%/an

// Treasury
const uint16_t T_DIVISOR = 8;

// Consensus (MAINNET defaults)
const int nHuBlockTimeSeconds = 60;
const int nHuQuorumSize = 12;
const int nHuQuorumThreshold = 8;
const int nHuQuorumRotationBlocks = 12;
const int nHuLeaderTimeoutSeconds = 45;
const int nHuMaxReorgDepth = 12;

// Per-network consensus values:
// | Network | QuorumSize | Threshold | Timeout | MaxReorg |
// |---------|------------|-----------|---------|----------|
// | MAINNET |     12     |     8     |   45s   |    12    |
// | TESTNET |      3     |     2     |   30s   |     6    |
// | REGTEST |      1     |     1     |    5s   |   100    |
```

---

## 6. Transaction Types

```cpp
enum TxType : int16_t {
    NORMAL          = 0,
    PROREG          = 1,
    PROUPSERV       = 2,
    PROUPREG        = 3,
    PROUPREV        = 4,
    // SLOT 5 RESERVED
    HU_MINT         = 6,   // PIV2 → KHU
    HU_REDEEM       = 7,   // KHU → PIV2
    HU_LOCK         = 8,   // KHU → ZKHU
    HU_UNLOCK       = 9,   // ZKHU → KHU + Yield
    HU_DOMC_COMMIT  = 10,
    HU_DOMC_REVEAL  = 11,
};
```

---

## 7. ConnectBlock Order

```cpp
bool ConnectBlock(...) {
    // (1) Daily updates (T + Yield) si height % 1440 == 0
    ApplyDailyUpdatesIfNeeded(state, nHeight);

    // (2) Process PIV2 transactions
    for (tx in block.vtx)
        ProcessHUTransaction(tx, state);

    // (3) Check invariants
    if (!state.CheckInvariants())
        return false;

    // (4) Persist
    WritePIV2State(state);
}
```

---

## 8. DOMC Cycle

```
0────────────132480────────152640────────172800
│              │              │              │
│   ACTIVE     │   COMMIT     │   REVEAL     │
│   (R%)       │   (2 sem)    │   (2 sem)    │
│              │              │              │
└──────────────┴──────────────┴──────────────┴──► New R%
```

---

## 9. Maturity

```
LOCK @ block N:
  - Blocks N to N+4319: MATURITY (no yield)
  - Block N+4320+: Yield active
```

---

## 10. Modèle Sapling PIV2 — S-pool vs K-pool

PIV2 utilise deux pools Sapling distincts avec des niveaux de validation différents.

### K-pool (ZKHU Staking)

```
Pipeline:
  KHU → ZKHU (LOCK)
  ZKHU → KHU + Y (UNLOCK)
  ZKHU → ZKHU: ❌ INTERDIT

Propriétés:
  - Locker = Unlocker (propriétaire unique)
  - Note identifiée par cm (stocké en DB)
  - Pas d'anonymity set (privacy simple)

Validation:
  ✅ ReadNote(cm) — note existe
  ✅ IsNullifierSpent(nf) — pas de double-spend
  ✅ Maturity >= 4320 blocs
  ✅ Invariants PIV2 (C==U+Z, Cr==Ur)

Non utilisé (K-pool):
  ❌ Merkle tree anchors
  ❌ zk-SNARK proof verification
  ❌ Binding signature check
```

### S-pool (sHU Shielded — privacy)

```
Pipeline:
  PIV2 → sHU (shield)
  sHU → sHU (Z→Z transfer)
  sHU → PIV2 (unshield)

Propriétés:
  - Transferts privés entre utilisateurs
  - Anonymity set Sapling complet
  - Privacy maximale

Validation (Sapling complet):
  ✅ Merkle tree anchor valide
  ✅ zk-SNARK proof (librustzcash)
  ✅ Binding signature
  ✅ Nullifier unique
```

### Raison Architecturale

K-pool n'a **pas besoin** de Sapling complet car:
1. Le locker est **toujours** l'unlocker (pas de transfert de propriété)
2. On **sait** quelle note est dépensée (cm connu via payload)
3. Le but est le staking privé, pas les paiements anonymes

Sapling complet est réservé au S-pool pour les cas où:
- Des transferts Z→Z entre utilisateurs sont nécessaires
- L'anonymity set doit masquer QUELLE note est dépensée

---

## 11. Serialization (GetHash)

```cpp
// Order is CONSENSUS-CRITICAL
ss << C << U << Z << Cr << Ur << T;
ss << R_annual << R_next << R_MAX_dynamic;
ss << last_yield_update_height << last_yield_amount;
ss << domc_cycle_start << domc_cycle_length;
ss << domc_commit_phase_start << domc_reveal_deadline;
ss << nHeight << hashBlock << hashPrevState;
```

---

## 12. PIV2 Consensus: DMM & HU (BLUEPRINT)

PIV2 uses two **independent** consensus layers inspired by ETH2/Tendermint architecture:

```
┌─────────────────────────────────────────────────────────┐
│                    PIV2 CONSENSUS                        │
├──────────────────────┬──────────────────────────────────┤
│   DMM Layer          │   HU Layer                       │
│   (Liveness)         │   (Security/Finality)            │
├──────────────────────┼──────────────────────────────────┤
│ - Produces blocks    │ - Signs finalized blocks         │
│ - NEVER waits for HU │ - 2/3 quorum for finality        │
│ - Deterministic      │ - Rotating quorum                │
│   producer selection │ - Anti-reorg protection          │
└──────────────────────┴──────────────────────────────────┘
```

### 12.1 Two Independent Layers

#### DMM Layer (Deterministic Masternode Mining)
- **Purpose**: Guarantees LIVENESS - the chain always progresses
- **Rule**: NEVER depends on HU status
- **Selection**: Deterministic based on `Hash(prevBlockHash + height + proTxHash)`
- **Fallback**: Timeout-based rotation to next MN if primary is offline

#### HU Layer (HORUS ULTRA Finality)
- **Purpose**: Guarantees SECURITY - BFT finality for confirmed blocks
- **Rule**: Provides finality AFTER block production
- **Selection**: Rotating quorum based on `Hash(lastFinalizedBlockHash + cycleIndex)`
- **Threshold**: 2/3 of quorum members must sign for finality

### 12.2 DMM Block Production

#### Producer Selection
```cpp
// Primary producer: highest score
score = Hash(prevBlockHash + height + proTxHash)
producer = MN with highest score

// Fallback (after timeout)
if (timeSincePrevBlock > leaderTimeout):
    fallbackIndex = 1 + (excessTime / fallbackInterval)
    fallbackIndex = fallbackIndex % numMNs  // Rotate through all
    producer = scoredMNs[fallbackIndex]
```

#### CRITICAL RULE: No HU Dependency
```cpp
// DMM-SCHEDULER pseudocode
void TryProducingBlock():
    if (!IsBlockchainSynced())
        return
    if (!IsLocalBlockProducer())
        return

    // DO NOT check PreviousBlockHasQuorum() - this would block liveness
    // HU finality is checked AFTER block is produced, not before

    CreateAndBroadcastBlock()
```

### 12.3 HU Quorum Selection

#### Cycle Configuration
- **Rotation**: Every `nHuQuorumRotationBlocks` DMM heights (default: 12)
- **Quorum Size**: `nHuQuorumSize` members (testnet: 3, mainnet: 12)
- **Threshold**: `nHuQuorumThreshold` signatures required (2/3)

#### Quorum Seed Calculation (CRITICAL - BLUEPRINT REQUIREMENT)
```
seed = SHA256(lastFinalizedBlockHash || cycleIndex || "HU_QUORUM")
```

**Security Rationale**:
- Using `lastFinalizedBlockHash` prevents adversaries from influencing quorum selection
- A finalized block cannot be reverted (BFT guarantee)
- This ensures the quorum is deterministic and manipulation-resistant

**Bootstrap Exception** (Cycle 0, blocks 0-11):
- Use genesis block hash or null
- This allows the first quorum to form before any finality exists

#### Member Selection Algorithm
```cpp
for each validMN in mnList:
    if mn.confirmedHash.IsNull():
        continue  // Skip unconfirmed MNs
    score = Hash(seed + mn.proTxHash)
    scoredMNs.push(score, mn)

sort(scoredMNs, descending)
quorum = scoredMNs[0..quorumSize]
```

### 12.4 HU Signature Flow
```
Block N produced by DMM
         │
         ▼
┌─────────────────────────────────────────┐
│      Block propagates to network         │
└─────────────────┬───────────────────────┘
                  │
    ┌─────────────┼─────────────┐
    ▼             ▼             ▼
┌───────┐    ┌───────┐    ┌───────┐
│ MN_A  │    │ MN_B  │    │ MN_C  │
│(quorum│    │(quorum│    │(quorum│
│member)│    │member)│    │member)│
└───┬───┘    └───┬───┘    └───┬───┘
    │            │            │
    ▼            ▼            ▼
 Sign(N)      Sign(N)      Sign(N)
    │            │            │
    └────────────┼────────────┘
                 │
                 ▼
    ┌─────────────────────────────┐
    │ Collect 2/3 signatures      │
    │ Block N is FINALIZED        │
    └─────────────────────────────┘
```

### 12.5 Anti-Reorg Protection

**Rule**: NEVER reorg below lastFinalizedHeight

```cpp
bool AcceptBlock(block, reorgDepth):
    if (chainActive.Height() - reorgDepth < lastFinalizedHeight):
        return error("Cannot reorg below finalized height")
    return true
```

### 12.6 Cold Start Recovery

When the network restarts after being stopped:

1. **Stale Tip Detection**: If tip age > 600 seconds (10 minutes)
2. **Bypass Finality Check**: Allow DMM to produce blocks without recent finality
3. **Resume Normal Operation**: Once new blocks are produced and signed

### 12.7 Bootstrap Phase (Blocks 0-5)
```
Block 0: Genesis (no MNs exist)
Block 1: Premine (creates collateral UTXOs)
Block 2: Collateral confirmation
Blocks 3-5: ProRegTx (MNs register)
Block 6+: DMM active, HU quorum operational
```

### 12.8 LevelDB Schema (Finality)
```
'F' + blockHash → CHuFinality {
    blockHash
    nHeight
    mapSignatures: proTxHash → ECDSA signature
}
```

### 12.9 Code Locations

| Component | File | Function |
|-----------|------|----------|
| DMM Scheduler | `src/activemasternode.cpp` | `TryProducingBlock()` |
| Block Producer | `src/evo/blockproducer.cpp` | `GetBlockProducerWithFallback()` |
| HU Signaling | `src/piv2/piv2_signaling.cpp` | `OnNewBlock()`, `ProcessHuSignature()` |
| HU Quorum | `src/piv2/piv2_quorum.cpp` | `GetHuQuorum()`, `ComputeHuQuorumSeed()` |
| HU Finality | `src/piv2/piv2_finality.cpp` | `AddSignature()`, `HasFinality()` |
| Sync State | `src/tiertwo/tiertwo_sync_state.cpp` | `IsBlockchainSynced()` |

### 12.10 Known Issues / TODO

#### ISSUE 1: Quorum Seed Uses prevCycleBlockHash Instead of lastFinalizedBlockHash

**Blueprint Requirement**:
```cpp
seed = Hash(lastFinalizedBlockHash + cycleIndex + "HU_QUORUM")
```

**Fix Applied** (`piv2_quorum.cpp:GetHuQuorumForHeight()`):
- Now searches for last finalized block via `huFinalityHandler->GetFinality()`
- Uses `HasFinality(nHuQuorumThreshold)` to verify block reached 2/3 signatures
- Falls back to previous cycle block hash during early chain bootstrap
- Bootstrap cycle 0 (blocks 0-11) uses null hash

**Security Impact**: Medium - Adversary could potentially manipulate block hash at cycle boundary
**Status**: ✅ FIXED (commit pending)

#### ISSUE 2: Missing Anti-Reorg Protection Below Finalized Height

**Blueprint Requirement**: `if (reorgDepth > chainActive.Height() - lastFinalizedHeight): return error(...)`

**Fix Applied** (`validation.cpp:ActivateBestChainStep()`):
- Added `WouldViolateHuFinality()` check before allowing reorgs
- Walks from fork point to current tip, checking for finalized blocks
- Returns DoS(100) error if any finalized block would be reverted
- Uses `pHuFinalityDB->IsBlockFinal()` to check finality status

```cpp
// PIV2 HU FINALITY: Check if this reorg would violate finality (BFT guarantee)
if (pindexFork && chainActive.Tip() && pindexFork != chainActive.Tip()) {
    if (hu::WouldViolateHuFinality(pindexMostWork, pindexFork)) {
        return state.DoS(100, error("%s: HU Finality violation...", __func__),
                         REJECT_INVALID, "bad-hu-finality-reorg");
    }
}
```

**Security Impact**: High - This is the BFT guarantee that prevents deep reorgs
**Status**: ✅ FIXED (commit pending)

---

## 13. PIV2-HTLC-CORE-v1

### Script Template
```
OP_IF
    OP_SIZE 32 OP_EQUALVERIFY
    OP_SHA256 <H> OP_EQUALVERIFY
    OP_DUP OP_HASH160 <HASH160(receiver_pubkey)>
OP_ELSE
    <expiry> OP_CHECKLOCKTIMEVERIFY OP_DROP
    OP_DUP OP_HASH160 <HASH160(refund_pubkey)>
OP_ENDIF
OP_EQUALVERIFY
OP_CHECKSIG
```

### Parameters
```
H      = SHA256(secret), 32 bytes
expiry = block height (absolute)
receiver_pubkey = claim destination
refund_pubkey   = timeout destination
```

### Spend Paths
```
Claim (receiver, avant expiry):
  scriptSig: <sig> <pubkey> <secret> OP_TRUE

Refund (refund, après expiry):
  scriptSig: <sig> <pubkey> OP_FALSE
  nLockTime >= expiry
  nSequence < 0xFFFFFFFF
```

### Usage
```
PIV2 natif:      vout.nValue = amount PIV2
KHU colored:   vout.nValue = amount KHU

Script identique pour les deux.
Zero impact sur C/U/Z/Cr/Ur.
```

---

*SPEC.md v2.0 - PIV2 Technical Specification*
