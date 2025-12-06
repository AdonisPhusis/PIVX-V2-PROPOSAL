# BLUEPRINT — DMM & HU INTERACTION (VERSION FINALE)

## 1. Deux couches indépendantes

### 1.1 DMM — Block Production (Liveness)

- Produit un bloc toutes les ~60 secondes
- **Ne dépend jamais de HU**
- Continue même si HU est cassé
- Fallback producer si producteur primaire offline

> **Garantie : La chaîne ne s'arrête jamais**

### 1.2 HU — Finality Layer (Security)

- Donne la finalité BFT sur les blocs
- Quorum fixe : **2/3 des membres du HU-quorum**
- Finalise un bloc (irréversible)
- Empêche reorg < lastFinalizedHeight

> **Garantie : La chaîne est sécurisée et irréversible**

---

## 2. Sélection des producteurs (déterministe)

### 2.1 Calcul de la liste ordonnée

Pour un bloc N, on calcule une liste ordonnée de producteurs candidats :

```cpp
producers = GetSortedMasternodesForBlock(N);  // tri par score déterministe
primary   = producers[0];
fallback1 = producers[1];
fallback2 = producers[2];
// ...
```

Cette liste est calculée sur **tous les DMNs enregistrés** (consensus), pas seulement les "online".

### 2.2 Fenêtres de production

En fonction du temps écoulé depuis le bloc précédent :

```cpp
// t = timeSincePrevBlock

if (t < leaderTimeout) {        // 30s sur testnet
    producerIndex = 0;          // primary producer
} else {
    // 1 + nombre de fenêtres fallback passées
    rawIndex      = 1 + (t - leaderTimeout) / fallbackWindow;  // 10s sur testnet
    producerIndex = rawIndex % producers.size();  // wrap-around modulo
}
```

Ensuite :
- **DMM local** : si `producers[producerIndex] == mon_proTxHash` → je produis le bloc
- **Tous les autres** : je n'ai pas le tour, je ne fais rien

### 2.3 Paramètres consensus

| Paramètre | Testnet | Mainnet |
|-----------|---------|---------|
| `nHuLeaderTimeoutSeconds` | 30s | 45s |
| `nHuFallbackRecoverySeconds` | 10s | 15s |
| `DMM_CHECK_INTERVAL_SECONDS` | 5s | 5s |
| `DMM_BLOCK_INTERVAL_SECONDS` | 60s | 60s |

---

## 3. Scheduler DMM — Fréquence de vérification

**Règle critique** : Le scheduler DMM doit vérifier fréquemment :

```
DMM_CHECK_INTERVAL_SECONDS << nHuFallbackRecoverySeconds
```

Exemple testnet :
- `nHuFallbackRecoverySeconds = 10s`
- `DMM_CHECK_INTERVAL_SECONDS = 5s`

→ On ne rate jamais son slot. Même si primary, fallback#1, fallback#2 sont offline,
on verra tôt ou tard une fenêtre où `producerIndex = moi`.

**Résultat** : Les masternodes offline restent dans la rotation (pour la vérification de signature),
mais en pratique ils ne "bloquent" plus la chaîne — ils font juste perdre du temps.

---

## 4. Rotation HU (Quorum-Cycle)

### Déclenchement

Le quorum HU change tous les `nHuQuorumRotationBlocks` (ex: toutes les 12 hauteurs de DMM).

> DMM sert uniquement de **timer**.

### Seed de rotation (sécurité BFT)

Le quorum HU n'est pas déterminé par DMM. Il est dérivé de :

```
seed = Hash(lastFinalizedBlockHash + cycleIndex)
```

où :
- `lastFinalizedBlockHash` = bloc finalisé HU (fortement cohérent)
- `cycleIndex = height / 12`

### Sélection déterministe

```cpp
quorum = DeterministicRandomSelect(allMNs, seed);
```

- Indépendant de l'état réseau
- Identique pour tous les nœuds honnêtes
- Impossible à manipuler par l'attaquant

> Sécurité maximale & résistance aux partitions réseau

---

## 5. Comportement en cas de panne

| État réseau | Finalité HU | DMM |
|-------------|-------------|-----|
| 🟩 2/3+ HU online | ✅ OK | ✅ Continue |
| 🟨 1/3–2/3 HU online | ⏸ Pause | ✅ Continue |
| 🟥 < 1/3 HU online | ❌ Arrêt temporaire | ✅ Continue |

