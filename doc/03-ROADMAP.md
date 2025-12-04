# 03 — ROADMAP: État du Code PIV2

**Version:** 2.1
**Date:** 2025-12-02
**Status:** Community Proposal Ready

---

## Vue d'Ensemble

Ce document décrit l'état actuel du code PIV2, structuré selon les blueprints.

| Métrique | Valeur |
|----------|--------|
| Module PIV2 (`src/hu/`) | 7,918 lignes |
| Tests (`khu/hu`) | 14,433 lignes |
| Total `src/` | ~91,000 lignes |
| Réduction vs PIVX | -69% |
| Issues GitHub | 78 → 0 |

---

## Correspondance Blueprints ↔ Code

### 01 — CONSENSUS DMM + FINALITY

| Composant | Fichiers | Status |
|-----------|----------|--------|
| DMM (Deterministic Masternode Minting) | `hu_quorum.cpp/h` | **DONE** |
| Finality 12/8 ECDSA | `hu_finality.cpp/h` | **DONE** |
| Quorum selection | `hu_quorum.cpp` | **DONE** |
| Commitment DB | `hu_commitment.cpp/h`, `hu_commitmentdb.cpp/h` | **DONE** |

```cpp
// Paramètres mainnet
HU_QUORUM_SIZE = 12
HU_QUORUM_THRESHOLD = 8
HU_MAX_REORG_DEPTH = 12
```

---

### 02 — CLEANROOM

| Élément Supprimé | Status |
|------------------|--------|
| `libzerocoin/` | **SUPPRIMÉ** |
| `llmq/` | **SUPPRIMÉ** |
| `bls/`, `chiabls/` | **SUPPRIMÉ** |
| `zpiv/` | **SUPPRIMÉ** |
| `kernel.cpp/h` (PoS) | **SUPPRIMÉ** |
| `pow.cpp/h` | **SUPPRIMÉ** → `hu_chainwork.cpp/h` |

Patterns vérifiés = 0: `STAKING_ADDRESS`, `UPGRADE_POS`, `nBudgetAmt`, `BLS_`, `LLMQ`, `Zerocoin`

---

### 03 — BLOCK REWARD ZERO

| Composant | Fichiers | Status |
|-----------|----------|--------|
| GetBlockValue() = 0 | `validation.cpp:832` | **DONE** |
| GetMasternodePayment() = 0 | `validation.cpp:850` | **DONE** |
| Regtest bootstrap (500 blocs) | `validation.cpp:843` | **DONE** |

```cpp
CAmount GetBlockValue(int nHeight) {
    if (IsRegTestNet() && nHeight <= 500) return 1000 * COIN;
    return 0;  // Mainnet/Testnet = 0
}
```

---

### 04 — PIV2 STATE MACHINE

| Composant | Fichiers | Status |
|-----------|----------|--------|
| HuGlobalState (C/U/Z/Cr/Ur/T) | `hu_state.cpp/h` | **DONE** |
| State persistence | `hu_statedb.cpp/h` | **DONE** |
| CheckInvariants() | `hu_state.cpp` | **DONE** |

```cpp
// Invariants vérifiés chaque bloc
C == U + Z      // Collateral = Transparent + Shielded
Cr == Ur        // Reward pool = Reward rights
T >= 0          // Treasury non-négatif
```

---

### 05 — MINT / REDEEM

| Composant | Fichiers | Status |
|-----------|----------|--------|
| MINT (PIV → PIV2) | `hu_mint.cpp/h` | **DONE** |
| REDEEM (PIV2 → PIV) | `hu_redeem.cpp/h` | **DONE** |
| UTXO tracking | `hu_utxo.cpp/h` | **DONE** |

```
MINT:   C += amount, U += amount
REDEEM: C -= amount, U -= amount
```

---

### 06 — LOCK / UNLOCK (ZKHU)

| Composant | Fichiers | Status |
|-----------|----------|--------|
| LOCK (PIV2 → ZKHU) | `hu_stake.cpp/h` | **DONE** |
| UNLOCK (ZKHU → PIV2 + yield) | `hu_unstake.cpp/h` | **DONE** |
| ZKHU notes | `hu_notes.cpp/h`, `zkhu_*.cpp/h` | **DONE** |
| Maturity (4320 blocs) | `hu_unstake.cpp` | **DONE** |

```
LOCK:   U -= amount, Z += amount
UNLOCK: Z -= P, U += (P + Y), C += Y, Cr -= Y, Ur -= Y
```

---

### 07 — YIELD + DOMC

| Composant | Fichiers | Status |
|-----------|----------|--------|
| Yield calculation | `hu_yield.cpp/h` | **DONE** |
| DOMC commit/reveal | `hu_domc.cpp/h` | **DONE** |
| DOMC transactions | `hu_domc_tx.cpp/h` | **DONE** |
| DOMC persistence | `hu_domcdb.cpp/h` | **DONE** |

```cpp
// Yield linéaire
daily_yield = (principal * R_annual) / 10000 / 365

// DOMC cycle: 172800 blocs (4 mois)
VOTE: 132480 → 152640
REVEAL: 152640
ACTIVATION: 172800
```

---

### 08 — HTLC

| Composant | Fichiers | Status |
|-----------|----------|--------|
| Conditional scripts | `script/conditional.cpp/h` | **DONE** |
| RPC commands | `rpc/conditional.cpp` | **DONE** |
| Tests | `test/script_conditional_tests.cpp` | **DONE** |

```bash
# Commandes RPC HTLC
createconditionalsecret    # Génère secret + hashlock
createconditional          # Crée adresse P2SH
decodeconditional          # Décode redeemScript
```

