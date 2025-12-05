# PIV2 Testnet Bootstrap - Issues & Solutions

Documentation des problèmes rencontrés lors du déploiement du testnet DMM (Deterministic Masternode Model) et leurs solutions.

**Date:** 2025-12-05
**Version:** v0.9.0.0
**Commits:** 307bf08 → 632813c

---

## Table des matières

1. [Problème: IsBlockchainSynced() retourne false](#1-problème-isblockchainsyncedy-retourne-false)
2. [Problème: Genesis block hash mismatch Python vs C++](#2-problème-genesis-block-hash-mismatch-python-vs-c)
3. [Problème: MN ne s'initialise pas - IP mismatch](#3-problème-mn-ne-sinitialise-pas---ip-mismatch)
4. [Problème: Invalid mnoperatorprivatekey](#4-problème-invalid-mnoperatorprivatekey)
5. [Problème: Config section - clé non lue](#5-problème-config-section---clé-non-lue)
6. [Problème: fInitialDownload bloque l'init MN](#6-problème-finitialdownload-bloque-linit-mn)
7. [Problème: invalid-time-mask rejection](#7-problème-invalid-time-mask-rejection)
8. [Problème: masternode=1 manquant](#8-problème-masternode1-manquant)
9. [Résumé des clés finales](#9-résumé-des-clés-finales)

---

## 1. Problème: IsBlockchainSynced() retourne false

### Symptôme
```
initial_block_downloading: true
```
Le réseau reste bloqué à block 0, aucun bloc n'est produit.

### Cause
Le timestamp du genesis block était dans le futur ou trop ancien par rapport à l'heure actuelle. La fonction `IsBlockchainSynced()` compare le timestamp du dernier bloc avec l'heure actuelle.

### Solution
Régénérer le genesis block avec un timestamp actuel:

```python
# Dans contrib/genesis/genesis_piv2.py
'testnet': {
    'timestamp': "PIVHU Testnet Dec 2025 - Knowledge Hedge Unit - 3 MN DMM Genesis v3",
    'nTime': 1764892800,  # Dec 5, 2025 00:00:00 UTC
    'nBits': 0x1e0ffff0,
    'nVersion': 1,
},
```

### Commit
`307bf08` - feat(dmm): testnet bootstrap + generate RPC restriction

---

## 2. Problème: Genesis block hash mismatch Python vs C++

### Symptôme
Le script Python `genesis_piv2.py` trouve un nonce différent de celui attendu par le code C++:
```
Python: nNonce=1265040 → hash=0x00000xxx...
C++:    Expected different hash
```

### Cause
Les algorithmes de sérialisation/hashing diffèrent légèrement entre Python et C++. Le coinbase transaction format peut varier.

### Solution
Utiliser la fonction `MineGenesisBlock()` du code C++ pour miner le genesis:

```cpp
// Dans chainparams.cpp (temporairement)
#define MINE_GENESIS 1
#if MINE_GENESIS
    MineGenesisBlock(genesis, consensus.powLimit.IsNull() ?
        uint256S("0x00000ffff0000000000000000000000000000000000000000000000000000000") :
        consensus.powLimit);
#endif
```

Compiler, exécuter, récupérer les valeurs loggées, puis mettre à jour chainparams.cpp avec les bonnes valeurs.

### Valeurs finales (genesis v3)
```cpp
genesis = CreatePIVHUTestnetGenesisBlock(1764892800, 523385, 0x1e0ffff0, 1);

assert(consensus.hashGenesisBlock == uint256S("0x00000b7a70dec14ae970c1be025fd15ff82afae417618a162a879b545025cb66"));
assert(genesis.hashMerkleRoot == uint256S("0x482105bd7541476193efce04deb39e7f0f75dfa7d5b25503e0a7eca58c103c74"));
```

### Commit
`c477d43` - (non pushé, intégré dans les commits suivants)

---

## 3. Problème: MN ne s'initialise pas - IP mismatch

### Symptôme
```
ERROR: Local address 37.59.114.129:27171 does not match the address from ProTx (57.131.33.151:27171)
```
Le log "Deterministic Masternode initialized" n'apparaît jamais.

### Cause
Le code `activemasternode.cpp` vérifie que l'IP locale correspond à l'IP configurée dans le genesis MN. Pour les genesis MNs, cette vérification est trop stricte car:
1. Les MNs peuvent être testés sur d'autres machines
2. L'authentification par clé opérateur est suffisante

### Solution
Skip la vérification IP pour les genesis MNs (nRegisteredHeight == 0):

```cpp
// Dans activemasternode.cpp, fonction Init()
bool isGenesisMN = (dmn->pdmnState->nRegisteredHeight == 0);

if (!isGenesisMN && info.service != dmn->pdmnState->addr) {
    state = MASTERNODE_ERROR;
    strError = strprintf("Local address %s does not match the address from ProTx (%s)",
                         info.service.ToStringIPPort(), dmn->pdmnState->addr.ToStringIPPort());
    return;
}

if (isGenesisMN && info.service != dmn->pdmnState->addr) {
    LogPrintf("%s: Genesis MN detected, accepting local address %s (configured: %s)\n",
              __func__, info.service.ToStringIPPort(), dmn->pdmnState->addr.ToStringIPPort());
}

// Skip connectivity check for genesis MNs too
if (!Params().IsRegTestNet() && !isGenesisMN) {
    // ... connectivity check ...
}
```

### Commit
`23d8336` - fix(dmm): skip IP verification for genesis masternodes

---

## 4. Problème: Invalid mnoperatorprivatekey

### Symptôme
```
Error: Invalid mnoperatorprivatekey. Please see the documentation.
```
Le daemon refuse de démarrer avec les clés MN2 et MN3.

### Cause
Les clés WIF pour MN2 et MN3 avaient des **checksums Base58 invalides**. Elles avaient été mal générées ou mal copiées.

### Diagnostic
Script Python pour valider les checksums WIF:

```python
import hashlib

ALPHABET = '123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz'

def b58decode(s):
    n = 0
    for c in s:
        n = n * 58 + ALPHABET.index(c)
    result = []
    while n > 0:
        result.insert(0, n % 256)
        n //= 256
    for c in s:
        if c == ALPHABET[0]:
            result.insert(0, 0)
        else:
            break
    return bytes(result)

def validate_wif(wif):
    decoded = b58decode(wif)
    checksum = decoded[-4:]
    data = decoded[:-4]
    expected = hashlib.sha256(hashlib.sha256(data).digest()).digest()[:4]
    return checksum == expected

# Test
keys = [
    ("MN1", "cNixwpwjwk8wK7B8PfwHV6526rb6k5dFgrgi3cKQBQey1HjMVi8M"),
    ("MN2", "cVBxWVdmXXHhvXvdVs4XAR8WfjSoN5WTjUpPmJjQw7UVSUX2dnk7"),  # INVALIDE
    ("MN3", "cUFCB3pJ2VR9rhCLvB6tPUEkvf3eBSLQbzZrWM4e4HECRkJ8h7Xz"),  # INVALIDE
]

for name, wif in keys:
    print(f"{name}: {'VALID' if validate_wif(wif) else 'INVALID CHECKSUM'}")
```

### Solution
Régénérer des clés WIF valides avec leurs pubkeys correspondantes:

```python
import hashlib
import os

def create_wif(privkey_bytes, testnet=True, compressed=True):
    prefix = bytes([239 if testnet else 128])  # 0xEF for testnet
    data = prefix + privkey_bytes
    if compressed:
        data += bytes([1])
    checksum = hashlib.sha256(hashlib.sha256(data).digest()).digest()[:4]
    return b58encode(data + checksum)

# Générer nouvelles clés
for name in ["MN2", "MN3"]:
    privkey = os.urandom(32)
    wif = create_wif(privkey, testnet=True)
    pubkey = get_pubkey_compressed(privkey.hex())
    print(f"{name}: WIF={wif}, PubKey={pubkey}")
```

### Nouvelles clés valides
| MN | WIF | PubKey |
|----|-----|--------|
| MN1 | `cNixwpwjwk8wK7B8PfwHV6526rb6k5dFgrgi3cKQBQey1HjMVi8M` | `02841677a39503313fb368490d1e817ee46ce78de803ef26cc684f773bfe510730` |
| MN2 | `cW1vzmUdmkKxSz24jjW6nciTo98V1odocKF4X7gaT5g92hwNwWmW` | `02e6133571bcde14ef46f7d332cb517e273e66dafe886e2792e650a36da3e41392` |
| MN3 | `cQ9Fsq7xcN5z89JVH5fwT1qXg2sosT4GkPumFqU9XzuD5PkgeD6k` | `030867eff81e9210f23ae4b0df4282cfdd254e4c7357e59370f40171af1587a9ea` |

### Commit
`a5adb8f` - fix(dmm): regenerate valid MN2/MN3 operator keys

---

## 5. Problème: Config section - clé non lue

### Symptôme
Le log "Initializing deterministic masternode..." n'apparaît pas malgré une clé dans le config.

### Cause
Le paramètre `mnoperatorprivatekey` était placé dans la section `[test]` du fichier piv2.conf, mais ce paramètre doit être dans la section globale.

### Configuration INCORRECTE
```ini
testnet=1
server=1

[test]
port=27171
rpcport=27172
mnoperatorprivatekey=cNixwpwjwk8wK7B8PfwHV6526rb6k5dFgrgi3cKQBQey1HjMVi8M  # NON LU!
```

### Configuration CORRECTE
```ini
testnet=1
server=1
listen=1
mnoperatorprivatekey=cNixwpwjwk8wK7B8PfwHV6526rb6k5dFgrgi3cKQBQey1HjMVi8M  # GLOBAL

[test]
port=27171
rpcport=27172
rpcuser=piv2user
rpcpassword=testpassword
rpcallowip=127.0.0.1
```

### Règle générale
- Les paramètres réseau (port, rpcport, etc.) vont dans `[test]` ou `[main]`
- Les paramètres fonctionnels (mnoperatorprivatekey, txindex, etc.) vont dans la section globale

---

## 6. Problème: fInitialDownload bloque l'init MN

### Symptôme
```
UpdatedBlockTip: height=0, fInitialDownload=1, fMasterNode=1, state=0
```
Le log "Deterministic Masternode initialized" n'apparaît jamais, même avec une clé valide.

### Cause
La fonction `UpdatedBlockTip()` dans `activemasternode.cpp` retourne immédiatement si `fInitialDownload` est true. Au bootstrap (block 0), `fInitialDownload` est toujours true car il n'y a pas de blocs récents. Cela empêche:
1. L'initialisation du MN (`Init()` n'est jamais appelé après genesis)
2. Le démarrage du scheduler DMM

De même, `TryProducingBlock()` vérifie `IsBlockchainSynced()` qui retourne false au bootstrap.

### Solution
Ajouter un bypass pour la phase bootstrap (10 premiers blocs):

```cpp
// Dans activemasternode.cpp, fonction UpdatedBlockTip()
bool isBootstrapPhase = (pindexNew->nHeight < 10);

if (fInitialDownload && !isBootstrapPhase)
    return;

if (fInitialDownload && isBootstrapPhase) {
    LogPrintf("%s: Bootstrap phase detected (height=%d), allowing MN init\n",
              __func__, pindexNew->nHeight);
}
```

```cpp
// Dans activemasternode.cpp, fonction TryProducingBlock()
bool isBootstrap = (pindexPrev->nHeight < 10);

if (!isBootstrap && !g_tiertwo_sync_state.IsBlockchainSynced()) {
    return false;
}

if (isBootstrap) {
    LogPrintf("DMM-SCHEDULER: Bootstrap mode active (height=%d), bypassing sync check\n",
              pindexPrev->nHeight);
}
```

### Commit
`632813c` - fix(dmm): bootstrap bypass for genesis MN block production

---

## 7. Problème: invalid-time-mask rejection

### Symptôme
```
AcceptBlockHeader: ContextualCheckBlockHeader failed for block xxx: invalid-time-mask
DMM-SCHEDULER: Block xxx REJECTED
```
Les blocs sont créés et signés correctement mais rejetés par la validation.

### Cause
Le Time Protocol V2 (actif depuis height 0) exige que les timestamps des blocs soient des multiples de 15 secondes (`nTimeSlotLength = 15`). La fonction `UpdateTime()` dans `blockassembler.cpp` utilisait `GetAdjustedTime()` directement sans arrondir au time slot.

### Vérification
```cpp
// Dans consensus/params.h
bool IsValidBlockTimeStamp(int64_t nTime, int nHeight) const {
    if (IsTimeProtocolV2(nHeight)) {
        return (nTime % nTimeSlotLength) == 0;  // Must be multiple of 15
    }
    return true;
}
```

### Solution
Modifier `UpdateTime()` pour arrondir au time slot:

```cpp
// Dans blockassembler.cpp, fonction UpdateTime()
int64_t nNewTime = std::max(pindexPrev->GetMedianTimePast()+1, GetAdjustedTime());

// PIV2 Time Protocol V2: Round to valid time slot
int nHeight = pindexPrev->nHeight + 1;
if (consensusParams.IsTimeProtocolV2(nHeight)) {
    nNewTime = GetTimeSlot(nNewTime);
    // If rounding down puts us before median time past, round up
    if (nNewTime <= pindexPrev->GetMedianTimePast()) {
        nNewTime += consensusParams.nTimeSlotLength;
    }
}
```

### Commit
`632813c` - fix(dmm): bootstrap bypass for genesis MN block production

---

## 8. Problème: masternode=1 manquant

### Symptôme
Le log "IS DETERMINISTIC MASTERNODE" n'apparaît pas au démarrage.

### Cause
Le code vérifie `fMasterNode` qui est initialisé via l'argument `-masternode`. Simplement avoir `mnoperatorprivatekey` ne suffit pas.

### Vérification dans le code
```cpp
// Dans tiertwo/init.cpp
fMasterNode = gArgs.GetBoolArg("-masternode", DEFAULT_MASTERNODE);
```

### Solution
Ajouter `masternode=1` dans la section globale du config:

```ini
testnet=1
server=1
listen=1
masternode=1
mnoperatorprivatekey=cNixwpwjwk8wK7B8PfwHV6526rb6k5dFgrgi3cKQBQey1HjMVi8M

[test]
port=27171
rpcport=27172
```

---

## 9. Résumé des clés finales

### Genesis Masternodes (Testnet v3)

| MN | IP:Port | WIF Private Key | Status |
|----|---------|-----------------|--------|
| MN1 | 57.131.33.151:27171 | `cNixwpwjwk8wK7B8PfwHV6526rb6k5dFgrgi3cKQBQey1HjMVi8M` | OK |
| MN2 | 57.131.33.152:27171 | `cW1vzmUdmkKxSz24jjW6nciTo98V1odocKF4X7gaT5g92hwNwWmW` | OK |
| MN3 | 57.131.33.214:27171 | `cQ9Fsq7xcN5z89JVH5fwT1qXg2sosT4GkPumFqU9XzuD5PkgeD6k` | OK |

### Genesis Block (Testnet v3)
```
Hash:        0x00000b7a70dec14ae970c1be025fd15ff82afae417618a162a879b545025cb66
Merkle Root: 0x482105bd7541476193efce04deb39e7f0f75dfa7d5b25503e0a7eca58c103c74
nTime:       1764892800 (Dec 5, 2025 00:00:00 UTC)
nNonce:      523385
nBits:       0x1e0ffff0
```

### Déploiement VPS

```bash
# Sur chaque VPS
cd ~/piv2  # ou ~/PIV2-Core
git pull origin main
make -j4

# Nettoyer les anciennes données
rm -rf ~/.piv2/testnet5

# Créer le fichier de config
cat > ~/.piv2/piv2.conf << 'EOF'
testnet=1
server=1
listen=1
logtimestamps=1
txindex=1
mnoperatorprivatekey=<VOTRE_CLE_WIF>

[test]
port=27171
rpcport=27172
rpcuser=piv2user
rpcpassword=<MOT_DE_PASSE_SECURISE>
rpcallowip=127.0.0.1
EOF

# Lancer le daemon
~/piv2/src/piv2d -testnet -daemon

# Vérifier les logs
tail -f ~/.piv2/testnet5/debug.log | grep -E "DMM|SCHEDULER|Masternode|block"
```

### Vérifications post-démarrage

```bash
# Vérifier que le MN est initialisé
grep "Deterministic Masternode initialized" ~/.piv2/testnet5/debug.log

# Vérifier que le DMM scheduler fonctionne
grep "DMM-SCHEDULER" ~/.piv2/testnet5/debug.log

# Vérifier la production de blocs
~/piv2/src/piv2-cli -testnet getblockcount
```

---

## Checklist Mainnet

Avant le lancement mainnet, vérifier:

### Clés et Sécurité
- [ ] Générer de NOUVELLES clés pour les genesis MNs mainnet
- [ ] Valider les checksums WIF de toutes les clés (script Python fourni)
- [ ] Vérifier que les pubkeys correspondent aux WIF
- [ ] Stocker les clés de manière sécurisée (pas dans les repos!)

### Configuration
- [ ] `masternode=1` présent dans la section globale du config
- [ ] `mnoperatorprivatekey` présent dans la section globale (pas dans `[main]`)
- [ ] Vérifier que le timestamp genesis est correct (proche de l'heure de lancement)
- [ ] Documenter les IPs des MNs mainnet

### Tests pré-lancement
- [ ] Tester la production de blocs sur testnet pendant plusieurs jours
- [ ] Vérifier que tous les 3 MNs produisent des blocs en rotation
- [ ] Tester la synchronisation entre 3+ nœuds
- [ ] Vérifier les logs pour les messages "Bootstrap mode active" (uniquement premiers 10 blocs)
- [ ] Confirmer que les blocs passent la validation time-mask (multiples de 15s)

### Post-lancement
- [ ] Surveiller que `fInitialDownload` passe à false après quelques blocs
- [ ] Vérifier que `IsBlockchainSynced()` retourne true après stabilisation
- [ ] Confirmer que le bypass bootstrap se désactive après height 10

---

*Document généré le 2025-12-05 par Claude Code*
