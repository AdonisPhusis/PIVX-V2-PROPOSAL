# PIV2 Chain — Documentation Index

**Version:** 1.0 | **Date:** 2025-12-02 | **Status:** Community Proposal Ready

---

## Quick Links

| # | Document | Description |
|---|----------|-------------|
| 01 | [ARCHITECTURE](01-ARCHITECTURE.md) | Vision technique globale |
| 02 | [SPEC](02-SPEC.md) | Spécifications (formules, invariants, constantes) |
| 03 | [ROADMAP](03-ROADMAP.md) | Status actuel + prochaines étapes |

---

## Presentation (Pour la Communauté)

Documents non-techniques pour comprendre PIV2:

| # | Document | Contenu |
|---|----------|---------|
| 01 | [PROPOSAL-PIV2](presentation/01-PROPOSAL-PIV2.md) | Proposition complète + motivation |
| 02 | [AUDIT-DIFF](presentation/02-AUDIT-DIFF-PIVX-PIV2.md) | Comparaison PIVX v5 → PIV2 |
| 03 | [QU-EST-CE-QUE-PIV2](presentation/03-QU-EST-CE-QUE-PIV2.md) | Explication simple |
| 04 | [ECONOMIE-SIMPLE](presentation/04-ECONOMIE-SIMPLE.md) | Modèle économique R%/DOMC |
| 05 | [TRANSITION-MODELES](presentation/05-TRANSITION-MODELES.md) | 3 scénarios de migration |

---

## Blueprints (Pour les Développeurs)

Spécifications détaillées par module:

| # | Document | Domaine | Fichiers Code |
|---|----------|---------|---------------|
| 01 | [CONSENSUS-DMM-FINALITY](blueprints/01-CONSENSUS-DMM-FINALITY.md) | DMM + Finality 12/8 | `hu_quorum.*`, `hu_finality.*` |
| 02 | [CLEANROOM](blueprints/02-CLEANROOM.md) | Code legacy supprimé | *(suppressions)* |
| 03 | [BLOCK-REWARD-ZERO](blueprints/03-BLOCK-REWARD-ZERO.md) | Block reward = 0 | `validation.cpp` |
| 04 | [PIV2-STATE-MACHINE](blueprints/04-PIV2-STATE-MACHINE.md) | C/U/Z/Cr/Ur/T | `hu_state.*` |
| 05 | [MINT-REDEEM](blueprints/05-MINT-REDEEM.md) | PIV2 ↔ KHU_T | `hu_mint.*`, `hu_redeem.*` |
| 06 | [LOCK-UNLOCK-ZKHU](blueprints/06-LOCK-UNLOCK-ZKHU.md) | KHU_T ↔ ZKHU | `hu_stake.*`, `hu_unstake.*` |
| 07 | [YIELD-DOMC](blueprints/07-YIELD-DOMC.md) | R% yield + governance | `hu_yield.*`, `hu_domc.*` |
| 08 | [HTLC](blueprints/08-HTLC.md) | Atomic swaps | `script/conditional.*`, `rpc/conditional.cpp` |
| 09 | [RPC-WALLET](blueprints/09-RPC-WALLET.md) | Commandes RPC | `rpc_hu.cpp`, `hu_wallet.*` |
| 10 | [DAO-TREASURY](blueprints/10-DAO-TREASURY.md) | Treasury T | `hu_dao.*` |
| 11 | [QT-INTERFACE](blueprints/11-QT-INTERFACE.md) | Interface Qt | `qt/*` |
| 12 | [TESTNET-PUBLIC](blueprints/12-TESTNET-PUBLIC.md) | Testnet communauté | *(infrastructure)* |
| 13 | [COMMUNITY-PROPOSAL](blueprints/13-COMMUNITY-PROPOSAL.md) | Vote governance | *(process)* |

---

## Structure Code

```
PIVX/src/
├── hu/                  ← Module PIV2
│   ├── hu_state.*       ← C/U/Z/Cr/Ur/T state machine
│   ├── hu_validation.*  ← Block processing
│   ├── hu_mint.*        ← MINT operation
│   ├── hu_redeem.*      ← REDEEM operation
│   ├── hu_lock.*        ← LOCK operation (KHU_T → ZKHU)
│   ├── hu_unlock.*      ← UNLOCK operation (ZKHU → KHU_T)
│   ├── hu_yield.*       ← Daily yield calculation
│   ├── hu_domc.*        ← R% governance (DOMC voting)
│   ├── hu_dao.*         ← Treasury T
│   ├── hu_finality.*    ← 12/8 ECDSA finality
│   ├── hu_quorum.*      ← MN selection
│   └── zkhu_*.*         ← Shielded notes (Sapling)
├── wallet/
│   ├── rpc_hu.cpp       ← RPC commands
│   └── hu_wallet.*      ← Wallet integration
└── test/
    ├── hu_state_tests.cpp       ← Invariants C/U/Z, Cr/Ur, T
    ├── hu_operations_tests.cpp  ← MINT, REDEEM, LOCK, UNLOCK
    ├── hu_yield_tests.cpp       ← Yield, maturity, R%
    ├── hu_fees_tests.cpp        ← Fees PIVX, OP_RETURN
    ├── hu_dao_tests.cpp         ← Treasury, proposals
    ├── hu_transfers_tests.cpp   ← PIV2↔ZKHU transfers
    └── hu_integration_tests.cpp ← End-to-end scenarios
```

---

## Autres Ressources

| Dossier | Contenu |
|---------|---------|
| `personal/archive/` | Documents historiques (ancien ROADMAPs, reports) |
| `/CLAUDE.md` | Cadre anti-dérive pour développement |

---

## Liens Externes

- **Repository:** [github.com/AdonisPhusis/PIV2-Core](https://github.com/AdonisPhusis/PIV2-Core)
- **Branch:** `testnet-local-prep`
- **PIVX Issues:** [github.com/PIVX-Project/PIVX/issues](https://github.com/PIVX-Project/PIVX/issues) (78 → 0 avec PIV2)

---

*PIV2 Chain — December 2025*
