# ROADMAP TESTNET PIV2 — Checklist Exhaustive

**Version:** 1.0
**Date:** 2025-12-04
**Status:** PRE-TESTNET

---

## 1. Objectif Global

Avoir un **testnet public PIV2** :
- Stable
- Complet (PIV2/KHU/ZKHU/T/DOMC)
- Testable
- Auditable
- Prêt pour mainnet sans aucune dépendance extérieure

---

## 2. Ce qu'il reste à coder AVANT testnet public

### 2.1 — Économie PIV2/KHU/ZKHU (PRIORITÉ 1)

#### (A) KHU (LOCK/UNLOCK) — Must Have

| Item | Status | Notes |
|------|--------|-------|
| LOCK: PIV2 → KHU (1:1) | ✅ DONE | hu_lock.cpp (356 lignes) |
| UNLOCK: KHU → PIV2 | ✅ DONE | hu_unlock.cpp (488 lignes) |
| Invariant C = U + Z | ✅ DONE | CheckInvariants() |
| Invariant Cr = Ur | ✅ DONE | CheckInvariants() |
| Tous ≥ 0 | ✅ DONE | Validation dans Check*() |

**Tests requis:**
- [x] 77 tests unitaires passent (hu_*_tests)
- [x] Check invariants après chaque bloc
- [ ] Crash + restart → invariants identiques

#### (B) ZKHU (STAKE/UNSTAKE)

| Item | Status | Notes |
|------|--------|-------|
| Staking 7 jours exact | ✅ DONE | 4320 blocs mainnet, 1260 regtest |
| Période fixe (4320 blocs) | ✅ DONE | hu_yield_tests vérifie |
| Cr, Ur mis à jour | ✅ DONE | 5 mutations atomiques dans ApplyPIV2Unlock |
| Z réduit uniquement par UNSTAKE | ✅ DONE | Z -= P dans ApplyPIV2Unlock |
| Rewards ajoutés à KHU (C incrémenté) | ✅ DONE | C += Y dans ApplyPIV2Unlock |

**Tests requis:**
- [x] Multi-stakers (hu_integration_tests)
- [x] Rewards corrects (hu_yield_tests)
- [x] Maturité exacte (no_yield_before_maturity)
- [x] Cancel early impossible (logique validation)
- [x] Overflow float impossible (CAmount = int64)

#### (C) Yield R% (DOMC-applied)

| Item | Status | Notes |
|------|--------|-------|
| R% calculé chaque jour | ✅ DONE | daily = (P * R) / 10000 / 365 |
| Ajouté à Cr/Ur proprement | ✅ DONE | hu_yield_tests: cr_ur_daily_accumulation |
| Treasury T réduit / pas de fuite | ✅ DONE | hu_dao_tests vérifie T >= 0 |

**Tests requis:**
- [x] 90 jours yield (hu_yield_tests: multi_day)
- [x] Multi-stakers (hu_integration_tests)
- [x] R% = 0 → aucun yield (r_rate_bounds)
- [x] R% très haut → T jamais < 0 (treasury_non_negative)

#### (D) Treasury (T)

| Item | Status | Notes |
|------|--------|-------|
| Accumule R% | ✅ DONE | treasury_daily_accumulation |
| Paie rewards | ✅ DONE | treasury_spending_proposal |
| Ne doit jamais passer négatif | ✅ DONE | invariant_treasury_non_negative |
| Test overflow | ✅ DONE | large_values_no_overflow |
| Test rollovers sur 1 an simulé | ✅ DONE | treasury_yearly_accumulation |

#### (E) DOMC (vote sur R%)

| Item | Status | Notes |
|------|--------|-------|
| Commit phase | ⚠️ PARTIAL | Structure en place, besoin RPC |
| Reveal phase | ⚠️ PARTIAL | Structure en place, besoin RPC |
| Median R_next | ✅ DONE | r_rate_change_proposal |
| Activation | ✅ DONE | domc_cycle_blocks |
| Cas zéro vote | ⚠️ TODO | Tests edge-case |
| Cas 1 vote | ⚠️ TODO | Tests edge-case |
| Vote threshold | ✅ DONE | vote_threshold_majority/not_met |

