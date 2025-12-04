# PIV2-MASTERNODE-SPORK-USAGE.md

**Version:** 1.0
**Date:** 2025-12-04
**Purpose:** Usage map for legacy masternode and spork files - Phase 1.1 Analysis

---

## Executive Summary

This document maps the usage of legacy `masternode*.{cpp,h}` and `spork*.{cpp,h}` files to determine what can be safely removed vs what is still needed by the DMN/PIV2 system.

### Quick Classification

| Category | Files | Action |
|----------|-------|--------|
| **USED_BY_DMN_PIV2** | `masternodeman.*`, `masternode_meta_manager.*`, `spork.*`, `sporkdb.*` | KEEP - Refactor later |
| **LEGACY_ONLY** | `masternodeconfig.*`, `masternode-sync.*`, `rpc/masternode.cpp` | CANDIDATE_FOR_REMOVAL |
| **MOSTLY_DEAD** | `masternode-payments.*`, `masternode.*` | CANDIDATE_FOR_REMOVAL (keep stubs) |
| **QT_LEGACY** | `qt/masternodewizarddialog.*`, `qt/masternodeswidget.*` | PHASE 2 (post-testnet) |

---

## Part A: Masternode Files Analysis

### 1. masternode.cpp/h

**Location:** `src/masternode.cpp`, `src/masternode.h`
**Lines:** ~619 lines (header)

#### Classes & Functions

| Item | Type | Classification | Callers |
|------|------|----------------|---------|
| `CMasternodePing` | Class | LEGACY_ONLY | Legacy MN ping system |
| `CMasternodePing::CheckAndUpdate()` | Method | LEGACY_ONLY | net_processing.cpp (legacy msg) |
| `CMasternodePing::Relay()` | Method | LEGACY_ONLY | Legacy P2P |
| `CMasternode` | Class | LEGACY_ONLY | Legacy MN object |
| `CMasternode::UpdateFromNewBroadcast()` | Method | LEGACY_ONLY | Legacy msg processing |
| `CMasternode::GetActiveState()` | Method | LEGACY_ONLY | Status checks |
| `CMasternode::CalculateScore()` | Method | LEGACY_ONLY | Legacy scoring |
| `CMasternodeBroadcast` | Class | LEGACY_ONLY | RPC: createmasternodebroadcast |
| `CMasternodeBroadcast::Sign()` | Method | LEGACY_ONLY | RPC only |
| `MasternodeMinPingSeconds()` | Function | LEGACY_ONLY | Time constants |
| `MakeMasternodeRefForDMN()` | Function | **USED_BY_DMN_PIV2** | DMN payment compat |

**Recommendation:** `CANDIDATE_FOR_REMOVAL` - Only `MakeMasternodeRefForDMN()` needed, can be moved elsewhere.

---

### 2. masternodeman.cpp/h

**Location:** `src/masternodeman.cpp`, `src/masternodeman.h`
**Lines:** ~200 lines (header)

#### Classes & Functions

| Item | Type | Classification | Callers |
|------|------|----------------|---------|
| `CMasternodeDB` | Class | LEGACY_ONLY | tiertwo/init.cpp (mncache.dat) |
| `CMasternodeMan` (global `mnodeman`) | Class | **USED_BY_DMN_PIV2** | Multiple core files |
| `CMasternodeMan::Add()` | Method | DEAD_CODE | Never called |
| `CMasternodeMan::AskForMN()` | Method | DEAD_CODE | Never called |
| `CMasternodeMan::CheckAndRemove()` | Method | **USED_BY_DMN_PIV2** | validation, evo/deterministicmns |
| `CMasternodeMan::Clear()` | Method | DEAD_CODE | Never called |
| `CMasternodeMan::Find()` (3 variants) | Method | **USED_BY_DMN_PIV2** | 12 call sites |
| `CMasternodeMan::CheckSpentCollaterals()` | Method | **USED_BY_DMN_PIV2** | validation.cpp |
| `CMasternodeMan::GetNextMasternodeInQueueForPayment()` | Method | DEAD_CODE | Legacy payment queue |
| `CMasternodeMan::GetCurrentMasterNode()` | Method | DEAD_CODE | Never called |
| `CMasternodeMan::GetMnScores()` | Method | LEGACY_ONLY | rpc/masternode.cpp only |
| `CMasternodeMan::GetMasternodeRanks()` | Method | DEAD_CODE | Never called |
| `CMasternodeMan::ProcessMessage()` | Method | LEGACY_ONLY | net_processing.cpp |
| `CMasternodeMan::ProcessGetMNList()` | Method | LEGACY_ONLY | tiertwo_networksync.cpp |
| `CMasternodeMan::Remove()` | Method | **USED_BY_DMN_PIV2** | Cleanup + tests |
| `CMasternodeMan::CacheBlockHash()` | Method | **USED_BY_DMN_PIV2** | validation.cpp (2x) |
| `CMasternodeMan::UncacheBlockHash()` | Method | **USED_BY_DMN_PIV2** | validation.cpp |
| `CMasternodeMan::SetBestHeight()` | Method | **USED_BY_DMN_PIV2** | validation.cpp, init.cpp |
| `CMasternodeMan::CountEnabled()` | Method | **USED_BY_DMN_PIV2** | tiertwo_networksync, rpc/hu.cpp |
| `ThreadCheckMasternodes()` | Function | LEGACY_ONLY | tiertwo/init.cpp |

