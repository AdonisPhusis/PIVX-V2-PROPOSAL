// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2022 The PIVX Core developers
// Copyright (c) 2025 The PIVHU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"

#include "arith_uint256.h"
#include "chainparamsseeds.h"
#include "consensus/merkle.h"
#include "tinyformat.h"
#include "utilstrencodings.h"

#include <assert.h>

/**
 * PIVHU Genesis Mining Utility
 * Finds a valid nonce for the genesis block that meets the difficulty target.
 * Call this once to get the correct nonce, then hardcode it.
 */
static void MineGenesisBlock(CBlock& genesis, const uint256& bnGenesisTarget)
{
    arith_uint256 bnTarget;
    bnTarget.SetCompact(genesis.nBits);

    // Check if current hash already meets target
    uint256 currentHash = genesis.GetHash();
    if (UintToArith256(currentHash) <= bnTarget) {
        printf("PIVHU Genesis: Already valid!\n");
        printf("  nNonce: %u\n", genesis.nNonce);
        printf("  Hash: %s\n", currentHash.ToString().c_str());
        printf("  MerkleRoot: %s\n", genesis.hashMerkleRoot.ToString().c_str());
        return;
    }

    printf("PIVHU Genesis Mining: Searching for nonce...\n");
    printf("  Time: %u\n", genesis.nTime);
    printf("  nBits: 0x%08x\n", genesis.nBits);
    printf("  Target: %s\n", bnTarget.ToString().c_str());
    printf("  MerkleRoot: %s\n", genesis.hashMerkleRoot.ToString().c_str());

    for (genesis.nNonce = 0; genesis.nNonce < UINT32_MAX; genesis.nNonce++) {
        uint256 hash = genesis.GetHash();
        if (UintToArith256(hash) <= bnTarget) {
            printf("PIVHU Genesis Found!\n");
            printf("  nNonce: %u\n", genesis.nNonce);
            printf("  Hash: %s\n", hash.ToString().c_str());
            printf("  MerkleRoot: %s\n", genesis.hashMerkleRoot.ToString().c_str());
            break;
        }
        if ((genesis.nNonce % 100000) == 0) {
            printf("  Mining... nNonce=%u\n", genesis.nNonce);
        }
    }
}

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.vtx.push_back(std::make_shared<const CTransaction>(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.nVersion = nVersion;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

void CChainParams::UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex idx, int nActivationHeight)
{
    assert(IsRegTestNet()); // only available for regtest
    assert(idx > Consensus::BASE_NETWORK && idx < Consensus::MAX_NETWORK_UPGRADES);
    consensus.vUpgrades[idx].nActivationHeight = nActivationHeight;
}

/**
 * Build the genesis block. Note that the output of the genesis coinbase cannot
 * be spent as it did not originally exist in the database.
 *
 * CBlock(hash=00000ffd590b14, ver=1, hashPrevBlock=00000000000000, hashMerkleRoot=e0028e, nTime=1390095618, nBits=1e0ffff0, nNonce=28917698, vtx=1)
 *   CTransaction(hash=e0028e, ver=1, vin.size=1, vout.size=1, nLockTime=0)
 *     CTxIn(COutPoint(000000, -1), coinbase 04ffff001d01044c5957697265642030392f4a616e2f3230313420546865204772616e64204578706572696d656e7420476f6573204c6976653a204f76657273746f636b2e636f6d204973204e6f7720416363657074696e6720426974636f696e73)
 *     CTxOut(nValue=50.00000000, scriptPubKey=0xA9037BAC7050C479B121CF)
 *   vMerkleTree: e0028e
 */
static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    const char* pszTimestamp = "U.S. News & World Report Jan 28 2016 With His Absence, Trump Dominates Another Debate";
    const CScript genesisOutputScript = CScript() << ParseHex("04c10e83b2703ccf322f7dbd62dd5855ac7c10bd055814ce121ba32607d573b8810c02c0582aed05b4deb9c4b77b26d92428c61256cd42774babea0a073b2ed0c9") << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward);
}

/**
 * PIVHU Genesis Block - Clean start with MN-only consensus
 *
 * MAINNET/TESTNET Distribution (99,120,000 PIVHU total):
 * - Swap Reserve:   98,000,000 PIVHU (HTLC atomic swap reserve, outside KHU)
 * - Dev/Test:          500,000 PIVHU (~0.5% development fund)
 * - DAO Treasury T:    500,000 PIVHU (~0.5% initial treasury)
 * - MN Collateral:     120,000 PIVHU (12 × 10,000 for initial masternodes)
 *
 * HuGlobalState at genesis: C=0, U=0, Z=0, Cr=0, Ur=0, T=500000*COIN
 * Block reward = 0 (all inflation via R% yield on ZKHU lock)
 */
