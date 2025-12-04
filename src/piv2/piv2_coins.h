// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef HU_HU_COINS_H
#define HU_HU_COINS_H

#include "amount.h"
#include "script/script.h"
#include "serialize.h"
#include "uint256.h"

/**
 * KHU UTXO Structure (Colored Coin)
 *
 * RÈGLE FONDAMENTALE: KHU_T est un colored coin UTXO
 *                     Structure IDENTIQUE à Coin (PIV UTXO)
 *                     SEULE différence: flags supplémentaires (coloring metadata)
 *
 * Source: docs/blueprints/02-KHU-COLORED-COIN.md
 * Phase: 2 (MINT/REDEEM)
 *
 * KHU_T = PIV UTXO + flag "fIsKHU"
 * ✅ Réutilise infrastructure UTXO PIVX existante
 * ✅ Compatible scripts Bitcoin standard
 * ✅ HTLC fonctionne automatiquement (Phase 7)
 */
struct CKHUUTXO {
    //! Montant KHU (CAmount = int64_t, identique à PIV)
    CAmount amount;

    //! Script de sortie (identique à PIV UTXO)
    CScript scriptPubKey;

    //! Hauteur de création (identique à PIV UTXO)
    uint32_t nHeight;

    //! ═══════════════════════════════════════════
    //! FLAGS SPÉCIFIQUES KHU (metadata coloring)
    //! ═══════════════════════════════════════════

    //! Flag: est-ce un UTXO KHU? (vs PIV)
    bool fIsKHU;

    //! Flag: KHU staké en ZKHU? (Phase 3 - LOCK/UNLOCK)
    //! ⚠️ Phase 2: toujours false (LOCK non implémenté)
    bool fLocked;

    //! Si staké: hauteur de début de lock (Phase 3)
    //! ⚠️ Phase 2: toujours 0 (LOCK non implémenté)
    uint32_t nLockStartHeight;

    //! Constructeur par défaut
    CKHUUTXO() : amount(0), nHeight(0), fIsKHU(true), fLocked(false), nLockStartHeight(0) {}

    //! Constructeur complet
    CKHUUTXO(CAmount amountIn, CScript scriptIn, uint32_t heightIn)
        : amount(amountIn), scriptPubKey(scriptIn), nHeight(heightIn),
          fIsKHU(true), fLocked(false), nLockStartHeight(0) {}

    //! Sérialisation (pour LevelDB persistence)
    SERIALIZE_METHODS(CKHUUTXO, obj) {
        READWRITE(obj.amount);
        READWRITE(obj.scriptPubKey);
        READWRITE(obj.nHeight);
        READWRITE(obj.fIsKHU);
        READWRITE(obj.fLocked);
        READWRITE(obj.nLockStartHeight);
    }

    //! Est-ce dépensé? (PIVX pattern)
    bool IsSpent() const {
        return amount == -1;
    }

    //! Marquer comme dépensé (PIVX pattern)
    void Clear() {
        amount = -1;
        scriptPubKey.clear();
    }

    //! Peut être dépensé? (pas staké)
    //! ⚠️ RÈGLE CRITIQUE: KHU staké NE PEUT PAS être REDEEM
    //! Source: docs/blueprints/03-MINT-REDEEM.md ligne 282
    bool IsSpendable() const {
        return !fLocked;
    }

    //! Initialiser à null (helper)
    void SetNull() {
        amount = 0;
        scriptPubKey.clear();
        nHeight = 0;
        fIsKHU = true;
        fLocked = false;
        nLockStartHeight = 0;
    }

    //! Est-ce null?
    bool IsNull() const {
        return amount == 0 && scriptPubKey.empty();
    }
};

/**
 * Outpoint KHU (TxHash + vout pour référencer UTXO)
 * Identique à COutPoint mais utilisé pour tracking KHU
 */
struct CKHUOutPoint {
    uint256 hash;  // Transaction hash
    uint32_t n;    // Output index

    CKHUOutPoint() : hash(), n(uint32_t(-1)) {}
    CKHUOutPoint(const uint256& hashIn, uint32_t nIn) : hash(hashIn), n(nIn) {}

    SERIALIZE_METHODS(CKHUOutPoint, obj) {
        READWRITE(obj.hash, obj.n);
    }

    bool IsNull() const { return hash.IsNull() && n == uint32_t(-1); }

    friend bool operator<(const CKHUOutPoint& a, const CKHUOutPoint& b) {
        return a.hash < b.hash || (a.hash == b.hash && a.n < b.n);
    }

    friend bool operator==(const CKHUOutPoint& a, const CKHUOutPoint& b) {
        return a.hash == b.hash && a.n == b.n;
    }

    friend bool operator!=(const CKHUOutPoint& a, const CKHUOutPoint& b) {
        return !(a == b);
    }
};

#endif // HU_HU_COINS_H
