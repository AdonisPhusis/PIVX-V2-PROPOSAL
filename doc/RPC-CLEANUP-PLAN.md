# PIV2-Core RPC Cleanup Plan

**Version:** 1.0
**Date:** 2025-12-04
**Status:** READY FOR IMPLEMENTATION

---

## Contexte

PIV2-Core est maintenant **MN-only DMM** (Deterministic Masternode Mining) :
- ❌ Pas de PoW (mining)
- ❌ Pas de PoS (staking)
- ✅ DMM uniquement (scheduler interne + DMN DIP3)
- ✅ Tests DMM validés (23k+ blocs automatiques)

---

## 1. Analyse de l'existant

### 1.1 RPC PIV2 (À GARDER - ✅ OK)

**Wallet PIV2** (`wallet/rpc_hu.cpp`):
| Commande | Description | Status |
|----------|-------------|--------|
| `hubalance` | Balance PIV2/KHU/ZKHU | ✅ OK |
| `hulistunspent` | UTXOs PIV2 | ✅ OK |
| `hugetinfo` | Info wallet PIV2 | ✅ OK |
| `husend` | Envoyer PIV2 | ✅ OK |
| `hurescan` | Rescan wallet | ✅ OK |
| `mint` | PIV2 → KHU | ✅ OK |
| `redeem` | KHU → PIV2 | ✅ OK |
| `lock` | KHU → ZKHU | ✅ OK |
| `unlock` | ZKHU → KHU + Yield | ✅ OK |
| `listlocked` | Notes ZKHU | ✅ OK |
| `hudiagnostics` | Diagnostics | ✅ OK |

**Consensus PIV2** (`rpc/hu.cpp`):
| Commande | Description | Status |
|----------|-------------|--------|
| `gethustate` | État global PIV2 | ✅ OK |
| `getstatecommitment` | Commitment à hauteur | ✅ OK |
| `domccommit` | Vote DOMC commit | ✅ OK |
| `domcreveal` | Vote DOMC reveal | ✅ OK |
| `getdaoinfo` | Info DAO Treasury | ✅ OK |

### 1.2 RPC EVO/ProTx (À GARDER - ✅ Utilisé par PIV2-DMM)

**EVO** (`rpc/rpcevo.cpp`) - Utilisé par `testnet_local.sh` :
| Commande | Description | Status |
|----------|-------------|--------|
| `generateoperatorkeypair` | Générer clé opérateur ECDSA | ✅ GARDER |
| `protx_list` | Liste DMN | ✅ GARDER |
| `protx_register` | Enregistrer DMN | ✅ GARDER |
| `protx_register_fund` | Enregistrer DMN + fund | ✅ GARDER (utilisé par testnet_local.sh) |
| `protx_register_prepare` | Préparer DMN | ✅ GARDER |
| `protx_register_submit` | Soumettre DMN | ✅ GARDER |
| `protx_revoke` | Révoquer DMN | ✅ GARDER |
| `protx_update_registrar` | Update registrar | ✅ GARDER |
| `protx_update_service` | Update service | ✅ GARDER |

### 1.3 RPC Masternode (NETTOYAGE REQUIS)

**Masternode legacy** (`rpc/masternode.cpp`):
| Commande | Description | Decision |
|----------|-------------|----------|
| `getmasternodecount` | Nombre de MN | ✅ GARDER |
| `getmasternodestatus` | Status MN local | ✅ GARDER |
| `listmasternodes` | Liste MN | ✅ GARDER |
| `getmasternodescores` | Scores MN | ⚠️ GARDER (debug) |
| `getmasternodewinners` | Winners MN | ⚠️ GARDER (debug) |
| `masternodecurrent` | MN courant | ⚠️ GARDER (debug) |
| `initmasternode` | Init MN | ⚠️ À VÉRIFIER |
| `createmasternodekey` | Créer clé MN | ❌ SUPPRIMER (remplacé par `generateoperatorkeypair`) |
| `getmasternodeoutputs` | UTXOs collatéral | ❌ SUPPRIMER (non utilisé) |
| `createmasternodebroadcast` | Broadcast legacy | ❌ SUPPRIMER |
| `decodemasternodebroadcast` | Decode legacy | ❌ SUPPRIMER |
| `relaymasternodebroadcast` | Relay legacy | ❌ SUPPRIMER |
| `startmasternode` | Start legacy | ❌ SUPPRIMER (cause erreur DIP3) |
| `listmasternodeconf` | masternode.conf | ❌ SUPPRIMER |
| `getcachedblockhashes` | Hidden | ⚠️ À VÉRIFIER |
| `mnping` | Hidden | ⚠️ À VÉRIFIER |

### 1.4 RPC Mining (À SUPPRIMER)

**Mining** (`rpc/mining.cpp`):
| Commande | Description | Decision |
|----------|-------------|----------|
| `estimatefee` | Estimation fee | ✅ GARDER |
| `estimatesmartfee` | Smart fee | ✅ GARDER |
| `prioritisetransaction` | Priorité tx | ✅ GARDER |
| `generate` | Générer blocs | ❌ SUPPRIMER (DMM uniquement) |
| `generatetoaddress` | Générer à adresse | ❌ SUPPRIMER |
| `submitblock` | Submit bloc | ⚠️ GARDER pour tests |

