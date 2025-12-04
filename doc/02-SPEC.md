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

## 12. PIV2 Finality System

### Quorum Selection
```
seed = SHA256(prevCycleBlockHash || cycleIndex || "HU_QUORUM")
score(MN) = SHA256(seed || proTxHash)

Quorum = Top nHuQuorumSize MNs by score (descending)
```

### Block Finality
```
A block is FINAL when:
  - nHuQuorumThreshold signatures collected from quorum members
  - Each signature is ECDSA (secp256k1) from operator key

Finality prevents:
  - Reorgs past finalized blocks
  - Double-spends on confirmed transactions
```

### DMM Leader Selection
```
score(MN) = SHA256(prevBlockHash || nHeight || proTxHash)

Leader = MN with highest score (confirmed MNs only)
```

### Timeout & Fallback
```
If leader times out (nHuLeaderTimeoutSeconds):
  - Round-robin to next MN in quorum
  - Blocks remain pending until finalized
```

### LevelDB Schema (Finality)
```
'F' + blockHash → CHuFinality {
    blockHash
    nHeight
    mapSignatures: proTxHash → ECDSA signature
}
```

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
