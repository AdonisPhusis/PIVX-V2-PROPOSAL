# BLUEPRINT — SYNCHRONISATION / COLDSYNC (VERSION FINALE)

## 1. Les 4 États d'un Masternode

```
OFFLINE → STARTUP → COLD_SYNC → SYNCED
```

Un masternode ne doit **jamais produire ni signer** quand il n'est pas totalement synchronisé.

---

## 2. État : SYNCED

> Le masternode est autorisé à participer au réseau (DMM + HU)

Un MN est SYNCED si **toutes** ces conditions sont vraies :

1. Il a le tip actuel (pas de retard de plus de 2 blocs)
2. Il connaît le dernier bloc finalisé HU
3. Il a appliqué tous les blocs jusqu'au tip
4. `IsInitialBlockDownload() == false`
5. Aucun peer n'annonce une chaîne plus longue
6. Le bloc précédent a été finalisé OU est au-dessus de la finalité HU

Quand toutes ces conditions sont vraies :

```cpp
IsBlockchainSynced() = true
```

→ Il peut produire (DMM)
→ Il peut signer HU
→ Il peut participer au quorum

---

## 3. État : COLD_SYNC

> Le masternode DOIT SE RATTRAPER, il ne peut pas produire / signer

Un MN entre en cold sync quand :

- Il n'a pas reçu de bloc depuis longtemps (ex. >5 min)
- OU le tip local est trop vieux
- OU le bloc finalisé local est en retard
- OU un peer annonce une hauteur supérieure
- OU le node redémarre après plusieurs heures/jours
- OU un reindex vient de se terminer
- OU il vient de se connecter à 3 peers sans pouvoir valider le tip

Dans cet état :

```cpp
IsBlockchainSynced() = false
CanProduceBlock() = false
CanSignHU() = false
```

Il doit faire :

- ✔ Téléchargement intensif des headers
- ✔ Vérification des finalités HU
- ✔ Validation de tous les blocs manquants
- ✔ Comparaison des 3 meilleurs peers
- ✔ Rollback si la branche locale viole HU-finality

Quand il rejoint la meilleure chaîne ET connaît la dernière finalité :
→ Il sort du COLD_SYNC et devient SYNCED.

---

## 4. État : STARTUP

> Juste après lancement ou reindex

Actions automatiques :

1. Attendre les premiers peers
2. Charger le `lastFinalizedHeight` depuis la base HU
3. IBD rapide (fast-sync si possible)
4. Ne RIEN signer
5. Ne RIEN produire

Condition :

```cpp
if (tipAge > STALE_TIMEOUT)
    → switch to COLD_SYNC
else if (headers match)
    → continue to SYNCED transition
```

---

## 5. État : OFFLINE

> Il ne participe à rien

---

## 6. Règle d'Or

> **Un masternode NE produit NI signe tant qu'il n'est pas SYNCED.**

C'est ainsi que tu évites :
- qu'il parte sur une "chaîne dans son coin"
- qu'il signe un bloc invalide
- qu'il produise sur un mauvais tip
- qu'il crée une branche fantôme

---

## 7. Pseudo-code : IsBlockchainSynced()

```cpp
bool IsBlockchainSynced() {
    // 0. Bootstrap phase - always synced
    if (height <= bootstrap)
        return true;

    // 1. Cold start priority (if chain stale)
    // Permet de produire même si stale pour ne pas bloquer
    if (tipAge > STALE_CHAIN_TIMEOUT)
        return true;

    // 2. Recent HU finality overrides IBD checks
    // Si on a une finalité récente, on est synced
    if (now - lastFinalizedTime <= SYNC_WINDOW)
        return true;

    // 3. CRITICAL: If best peer is ahead, we're NOT synced
    // Doit télécharger les blocs avant de produire
    if (bestPeerHeight > localHeight)
        return false;

    // 4. If still in IBD → not synced
    if (IsInitialBlockDownload())
        return false;

    // 5. Otherwise synced
    return true;
}
```

---

## 8. Pseudo-code : ShouldProduceBlock()

```cpp
bool ShouldProduceBlock() {
    // Must be blockchain synced first
    if (!IsBlockchainSynced())
        return false;

    // Must not be behind any peer
    if (bestPeerHeight > localHeight + 2)
        return false;

    return true;  // DMM can produce
}
```

---

## 9. Pseudo-code : ShouldSignHU()

```cpp
bool ShouldSignHU() {
    // Must be blockchain synced
    if (!IsBlockchainSynced())
        return false;

    // Must have received the block we're signing
    if (!HaveBlock(blockHash))
        return false;

    return true;  // Can sign HU
}
```

---

## 10. Paramètres de Synchronisation

| Paramètre | Testnet | Mainnet | Description |
|-----------|---------|---------|-------------|
| `STALE_CHAIN_TIMEOUT` | 300s | 600s | Tip trop vieux = cold start |
| `SYNC_WINDOW` | 120s | 300s | Fenêtre de finalité récente |
| `MAX_TIP_AGE` | 3600s | 7200s | Age max du tip avant IBD |
| `PEER_HEIGHT_TOLERANCE` | 2 | 2 | Blocs de retard autorisés |

---

## 11. Diagramme d'État

```
                     ┌──────────────┐
                     │   OFFLINE    │
                     └──────┬───────┘
                            │ daemon start
                            ▼
                     ┌──────────────┐
                     │   STARTUP    │
                     └──────┬───────┘
                            │ peers connected
                            ▼
         ┌──────────────────┴──────────────────┐
         │                                      │
         ▼                                      ▼
┌──────────────────┐                  ┌──────────────────┐
│    COLD_SYNC     │◀────────────────▶│      SYNCED      │
│                  │   tip behind      │                  │
│ - Download blocks│   or stale        │ - Produce blocks │
│ - Verify HU      │                   │ - Sign HU        │
│ - Catch up       │                   │ - Participate    │
└──────────────────┘                  └──────────────────┘
```

---

## 12. Comportements Attendus

1. **Démarrage** → STARTUP
2. **Connexion aux peers** → vérification des hauteurs
3. **Si en retard** → COLD_SYNC (téléchargement)
4. **Si rattrape** → SYNCED
5. **SYNCED** → peut produire DMM & signer HU
6. **Si décroche** (retard, mauvaise branche) → retourne en COLD_SYNC
7. **Automatique, autonome, sans intervention**

---

## 13. Protection contre les Forks Fantômes

Avec ce design :

- ❌ Plus de masternode qui fait sa chaîne en solo
- ❌ Plus de mauvaise signature HU
- ❌ Plus de DMM qui produit à côté
- ❌ Plus de bloc fantôme ou "îlot réseau"

---

## 14. Fichiers Source Concernés

| Fichier | Rôle |
|---------|------|
| `src/tiertwo/tiertwo_sync_state.cpp` | `IsBlockchainSynced()` |
| `src/validation.cpp` | `IsInitialBlockDownload()` |
| `src/activemasternode.cpp` | DMM-SCHEDULER avec vérification sync |
| `src/piv2/piv2_signaling.cpp` | HU Signaling avec vérification sync |
| `src/net_processing.cpp` | Peer height tracking |

---

## 15. Résumé Ultra-Compact

1. **4 états** : OFFLINE → STARTUP → COLD_SYNC → SYNCED
2. **Jamais produire si pas SYNCED**
3. **Jamais signer HU si pas SYNCED**
4. **Télécharger avant de produire**
5. **Vérifier les peers avant de produire**
6. **Rollback si branche viole HU-finality**
7. **Automatique et autonome**
