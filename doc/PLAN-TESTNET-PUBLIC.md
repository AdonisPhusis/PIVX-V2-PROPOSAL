# PLAN COMPLET — LANCEMENT DU TESTNET PUBLIC PIV2

**Version:** 1.0
**Date:** 2025-12-04
**Status:** EN COURS
**Scope:** 100% L1 PIV2-core (zéro DEX, zéro L2)

---

## SECTION 1 — Préparation PIV2-Core (local)

**Objectif:** Avoir un PIV2-core irréprochable avant mise en réseau public

### 1.1 Batterie de tests DMM/PIV2

| Test | Status | Notes |
|------|--------|-------|
| Unit tests (77/77) | ✅ PASS | hu_*_tests |
| Integration tests PIV2 (regtest) | ✅ PASS | hu_integration_tests |
| DMM scheduler long-run (500-1000 blocs) | ✅ PASS | 773+ blocs testés |
| Rotation MN (3 producteurs) | ✅ PASS | Distribution équilibrée |
| MN offline recovery | ⬜ TODO | Test à faire |
| Shutdown/restart | ⬜ TODO | Test à faire |
| No REJECTED blocks | ✅ PASS | Vérifié en regtest |
| No bad-txnmrklroot | ✅ PASS | Vérifié en regtest |
| Sync multi-nodes | ✅ PASS | 4 nodes synchronisés |
| Finalité 12/8 | ⬜ TODO | À tester |

### 1.2 Reorg Test (fork local)

| Test | Status | Notes |
|------|--------|-------|
| Isoler un node | ⬜ TODO | Script à créer |
| Produire side-chain | ⬜ TODO | |
| Reconnecter | ⬜ TODO | |
| Reorg ≤ finality depth | ⬜ TODO | |
| Reorg impossible si finalité | ⬜ TODO | |

**GATE:** Cette section doit être 100% avant Section 2.

---

## SECTION 2 — Re-genesis PIV2-testnet propre

### 2.1 Génération des clés genesis (offline)

```
Adresses à générer:
├── MN1_genesis     → 10,000 PIV2 (collateral)
├── MN2_genesis     → 10,000 PIV2 (collateral)
├── MN3_genesis     → 10,000 PIV2 (collateral)
├── Dev_wallet      → 50,000,000 PIV2
└── Faucet_wallet   → 50,000,000 PIV2
```

**Stockage:** Cold storage hors repo (JAMAIS sur GitHub)

### 2.2 Modifier CreateGenesisBlock()

| Item | Status | Notes |
|------|--------|-------|
| ScriptsPubKey = clés réelles | ⬜ TODO | |
| Montants corrects | ⬜ TODO | 3×10k + 50M + 50M |
| Message coinbase | ⬜ TODO | "PIV2 Testnet Genesis" |
| Recalcul Merkle | ⬜ TODO | |

### 2.3 Re-miner genesis avec genesis_hu.py

| Item | Status | Notes |
|------|--------|-------|
| Trouver nonce | ⬜ TODO | |
| Calculer hash | ⬜ TODO | |
| Fixer nTime | ⬜ TODO | |
| Fixer hashGenesisBlock | ⬜ TODO | |

### 2.4 Mettre à jour CTestNetParams PIV2

| Item | Status | Notes |
|------|--------|-------|
| Magic bytes uniques | ⬜ TODO | 0xPIV2... |
| Port P2P | ⬜ TODO | 27171 |
| Port RPC | ⬜ TODO | 27172 |
| Genesis hash | ⬜ TODO | |
| Seeds DNS | ⬜ TODO | Vides initialement |

---

## SECTION 3 — Nettoyage GitHub PIV2-Core

### 3.1 Structure du repo

```
PIV2-Core/
├── src/                          # Code source C++
├── contrib/
│   ├── testnet/                  # Scripts testnet
│   └── genesis/                  # Outils genesis
├── doc/
│   ├── TESTNET-PIV2-PUBLIC.md      # Guide utilisateur
│   ├── PIV2-CONSENSUS.md           # Spec consensus
│   └── PIV2-ECONOMICS.md           # Spec économie
├── test/                         # Tests
├── scripts/                      # Scripts déploiement
├── CMakeLists.txt
├── .gitignore
└── README.md
```

### 3.2 Branches

| Branche | Usage |
|---------|-------|
| `main` | Version stable testnet |
| `dev` | Développement actif |
| `feature/*` | PR isolées |

### 3.3 Code à supprimer (cleanroom)

| Composant | Status | Notes |
|-----------|--------|-------|
| ColdStaking | ⬜ TODO | Remplacé par ZKHU |
| PoW | ⬜ TODO | DMM only |
| PoS | ⬜ TODO | DMM only |
| BLS | ✅ DONE | ECDSA uniquement |
| LLMQ | ✅ DONE | Supprimé |
| Zerocoin | ✅ DONE | Supprimé |
| Budget/Superblocks | ⬜ TODO | Remplacé par DOMC |
| Sporks | ⬜ TODO | À évaluer |
| Legacy MN | ⬜ TODO | DMN uniquement |