**Summary:**
- **USED_BY_DMN_PIV2:** 12 methods (validation, state tracking, cache)
- **LEGACY_ONLY:** 11 methods
- **DEAD_CODE:** 13 methods

**Recommendation:** `KEEP_FOR_NOW` - Critical for DMN validation. Refactor later to remove dead code.

---

### 3. masternodeconfig.cpp/h

**Location:** `src/masternodeconfig.cpp`, `src/masternodeconfig.h`
**Lines:** ~65 lines (header)

#### Classes & Functions

| Item | Type | Classification | Callers |
|------|------|----------------|---------|
| `CMasternodeConfig` | Class | LEGACY_ONLY | masternode.conf parsing |
| `CMasternodeConfig::read()` | Method | LEGACY_ONLY | tiertwo/init.cpp, rpc/masternode.cpp |
| `CMasternodeConfig::add()` | Method | LEGACY_ONLY | Qt only |
| `CMasternodeConfig::remove()` | Method | LEGACY_ONLY | Qt only |
| `CMasternodeConfig::getCount()` | Method | LEGACY_ONLY | tiertwo/init.cpp |
| `CMasternodeConfig::getEntries()` | Method | LEGACY_ONLY | RPC, init, Qt |

**Recommendation:** `CANDIDATE_FOR_REMOVAL` - Entire file is for deprecated masternode.conf system.

---

### 4. masternode-sync.cpp/h

**Location:** `src/masternode-sync.cpp`, `src/masternode-sync.h`
**Lines:** ~97 lines (header)

#### Classes & Functions

| Item | Type | Classification | Callers |
|------|------|----------------|---------|
| `CMasternodeSync` (global `masternodeSync`) | Class | LEGACY_ONLY | Superseded by DMN sync |
| `CMasternodeSync::GetSyncStatus()` | Method | LEGACY_ONLY | rpc/misc.cpp (mnsync RPC) |
| `CMasternodeSync::Reset()` | Method | LEGACY_ONLY | rpc/misc.cpp |
| `CMasternodeSync::Process()` | Method | LEGACY_ONLY | Legacy thread |
| `CMasternodeSync::NotCompleted()` | Method | LEGACY_ONLY | Legacy checks |
| `CMasternodeSync::MessageDispatcher()` | Method | LEGACY_ONLY | net_processing.cpp |

**Recommendation:** `CANDIDATE_FOR_REMOVAL` - Entire sync system is superseded by `tiertwo_sync_state`.

---

### 5. masternode-payments.cpp/h

**Location:** `src/masternode-payments.cpp`, `src/masternode-payments.h`
**Lines:** ~261 lines (header)

#### Classes & Functions

| Item | Type | Classification | Callers |
|------|------|----------------|---------|
| `CMasternodePaymentDB` | Class | DEAD_CODE | Stub implementation |
| `CMasternodePayee` | Class | DEAD_CODE | Stub |
| `CMasternodeBlockPayees` | Class | DEAD_CODE | Stub |
| `CMasternodePaymentWinner` | Class | DEAD_CODE | Stub |
| `CMasternodePayments` (global) | Class | **USED_BY_DMN_PIV2** | Payment interface |
| `CMasternodePayments::UpdatedBlockTip()` | Method | **USED_BY_DMN_PIV2** | ValidationInterface |
| `CMasternodePayments::CleanPaymentList()` | Method | LEGACY_ONLY | masternodeman.cpp |
| `CMasternodePayments::ProcessMessageMasternodePayments()` | Method | LEGACY_ONLY | net_processing.cpp |
| `IsCoinbaseValueValid()` | Function | **USED_BY_DMN_PIV2** | validation.cpp |
| `IsBlockPayeeValid()` | Function | DEAD_CODE | Stub (returns true) |
| `IsBlockValueValid()` | Function | DEAD_CODE | Stub (returns true) |
| `FillBlockPayee()` | Function | DEAD_CODE | Stub (no-op) |