static CBlock CreatePIVHUGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion)
{
    const char* pszTimestamp = "PIVHU Genesis Nov 2025 - Knowledge Hedge Unit - MN Consensus - Zero Block Reward";

    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));

    // PIVHU Genesis Distribution - 4 outputs (mainnet/testnet)
    // Note: These are placeholder addresses - replace with real addresses before mainnet launch
    // Output 0: Swap Reserve (98,000,000 PIVHU for HTLC atomic swaps)
    const CScript swapReserveScript = CScript() << ParseHex("04c6047f9441ed7d6d3045406e95c07cd85c778e4b8cef3ca7abac09b95c709ee51ae168fea63dc339a3c58419466ceae1061021a6e8c1b0ec7e3c0d4b2a9d2d3c") << OP_CHECKSIG;
    // Output 1: Dev/Test Wallet (500,000 PIVHU)
    const CScript devRewardScript = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;
    // Output 2: DAO Treasury T Initial (500,000 PIVHU - will be converted to T on init)
    const CScript daoTreasuryScript = CScript() << ParseHex("0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8") << OP_CHECKSIG;
    // Output 3: MN Collateral Pool (120,000 PIVHU = 12 × 10,000)
    const CScript mnCollateralScript = CScript() << ParseHex("04f9308a019258c31049344f85f89d5229b531c845836f99b08601f113bce036f9388f7b0f632de8140fe337e62a37f3566500a99934c2231b6cb9fd7584b8e672") << OP_CHECKSIG;

    txNew.vout.resize(4);
    txNew.vout[0].nValue = 98000000 * COIN;    // Swap Reserve
    txNew.vout[0].scriptPubKey = swapReserveScript;
    txNew.vout[1].nValue = 500000 * COIN;      // Dev/Test
    txNew.vout[1].scriptPubKey = devRewardScript;
    txNew.vout[2].nValue = 500000 * COIN;      // DAO Treasury T initial
    txNew.vout[2].scriptPubKey = daoTreasuryScript;
    txNew.vout[3].nValue = 120000 * COIN;      // MN Collateral Pool
    txNew.vout[3].scriptPubKey = mnCollateralScript;

    CBlock genesis;
    genesis.vtx.push_back(std::make_shared<const CTransaction>(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.nVersion = nVersion;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

/**
 * PIVHU Testnet Genesis Block - Public test network
 *
 * TESTNET Distribution (100,030,000 PIVHU total):
 * - MN1 Collateral:   10,000 PIVHU (masternode 1)
 * - MN2 Collateral:   10,000 PIVHU (masternode 2)
 * - MN3 Collateral:   10,000 PIVHU (masternode 3)
 * - Dev Wallet:   50,000,000 PIVHU (development/testing)
 * - Faucet:       50,000,000 PIVHU (public faucet for testers)
 *
 * Testnet uses 3 masternodes minimum for DMM quorum testing.
 * Private keys are documented but NEVER used on mainnet.
 */
static CBlock CreatePIVHUTestnetGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion)
{
    const char* pszTimestamp = "PIVHU Testnet Dec 2025 - Knowledge Hedge Unit - 3 MN DMM Genesis";

    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));

    // ═══════════════════════════════════════════════════════════════════════════
    // PIVHU Testnet Distribution - P2PKH outputs
    // ═══════════════════════════════════════════════════════════════════════════
    //
    // Testnet addresses (base58 prefix 139 = 'x' or 'y')
    // Private keys will be generated via RPC and documented separately
    //
    // Output 0: MN1 Collateral (10,000 HU)
    // Output 1: MN2 Collateral (10,000 HU)
    // Output 2: MN3 Collateral (10,000 HU)
    // Output 3: Dev Wallet (50,000,000 HU)
    // Output 4: Faucet (50,000,000 HU)
    //
    // Total: 100,030,000 HU
    //
    // ═══════════════════════════════════════════════════════════════════════════

    txNew.vout.resize(5);

    // ═══════════════════════════════════════════════════════════════════════
    // PIV2 Testnet Genesis Keys - Generated 2025-12-04
    // ═══════════════════════════════════════════════════════════════════════
    //
    // Output 0: MN1 Collateral (10,000 PIV2)
    //   pubKeyHash: 87060609b12d797fd2396629957fde4a3d3adbff
    txNew.vout[0].nValue = 10000 * COIN;
    txNew.vout[0].scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ParseHex("87060609b12d797fd2396629957fde4a3d3adbff") << OP_EQUALVERIFY << OP_CHECKSIG;

    // Output 1: MN2 Collateral (10,000 PIV2)
    //   pubKeyHash: 2563dfb22c186e7d2741ed6d785856f7f17e187a
    txNew.vout[1].nValue = 10000 * COIN;
    txNew.vout[1].scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ParseHex("2563dfb22c186e7d2741ed6d785856f7f17e187a") << OP_EQUALVERIFY << OP_CHECKSIG;

    // Output 2: MN3 Collateral (10,000 PIV2)
    //   pubKeyHash: dd2ba22aec7280230ff03da61b7141d7acf12edd
    txNew.vout[2].nValue = 10000 * COIN;
    txNew.vout[2].scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ParseHex("dd2ba22aec7280230ff03da61b7141d7acf12edd") << OP_EQUALVERIFY << OP_CHECKSIG;

    // Output 3: Dev Wallet (50,000,000 PIV2)
    //   pubKeyHash: 197cf6a11f4214b4028389c77b90f27bc90dc839
    txNew.vout[3].nValue = 50000000 * COIN;
    txNew.vout[3].scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ParseHex("197cf6a11f4214b4028389c77b90f27bc90dc839") << OP_EQUALVERIFY << OP_CHECKSIG;

    // Output 4: Faucet (50,000,000 PIV2)
    //   pubKeyHash: ec1ab14139850ef2520199c49ba1e46656c9e84f
    txNew.vout[4].nValue = 50000000 * COIN;
    txNew.vout[4].scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ParseHex("ec1ab14139850ef2520199c49ba1e46656c9e84f") << OP_EQUALVERIFY << OP_CHECKSIG;

    CBlock genesis;
    genesis.vtx.push_back(std::make_shared<const CTransaction>(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.nVersion = nVersion;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

/**
 * PIVHU Regtest Genesis Block - Simplified for testing
 *
 * REGTEST Distribution (99,120,000 PIVHU total):
 * - Test Wallet:    50,000,000 PIVHU (~50% for easy testing)
 * - Swap Reserve:   48,500,000 PIVHU (remaining swap reserve)
 * - DAO Treasury T:    500,000 PIVHU (initial treasury)
 * - MN Collateral:     120,000 PIVHU (12 × 10,000 for masternodes)
 *
 * Regtest gives majority to test wallet for convenient testing of KHU operations.
 */
static CBlock CreatePIVHURegtestGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion)
{
    const char* pszTimestamp = "PIVHU Regtest Dec 2025 - Knowledge Hedge Unit - Test Genesis v2";

    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));

    // ═══════════════════════════════════════════════════════════════════════════
    // PIVHU Regtest Distribution - P2PKH outputs with KNOWN private keys
    // ═══════════════════════════════════════════════════════════════════════════
    //
    // Generated from regtest wallet - NEVER use on mainnet!
    //
    // Output 0: Test Wallet (50M HU)
    //   Address: y65ffDxjd8WVQn4J4ByKhSDWwVMs2r4k7d
    //   WIF:     cMpec6ZShrJvVMfehkdqVbkK9sHQCsqeBpyd7q5c682KxpbNT2aR
    //
    // Output 1: MN1 Collateral (100 HU)
    //   Address: y48kso2j49HW3mZtNasQxumSVpWzN6H16H
    //   WIF:     cRtHEkQ53gfYg3NWbpb8nCLPxebyRVEEfRpcWJVyLMhhp1wLmhdB
    //
    // Output 2: MN2 Collateral (100 HU)
    //   Address: y9Drs8V4updrVkuEAP3HyZfJukrZh3LBNm
    //   WIF:     cS3s7E4zVgtvn5BBz1ZcDgdfbs2t1qcNB8tQjffq64xo2aHc7XSq
    //
    // Output 3: MN3 Collateral (100 HU)
    //   Address: yEvakh8hWeVvfHY4kXBxowQ1gus2Q1imTP
    //   WIF:     cQX5FKoWNny66nYJEwwCwXVvhzn7Mm6C6u2zcPrhDFZ6tgPMiPni
    //
    // Output 4: MN Ops Fund (119,700 HU)
    //   Address: y6wgMBkg9BXfdMAH7Cf1quRZjJz98qaPAq
    //   WIF:     cPP8PfQgEaStUECCpKFzpZt9hFis8tj6E2vtqr3gweLyZkuwuvvY
    //
    // Output 5: Swap Reserve (48.5M HU)
    //   Address: y4wrFnnsRTkDhxBp61gDnjZ9Fg8yt7x34D
    //   WIF:     cNYJdV6Muuu1oVRP2fsCHYeTx3pkaq7itEV45mK36gTziSLQ4Qox
    //
    // Output 6: DAO Treasury T (500K HU)
    //   Address: yBNsxgEURuLLSYTjgT5fmUwBPK77s8a5fZ
    //   WIF:     cUhVQbjcbttjN8yLVyY5maqweRZsFSRBsrbo3335AiPWscYAVa66
    //
    // ═══════════════════════════════════════════════════════════════════════════

    txNew.vout.resize(7);

    // Output 0: Test Wallet (50M)
    txNew.vout[0].nValue = 50000000 * COIN;
    txNew.vout[0].scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ParseHex("63d31c01f548cc5d314cf692f727157475b9d4a9") << OP_EQUALVERIFY << OP_CHECKSIG;

    // Output 1: MN1 Collateral (100)
    txNew.vout[1].nValue = 100 * COIN;
    txNew.vout[1].scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ParseHex("4e7875de8946177c9fd5fc55fcbc54a34c8a4ab9") << OP_EQUALVERIFY << OP_CHECKSIG;

    // Output 2: MN2 Collateral (100)
    txNew.vout[2].nValue = 100 * COIN;
    txNew.vout[2].scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ParseHex("86482b0b101caf70223a43ca2a68f91aaf02786d") << OP_EQUALVERIFY << OP_CHECKSIG;

    // Output 3: MN3 Collateral (100)
    txNew.vout[3].nValue = 100 * COIN;
    txNew.vout[3].scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ParseHex("c4d467187c9287c486e2954e72275cd767bf361a") << OP_EQUALVERIFY << OP_CHECKSIG;

    // Output 4: MN Ops Fund (119,700)
    txNew.vout[4].nValue = 119700 * COIN;
    txNew.vout[4].scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ParseHex("6d487b8e666a54a23bbdf5d5fcb6d55c677ee82a") << OP_EQUALVERIFY << OP_CHECKSIG;

    // Output 5: Swap Reserve (48.5M)
    txNew.vout[5].nValue = 48500000 * COIN;
    txNew.vout[5].scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ParseHex("5760804121da48fd43d266282cbddc8f0e7962af") << OP_EQUALVERIFY << OP_CHECKSIG;

    // Output 6: DAO Treasury T (500K)
    txNew.vout[6].nValue = 500000 * COIN;
    txNew.vout[6].scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ParseHex("9ded13f5233a7fede9f7f70de3a9739d1405d001") << OP_EQUALVERIFY << OP_CHECKSIG;

    CBlock genesis;
    genesis.vtx.push_back(std::make_shared<const CTransaction>(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.nVersion = nVersion;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

/**
 * Main network
 */
/**
 * What makes a good checkpoint block?
 * + Is surrounded by blocks with reasonable timestamps
 *   (no blocks before with a timestamp after, none after with
 *    timestamp before)
 * + Contains no strange transactions
 */
// PIVHU will have its own genesis and checkpoint history
static MapCheckpoints mapCheckpoints = {};

static const CCheckpointData data = {
    &mapCheckpoints,
    0,    // * UNIX timestamp of last checkpoint block
    0,    // * total number of transactions between genesis and last checkpoint
    0     // * estimated number of transactions per day after checkpoint
};

static MapCheckpoints mapCheckpointsTestnet = {};

static const CCheckpointData dataTestnet = {
    &mapCheckpointsTestnet,
    0,
    0,
    0};

static MapCheckpoints mapCheckpointsRegtest = {};
static const CCheckpointData dataRegtest = {
    &mapCheckpointsRegtest,
    0,
    0,
    0};

class CMainParams : public CChainParams
{
public:
    CMainParams()
    {
        strNetworkID = "hu-main";

        // PIVHU Genesis Block
        // Timestamp: Nov 30, 2025 00:00:00 UTC (1732924800)
        // PIVHU uses higher difficulty target initially for MN-only consensus
        // nNonce and hashes will be mined - use placeholders for now
        genesis = CreatePIVHUGenesisBlock(1732924800, 0, 0x1e0ffff0, 1);
        consensus.hashGenesisBlock = genesis.GetHash();

        // Genesis hashes - run with -printgenesis to mine
        // TODO: Mine and replace these placeholder values
        // assert(consensus.hashGenesisBlock == uint256S("0x..."));
        // assert(genesis.hashMerkleRoot == uint256S("0x..."));

        // ═══════════════════════════════════════════════════════════════════════
        // HU Core Economic Parameters - MAINNET
        // ═══════════════════════════════════════════════════════════════════════
        consensus.nMaxMoneyOut = 99120000 * COIN;   // HU: 99.12M total supply at genesis
        consensus.nMNCollateralAmt = 10000 * COIN;  // HU: 10,000 HU per masternode
        consensus.nMNBlockReward = 0;               // HU: Block reward = 0 (R% yield economy)
        consensus.nNewMNBlockReward = 0;            // HU: Block reward = 0 (R% yield economy)
        consensus.nTargetTimespan = 40 * 60;
        consensus.nTargetTimespanV2 = 30 * 60;
        consensus.nTargetSpacing = 1 * 60;          // HU: 60 second blocks
        consensus.nTimeSlotLength = 15;

        // KHU DAO Treasury (Phase 6)
        // TODO: Replace with actual multisig DAO council address before mainnet deployment
        consensus.strDaoTreasuryAddress = "DPLACEHOLDERMainnetDaoTreasuryAddressHere";

        // ═══════════════════════════════════════════════════════════════════════
        // KHU Timing Parameters - MAINNET (production values)
        // ═══════════════════════════════════════════════════════════════════════

        // ZKHU lock maturity: 3 days before yield accrues
        consensus.nZKHUMaturityBlocks = 4320;       // 3 days × 1440 blocks/day

        // ═══════════════════════════════════════════════════════════════════════
        // SYNCHRONIZED DOMC + DAO GOVERNANCE (3 months / 1 month)
        // ═══════════════════════════════════════════════════════════════════════
        // DOMC (R% vote) = 90 days = 129600 blocks
        // DAO (T proposals) = 30 days = 43200 blocks
        // 3 DAO cycles = 1 DOMC cycle (synchronized)
        //
        // Timeline (per DOMC cycle = 90 days):
        //   Month 1: DAO cycle 1 (submit/study/vote)
        //   Month 2: DAO cycle 2 (submit/study/vote)
        //   Month 3: DAO cycle 3 (submit/study/vote) + DOMC commit/reveal
        //
        // Final week alignment:
        //   - Day 83-86: DOMC commit phase + DAO3 vote phase
        //   - Day 86-89: DOMC reveal phase + DAO3 vote ends
        //   - Day 89-90: Validation week (everyone sees results)
        //   - Day 90 (block 129600): ACTIVATION (R% + DAO payouts)
        // ═══════════════════════════════════════════════════════════════════════

        // DOMC (Democratic Oversight of Monetary Committee) cycle: 90 days
        consensus.nDOMCCycleBlocks = 129600;        // 90 days × 1440 blocks/day
        consensus.nDOMCCommitOffset = 119520;       // Day 83 (start commit in last week)
        consensus.nDOMCRevealOffset = 123840;       // Day 86 (reveal deadline)
        consensus.nDOMCPhaseDuration = 4320;        // 3 days per phase

        // DAO Treasury proposal cycle: 30 days
        consensus.nDAOCycleBlocks = 43200;          // 30 days × 1440 blocks/day
        consensus.nDAOSubmitWindow = 10080;         // 7 days submit window
        consensus.nDAOStudyWindow = 20160;          // 14 days study window
        consensus.nDAOVoteWindow = 10080;           // 7 days vote window (ends with DOMC reveal)

        // R% yield parameters (basis points)
        consensus.nRInitial = 4000;                 // 40% initial APY
        consensus.nRFloor = 700;                    // 7% floor APY
        consensus.nRDecayPerYear = 100;             // 1% decay per year

        // Daily yield/treasury update interval
        consensus.nBlocksPerDay = 1440;             // 1440 blocks per day (1-min blocks)

        // ═══════════════════════════════════════════════════════════════════════
        // HU DMM + Finality Parameters - MAINNET
        // Quorum: 12 MNs ("apostles"), 8/12 threshold, rotate every 12 blocks
        // ═══════════════════════════════════════════════════════════════════════
        consensus.nHuBlockTimeSeconds = 60;         // 60 second target block time
        consensus.nHuQuorumSize = 12;               // 12 masternodes per quorum
        consensus.nHuQuorumThreshold = 8;           // 8/12 signatures for finality
        consensus.nHuQuorumRotationBlocks = 12;     // New quorum every 12 blocks
        consensus.nHuLeaderTimeoutSeconds = 45;     // DMM leader timeout (fallback after 45s)
        consensus.nHuMaxReorgDepth = 12;            // Max reorg before finality

        // spork keys
        consensus.strSporkPubKey = "0410050aa740d280b134b40b40658781fc1116ba7700764e0ce27af3e1737586b3257d19232e0cb5084947f5107e44bcd577f126c9eb4a30ea2807b271d2145298";
        consensus.strSporkPubKeyOld = "040F129DE6546FE405995329A887329BED4321325B1A73B0A257423C05C1FCFE9E40EF0678AEF59036A22C42E61DFD29DF7EFB09F56CC73CADF64E05741880E3E7";
        consensus.nTime_EnforceNewSporkKey = 1608512400;    //!> December 21, 2020 01:00:00 AM GMT
        consensus.nTime_RejectOldSporkKey = 1614560400;     //!> March 1, 2021 01:00:00 AM GMT

        consensus.height_last_invalid_UTXO = -1;  // No invalid UTXOs in PIVHU

        consensus.nPivxBadBlockTime = 0;
        consensus.nPivxBadBlockBits = 0;

        // ALL upgrades active from GENESIS (no height-based activation)
        // This is the PIVHU way: clean start, all features active from block 0
        consensus.vUpgrades[Consensus::BASE_NETWORK].nActivationHeight =
                Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_TESTDUMMY].nActivationHeight =
                Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
        consensus.vUpgrades[Consensus::UPGRADE_BIP65].nActivationHeight         =
                Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_V3_4].nActivationHeight          =
                Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_V4_0].nActivationHeight          =
                Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_V5_0].nActivationHeight          =
                Consensus::NetworkUpgrade::ALWAYS_ACTIVE;  // Sapling version
        consensus.vUpgrades[Consensus::UPGRADE_V5_2].nActivationHeight          =
                Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_V5_3].nActivationHeight          =
                Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_V5_5].nActivationHeight          =
                Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_V5_6].nActivationHeight          =
                Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight          =
                Consensus::NetworkUpgrade::ALWAYS_ACTIVE;  // KHU active from genesis


        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 4-byte int at any alignment.
         */
        pchMessageStart[0] = 0x90;
        pchMessageStart[1] = 0xc4;
        pchMessageStart[2] = 0xfd;
        pchMessageStart[3] = 0xe9;
        nDefaultPort = 51472;

        // vSeeds.emplace_back("pivx.seed.fuzzbawls.pw", true);
        // vSeeds.emplace_back("pivx.seed2.fuzzbawls.pw", true);
        // vSeeds.emplace_back("dnsseed.liquid369.wtf", true);

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 30);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 13);
        base58Prefixes[EXCHANGE_ADDRESS] = {0x01, 0xb9, 0xa2};   // starts with EXM
        base58Prefixes[SECRET_KEY] = std::vector<unsigned char>(1, 212);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x02, 0x2D, 0x25, 0x33};
        base58Prefixes[EXT_SECRET_KEY] = {0x02, 0x21, 0x31, 0x2B};
        // BIP44 coin type is from https://github.com/satoshilabs/slips/blob/master/slip-0044.md
        base58Prefixes[EXT_COIN_TYPE] = {0x80, 0x00, 0x00, 0x77};

        vFixedSeeds = std::vector<uint8_t>(std::begin(chainparams_seed_main), std::end(chainparams_seed_main));

        // Reject non-standard transactions by default
        fRequireStandard = true;

        // Sapling
        bech32HRPs[SAPLING_PAYMENT_ADDRESS]      = "ps";
        bech32HRPs[SAPLING_FULL_VIEWING_KEY]     = "pviews";
        bech32HRPs[SAPLING_INCOMING_VIEWING_KEY] = "pivks";
        bech32HRPs[SAPLING_EXTENDED_SPEND_KEY]   = "p-secret-spending-key-main";
        bech32HRPs[SAPLING_EXTENDED_FVK]         = "pxviews";

        // Tier two
        nFulfilledRequestExpireTime = 60 * 60; // fulfilled requests expire in 1 hour
    }

    const CCheckpointData& Checkpoints() const
    {
        return data;
    }

};

