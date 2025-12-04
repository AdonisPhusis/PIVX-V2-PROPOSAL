// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef HU_HU_ZKHU_DB_H
#define HU_HU_ZKHU_DB_H

#include "dbwrapper.h"
#include "piv2/zkpiv2_note.h"
#include "sapling/incrementalmerkletree.h"
#include "uint256.h"

/**
 * CZKHUTreeDB - ZKHU Database (namespace 'K')
 *
 * Source: docs/blueprints/05-ZKHU-SAPLING-LOCK.md section 4
 * Phase: 4 (ZKHU Staking)
 *
 * RÈGLE CRITIQUE: ZKHU utilise namespace 'K', Shield utilise 'S'/'s'
 * Aucun chevauchement de clés entre ZKHU et Shield.
 *
 * Key Prefixes:
 * - 'K' + 'A' + anchor → SaplingMerkleTree (ZKHU anchor)
 * - 'K' + 'N' + nullifier → bool (ZKHU nullifier spent flag)
 * - 'K' + 'T' + note_id → ZKHUNoteData (ZKHU note metadata)
 * - 'K' + 'L' + nullifier → cm (nullifier→commitment mapping for UNLOCK)
 */
class CZKHUTreeDB : public CDBWrapper
{
public:
    explicit CZKHUTreeDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

private:
    CZKHUTreeDB(const CZKHUTreeDB&);
    void operator=(const CZKHUTreeDB&);

public:
    /**
     * Anchor operations
     */
    bool WriteAnchor(const uint256& anchor, const SaplingMerkleTree& tree);
    bool ReadAnchor(const uint256& anchor, SaplingMerkleTree& tree) const;

    /**
     * Nullifier operations
     */
    bool WriteNullifier(const uint256& nullifier);
    bool IsNullifierSpent(const uint256& nullifier) const;
    bool EraseNullifier(const uint256& nullifier);

    /**
     * Note operations
     */
    bool WriteNote(const uint256& noteId, const ZKHUNoteData& data);
    bool ReadNote(const uint256& noteId, ZKHUNoteData& data) const;
    bool EraseNote(const uint256& noteId);

    /**
     * Nullifier → Commitment mapping (for UNLOCK lookup)
     * Phase 5: Required for ApplyHUUnlock to find note from nullifier
     */
    bool WriteNullifierMapping(const uint256& nullifier, const uint256& cm);
    bool ReadNullifierMapping(const uint256& nullifier, uint256& cm) const;
    bool EraseNullifierMapping(const uint256& nullifier);

    /**
     * Iterate all ZKHU notes (for yield calculation)
     * Bug #8 Fix: Uses encapsulated iteration with correct key format
     * @param func Functor: bool(uint256 noteId, ZKHUNoteData& data) - return false to stop
     * @return true if iteration completed, false on error
     */
    template<typename Func>
    bool IterateNotes(Func func) const;

    /**
     * Get all ZKHU notes as a vector (convenience function)
     * Note: Not const because NewIterator() is non-const in CDBWrapper
     * @return vector of (noteId, noteData) pairs
     */
    std::vector<std::pair<uint256, ZKHUNoteData>> GetAllNotes();
};

#endif // HU_HU_ZKHU_DB_H