---

### 2.2 — Consensus DMM + Finalité (PRIORITÉ 1)

#### (F) DMM Scheduler

| Item | Status | Notes |
|------|--------|-------|
| Block production automatique | ✅ DONE | 773 blocs testés |
| Rotation 3 MN | ✅ DONE | Distribution équilibrée |
| Arrondir code | ⬜ TODO | |
| Ajouter logs | ⬜ TODO | |
| Random offset (si prévu) | ⬜ TODO | |
| Stable entre restart | ⬜ TODO | |

#### (G) Finalité 12/8

| Item | Status | Notes |
|------|--------|-------|
| MN offline (1) | ⬜ TODO | |
| 2 MN offline | ⬜ TODO | |
| Partition réseau (split brain) | ⬜ TODO | |
| Reorg 4 blocs (acceptable) | ⬜ TODO | |
| Reorg 12 blocs (bloqué) | ⬜ TODO | |
| Reorg 20 blocs (impossible) | ⬜ TODO | |

---

### 2.3 — HTLC-CORE (Foundation L1) (PRIORITÉ 1.5)

#### (H) Script HTLC PIV2/KHU (core)

| Item | Status | Notes |
|------|--------|-------|
| Claim path | ⬜ TODO | |
| Refund path | ⬜ TODO | |
| Correctness SHA256 | ⬜ TODO | |
| CLTV correct | ⬜ TODO | |
| Tests de script unitaires | ⬜ TODO | |
| Aucune modification consensus | ⬜ TODO | |
| Reproductible sur tous les noeuds | ⬜ TODO | |

> **Pourquoi indispensable au testnet?**
> Primitive crypto PIV2, même indépendante d'un DEX.
> Permet de tester scripts basiques et L1.

---

### 2.4 — RPC API stable (PRIORITÉ 1.5)

| RPC | Status | Notes |
|-----|--------|-------|
| `mint` | ⬜ TODO | PIV → PIV2 |
| `redeem` | ⬜ TODO | PIV2 → PIV |
| `khulock` | ⬜ TODO | PIV2 → KHU |
| `khuunlock` | ⬜ TODO | KHU → PIV2 |
| `zkstake` | ⬜ TODO | KHU → ZKHU |
| `zkunstake` | ⬜ TODO | ZKHU → KHU + yield |
| `gethustate` | ⬜ TODO | État PIV2 complet |
| `getdomcstate` | ⬜ TODO | |
| `gettreasury` | ⬜ TODO | |
| `createhtlc` | ⬜ TODO | |
| `decodehtlc` | ⬜ TODO | |

**Critères:**
- [ ] Documentés
- [ ] Stables
- [ ] Testables en CLI
- [ ] Identiques entre noeuds

---

## 3. Génération du Testnet Public

### 3.1 — Regenesis propre

| Item | Status | Notes |
|------|--------|-------|
| Nouveau bloc genesis | ⬜ TODO | |
| Clés MN propres | ⬜ TODO | |
| Ports testnet | ⬜ TODO | |
| Blocktime réglé | ⬜ TODO | |
| Difficulty initiale simple | ⬜ TODO | |
| Seeds DNS (2 minimum) | ⬜ TODO | |

### 3.2 — Distribution testnet

| Item | Status | Notes |
|------|--------|-------|
| Treasury initiale | ⬜ TODO | |
| MN collateral | ⬜ TODO | |
| Faucet testnet | ⬜ TODO | |
| Adresses préconfigurées | ⬜ TODO | |

### 3.3 — Infra testnet

| Item | Status | Notes |
|------|--------|-------|
| 3 masternodes officiels | ⬜ TODO | Hot signing keys |
| 2 full nodes | ⬜ TODO | |
| 1 block explorer | ⬜ TODO | |
| 1 faucet | ⬜ TODO | |
| 1 dashboard DMM | ⬜ TODO | |

---

## 4. TEST SUITE PRÉ-MAINNET