/**
 * PIVHU Testnet - for testing MN-only consensus and KHU features
 */
class CTestNetParams : public CChainParams
{
public:
    CTestNetParams()
    {
        strNetworkID = "piv2-testnet";

        // ═══════════════════════════════════════════════════════════════════════
        // PIVHU Testnet Genesis - FINAL
        // ═══════════════════════════════════════════════════════════════════════
        // Timestamp: Dec 2025 (1733270400)
        // Keys generated: 2025-12-03
        // nNonce mined: 69256
        // Distribution:
        //   Output 0: MN1 Collateral - 10,000 HU
        //   Output 1: MN2 Collateral - 10,000 HU
        //   Output 2: MN3 Collateral - 10,000 HU
        //   Output 3: Dev Wallet - 50,000,000 HU
        //   Output 4: Faucet - 50,000,000 HU
        // Total: 100,030,000 HU
        // ═══════════════════════════════════════════════════════════════════════
        // PIV2 Testnet Genesis - v2 keys mined 2025-12-04
        genesis = CreatePIVHUTestnetGenesisBlock(1733270400, 575173, 0x1e0ffff0, 1);
        consensus.hashGenesisBlock = genesis.GetHash();

        // Genesis validation - v2 keys generated 2025-12-04
        assert(consensus.hashGenesisBlock == uint256S("0x000001a025bee548de2afe598046e04dfbffd26180207558b65104c4cc7b626d"));
        assert(genesis.hashMerkleRoot == uint256S("0xf14fac7a43eff3c44336a76109ac95717075785e4c48c496c384f8aa3198b5a3"));

        // ═══════════════════════════════════════════════════════════════════════
        // HU Core Economic Parameters - TESTNET
        // ═══════════════════════════════════════════════════════════════════════
        consensus.nMaxMoneyOut = 100030000 * COIN;  // HU: 100.03M (3×10k MN + 50M dev + 50M faucet)
        consensus.nMNCollateralAmt = 10000 * COIN;  // HU: 10,000 HU per masternode
        consensus.nMNBlockReward = 0;               // HU: Block reward = 0 (R% yield economy)
        consensus.nNewMNBlockReward = 0;            // HU: Block reward = 0 (R% yield economy)
        consensus.nTargetTimespan = 40 * 60;
        consensus.nTargetTimespanV2 = 30 * 60;
        consensus.nTargetSpacing = 1 * 60;          // HU: 60 second blocks
        consensus.nTimeSlotLength = 15;

        // KHU DAO Treasury (Phase 6 - Testnet)
        consensus.strDaoTreasuryAddress = "yPLACEHOLDERTestnetDaoTreasuryAddressHere";

        // ═══════════════════════════════════════════════════════════════════════
        // KHU Timing Parameters - TESTNET (accelerated for testing)
        // ═══════════════════════════════════════════════════════════════════════

        // ZKHU Staking maturity: ~1 hour before yield accrues
        consensus.nZKHUMaturityBlocks = 60;         // 1 hour × 1 block/min

        // ═══════════════════════════════════════════════════════════════════════
        // SYNCHRONIZED DOMC + DAO GOVERNANCE (TESTNET: 3 days / 1 day)
        // ═══════════════════════════════════════════════════════════════════════
        // DOMC = 3 DAO cycles (synchronized)
        // Testnet: DOMC = 3 days = 4320 blocks, DAO = 1 day = 1440 blocks
        // ═══════════════════════════════════════════════════════════════════════

        // DOMC cycle: 3 days (accelerated for testnet)
        consensus.nDOMCCycleBlocks = 4320;          // 3 days × 1440 blocks/day
        consensus.nDOMCCommitOffset = 3600;         // Start commit at day 2.5
        consensus.nDOMCRevealOffset = 3960;         // Start reveal at day 2.75
        consensus.nDOMCPhaseDuration = 360;         // 6 hours per phase

        // DAO Treasury proposal cycle: 1 day (accelerated for testnet)
        consensus.nDAOCycleBlocks = 1440;           // 1 day × 1440 blocks/day
        consensus.nDAOSubmitWindow = 480;           // 8 hours submit window
        consensus.nDAOStudyWindow = 480;            // 8 hours study window
        consensus.nDAOVoteWindow = 480;             // 8 hours vote window

        // R% yield parameters (same as mainnet)
        consensus.nRInitial = 4000;                 // 40% initial APY
        consensus.nRFloor = 700;                    // 7% floor APY
        consensus.nRDecayPerYear = 100;             // 1% decay per year

        // Daily yield/treasury update interval
        consensus.nBlocksPerDay = 1440;             // 1440 blocks per day (1-min blocks)

        // ═══════════════════════════════════════════════════════════════════════
        // HU DMM + Finality Parameters - TESTNET
        // Smaller quorum (3 MNs), faster rotation for testing
        // ═══════════════════════════════════════════════════════════════════════
        consensus.nHuBlockTimeSeconds = 60;         // 60 second target block time
        consensus.nHuQuorumSize = 3;                // 3 masternodes per quorum (all MNs in small testnet)
        consensus.nHuQuorumThreshold = 2;           // 2/3 signatures for finality
        consensus.nHuQuorumRotationBlocks = 3;      // Fast rotation (every 3 blocks)
        consensus.nHuLeaderTimeoutSeconds = 30;     // Aggressive timeout for testing
        consensus.nHuMaxReorgDepth = 6;             // More tolerance for testnet

        // spork keys
        consensus.strSporkPubKey = "04677c34726c491117265f4b1c83cef085684f36c8df5a97a3a42fc499316d0c4e63959c9eca0dba239d9aaaf72011afffeb3ef9f51b9017811dec686e412eb504";
        consensus.strSporkPubKeyOld = "04E88BB455E2A04E65FCC41D88CD367E9CCE1F5A409BE94D8C2B4B35D223DED9C8E2F4E061349BA3A38839282508066B6DC4DB72DD432AC4067991E6BF20176127";
        consensus.nTime_EnforceNewSporkKey = 1608512400;    //!> December 21, 2020 01:00:00 AM GMT
        consensus.nTime_RejectOldSporkKey = 1614560400;     //!> March 1, 2021 01:00:00 AM GMT

        // height based activations
        consensus.height_last_invalid_UTXO = -1;


        // ALL upgrades active from GENESIS (no height-based activation)
        // This is the PIVHU way: clean start, all features active from block 0
        consensus.vUpgrades[Consensus::BASE_NETWORK].nActivationHeight =
                Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_TESTDUMMY].nActivationHeight =
                Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
        consensus.vUpgrades[Consensus::UPGRADE_BIP65].nActivationHeight         =
                Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_V3_4].nActivationHeight          =
                Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_V4_0].nActivationHeight          =
                Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_V5_0].nActivationHeight          =
                Consensus::NetworkUpgrade::ALWAYS_ACTIVE;  // Sapling version
        consensus.vUpgrades[Consensus::UPGRADE_V5_2].nActivationHeight          =
                Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_V5_3].nActivationHeight          =
                Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_V5_5].nActivationHeight          =
                Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_V5_6].nActivationHeight          =
                Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight          =
                Consensus::NetworkUpgrade::ALWAYS_ACTIVE;  // KHU active from genesis

        // ═══════════════════════════════════════════════════════════════════════
        // PIV2 Genesis Masternodes - Testnet Bootstrap
        // These MNs are injected at block 0 to enable DMM block production
        // without the "egg and chicken" problem of needing blocks to register MNs
        // and needing MNs to produce blocks.
        // ═══════════════════════════════════════════════════════════════════════
        // Keys generated: 2025-12-04 via regtest key derivation
        // Coinbase txid (merkle root): f14fac7a43eff3c44336a76109ac95717075785e4c48c496c384f8aa3198b5a3
        // VPS IPs: 57.131.33.151, 57.131.33.152, 57.131.33.214
        // ═══════════════════════════════════════════════════════════════════════
        consensus.genesisMNs = {
            // MN1 - VPS 57.131.33.151
            {
                "c550f0790797e42234b8e9b318a28b8a508f2cf70cdf1b4e60c7d4fdb33787df",  // proTxHash (synthetic: sha256(genesis_hash + 0))
                "f14fac7a43eff3c44336a76109ac95717075785e4c48c496c384f8aa3198b5a3",  // collateralTxHash (genesis coinbase)
                0,  // collateralIndex (output 0)
                "87060609b12d797fd2396629957fde4a3d3adbff",  // ownerKeyID
                "02841677a39503313fb368490d1e817ee46ce78de803ef26cc684f773bfe510730",  // operatorPubKey
                "87060609b12d797fd2396629957fde4a3d3adbff",  // votingKeyID (same as owner)
                "57.131.33.151:27171",  // serviceAddr
                "76a91487060609b12d797fd2396629957fde4a3d3adbff88ac"  // payoutAddress (P2PKH)
            },
            // MN2 - VPS 57.131.33.152
            {
                "a838b47c0d8f70e356bcdffe7b11d52fa73969aa729f4122cb77c29ef66b201f",  // proTxHash (synthetic: sha256(genesis_hash + 1))
                "f14fac7a43eff3c44336a76109ac95717075785e4c48c496c384f8aa3198b5a3",  // collateralTxHash (genesis coinbase)
                1,  // collateralIndex (output 1)
                "2563dfb22c186e7d2741ed6d785856f7f17e187a",  // ownerKeyID
                "0252578510b38f3cd4f520faab8ccfe2703f0a21498cc7071a2f1d3483209eb8a1",  // operatorPubKey
                "2563dfb22c186e7d2741ed6d785856f7f17e187a",  // votingKeyID (same as owner)
                "57.131.33.152:27171",  // serviceAddr
                "76a9142563dfb22c186e7d2741ed6d785856f7f17e187a88ac"  // payoutAddress (P2PKH)
            },
            // MN3 - VPS 57.131.33.214
            {
                "1176beec1bafbedbb07da904e3b45aca466efc3eaae7edd68753a816187e0987",  // proTxHash (synthetic: sha256(genesis_hash + 2))
                "f14fac7a43eff3c44336a76109ac95717075785e4c48c496c384f8aa3198b5a3",  // collateralTxHash (genesis coinbase)
                2,  // collateralIndex (output 2)
                "dd2ba22aec7280230ff03da61b7141d7acf12edd",  // ownerKeyID
                "03c354dd0ded289836371337ffe18f4b8d3c06f009858e68d49705ba3e6b67c25c",  // operatorPubKey
                "dd2ba22aec7280230ff03da61b7141d7acf12edd",  // votingKeyID (same as owner)
                "57.131.33.214:27171",  // serviceAddr
                "76a914dd2ba22aec7280230ff03da61b7141d7acf12edd88ac"  // payoutAddress (P2PKH)
            }
        };

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 4-byte int at any alignment.
         */
        // PIV2 Testnet Magic Bytes (unique, not PIVX/Dash/BTC)
        pchMessageStart[0] = 0xfa;
        pchMessageStart[1] = 0xbf;
        pchMessageStart[2] = 0xb5;
        pchMessageStart[3] = 0xda;
        nDefaultPort = 27171;  // PIV2 Testnet P2P port

        // vSeeds.emplace_back("pivx-testnet.seed.fuzzbawls.pw", true);
        // vSeeds.emplace_back("pivx-testnet.seed2.fuzzbawls.pw", true);

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 139); // Testnet pivx addresses start with 'x' or 'y'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 19);  // Testnet pivx script addresses start with '8' or '9'
        base58Prefixes[EXCHANGE_ADDRESS] = {0x01, 0xb9, 0xb1};   // EXT prefix for the address
        base58Prefixes[SECRET_KEY] = std::vector<unsigned char>(1, 239);     // Testnet private keys start with '9' or 'c' (Bitcoin defaults)
        // Testnet pivx BIP32 pubkeys start with 'DRKV'
        base58Prefixes[EXT_PUBLIC_KEY] = {0x3a, 0x80, 0x61, 0xa0};
        // Testnet pivx BIP32 prvkeys start with 'DRKP'
        base58Prefixes[EXT_SECRET_KEY] = {0x3a, 0x80, 0x58, 0x37};
        // Testnet pivx BIP44 coin type is '1' (All coin's testnet default)
        base58Prefixes[EXT_COIN_TYPE] = {0x80, 0x00, 0x00, 0x01};

        vFixedSeeds = std::vector<uint8_t>(std::begin(chainparams_seed_test), std::end(chainparams_seed_test));

        fRequireStandard = false;

        // Sapling
        bech32HRPs[SAPLING_PAYMENT_ADDRESS]      = "ptestsapling";
        bech32HRPs[SAPLING_FULL_VIEWING_KEY]     = "pviewtestsapling";
        bech32HRPs[SAPLING_INCOMING_VIEWING_KEY] = "pivktestsapling";
        bech32HRPs[SAPLING_EXTENDED_SPEND_KEY]   = "p-secret-spending-key-test";
        bech32HRPs[SAPLING_EXTENDED_FVK]         = "pxviewtestsapling";

        // Tier two
        nFulfilledRequestExpireTime = 60 * 60; // fulfilled requests expire in 1 hour
    }

    const CCheckpointData& Checkpoints() const
    {
        return dataTestnet;
    }
};