Lors d'une panne majeure :
- Finalité HU = arrêt temporaire
- DMM = continue
- HU = se répare à chaque rotation grâce au tirage aléatoire
- Finalité revient naturellement dès qu'un quorum valide apparaît

---

## 6. Masternodes offline

### Règle consensus

> **On ne peut pas utiliser "online/offline" comme critère d'acceptation d'un bloc.**

Les masternodes offline restent visibles dans la liste globale car :
- L'"online" est local et sujet à perception
- Un attaquant peut faire croire qu'un node est down (eclipse)
- La vérification doit rester 100% déterministe à partir de la liste DMN on-chain

### Règle liveness

> **La chaîne n'attend jamais indéfiniment un MN offline.**

Avec le fix `DMM_CHECK_INTERVAL_SECONDS + fallback rotation` :
- Les slots des MN offline deviennent des "créneaux perdus"
- Le prochain MN online dans la rotation enchaîne

### PoSe (future amélioration)

Un MN systématiquement absent sera pénalisé via PoSe :
- Incrémente un compteur de pénalité
- À un seuil → retiré de l'active set via tx DMN
- Ne sera plus dans `producers[]`

---

## 7. Règles de consensus — Résumé

### Production de blocs (DMM)

```cpp
if (IsBlockchainSynced()) {
    ProduceNewBlock();
}
```

→ Ne regarde jamais HU
→ Continue même sans finalité

### HU Finality

```cpp
if (signatures >= 2/3 * quorumSize) {
    FinalizeBlock();
    Update(lastFinalizedHeight);
}
```

### Anti-Reorg

```cpp
RejectReorgBelow(lastFinalizedHeight);
```

---

## 8. Résumé ultra-compact

1. **DMM produit les blocs** — indépendamment de HU
2. **HU finalise les blocs** — couche séparée BFT
3. **Rotation HU** toutes les 12 hauteurs DMM
4. **Seed de quorum** = `Hash(lastFinalizedBlock + cycleIndex)`
5. **Sélection quorum** = déterministe, aléatoire, indépendante du réseau
6. **Finalité** = 2/3 du HU quorum
7. **DMM ne dépend jamais de HU**
8. **HU peut s'arrêter temporairement, DMM jamais**
9. **Le prochain cycle HU répare tout automatiquement**
10. **Scheduler DMM vérifie toutes les 5s** pour ne jamais rater son slot

---

## 9. Pseudo-code des fonctions clés

### GetBlockProducerWithFallback()

```cpp
function GetBlockProducerWithFallback(prevBlock, mnList, currentTime):
    scores = CalculateBlockProducerScores(prevBlock, mnList)
    if scores.empty():
        return null

    timeSincePrev = currentTime - prevBlock.time

    if timeSincePrev <= leaderTimeout:
        producerIndex = 0  // primary
    else:
        fallbackTime = timeSincePrev - leaderTimeout
        rawIndex = 1 + (fallbackTime / fallbackWindow)
        producerIndex = rawIndex % scores.size()  // wrap-around

    return scores[producerIndex].mn
```

### DMM-SCHEDULER Thread

```cpp
function DMMSchedulerThread():
    while running:
        sleep(DMM_CHECK_INTERVAL_SECONDS)  // 5s

        if not IsBlockchainSynced():
            continue

        tip = GetChainTip()
        producer = GetBlockProducerWithFallback(tip, mnList, now())

        if producer == localMN:
            if (now - lastBlockProduced) >= DMM_BLOCK_INTERVAL_SECONDS:
                TryProducingBlock(tip)
```

---

## 10. Fichiers source concernés

| Fichier | Rôle |
|---------|------|
| `src/evo/blockproducer.cpp` | `GetBlockProducerWithFallback()`, `VerifyBlockProducerSignature()` |
| `src/activemasternode.cpp` | DMM-SCHEDULER thread, `TryProducingBlock()` |
| `src/activemasternode.h` | Constantes `DMM_CHECK_INTERVAL_SECONDS`, `DMM_BLOCK_INTERVAL_SECONDS` |
| `src/piv2/piv2_finality.cpp` | HU Finality layer |
| `src/piv2/piv2_signaling.cpp` | HU Signaling (signatures) |
| `src/consensus/params.h` | `nHuLeaderTimeoutSeconds`, `nHuFallbackRecoverySeconds` |