**Summary:**
- **USED_BY_DMN_PIV2:** 3 functions (coinbase validation)
- **LEGACY_ONLY:** 5 methods
- **DEAD_CODE:** 15+ stub functions

**Recommendation:** `CANDIDATE_FOR_REMOVAL` - Keep only `IsCoinbaseValueValid()`, move to validation.cpp.

---

### 6. tiertwo/masternode_meta_manager.cpp/h

**Location:** `src/tiertwo/masternode_meta_manager.cpp/h`
**Lines:** ~95 lines (header)

#### Classes & Functions

| Item | Type | Classification | Callers |
|------|------|----------------|---------|
| `CMasternodeMetaInfo` | Class | **USED_BY_DMN_PIV2** | DMN peer metadata |
| `CMasternodeMetaInfo::GetLastOutboundAttempt()` | Method | **USED_BY_DMN_PIV2** | P2P connection |
| `CMasternodeMetaInfo::SetLastOutboundAttempt()` | Method | **USED_BY_DMN_PIV2** | P2P connection |
| `CMasternodeMetaMan` (global `g_mmetaman`) | Class | **USED_BY_DMN_PIV2** | DMN peer tracking |
| `CMasternodeMetaMan::GetMetaInfo()` | Method | **USED_BY_DMN_PIV2** | P2P code |
| `CMasternodeMetaMan::Clear()` | Method | DEAD_CODE | Never called |

**Recommendation:** `KEEP_FOR_NOW` - Essential for DMN P2P peer management.

---

### 7. rpc/masternode.cpp

**Location:** `src/rpc/masternode.cpp`
**Lines:** ~1050+ lines

#### RPC Functions

| Function | Classification | Usage |
|----------|----------------|-------|
| `mnping()` | LEGACY_ONLY | Legacy MN ping (regtest) |
| `initmasternode()` | **USED_BY_DMN_PIV2** | Init DMN/legacy MN |
| `listmasternodes()` | **USED_BY_DMN_PIV2** | Lists DMN + legacy |
| `getmasternodecount()` | **USED_BY_DMN_PIV2** | Count enabled MNs |
| `startmasternode()` | LEGACY_ONLY | Legacy masternode.conf |
| `createmasternodekey()` | LEGACY_ONLY | Legacy key gen |
| `getmasternodeoutputs()` | LEGACY_ONLY | Legacy collateral |
| `listmasternodeconf()` | LEGACY_ONLY | masternode.conf |
| `getmasternodestatus()` | LEGACY_ONLY | Legacy status |
| `getmasternodewinners()` | DEAD_CODE | Stub |
| `getmasternodescores()` | DEAD_CODE | Never used |
| `createmasternodebroadcast()` | LEGACY_ONLY | Legacy broadcast |
| `decodemasternodebroadcast()` | LEGACY_ONLY | Legacy decode |
| `relaymasternodebroadcast()` | LEGACY_ONLY | Legacy relay |

**Recommendation:** `CANDIDATE_FOR_REMOVAL` - Keep only `listmasternodes`, `getmasternodecount`, `initmasternode`. Remove 12+ legacy RPCs.

---

## Part B: Spork Files Analysis

### 1. sporkid.h

**Location:** `src/sporkid.h`

#### Defined Sporks (4 total)

| ID | Name | Default | Classification |
|----|------|---------|----------------|
| 10007 | `SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT` | OFF (4070908800) | **USED_BY_PIV2** |
| 10020 | `SPORK_20_SAPLING_MAINTENANCE` | OFF | **USED_BY_PIV2** |
| 10022 | `SPORK_22_HU_MAINTENANCE` | OFF | **UNUSED** |
| 10023 | `SPORK_23_HU_FINALITY_ENFORCEMENT` | OFF | **USED_BY_PIV2** |

#### Spork Usage Details

**SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT:**
- **Used in:** masternodeman.cpp (2x), masternode-sync.cpp, test/mnpayments_tests.cpp
- **Purpose:** Gates MN payment eligibility during network startup
- **Status:** CRITICAL for DMM payments

**SPORK_20_SAPLING_MAINTENANCE:**
- **Used in:** validation.cpp (2x), blockassembler.cpp, wallet/rpcwallet.cpp, qt/walletmodel.cpp, net_processing.cpp
- **Purpose:** Emergency switch to disable Sapling/ZKHU transactions
- **Status:** CRITICAL for Sapling system control

**SPORK_22_HU_MAINTENANCE:**
- **Used in:** NOWHERE (0 code references)
- **Purpose:** Reserved placeholder
- **Status:** UNUSED - safe to remove or repurpose

**SPORK_23_HU_FINALITY_ENFORCEMENT:**
- **Used in:** validation.cpp (ConnectBlock)
- **Purpose:** Enforces V6 PIV2 finality rules
- **Status:** CRITICAL for V6 consensus

---

### 2. spork.cpp/h

**Location:** `src/spork.cpp`, `src/spork.h`

#### CSporkManager Methods

| Method | Classification | Callers |
|--------|----------------|---------|
| `IsSporkActive(SporkId)` | **USED_BY_PIV2** | 8 files |
| `GetSporkValue(SporkId)` | **USED_BY_PIV2** | 2 files |
| `UpdateSpork()` | **USED_BY_PIV2** | rpc/misc.cpp |
| `ProcessSpork()` | **USED_BY_PIV2** | net_processing.cpp |
| `ProcessSporkMsg()` | **USED_BY_PIV2** | tiertwo_networksync.cpp |
| `SetPrivKey()` | **USED_BY_PIV2** | init.cpp |
| `LoadSporksFromDB()` | **USED_BY_PIV2** | init.cpp |
| `GetSporkNameByID()` | **USED_BY_PIV2** | Logging |
| `GetSporkIDByName()` | **USED_BY_PIV2** | rpc/misc.cpp |

**Recommendation:** `KEEP_FOR_NOW` - Spork system is essential for network governance.

---

### 3. sporkdb.cpp/h

**Location:** `src/sporkdb.cpp`, `src/sporkdb.h`

#### Methods

| Method | Classification | Usage |
|--------|----------------|-------|
| `WriteSpork()` | **USED_BY_PIV2** | Persist spork to LevelDB |
| `ReadSpork()` | **USED_BY_PIV2** | Load spork from DB |
| `SporkExists()` | **USED_BY_PIV2** | IBD check (net_processing.cpp) |

**Recommendation:** `KEEP_FOR_NOW` - Required for spork persistence.

---

## Part C: Qt Masternode Files

### Files Identified

| File | Classification | Recommendation |
|------|----------------|----------------|
| `qt/masternodewizarddialog.cpp` | LEGACY_ONLY | PHASE_2_CLEANUP |
| `qt/masternodewizarddialog.h` | LEGACY_ONLY | PHASE_2_CLEANUP |
| `qt/masternodeswidget.cpp` | LEGACY_ONLY | PHASE_2_CLEANUP |
| `qt/masternodeswidget.h` | LEGACY_ONLY | PHASE_2_CLEANUP |
| `qt/forms/masternodewizarddialog.ui` | UI Form | PHASE_2_CLEANUP |
| `qt/forms/masternodeswidget.ui` | UI Form | PHASE_2_CLEANUP |

**Reason:** PIV2 will have a new Qt PIV2-Light interface. Legacy PIVX masternode UI is not needed.

### Qt Cleanup Complexity

Removing these files requires updating **7+ files**:

1. **Makefile.qt.include** — Remove 6 entries:
   - Lines 33, 35: `.ui` forms
   - Lines 118, 120: `moc_*.cpp` generated files
   - Lines 230, 232: Headers
   - Lines 568, 570: Source files

2. **qt/pivxgui.h** — Remove:
   - Line 20: `#include "masternodeswidget.h"`
   - Line 72: `void goToMasterNodes();`
   - Line 142: `MasterNodesWidget *masterNodesWidget = nullptr;`

3. **qt/pivxgui.cpp** — Remove 11 references:
   - Line 126: `masterNodesWidget = new MasterNodesWidget(this);`
   - Line 135: `stackedContainer->addWidget(masterNodesWidget);`
   - Lines 199-200: Signal connects
   - Line 252: `masterNodesWidget->setClientModel(clientModel);`
   - Lines 502-503: `goToMasterNodes()` implementation
   - Lines 623, 639, 645: Model setters