### 1.5 Wallet passphrase (NETTOYAGE)

**Wallet** (`wallet/rpcwallet.cpp` + `rpc/client.cpp`):
| Paramètre | Description | Decision |
|-----------|-------------|----------|
| `staking_only` dans `walletpassphrase` | PoS legacy | ❌ SUPPRIMER |

### 1.6 Legacy déjà supprimé (✅ OK)

- ❌ Zerocoin/zPIV : Non trouvé
- ❌ Cold Staking : Non trouvé

---

## 2. Plan d'action

### Phase 1: Masternode RPC Cleanup

**Fichier:** `src/rpc/masternode.cpp`

**À supprimer du tableau CRPCCommand:**
```cpp
// SUPPRIMER ces lignes:
{ "masternode", "createmasternodebroadcast", ... },
{ "masternode", "createmasternodekey", ... },        // Remplacé par generateoperatorkeypair
{ "masternode", "decodemasternodebroadcast", ... },
{ "masternode", "getmasternodeoutputs", ... },
{ "masternode", "listmasternodeconf", ... },
{ "masternode", "relaymasternodebroadcast", ... },
{ "masternode", "startmasternode", ... },
```

**À garder:**
```cpp
// GARDER ces lignes:
{ "masternode", "getmasternodecount", ... },
{ "masternode", "getmasternodescores", ... },
{ "masternode", "getmasternodestatus", ... },
{ "masternode", "getmasternodewinners", ... },
{ "masternode", "initmasternode", ... },            // Peut être utile pour DMM
{ "masternode", "listmasternodes", ... },
{ "masternode", "masternodecurrent", ... },
```

**Fonctions à supprimer:**
- `createmasternodebroadcast()`
- `createmasternodekey()`
- `decodemasternodebroadcast()`
- `getmasternodeoutputs()`
- `listmasternodeconf()`
- `relaymasternodebroadcast()`
- `startmasternode()`

### Phase 2: Mining RPC Cleanup

**Fichier:** `src/rpc/mining.cpp`

**À supprimer:**
```cpp
// SUPPRIMER ces lignes:
{ "hidden", "generate", &generate, ... },
{ "hidden", "generatetoaddress", &generatetoaddress, ... },
```

**Fonctions à supprimer:**
- `generate()`
- `generatetoaddress()`

**Fichier:** `src/rpc/client.cpp`

**À supprimer:**
```cpp
// SUPPRIMER ces lignes:
{ "setgenerate", 0, "generate" },
{ "setgenerate", 1, "genproclimit" },
```

### Phase 3: Wallet staking_only Cleanup

**Fichier:** `src/wallet/rpcwallet.cpp`

**Modifier `walletpassphrase`:**
- Supprimer le paramètre `staking_only`
- Ou le garder mais l'ignorer avec un warning

**Fichier:** `src/rpc/client.cpp`

**À supprimer:**
```cpp
// SUPPRIMER cette ligne:
{ "walletpassphrase", 2, "staking_only" },
```

---

## 3. Vérifications post-cleanup

### Build
```bash
cd PIVX && make -j4
```

### Tests
```bash
./src/test/test_pivx --run_test=hu_*
```

### Help clean
```bash
hu-cli help | grep -E "masternode|generate|staking"
# Ne doit PAS afficher: startmasternode, createmasternodebroadcast, generate, setgenerate
# DOIT afficher: getmasternodecount, getmasternodestatus, listmasternodes
```

---

## 4. Résumé final

### RPC à SUPPRIMER (7 masternode + 2 mining + 1 param)

| Catégorie | Commande | Raison |
|-----------|----------|--------|
| masternode | `createmasternodebroadcast` | Legacy pré-DIP3 |
| masternode | `createmasternodekey` | Remplacé par `generateoperatorkeypair` |
| masternode | `decodemasternodebroadcast` | Legacy pré-DIP3 |
| masternode | `getmasternodeoutputs` | Non utilisé |
| masternode | `listmasternodeconf` | masternode.conf obsolète |
| masternode | `relaymasternodebroadcast` | Legacy pré-DIP3 |
| masternode | `startmasternode` | Cause erreur DIP3 |
| mining | `generate` | DMM uniquement |
| mining | `generatetoaddress` | DMM uniquement |
| wallet | `staking_only` param | Pas de PoS |

### RPC à GARDER

**PIV2 (12):** hubalance, hulistunspent, hugetinfo, husend, hurescan, mint, redeem, lock, unlock, listlocked, hudiagnostics + gethustate, getstatecommitment, domccommit, domcreveal, getdaoinfo

**EVO/ProTx (9):** generateoperatorkeypair, protx_list, protx_register, protx_register_fund, protx_register_prepare, protx_register_submit, protx_revoke, protx_update_registrar, protx_update_service

**Masternode DMM (7):** getmasternodecount, getmasternodescores, getmasternodestatus, getmasternodewinners, initmasternode, listmasternodes, masternodecurrent

**Utilitaires:** estimatefee, estimatesmartfee, prioritisetransaction, submitblock

---

*RPC-CLEANUP-PLAN.md v1.0*