**Le repo doit être cleanroom PIV2:**
- DMM-only
- ActiveMasternode scheduler
- KHU pipeline
- PIV2 economics core
- Script HTLC minimal

---

## SECTION 4 — Documentation technique

### 4.1 PIV2-CONSENSUS.md

```markdown
# Contenu requis:
- DMM Scheduler
  - Block time (2 sec)
  - Rate limiting
  - Rotation algorithm
- Finalité PIV2
  - Depth: 12 blocs
  - Quorum: 8/12 signatures
- Reorg rules
  - Max reorg: 12 blocs
  - Protection: checkpoint rolling
```

### 4.2 PIV2-ECONOMICS.md

```markdown
# Contenu requis:
- Invariants
  - C == U + Z
  - Cr == Ur
  - T >= 0
- Opérations
  - MINT: PIV → PIV2
  - REDEEM: PIV2 → PIV
  - LOCK: PIV2 → KHU
  - UNLOCK: KHU → PIV2 + Yield
- State machine diagram
- RPC spec complète
```

### 4.3 TEST-SUITE.md

```markdown
# Contenu requis:
- Liste tests unitaires
- Liste tests intégration
- Scripts de test
- Logs attendus
- Critères PASS/FAIL
```

---

## SECTION 5 — Infrastructure Testnet Public

### 5.1 VPS Configuration

| Rôle | VPS | IP | Notes |
|------|-----|-------|-------|
| MN1 | VPS-1 | TBD | Port 27171 |
| MN2 | VPS-1 | TBD | Port 27171 |
| MN3 | VPS-1 | TBD | Port 27171 |
| Seed + Faucet + Explorer | VPS-1 | TBD | Port 27171/80 |

### 5.2 hu.conf Template

```ini
# PIV2 Testnet Configuration
testnet=1
server=1
listen=1
daemon=1

# Network
port=27171
rpcport=27172
externalip=<IP_PUBLIQUE>

# RPC
rpcuser=<SECURE_USER>
rpcpassword=<SECURE_PASSWORD>
rpcallowip=127.0.0.1

# Masternode (si applicable)
masternode=1
mnoperatorprivatekey=<OPERATOR_KEY>

# Peers
addnode=seed1.hu-chain.io:27171
addnode=seed2.hu-chain.io:27171

# Debug
debug=masternode
debug=net
printtoconsole=0
```

### 5.3 Scripts de déploiement

```bash
# hu-deploy-mn.sh
# - Copie binaries
# - Création datadir
# - Configuration hu.conf
# - Création service systemd
# - Démarrage automatique
```

---

## SECTION 6 — Public Testnet Launch Packet

### 6.1 TESTNET-PIV2-PUBLIC.md (pour GitHub)

#### A. Installation PIV2

```bash
# Télécharger binary
wget https://github.com/PIV2-Core/releases/hu-testnet-v1.0.tar.gz
tar xzf hu-testnet-v1.0.tar.gz
cd hu-testnet

# Configuration
mkdir -p ~/.hu
cp hu.conf.example ~/.hu/hu.conf
# Éditer hu.conf avec vos paramètres
```

#### B. Lancer un node PIV2-testnet

```bash
./hud -testnet -daemon
./hu-cli -testnet getblockchaininfo
```

#### C. Lancer un masternode (optionnel)

```bash
# Prérequis: 10,000 PIV2 collateral
./hu-cli -testnet protx_register_fund ...
```

#### D. Faucet PIV2-testnet

```
URL: https://faucet.hu-chain.io
Limite: 100 PIV2 / 24h / adresse
```

#### E. Explorateur testnet

```
URL: https://explorer.hu-chain.io
```

#### F. RPC utiles

```bash
# État PIV2
./hu-cli -testnet gethustate

# Envoyer PIV2
./hu-cli -testnet sendtoaddress <addr> <amount>

# Info bloc
./hu-cli -testnet getblock <hash>

# Peers
./hu-cli -testnet getpeerinfo
```

---

## SECTION 7 — AUDIT + RED TEAM

### 7.1 Audit interne PIV2-core

#### Consensus

| Check | Status | Notes |
|-------|--------|-------|
| DMM production | ⬜ | Blocks produits correctement |
| Rotation MN | ⬜ | Distribution équitable |
| Block signatures | ⬜ | ECDSA valide |
| Scheduler timing | ⬜ | 2 sec respecté |
| Race conditions | ⬜ | Thread-safe |
| Deadlocks | ⬜ | Aucun |

#### Validation

