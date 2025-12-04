# ARCHITECTURE - PIV2 Chain Vision

**Version:** 1.0
**Date:** December 2025
**Status:** Genesis Ready

---

## 1. Qu'est-ce que PIV2?

**PIV2 (Hedge Unit)** est une blockchain indépendante dérivée de PIVX v5.6.1.

```
PIV2 Chain
 │
 ├── PIV2 (coin natif)
 │    └── PIV2 SHIELD (Sapling) ─── Privacy sans yield
 │
 └── KHU (colored coin 1:1 sur PIV2)
      ├── KHU_T ─── Transparent (trade, hold)
      └── ZKHU ─── Shielded + Yield R%
```

**PIV2 n'est PAS:**
- Un stablecoin (pas de peg USD/EUR)
- Un fork avec même consensus que PIVX

**PIV2 EST:**
- Une chaîne avec consensus DMM (Deterministic Masternode Mining)
- Un système de yield gouverné par masternodes (DOMC)
- Une architecture colored-coin avec privacy optionnelle

---

## 2. Consensus: DMM + ECDSA 12/8

### Block Production
- **DMM**: Le leader est sélectionné déterministiquement parmi les MNs
- **Pas de PoW**: Pas de mining traditionnel
- **Pas de PoS**: Pas de staking pour produire des blocs

### Finality
- **12/8 ECDSA Quorum**: 12 MNs signent, 8 signatures minimum
- **Pas de BLS**: Signatures ECDSA standard (secp256k1)
- **Max reorg**: 12 blocs (nHuMaxReorgDepth)
- **Persistence**: CHuFinalityDB (LevelDB separate namespace 'F')

### Paramètres

| Réseau | Quorum | Threshold | Rotation | Timeout | MaxReorg |
|--------|--------|-----------|----------|---------|----------|
| MAINNET | 12 | 8 | 12 blocs | 45s | 12 |
| TESTNET | 3 | 2 | 3 blocs | 30s | 6 |
| REGTEST | 1 | 1 | 1 bloc | 5s | 100 |

---

## 3. Économie: R% Gouverné

### Block Reward = 0
Post-activation, aucune émission par bloc. Toute l'économie est via R%.

### Yield ZKHU
```
daily_yield = (principal × R%) / 365
```

### Treasury T
```
T_daily = (U × R%) / 8 / 365    (~5% à R=40%)
```

### R% Voté par DOMC
- Initial: 40%
- Plancher: 7%
- Décroissance: -1%/an sur 33 ans
- Cycle DOMC: 4 mois (172800 blocs)

---

## 4. Stack Technique

### Coins & Tokens

| Asset | Type | Pool |
|-------|------|------|
| PIV2 | Coin natif | - |
| PIV2 SHIELD | PIV2 shielded | Sapling 'S' |
| KHU_T | Colored coin transparent | - |
| ZKHU | Colored coin shielded + yield | Sapling 'K' |

### Transactions Types

| Type | Code | Description |
|------|------|-------------|
| NORMAL | 0 | Transaction standard |
| PROREG | 1 | Enregistrement DMN |
| PROUPSERV | 2 | Mise à jour service DMN |
| PROUPREG | 3 | Mise à jour registration DMN |
| PROUPREV | 4 | Révocation DMN |
| *(reserved)* | 5 | Slot réservé |
| KHU_MINT | 6 | PIV2 → KHU_T |
| KHU_REDEEM | 7 | KHU_T → PIV2 |
| KHU_LOCK | 8 | KHU_T → ZKHU |
| KHU_UNLOCK | 9 | ZKHU → KHU_T + yield |
| KHU_DOMC_COMMIT | 10 | Vote R% (commit) |
| KHU_DOMC_REVEAL | 11 | Vote R% (reveal) |

---

## 5. Organisation Code

```
src/
├── hu/                      # PIV2 State Machine
│   ├── hu_state.h/cpp       # HuGlobalState
│   ├── hu_statedb.cpp       # LevelDB persistence
│   ├── hu_validation.cpp    # Block validation
│   ├── hu_mint.cpp          # MINT
│   ├── hu_redeem.cpp        # REDEEM
│   ├── hu_stake.cpp         # STAKE
│   ├── hu_unstake.cpp       # UNSTAKE
│   ├── hu_yield.cpp         # Yield calculation
│   ├── hu_domc.cpp          # DOMC governance
│   ├── hu_dao.cpp           # Treasury T
│   ├── hu_quorum.cpp        # 12/8 quorum selection
│   └── hu_finality.cpp      # Finality tracking
│
├── evo/                     # Deterministic MN + Block Production
│   ├── deterministicmns.cpp  # DMN list management
│   ├── blockproducer.cpp     # DMM leader selection
│   └── providertx.cpp        # ProTx payloads
│
├── consensus/               # MN-only consensus
│   ├── mn_validation.cpp    # Block producer validation
│   └── params.h             # PIV2 params per network
│
├── sapling/                 # Privacy (Sapling)
│   └── ...
│
└── rpc/                     # RPC interface
    └── hu.cpp               # PIV2 commands
```

