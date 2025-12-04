# PIVX V2 Chain - Nomenclature Officielle

**Version:** 1.2
**Date:** 2025-12-04
**Status:** DÉFINITIF

---

## Coins

| Symbole | Nom | Description |
|---------|-----|-------------|
| **PIV2** | PIVX V2 | Coin natif transparent de la chain |
| **sHU** | Shielded PIVX V2 | PIVX V2 en mode privé (Sapling Z-to-Z) |
| **KHU** | Locked PIVX V2 | PIVX V2 locké (préparation au staking) |
| **ZKHU** | Staked KHU | KHU staké avec yield (shielded Sapling) |

---

## Opérations

| Commande | De → Vers | Description |
|----------|-----------|-------------|
| `shield` | PIV2 → sHU | Rendre privé (Z-to-Z) |
| `unshield` | sHU → PIV2 | Rendre transparent |
| `mint` | PIV2 → KHU | Locker pour préparer staking |
| `redeem` | KHU → PIV2 | Délocker (sortir du lock) |
| `lock` | KHU → ZKHU | Staker (yield commence) |
| `unlock` | ZKHU → KHU + Yield | Récupérer + rendement |

---

## Flow Complet

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

## Variables d'État

| Variable | Description | Formule |
|----------|-------------|---------|
| `C` | Collateral total | C = U + Z |
| `U` | Supply PIVX V2 transparent | - |
| `Z` | Supply ZKHU shielded | - |
| `Cr` | Reward pool | Cr = Ur |
| `Ur` | Reward rights | - |
| `T` | DAO Treasury | T >= 0 |

---

## Invariants

```
INVARIANT_1:  C == U + Z
INVARIANT_2:  Cr == Ur
INVARIANT_3:  T >= 0
```

---

## RPC Commands

### Wallet RPC (catégorie `piv2`)

| Commande | Description |
|----------|-------------|
| `piv2balance` | Balance PIVX V2/KHU détaillée |
| `piv2listunspent` | Liste UTXOs KHU non dépensés |
| `piv2getinfo` | Info complète wallet PIVX V2 |
| `piv2send` | Envoyer KHU_T à une adresse |
| `piv2rescan` | Rescan wallet pour KHU |
| `piv2diagnostics` | Diagnostic wallet PIVX V2 |
| `mint <amount>` | PIV2 → KHU |
| `redeem <amount>` | KHU → PIV2 |
| `lock <amount>` | KHU → ZKHU |
| `unlock [commitment]` | ZKHU → KHU + Yield |
| `listlocked` | Liste positions ZKHU |

### Info RPC (catégorie `piv2`)

| Commande | Description |
|----------|-------------|
| `getpiv2state` | État global (C, U, Z, T, R%) |
| `getdaoinfo` | Info DAO Treasury |
| `getstatecommitment` | Commitment état à une hauteur |
| `domccommit` | Soumettre vote DOMC (commit) |
| `domcreveal` | Révéler vote DOMC |

---

## Migration depuis anciennes versions

| Ancienne commande | Nouvelle commande |
|-------------------|-------------------|
| `khubalance` | `piv2balance` |
| `khulistunspent` | `piv2listunspent` |
| `khugetinfo` | `piv2getinfo` |
| `khusend` | `piv2send` |
| `khurescan` | `piv2rescan` |
| `khudiagnostics` | `piv2diagnostics` |
| `getkhustate` / `gethustate` | `getpiv2state` |
| `khudaoinfo` | `getdaoinfo` |

---

## Convention de nommage

| Contexte | Format | Exemple |
|----------|--------|---------|
| Documentation, textes | PIVX V2 | "PIVX V2 is a fork of PIVX" |
| Commandes RPC | piv2* | `piv2balance`, `getpiv2state` |
| Fichiers source | hu_*, piv2_* | `hu_state.cpp`, `rpc_hu.cpp` |
| Binaires | piv2d, piv2-cli | `./piv2d -regtest` |
| Ticker/symbole | PIV2 | "100 PIV2" |

---

*00-NOMENCLATURE.md v1.2*