| Check | Status | Notes |
|-------|--------|-------|
| CheckBlockMNOnly | ⬜ | Validation correcte |
| ConnectBlock | ⬜ | State transitions OK |
| Reorg rules | ⬜ | Max 12 blocs |
| Finalité | ⬜ | Si implémenté |

#### RPC Security

| Check | Status | Notes |
|-------|--------|-------|
| Pas d'injection | ⬜ | Input sanitized |
| Pas de bypass consensus | ⬜ | RPC safe |
| Outils dangereux désactivés | ⬜ | generate disabled |

#### Economic Invariants

| Check | Status | Notes |
|-------|--------|-------|
| C == U + Z | ⬜ | Toujours vrai |
| Cr == Ur | ⬜ | Toujours vrai |
| T >= 0 | ⬜ | Jamais négatif |
| No mint-from-thin-air | ⬜ | Supply contrôlée |

#### Mempool Rules

| Check | Status | Notes |
|-------|--------|-------|
| Standardness | ⬜ | TX standard only |
| HTLC allowed | ⬜ | Si P0 activé |
| DUST rules | ⬜ | Min amount respecté |

### 7.2 Red Team Protocol

#### Attaques autorisées

| Attaque | Objectif | Expected Result |
|---------|----------|-----------------|
| MN offline | Tester failover | Chain continue |
| Spam TX | Tester mempool | TX rejetées |
| Fork local | Tester reorg | Reorg ≤12 |
| Double-produce block | Tester scheduler | Rejet duplicate |
| Replay blocks | Tester validation | Blocks rejetés |
| Reorg >12 | Tester finalité | DOIT ÉCHOUER |
| Malformed signatures | Tester validation | Blocks rejetés |

#### Attaques interdites

- DOS des VPS
- Attaque hors-protocol
- Brute-force keys
- Spam RPC off-chain

#### Reporting

```markdown
# Format issue GitHub:
## Bug Report - Red Team
**Severity:** [Critical/High/Medium/Low]
**Attack:** [Description]
**Steps:** [Reproduction]
**Logs:** [Attached]
**Expected:** [Behavior]
**Actual:** [Behavior]
```

---

## SECTION 8 — GO/NO-GO Checklist

### Core

| Item | Status | Blocker? |
|------|--------|----------|
| Genesis PIV2-testnet remade | ⬜ | YES |
| All tests pass (77/77) | ✅ | YES |
| Scheduler stable >1000 blocs | ⬜ | YES |
| No reorg >12 | ⬜ | YES |
| No memory leak | ⬜ | YES |

### GitHub

| Item | Status | Blocker? |
|------|--------|----------|
| Repo créé | ⬜ | YES |
| README | ⬜ | YES |
| Docs core | ⬜ | NO |
| Scripts testnet | ⬜ | NO |
| Scripts deploy | ⬜ | NO |

### Infra

| Item | Status | Blocker? |
|------|--------|----------|
| VPS préparés | ⬜ | YES |
| Seeds configurés | ⬜ | YES |
| Faucet opérationnel | ⬜ | NO |
| Explorer opérationnel | ⬜ | NO |

### Public Docs

| Item | Status | Blocker? |
|------|--------|----------|
| TESTNET-PIV2-PUBLIC.md | ⬜ | YES |
| Audit instructions | ⬜ | NO |
| Red team instructions | ⬜ | NO |

---

## Ordre d'exécution

```
1️⃣ Terminer Section 1 (Tests DMM/Reorg)
   └── MN offline, shutdown/restart, reorg tests

2️⃣ Section 2 (Re-genesis)
   └── Clés, genesis block, CTestNetParams

3️⃣ Section 3 (GitHub cleanup)
   └── Supprimer legacy code, créer structure

4️⃣ Section 4 (Documentation)
   └── PIV2-CONSENSUS.md, PIV2-ECONOMICS.md

5️⃣ Section 5 (Infrastructure)
   └── VPS, configs, scripts

6️⃣ Section 6 (Launch packet)
   └── TESTNET-PIV2-PUBLIC.md

7️⃣ Section 7 (Audit)
   └── Internal audit, red team

8️⃣ Section 8 (GO/NO-GO)
   └── Final checklist

🚀 LAUNCH TESTNET PUBLIC
```

---

## Current Progress

| Section | Progress | Blocker |
|---------|----------|---------|
| 1. Tests PIV2-Core | 80% | Reorg tests |
| 2. Re-genesis | 0% | Waiting S1 |
| 3. GitHub cleanup | 30% | Waiting S2 |
| 4. Documentation | 20% | Can parallel |
| 5. Infrastructure | 0% | Waiting S2 |
| 6. Launch packet | 0% | Waiting S5 |
| 7. Audit | 0% | Waiting S6 |
| 8. GO/NO-GO | 0% | Waiting S7 |

---

*PLAN-TESTNET-PUBLIC.md v1.0*