/**
 * PIVHU Regression test - fast local testing
 */
class CRegTestParams : public CChainParams
{
public:
    CRegTestParams()
    {
        strNetworkID = "hu-regtest";

        // PIVHU Regtest Genesis - uses regtest-specific allocations
        // 50M test wallet, 48.5M swap reserve, 500k T, 120k MN = 99.12M total
        // nNonce will be mined by MineGenesisBlock utility
        genesis = CreatePIVHURegtestGenesisBlock(1732924800, 0, 0x207fffff, 1);
        consensus.hashGenesisBlock = genesis.GetHash();

        // PIVHU Regtest genesis hashes - will be updated after compilation
        // assert(consensus.hashGenesisBlock == uint256S("0x..."));
        // assert(genesis.hashMerkleRoot == uint256S("0x..."));
        // ═══════════════════════════════════════════════════════════════════════
        // HU Core Economic Parameters - REGTEST
        // ═══════════════════════════════════════════════════════════════════════
        consensus.nMaxMoneyOut = 99120000 * COIN;   // HU: 99.12M total supply at genesis
        consensus.nMNCollateralAmt = 100 * COIN;    // HU: 100 HU per MN (low for testing)
        consensus.nMNBlockReward = 0;               // HU: Block reward = 0 (R% yield economy)
        consensus.nNewMNBlockReward = 0;            // HU: Block reward = 0 (R% yield economy)
        consensus.nTargetTimespan = 40 * 60;
        consensus.nTargetTimespanV2 = 30 * 60;
        consensus.nTargetSpacing = 1 * 60;          // HU: 60 second blocks
        consensus.nTimeSlotLength = 15;

        // KHU DAO Treasury (Phase 6 - RegTest)
        consensus.strDaoTreasuryAddress = "yPLACEHOLDERRegtestDaoTreasuryAddressHere";

        // ═══════════════════════════════════════════════════════════════════════
        // SYNCHRONIZED DOMC + DAO GOVERNANCE (REGTEST: ultra-fast)
        // ═══════════════════════════════════════════════════════════════════════
        // DOMC = 90 blocks = 3 × DAO cycle (30 blocks each)
        // Timeline:
        //   Block 0-29: DAO cycle 1 (submit 0-9, study 10-19, vote 20-29)
        //   Block 30-59: DAO cycle 2
        //   Block 60-89: DAO cycle 3 + DOMC commit/reveal
        //   Block 90: ACTIVATION (R% + DAO payouts)
        // ═══════════════════════════════════════════════════════════════════════

        // ZKHU Staking maturity: 10 blocks (instant for testing)
        consensus.nZKHUMaturityBlocks = 10;         // ~10 minutes

        // DOMC cycle: 90 blocks = 3 × DAO cycle (synchronized)
        consensus.nDOMCCycleBlocks = 90;            // 3 × 30 = 90 blocks
        consensus.nDOMCCommitOffset = 75;           // Start commit at block 75 (DAO3 vote phase)
        consensus.nDOMCRevealOffset = 82;           // Start reveal at block 82
        consensus.nDOMCPhaseDuration = 7;           // 7 blocks per phase

        // DAO Treasury proposal cycle: 30 blocks (ultra-fast)
        consensus.nDAOCycleBlocks = 30;             // ~30 minutes
        consensus.nDAOSubmitWindow = 10;            // 10 blocks submit window
        consensus.nDAOStudyWindow = 10;             // 10 blocks study window
        consensus.nDAOVoteWindow = 10;              // 10 blocks vote window

        // R% yield parameters (same as mainnet)
        consensus.nRInitial = 4000;                 // 40% initial APY
        consensus.nRFloor = 700;                    // 7% floor APY
        consensus.nRDecayPerYear = 100;             // 1% decay per year

        // Daily yield/treasury update interval (accelerated for regtest)
        consensus.nBlocksPerDay = 10;               // Treat 10 blocks as "1 day" for fast testing

        // ═══════════════════════════════════════════════════════════════════════
        // HU DMM + Finality Parameters - REGTEST
        // Trivial quorum (1 MN), instant finality for automated tests
        // ═══════════════════════════════════════════════════════════════════════
        consensus.nHuBlockTimeSeconds = 1;          // Virtual (controlled by scripts)
        consensus.nHuQuorumSize = 1;                // Single MN quorum
        consensus.nHuQuorumThreshold = 1;           // 1 signature = finality
        consensus.nHuQuorumRotationBlocks = 1;      // Rotate every block
        consensus.nHuLeaderTimeoutSeconds = 5;      // Short timeout (less relevant in regtest)
        consensus.nHuMaxReorgDepth = 100;           // Large tolerance for test scenarios

        /* Spork Key for RegTest:
        WIF private key: 932HEevBSujW2ud7RfB1YF91AFygbBRQj3de3LyaCRqNzKKgWXi
        private key hex: bd4960dcbd9e7f2223f24e7164ecb6f1fe96fc3a416f5d3a830ba5720c84b8ca
        Address: yCvUVd72w7xpimf981m114FSFbmAmne7j9
        */
        consensus.strSporkPubKey = "043969b1b0e6f327de37f297a015d37e2235eaaeeb3933deecd8162c075cee0207b13537618bde640879606001a8136091c62ec272dd0133424a178704e6e75bb7";
        consensus.strSporkPubKeyOld = "";
        consensus.nTime_EnforceNewSporkKey = 0;
        consensus.nTime_RejectOldSporkKey = 0;

        // height based activations
        consensus.height_last_invalid_UTXO = -1;


        // ALL upgrades active from GENESIS (no height-based activation)
        // This is the PIVHU way: clean start, all features active from block 0
        consensus.vUpgrades[Consensus::BASE_NETWORK].nActivationHeight =
                Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_TESTDUMMY].nActivationHeight =
                Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
        consensus.vUpgrades[Consensus::UPGRADE_BIP65].nActivationHeight         =
                Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_V3_4].nActivationHeight          =
                Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_V4_0].nActivationHeight          =
                Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_V5_0].nActivationHeight          =
                Consensus::NetworkUpgrade::ALWAYS_ACTIVE;  // Sapling version
        consensus.vUpgrades[Consensus::UPGRADE_V5_2].nActivationHeight          =
                Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_V5_3].nActivationHeight          =
                Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_V5_5].nActivationHeight          =
                Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_V5_6].nActivationHeight          =
                Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight          =
                Consensus::NetworkUpgrade::ALWAYS_ACTIVE;  // KHU active from genesis

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 4-byte int at any alignment.
         */
        pchMessageStart[0] = 0xa1;
        pchMessageStart[1] = 0xcf;
        pchMessageStart[2] = 0x7e;
        pchMessageStart[3] = 0xac;
        nDefaultPort = 51476;

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 139); // Testnet pivx addresses start with 'x' or 'y'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 19);  // Testnet pivx script addresses start with '8' or '9'
        base58Prefixes[EXCHANGE_ADDRESS] = {0x01, 0xb9, 0xb1};   // EXT prefix for the address
        base58Prefixes[SECRET_KEY] = std::vector<unsigned char>(1, 239);     // Testnet private keys start with '9' or 'c' (Bitcoin defaults)
        // Testnet pivx BIP32 pubkeys start with 'DRKV'
        base58Prefixes[EXT_PUBLIC_KEY] = {0x3a, 0x80, 0x61, 0xa0};
        // Testnet pivx BIP32 prvkeys start with 'DRKP'
        base58Prefixes[EXT_SECRET_KEY] = {0x3a, 0x80, 0x58, 0x37};
        // Testnet pivx BIP44 coin type is '1' (All coin's testnet default)
        base58Prefixes[EXT_COIN_TYPE] = {0x80, 0x00, 0x00, 0x01};

        // Reject non-standard transactions by default
        fRequireStandard = true;

        // Sapling
        bech32HRPs[SAPLING_PAYMENT_ADDRESS]      = "ptestsapling";
        bech32HRPs[SAPLING_FULL_VIEWING_KEY]     = "pviewtestsapling";
        bech32HRPs[SAPLING_INCOMING_VIEWING_KEY] = "pivktestsapling";
        bech32HRPs[SAPLING_EXTENDED_SPEND_KEY]   = "p-secret-spending-key-test";
        bech32HRPs[SAPLING_EXTENDED_FVK]         = "pxviewtestsapling";

        // Tier two
        nFulfilledRequestExpireTime = 60 * 60; // fulfilled requests expire in 1 hour
    }

    const CCheckpointData& Checkpoints() const
    {
        return dataRegtest;
    }
};

static std::unique_ptr<CChainParams> globalChainParams;

const CChainParams &Params()
{
    assert(globalChainParams);
    return *globalChainParams;
}

std::unique_ptr<CChainParams> CreateChainParams(const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN)
        return std::unique_ptr<CChainParams>(new CMainParams());
    else if (chain == CBaseChainParams::TESTNET)
        return std::unique_ptr<CChainParams>(new CTestNetParams());
    else if (chain == CBaseChainParams::REGTEST)
        return std::unique_ptr<CChainParams>(new CRegTestParams());
    throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network)
{
    SelectBaseParams(network);
    globalChainParams = CreateChainParams(network);
}

void UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex idx, int nActivationHeight)
{
    globalChainParams->UpdateNetworkUpgradeParameters(idx, nActivationHeight);
}
