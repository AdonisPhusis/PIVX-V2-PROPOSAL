// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef HU_SPORKID_H
#define HU_SPORKID_H

/**
 * HU Spork System
 *
 * Minimal spork set for HU consensus:
 * - SPORK_8:  Masternode payment enforcement (DMM)
 * - SPORK_20: Sapling/ZKHU maintenance mode
 * - SPORK_23: HU Finality enforcement
 */

enum SporkId : int32_t {
    SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT  = 10007,
    SPORK_20_SAPLING_MAINTENANCE            = 10020,
    SPORK_23_HU_FINALITY_ENFORCEMENT        = 10023,

    SPORK_INVALID                           = -1
};

struct CSporkDef
{
    CSporkDef(): sporkId(SPORK_INVALID), defaultValue(0) {}
    CSporkDef(SporkId id, int64_t val, std::string n): sporkId(id), defaultValue(val), name(n) {}
    SporkId sporkId;
    int64_t defaultValue;
    std::string name;
};

#endif // HU_SPORKID_H
