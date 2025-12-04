// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "piv2/piv2_yield.h"

#include "chainparams.h"
#include "piv2/piv2_state.h"
#include "piv2/piv2_validation.h"
#include "piv2/zkpiv2_db.h"
#include "piv2/zkpiv2_note.h"
#include "logging.h"

#include <boost/multiprecision/cpp_int.hpp>

namespace khu_yield {

// ============================================================================
// Network-aware parameter getters (uses Consensus::Params for per-network values)
// ============================================================================

uint32_t GetMaturityBlocks()
{
    return Params().GetConsensus().nZKHUMaturityBlocks;
}

uint32_t GetYieldInterval()
{
    return Params().GetConsensus().nBlocksPerDay;
}

// ============================================================================
// Internal Functions
// ============================================================================

/**
 * IterateLockedNotes - Stream all ZKHU notes from LevelDB and apply functor
 *
 * RÈGLE CONSENSUS-CRITICAL:
 * - LevelDB cursors provide deterministic lexicographic order
 * - All nodes will process notes in the same order
 * - No in-memory sorting required
 *
 * Uses CZKHUTreeDB::GetAllNotes() which provides correct key format and
 * deterministic iteration order matching the database key serialization.
 *
 * @param func Functor to apply to each note: bool(uint256 noteId, ZKHUNoteData& data)
 * @return true if iteration completed successfully
 */
template<typename Func>
static bool IterateLockedNotes(Func func)
{
    CZKHUTreeDB* zkhuDB = GetZKHUDB();
    if (!zkhuDB) {
        // DB not initialized (e.g., test environment or before ZKHU activation)
        // This is valid - treat as empty note set, iteration succeeds with zero notes
        LogPrint(BCLog::HU, "IterateLockedNotes: ZKHU DB not initialized (empty note set)\n");
        return true;
    }

    LogPrint(BCLog::HU, "IterateLockedNotes: DB initialized, using GetAllNotes()...\n");

    // Use GetAllNotes() for correct key format matching WriteNote() serialization
    std::vector<std::pair<uint256, ZKHUNoteData>> allNotes = zkhuDB->GetAllNotes();

    LogPrint(BCLog::HU, "IterateLockedNotes: GetAllNotes returned %zu notes\n", allNotes.size());

    for (const auto& notePair : allNotes) {
        const uint256& noteId = notePair.first;
        ZKHUNoteData noteData = notePair.second;  // Copy for functor to modify

        LogPrint(BCLog::HU, "IterateLockedNotes: processing note %s amount=%lld lockHeight=%u\n",
                 noteId.GetHex().substr(0, 16).c_str(), (long long)noteData.amount, noteData.nLockStartHeight);

        // Apply functor
        if (!func(noteId, noteData)) {
            return false;
        }
    }

    LogPrint(BCLog::HU, "IterateLockedNotes: iteration complete, processed %zu notes\n", allNotes.size());

    return true;
}

/**
 * CalculateAndAccumulateYield - Calculate yield for all mature notes AND update their Ur_accumulated
 *
 * ALGORITHME CONSENSUS-CRITICAL:
 * 1. Iterate all ZKHU notes via LevelDB cursor (deterministic order)
 * 2. For each mature note: calculate daily yield, update Ur_accumulated, write to DB
 * 3. Accumulate total yield for global state update
 * 4. Protect against overflow using int128_t
 *
 * Updates each note's Ur_accumulated in the ZKHU database (per-note tracking).
 * This ensures UNLOCK can retrieve the correct yield for each individual note.
 *
 * @param nHeight Current block height
 * @param R_annual Annual yield rate (basis points)
 * @param totalYield Output: total yield calculated (satoshis)
 * @return true on success, false on overflow or error
 */
static bool CalculateAndAccumulateYield(uint32_t nHeight, uint16_t R_annual, CAmount& totalYield)
{
    totalYield = 0;

    CZKHUTreeDB* zkhuDB = GetZKHUDB();
    if (!zkhuDB) {
        // DB not initialized - no notes to process
        LogPrint(BCLog::HU, "CalculateAndAccumulateYield: ZKHU DB not initialized\n");
        return true;
    }

    // Use int128_t for overflow-safe accumulation
    using int128_t = boost::multiprecision::int128_t;
    int128_t totalYield128 = 0;

    // Collect notes to update (we can't modify during iteration)
    std::vector<std::pair<uint256, ZKHUNoteData>> notesToUpdate;

    bool success = IterateLockedNotes([&](const uint256& noteId, const ZKHUNoteData& note) {
        // Skip notes that have been spent (UNLOCK'd)
        // Notes are kept in DB for undo support, but bSpent flag excludes from yield
        if (note.bSpent) {
            LogPrint(BCLog::HU, "CalculateAndAccumulateYield: Skipping spent note %s (bSpent=true)\n",
                     noteId.GetHex().substr(0, 16).c_str());
            return true; // Skip spent note, continue iteration
        }

        // Check note maturity
        if (!IsNoteMature(note.nLockStartHeight, nHeight)) {
            return true; // Skip immature note, continue iteration
        }

        // Calculate daily yield for this note
        CAmount dailyYield = CalculateDailyYieldForNote(note.amount, R_annual);

        // Accumulate with overflow protection
        totalYield128 += dailyYield;

        // Check for overflow (should never happen with realistic values)
        if (totalYield128 > std::numeric_limits<CAmount>::max()) {
            LogPrintf("ERROR: CalculateAndAccumulateYield: Overflow detected at note %s\n",
                      noteId.GetHex());
            return false; // Stop iteration
        }

        // Store note for per-note Ur_accumulated update
        ZKHUNoteData updatedNote = note;
        updatedNote.Ur_accumulated += dailyYield;
        notesToUpdate.emplace_back(noteId, updatedNote);

        LogPrint(BCLog::HU, "CalculateAndAccumulateYield: Note %s amount=%lld dailyYield=%lld newUr=%lld\n",
                 noteId.GetHex().substr(0, 16).c_str(), (long long)note.amount,
                 (long long)dailyYield, (long long)updatedNote.Ur_accumulated);

        return true; // Continue iteration
    });

    if (!success) {
        return false;
    }

    // Write updated notes with accumulated yield back to database
    for (const auto& notePair : notesToUpdate) {
        if (!zkhuDB->WriteNote(notePair.first, notePair.second)) {
            LogPrintf("ERROR: CalculateAndAccumulateYield: Failed to write note %s\n",
                      notePair.first.GetHex());
            return false;
        }
    }

    LogPrint(BCLog::HU, "CalculateAndAccumulateYield: Updated %zu notes with yield\n",
             notesToUpdate.size());

    // Safe cast to CAmount (overflow already checked)
    totalYield = static_cast<CAmount>(totalYield128);

    return true;
}

// ============================================================================
// Public Functions
// ============================================================================

bool ShouldApplyDailyYield(uint32_t nHeight, uint32_t nV6ActivationHeight, uint32_t nLastYieldHeight)
{
    // Before V6 activation: no yield
    if (nHeight < nV6ActivationHeight) {
        return false;
    }

    // First yield at activation height
    if (nLastYieldHeight == 0 && nHeight == nV6ActivationHeight) {
        return true;
    }

    // Subsequent yields every GetYieldInterval() blocks (exact boundary only)
    if (nLastYieldHeight > 0 && (nHeight - nLastYieldHeight) == GetYieldInterval()) {
        return true;
    }

    return false;
}

bool ApplyDailyYield(HuGlobalState& state, uint32_t nHeight, uint32_t nV6ActivationHeight)
{
    // Sanity check: height must be at yield boundary
    if (!ShouldApplyDailyYield(nHeight, nV6ActivationHeight, state.last_yield_update_height)) {
        LogPrintf("ERROR: ApplyDailyYield: Not at yield boundary (height=%u, last=%u)\n",
                  nHeight, state.last_yield_update_height);
        return false;
    }

    // Calculate total yield AND update each note's per-note Ur_accumulated
    CAmount totalYield = 0;
    if (!CalculateAndAccumulateYield(nHeight, state.R_annual, totalYield)) {
        LogPrintf("ERROR: ApplyDailyYield: Failed to calculate/accumulate yield at height %u\n", nHeight);
        return false;
    }

    // ═══════════════════════════════════════════════════════════
    // DOUBLE MUTATION ATOMIQUE — Cr ET Ur ensemble
    // Invariant Cr == Ur doit être préservé
    // ═══════════════════════════════════════════════════════════
    state.Cr += totalYield;
    state.Ur += totalYield;

    // Store yield for exact undo (P1 fix: avoid recalculation on reorg)
    state.last_yield_amount = totalYield;

    // Update last yield height
    state.last_yield_update_height = nHeight;

    LogPrint(BCLog::HU, "ApplyDailyYield: height=%u R_annual=%u (%.2f%%) totalYield=%d Cr=%d Ur=%d\n",
             nHeight, state.R_annual, state.R_annual / 100.0, totalYield, state.Cr, state.Ur);

    return true;
}

bool UndoDailyYield(HuGlobalState& state, uint32_t nHeight, uint32_t nV6ActivationHeight)
{
    // P1 FIX: Use stored yield amount instead of recalculating
    // This ensures exact undo even if note set changed during reorg
    CAmount totalYield = state.last_yield_amount;

    // Sanity check: yield must have been stored
    if (totalYield < 0) {
        LogPrintf("ERROR: UndoDailyYield: Invalid stored yield %d at height %u\n", totalYield, nHeight);
        return false;
    }

    // ═══════════════════════════════════════════════════════════
    // Undo per-note Ur_accumulated updates (reverse of Apply)
    // ═══════════════════════════════════════════════════════════
    CZKHUTreeDB* zkhuDB = GetZKHUDB();
    if (zkhuDB && totalYield > 0) {
        // Collect notes to update
        std::vector<std::pair<uint256, ZKHUNoteData>> notesToUpdate;

        IterateLockedNotes([&](const uint256& noteId, const ZKHUNoteData& note) {
            // Skip spent notes (same logic as CalculateAndAccumulateYield)
            // Spent notes never had yield added, so nothing to undo
            if (note.bSpent) {
                return true; // Skip spent note
            }

            // Only process notes that were mature at this height
            if (!IsNoteMature(note.nLockStartHeight, nHeight)) {
                return true; // Skip immature note
            }

            // Calculate the yield that was added for this note
            CAmount dailyYield = CalculateDailyYieldForNote(note.amount, state.R_annual);

            // Prepare updated note with yield subtracted
            ZKHUNoteData updatedNote = note;
            if (updatedNote.Ur_accumulated >= dailyYield) {
                updatedNote.Ur_accumulated -= dailyYield;
            } else {
                // Edge case: yield was already claimed via UNLOCK, skip
                LogPrint(BCLog::HU, "UndoDailyYield: Note %s has Ur_accumulated=%lld < dailyYield=%lld, skipping\n",
                         noteId.GetHex().substr(0, 16).c_str(),
                         (long long)updatedNote.Ur_accumulated, (long long)dailyYield);
            }
            notesToUpdate.emplace_back(noteId, updatedNote);

            return true;
        });

        // Write updated notes back to DB
        for (const auto& notePair : notesToUpdate) {
            if (!zkhuDB->WriteNote(notePair.first, notePair.second)) {
                LogPrintf("ERROR: UndoDailyYield: Failed to write note %s\n", notePair.first.GetHex());
                return false;
            }
        }

        LogPrint(BCLog::HU, "UndoDailyYield: Reverted yield on %zu notes\n", notesToUpdate.size());
    }

    // ═══════════════════════════════════════════════════════════
    // DOUBLE MUTATION ATOMIQUE REVERSE — Cr ET Ur ensemble
    // ═══════════════════════════════════════════════════════════
    if (state.Cr < totalYield) {
        LogPrintf("ERROR: UndoDailyYield: Underflow Cr=%d < totalYield=%d at height %u\n",
                  state.Cr, totalYield, nHeight);
        return false;
    }
    if (state.Ur < totalYield) {
        LogPrintf("ERROR: UndoDailyYield: Underflow Ur=%d < totalYield=%d at height %u\n",
                  state.Ur, totalYield, nHeight);
        return false;
    }
    state.Cr -= totalYield;
    state.Ur -= totalYield;

    // Clear stored yield amount
    state.last_yield_amount = 0;

    // Restore previous last_yield_update_height
    uint32_t yieldInterval = GetYieldInterval();
    if (nHeight == nV6ActivationHeight) {
        state.last_yield_update_height = 0;
    } else if (nHeight > nV6ActivationHeight + yieldInterval) {
        state.last_yield_update_height = nHeight - yieldInterval;
    } else {
        state.last_yield_update_height = nV6ActivationHeight;
    }

    LogPrint(BCLog::HU, "UndoDailyYield: height=%u totalYield=%d (stored) Cr=%d Ur=%d\n",
             nHeight, totalYield, state.Cr, state.Ur);

    return true;
}

CAmount CalculateDailyYieldForNote(CAmount amount, uint16_t R_annual)
{
    // FORMULE CONSENSUS (basis points):
    // daily = (amount × R_annual / 10000) / 365
    //
    // Use int128_t to avoid overflow

    if (amount <= 0 || R_annual == 0) {
        return 0;
    }

    using int128_t = boost::multiprecision::int128_t;

    // annual_yield = amount × R_annual / 10000
    int128_t annual128 = static_cast<int128_t>(amount) * R_annual / 10000;

    // daily_yield = annual_yield / 365
    int128_t daily128 = annual128 / DAYS_PER_YEAR;

    // Check overflow (should never happen with realistic values)
    if (daily128 > std::numeric_limits<CAmount>::max()) {
        LogPrintf("ERROR: CalculateDailyYieldForNote: Overflow (amount=%d, R=%u)\n",
                  amount, R_annual);
        return 0;
    }

    return static_cast<CAmount>(daily128);
}

bool IsNoteMature(uint32_t noteHeight, uint32_t currentHeight)
{
    // RÈGLE CONSENSUS: Note must be lockd for at least maturity period
    // MAINNET/TESTNET: 4320 blocks (~3 days)
    // REGTEST: 1260 blocks (~21 hours for fast testing)
    if (currentHeight < noteHeight) {
        return false; // Invalid state
    }

    return (currentHeight - noteHeight) >= GetMaturityBlocks();
}

} // namespace khu_yield