*À passer entièrement AVANT de dire "OK mainnet"*

### 4.1 — Économie PIV2

- [ ] LOCK/UNLOCK 10 000 fois
- [ ] STAKE/UNSTAKE 100 cycles
- [ ] R% variation 10 cycles
- [ ] Treasury T jamais négatif
- [ ] Cr/Ur invariants OK
- [ ] C=U+Z toujours vrai

### 4.2 — Consensus

- [ ] Rotation 1000 blocs
- [ ] MN offline (1 → 2 → 3)
- [ ] Partition réseau
- [ ] Reorg 4, 8, 12, 20
- [ ] Restart + resync clean

### 4.3 — Script engine

- [ ] HTLC tests
- [ ] CLTV edge-cases
- [ ] invalid S
- [ ] invalid expiry
- [ ] replay protections

### 4.4 — RPC

- [ ] Full RPC test QA
- [ ] mempool spam test
- [ ] serialization fuzz

---

## 5. Red Team (sécurité pré-mainnet)

### Tests offensifs:

- [ ] Double-spend <12 blocs
- [ ] Tentative reorg >12 blocs
- [ ] MnKey signature abuse
- [ ] HTLC malformé
- [ ] Overflow yields
- [ ] Overflow treasury
- [ ] Invariants falsifiables
- [ ] RPC injection
- [ ] Node crash (fuzzer)

### Rapport requis:
- Bugs critiques
- Crash fatal
- Leaks

---

## 6. Audit externe (obligatoire avant mainnet)

### Scope audit:

- [ ] Logique Mint/Redeem
- [ ] Lock/Unlock KHU
- [ ] Cr/Ur transitions
- [ ] ZKHU staking
- [ ] Treasury logic
- [ ] DOMC median
- [ ] DMM scheduler
- [ ] Finalité 12/8
- [ ] HTLC-core scripts
- [ ] RPC input validation
- [ ] Script engine

### Le rapport d'audit doit confirmer:

- [ ] Pas de bug monétaire
- [ ] Pas de bug consensus
- [ ] Pas d'inflation cachée
- [ ] Invariants respectés
- [ ] Pas de crash VM

---

## 7. Conditions GO-MAINNET

On lance un mainnet **SEULEMENT SI**:

| Condition | Status |
|-----------|--------|
| 10 000 blocs testnet sans fork | ⬜ |
| Invariants PIV2 (C/U/Z/Cr/Ur/T) stables | ⬜ |
| Yields corrects sur 30 jours | ⬜ |
| DOMC fonctionne en 3 cycles | ⬜ |
| HTLC-core flawless | ⬜ |
| RPC stable | ⬜ |
| Reorg tests validés | ⬜ |
| Audit externe OK | ⬜ |
| Red-team OK | ⬜ |
| Aucun bug consensus | ⬜ |

---

## 8. Résumé — Ordre d'exécution

```
1️⃣ Finir PIV2-core (KHU, ZKHU, R%, T, DOMC)
2️⃣ Finaliser DMM + Finality tests
3️⃣ Implémenter HTLC-core v1
4️⃣ Configurer testnet public (regenesis + nodes + explorer)
5️⃣ Passer la Pre-Mainnet Suite
6️⃣ Audit & Red Team
7️⃣ Mainnet Go/NoGo
```

---

## Progress Tracker

| Phase | Progress | Notes |
|-------|----------|-------|
| DMM Scheduler | ✅ 100% | 773+ blocs auto-produits |
| PIV2-core (KHU/ZKHU) | ✅ 100% | 77 tests passent |
| DOMC | ⚠️ 70% | Logique OK, RPC manquants |
| HTLC-core | ⬜ 0% | - |
| RPC API | ⬜ 20% | Besoin implémentation |
| Testnet Genesis | ⬜ 0% | - |
| Pre-mainnet tests | ⬜ 0% | - |
| Audit | ⬜ 0% | - |
| Mainnet | ⬜ 0% | - |

---

*ROADMAP-TESTNET.md v1.0 — Pre-Mainnet Checklist*
