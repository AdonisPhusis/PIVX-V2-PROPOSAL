// Copyright (c) 2020-2022 The PIVX Core developers
// Copyright (c) 2025 The PIV2 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

/**
 * PIV2: Simplified tier-two network sync
 *
 * Legacy sync messages (GETSPORKS, GETMNLIST, GETMNWINNERS, SYNCSTATUSCOUNT)
 * are still processed for backward compatibility, but the sync state is
 * now managed by HU finality in TierTwoSyncState.
 */

#include "masternode-sync.h"

#include "masternodeman.h"
#include "net_processing.h"
#include "netmessagemaker.h"
#include "spork.h"
#include "streams.h"
#include "tiertwo/tiertwo_sync_state.h"

// PIV2: Legacy sync functions removed - sync is now automatic via HU finality