4. **qt/navmenuwidget.h** — Remove:
   - Line 35: `void onMasterNodesClicked();`

5. **qt/navmenuwidget.cpp** — Remove:
   - Lines 36-37: `btnMaster` setup
   - Line 42: Remove from `btns` list
   - Line 68: `connect()` for btnMaster
   - Line 77: Keyboard shortcut (Key_5)
   - Lines 97-100: `onMasterNodesClicked()` implementation

6. **qt/forms/navmenuwidget.ui** — Remove btnMaster button element

7. **qt/res/images/** — Remove (optional):
   - `img-empty-dark-masternode.svg`
   - `img-empty-masternode.svg`

**Status:** Deferred to Phase 2 due to UI form file complexity. Does not block testnet.

---

## Summary: What To Do

### DEFERRED TO PHASE 2 (Qt UI cleanup)

```
qt/masternodewizarddialog.cpp
qt/masternodewizarddialog.h
qt/masternodeswidget.cpp
qt/masternodeswidget.h
qt/forms/masternodewizarddialog.ui
qt/forms/masternodeswidget.ui
+ 7 files need modification (see Part C above)
```

**Note:** Qt masternode UI removal requires coordinated changes to UI forms, main window, and navigation. Deferred to Phase 2 after testnet launch.

### CANDIDATE_FOR_REMOVAL (Phase 2.1 - With Refactoring)

```
masternodeconfig.cpp/h         → Remove masternode.conf support entirely
masternode-sync.cpp/h          → Remove legacy sync (use tiertwo_sync_state)
masternode.cpp/h               → Keep only MakeMasternodeRefForDMN(), move elsewhere
masternode-payments.cpp/h      → Keep only IsCoinbaseValueValid(), move to validation.cpp
rpc/masternode.cpp             → Remove 12+ legacy RPCs, keep 3 info RPCs
```

### KEEP_FOR_NOW (Phase 3 - Future Refactor)

```
masternodeman.cpp/h            → 12 methods used by DMN, remove 13 dead methods
tiertwo/masternode_meta_manager.cpp/h → Essential for DMN P2P
spork.cpp/h                    → Essential for network governance
sporkdb.cpp/h                  → Required for persistence
sporkid.h                      → Remove SPORK_22 if unused
```

### Spork Actions

| Spork | Action |
|-------|--------|
| SPORK_8 | KEEP |
| SPORK_20 | KEEP |
| SPORK_22 | REMOVE (unused) or IMPLEMENT |
| SPORK_23 | KEEP |

---

## Critical Code Paths (DO NOT BREAK)

The following code paths depend on masternode/spork files:

1. **Block Validation (validation.cpp):**
   - `mnodeman.SetBestHeight()`
   - `mnodeman.CacheBlockHash()`
   - `mnodeman.CheckSpentCollaterals()`
   - `IsCoinbaseValueValid()`
   - `sporkManager.IsSporkActive(SPORK_23)`

2. **DMN System (evo/*.cpp):**
   - `mnodeman.Find()`
   - `mnodeman.CheckAndRemove()`

3. **P2P (net_processing.cpp):**
   - `sporkManager.ProcessSpork()`
   - `g_mmetaman.GetMetaInfo()`

4. **Init (init.cpp, tiertwo/init.cpp):**
   - `sporkManager.LoadSporksFromDB()`
   - `masternodeConfig.read()` (legacy, can be removed)

---

## Next Steps

### Pre-Testnet (Phase 1)
- ✅ **Phase 1.1:** Analysis complete (this document)
- ⏸️ **Phase 1.2:** Qt masternode cleanup — DEFERRED (complex, 7+ files)

### Post-Testnet (Phase 2)
1. **Phase 2.0:** Remove legacy RPCs from rpc/masternode.cpp (12+ RPCs)
2. **Phase 2.1:** Remove masternodeconfig.*, masternode-sync.*
3. **Phase 2.2:** Stub out masternode-payments.* (keep IsCoinbaseValueValid)
4. **Phase 2.3:** Qt masternode UI cleanup (coordinated with PIV2-Light design)

### Future (Phase 3)
5. **Phase 3.0:** Refactor masternodeman.* to remove 13 dead methods
6. **Phase 3.1:** Remove SPORK_22 or implement it

---

*Document generated: 2025-12-04*
*Author: Claude (PIV2-Core Developer)*