---

## 6. LevelDB Schema

### Namespace 'K' (KHU/ZKHU)
```
'K' + 'S' + height     → HuGlobalState
'K' + 'B'              → Best state hash
'K' + 'U' + outpoint   → KHU UTXO
'K' + 'N' + nullifier  → ZKHU nullifier
'K' + 'A' + anchor     → ZKHU Merkle tree
```

### Namespace 'S' (PIV2 Shield - séparé)
```
'S' + anchor           → Shield Merkle tree
's' + nullifier        → Shield nullifier
```

### Namespace 'F' (PIV2 Finality)
```
'F' + blockHash        → CHuFinality {
                           blockHash,
                           nHeight,
                           mapSignatures: {proTxHash → signature}
                         }
```
- Stored in separate LevelDB: `hu_finality/`
- Tracks signatures from quorum members
- Block final when signatures >= nHuQuorumThreshold

---

## 7. Modèle de Validation Sapling — S vs K

PIV2 utilise Sapling avec deux modèles de validation distincts.

### K-pool: Validation DB-based (ZKHU Staking)

```
K-pool = ZKHU (staking privé)

Pipeline:
  U → K (STAKE)
  K → U+Y (UNSTAKE)
  K → K: ❌ INTERDIT

Validation:
  ✅ ReadNote(cm) — note existe
  ✅ IsNullifierSpent(nf) — pas de double-spend
  ✅ Maturity (4320 blocs)
  ✅ Invariants (C==U+Z, Cr==Ur)

NON utilisé:
  ❌ Merkle tree anchors
  ❌ zk-SNARK proofs (librustzcash)
  ❌ Binding signatures
```

**Pourquoi?** Le staker est TOUJOURS l'unstaker. La note est identifiée directement par `cm`. Pas besoin d'anonymity set.

### S-pool: Sapling Complet (PIV2 Shielded — si activé)

```
S-pool = PIV2 SHIELD (privacy complète)

Pipeline:
  U → S (shield)
  S → S (Z→Z transfer privé)
  S → U (unshield)

Validation Sapling complète:
  ✅ Merkle anchor valide
  ✅ zk-SNARK proof verification
  ✅ Binding signature
  ✅ Nullifier unique
```

**Pourquoi?** Transferts entre utilisateurs différents. L'anonymity set masque QUELLE note est dépensée.

### TL;DR pour Auditeurs

| Pool | Usage | Z→Z? | Validation |
|------|-------|------|------------|
| K | Staking | ❌ | DB + nullifier |
| S | Privacy | ✅ | Sapling complet |

**Ce n'est pas une faille.** C'est un **choix architectural** documenté.

---

## 8. Binaires

| Binaire | Description |
|---------|-------------|
| `hud` | Daemon PIV2 |
| `hu-cli` | CLI PIV2 |
| `hu-tx` | Transaction utility |

---

## 9. PIV2-HTLC-CORE-v1

Script HTLC standard pour PIV2 et KHU_T:

```
OP_IF
    OP_SIZE 32 OP_EQUALVERIFY
    OP_SHA256 <H> OP_EQUALVERIFY
    <receiver>
OP_ELSE
    <expiry> OP_CHECKLOCKTIMEVERIFY OP_DROP
    <refund>
OP_ENDIF
OP_EQUALVERIFY OP_CHECKSIG
```

**Usage:** Swaps atomiques, escrow, time-locked payments, L2 integrations.

**Zero impact:** Pas de modification C/U/Z/Cr/Ur. P2SH standard.

---

## 10. Supprimé de PIVX

| Système | Raison |
|---------|--------|
| LLMQ/BLS | Remplacé par ECDSA 12/8 |
| Zerocoin | Non utilisé |
| ColdStaking | Remplacé par ZKHU |
| PoW mining | DMM consensus |
| PoS staking | DMM consensus |
| Legacy MN | DMN uniquement |

---

*ARCHITECTURE.md - Vision globale PIV2 Chain*
