// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "piv2/piv2_statedb.h"
#include "piv2/piv2_domc.h"

#include "util/system.h"
#include "streams.h"

static const char DB_KHU_STATE = 'K';
static const char DB_KHU_STATE_PREFIX = 'S';
static const char DB_KHU_UTXO_PREFIX = 'U';

CKHUStateDB::CKHUStateDB(size_t nCacheSize, bool fMemory, bool fWipe) :
    CDBWrapper(GetDataDir() / "khu" / "state", nCacheSize, fMemory, fWipe)
{
}

bool CKHUStateDB::WriteKHUState(int nHeight, const HuGlobalState& state)
{
    return Write(std::make_pair(DB_KHU_STATE, std::make_pair(DB_KHU_STATE_PREFIX, nHeight)), state);
}

bool CKHUStateDB::ReadKHUState(int nHeight, HuGlobalState& state)
{
    return Read(std::make_pair(DB_KHU_STATE, std::make_pair(DB_KHU_STATE_PREFIX, nHeight)), state);
}

bool CKHUStateDB::ExistsKHUState(int nHeight)
{
    return Exists(std::make_pair(DB_KHU_STATE, std::make_pair(DB_KHU_STATE_PREFIX, nHeight)));
}

bool CKHUStateDB::EraseKHUState(int nHeight)
{
    return Erase(std::make_pair(DB_KHU_STATE, std::make_pair(DB_KHU_STATE_PREFIX, nHeight)));
}

HuGlobalState CKHUStateDB::LoadKHUState_OrGenesis(int nHeight)
{
    HuGlobalState state;

    if (ReadKHUState(nHeight, state)) {
        return state;
    }

    // Return genesis state if not found
    state.SetNull();
    state.nHeight = nHeight;

    // PIVHU Genesis State Initialization
    // At genesis (height 0), set initial parameters:
    // - T = 500,000 PIVHU initial DAO Treasury
    // - R_annual = 40% (4000 basis points)
    // - R_MAX_dynamic = 40% (initial ceiling)
    // - DOMC cycle starts at block 0
    if (nHeight == 0) {
        state.T = khu_domc::T_GENESIS_INITIAL;
        state.R_annual = khu_domc::R_DEFAULT;
        state.R_MAX_dynamic = khu_domc::R_MAX_DYNAMIC_INITIAL;
        state.domc_cycle_start = 0;
        state.domc_cycle_length = khu_domc::GetDomcCycleLength();
        state.domc_commit_phase_start = khu_domc::GetDomcVoteOffset();
        state.domc_reveal_deadline = khu_domc::GetDomcRevealHeight();
    }

    return state;
}

// ═══════════════════════════════════════════════════════════════════════════
// KHU UTXO Persistence
// ═══════════════════════════════════════════════════════════════════════════

bool CKHUStateDB::WriteKHUUTXO(const COutPoint& outpoint, const CKHUUTXO& utxo)
{
    return Write(std::make_pair(DB_KHU_UTXO_PREFIX, outpoint), utxo);
}

bool CKHUStateDB::ReadKHUUTXO(const COutPoint& outpoint, CKHUUTXO& utxo)
{
    return Read(std::make_pair(DB_KHU_UTXO_PREFIX, outpoint), utxo);
}

bool CKHUStateDB::EraseKHUUTXO(const COutPoint& outpoint)
{
    return Erase(std::make_pair(DB_KHU_UTXO_PREFIX, outpoint));
}

bool CKHUStateDB::ExistsKHUUTXO(const COutPoint& outpoint)
{
    return Exists(std::make_pair(DB_KHU_UTXO_PREFIX, outpoint));
}

bool CKHUStateDB::LoadAllKHUUTXOs(std::vector<std::pair<COutPoint, CKHUUTXO>>& utxos)
{
    std::unique_ptr<CDBIterator> pcursor(NewIterator());

    // Seek to start of UTXO prefix
    pcursor->Seek(std::make_pair(DB_KHU_UTXO_PREFIX, COutPoint()));

    while (pcursor->Valid()) {
        std::pair<char, COutPoint> key;
        if (pcursor->GetKey(key) && key.first == DB_KHU_UTXO_PREFIX) {
            CKHUUTXO utxo;
            if (pcursor->GetValue(utxo)) {
                utxos.emplace_back(key.second, utxo);
            }
            pcursor->Next();
        } else {
            break;
        }
    }

    return true;
}