---

### 09 — RPC / WALLET

| Composant | Fichiers | Status |
|-----------|----------|--------|
| RPC commands | `wallet/rpc_hu.cpp` | **DONE** |
| Wallet integration | `wallet/hu_wallet.cpp/h` | **DONE** |
| Balance tracking | `wallet/wallet.cpp` | **DONE** |

```
Commandes: mint, redeem, lock, unlock, hubalance, hustatus
```

---

### 10 — DAO TREASURY

| Composant | Fichiers | Status |
|-----------|----------|--------|
| Treasury T accumulation | `hu_dao.cpp/h` | **DONE** |
| Proposal system | `hu_dao.cpp` | **DONE** |

```cpp
// T accumulation
T_daily = (U * R_annual) / 10000 / 8 / 365
```

---

### 11 — QT INTERFACE

| Composant | Fichiers | Status |
|-----------|----------|--------|
| Qt adaptations | `qt/*` | **PARTIAL** |
| Staking UI removed | `qt/topbar.cpp` | **DONE** |
| Cold Staking UI removed | `qt/coldstaking*` | **DONE** |

*Interface Qt fonctionnelle mais pas encore optimisée pour PIV2.*

---

### 12 — TESTNET PUBLIC

| Composant | Description | Status |
|-----------|-------------|--------|
| Infrastructure | Seeds, Explorer, Faucet | **PLANNED** |
| Bootstrap MN | 5 MN initiaux | **PLANNED** |
| Documentation | Guide utilisateur | **PLANNED** |

*Testnet public pour tests communauté avant mainnet.*

---

### 13 — COMMUNITY PROPOSAL

| Composant | Description | Status |
|-----------|-------------|--------|
| Documents | Presentation docs (01-05) | **READY** |
| Blueprints | Technical specs (01-13) | **READY** |
| Code | PIV2 module complet | **READY** |
| Vote | Soumission DAO PIVX | **PENDING** |

*Proposition prête pour vote communauté PIVX.*

---

## Tests

| Suite | Fichiers | Status |
|-------|----------|--------|
| Invariants | `hu_invariants_tests.cpp` | **PASS** |
| Pipeline | `hu_pipeline_tests.cpp` | **PASS** |
| Yield | `hu_yield_tests.cpp` | **PASS** |
| Finality | `hu_finality_tests.cpp` | **PASS** |
| Fees | `hu_fees_tests.cpp` | **PASS** |
| Sapling | `hu_sapling_tests.cpp` | **PASS** |
| Integration | `khu_phase*_tests.cpp` | **PASS** |

**Total: 95 tests, 14,433 lignes**

---

## Testnet Local

```
Nodes: 5 (mn1, mn2, mn3, client1, client2)
Status: RUNNING
Blocks: 280+
Mining: Fonctionnel
```

---

## Résumé

| Blueprint | Status |
|-----------|--------|
| 01 Consensus DMM | **DONE** |
| 02 Cleanroom | **DONE** |
| 03 Block Reward | **DONE** |
| 04 State Machine | **DONE** |
| 05 Mint/Redeem | **DONE** |
| 06 Lock/Unlock | **DONE** |
| 07 Yield/DOMC | **DONE** |
| 08 HTLC | **DONE** |
| 09 RPC/Wallet | **DONE** |
| 10 DAO Treasury | **DONE** |
| 11 Qt Interface | PARTIAL |
| 12 Testnet Public | PLANNED |
| 13 Community Proposal | **READY** |

**Code prêt pour proposal communauté.**

---

## Sécurité: Fixes Critiques v2.1

Les points critiques identifiés lors de l'audit ont été adressés:

### ✅ UNSTAKE Sapling Validation

| Composant | Fichier | Status |
|-----------|---------|--------|
| Anchor validation K-pool | `hu_unstake.cpp:173-180` | **DONE** |
| zk-SNARK proof verification | `hu_unstake.cpp:200-214` | **DONE** |
| Binding signature check | `hu_unstake.cpp:224-232` | **DONE** |

```cpp
// Validation complète Sapling pour UNSTAKE
librustzcash_sapling_check_spend()    // Vérifie zk-proof
librustzcash_sapling_final_check()    // Vérifie binding sig
zkhuDB->ReadAnchor()                  // Vérifie anchor K-pool
```

### ✅ DOMC Masternode Signature

| Composant | Fichier | Status |
|-----------|---------|--------|
| Commit signature verification | `hu_domc_tx.cpp:250-260` | **DONE** |
| Reveal signature verification | `hu_domc_tx.cpp:343-353` | **DONE** |
| MN operator key validation | `hu_domc_tx.cpp:66-111` | **DONE** |

```cpp
// Vérification signature ECDSA depuis operator key du MN
VerifyMasternodeSignature(mnOutpoint, sigHash, vchSig)
```

### ✅ Thread-Safety Documentation

| Composant | Status |
|-----------|--------|
| KHUStateDB access pattern | Documenté |
| cs_khu lock model | Clarifié |
| LevelDB thread-safety | Confirmé |

### ✅ Code Cleanup

- Tous les commentaires "BUG #x FIX" remplacés par descriptions claires
- Documentation thread-safety ajoutée aux getters DB
- Références historiques déplacées vers commits Git

---

## Liens

| Ressource | Chemin |
|-----------|--------|
| Repository | `github.com/AdonisPhusis/PIVX-V2-PROPOSAL` |
| Branch | `testnet-local-prep` |
| Blueprints | `/docs/blueprints/` |
| Presentation | `/docs/presentation/` |
