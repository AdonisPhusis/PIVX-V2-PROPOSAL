// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2015-2022 The PIVX Core developers
// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activemasternode.h"
#include "evo/deterministicmns.h"
#include "key_io.h"
#include "masternodeman.h"
#include "netbase.h"
#include "tiertwo/tiertwo_sync_state.h"
#include "rpc/server.h"

#include <univalue.h>

// Helper to convert DMN to JSON (used by listmasternodes)
static UniValue DmnToJson(const CDeterministicMNCPtr dmn)
{
    UniValue ret(UniValue::VOBJ);
    dmn->ToJson(ret);
    Coin coin;
    if (!WITH_LOCK(cs_main, return pcoinsTip->GetUTXOCoin(dmn->collateralOutpoint, coin); )) {
        return ret;
    }
    CTxDestination dest;
    if (!ExtractDestination(coin.out.scriptPubKey, dest)) {
        return ret;
    }
    ret.pushKV("collateralAddress", EncodeDestination(dest));
    return ret;
}

UniValue mnping(const JSONRPCRequest& request)
{
    if (request.fHelp || !request.params.empty()) {
        throw std::runtime_error(
            "mnping \n"
            "\nSend masternode ping. Only for remote masternodes on Regtest\n"

            "\nResult:\n"
            "{\n"
            "  \"sent\":           (string YES|NO) Whether the ping was sent and, if not, the error.\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("mnping", "") + HelpExampleRpc("mnping", ""));
    }

    throw JSONRPCError(RPC_MISC_ERROR, "mnping is deprecated - DMN uses HUSIG quorum signaling");
}

UniValue initmasternode(const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() < 1 || request.params.size() > 2)) {
        throw std::runtime_error(
                "initmasternode \"privkey\" ( \"address\" )\n"
                "\nInitialize deterministic masternode on demand if it's not already initialized.\n"
                "\nArguments:\n"
                "1. privkey          (string, required) The masternode operator private key (bech32 format).\n"
                "2. address          (string, optional) The IP:Port of the masternode. (Legacy only, deprecated)\n"

                "\nResult:\n"
                " success            (string) if the masternode initialization succeeded.\n"

                "\nExamples:\n" +
                HelpExampleCli("initmasternode", "\"huopsecret1...\"") +
                HelpExampleRpc("initmasternode", "\"huopsecret1...\""));
    }

    std::string _strMasterNodePrivKey = request.params[0].get_str();
    if (_strMasterNodePrivKey.empty()) throw JSONRPCError(RPC_INVALID_PARAMETER, "Masternode key cannot be empty.");

    const auto& params = Params();
    bool isDeterministic = _strMasterNodePrivKey.find(params.Bech32HRP(CChainParams::OPERATOR_SECRET_KEY)) != std::string::npos;

    if (!isDeterministic) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "HU-Core requires deterministic masternode keys (bech32 operator key format).");
    }

    if (!activeMasternodeManager) {
        activeMasternodeManager = new CActiveDeterministicMasternodeManager();
        RegisterValidationInterface(activeMasternodeManager);
    }
    auto res = activeMasternodeManager->SetOperatorKey(_strMasterNodePrivKey);
    if (!res) throw std::runtime_error(res.getError());
    const CBlockIndex* pindexTip = WITH_LOCK(cs_main, return chainActive.Tip(); );
    activeMasternodeManager->Init(pindexTip);
    if (activeMasternodeManager->GetState() == CActiveDeterministicMasternodeManager::MASTERNODE_ERROR) {
        throw std::runtime_error(activeMasternodeManager->GetStatus());
    }
    return "success";
}

UniValue getcachedblockhashes(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 0)
        throw std::runtime_error(
            "getcachedblockhashes \n"
            "\nReturn the block hashes cached in the masternode manager\n"

            "\nResult:\n"
            "[\n"
            "  ...\n"
            "  \"xxxx\",   (string) hash at Index d (height modulo max cache size)\n"
            "  ...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("getcachedblockhashes", "") + HelpExampleRpc("getcachedblockhashes", ""));

    std::vector<uint256> vCacheCopy = mnodeman.GetCachedBlocks();
    UniValue ret(UniValue::VARR);
    for (int i = 0; (unsigned) i < vCacheCopy.size(); i++) {
        ret.push_back(vCacheCopy[i].ToString());
    }
    return ret;
}

static inline bool filter(const std::string& str, const std::string& strFilter)
{
    return str.find(strFilter) != std::string::npos;
}

static inline bool filterMasternode(const UniValue& dmno, const std::string& strFilter, bool fEnabled)
{
    return strFilter.empty() || (filter("ENABLED", strFilter) && fEnabled)
                             || (filter("POSE_BANNED", strFilter) && !fEnabled)
                             || (filter(dmno["proTxHash"].get_str(), strFilter))
                             || (filter(dmno["collateralHash"].get_str(), strFilter))
                             || (filter(dmno["collateralAddress"].get_str(), strFilter))
                             || (filter(dmno["dmnstate"]["ownerAddress"].get_str(), strFilter))
                             || (filter(dmno["dmnstate"]["operatorPubKey"].get_str(), strFilter))
                             || (filter(dmno["dmnstate"]["votingAddress"].get_str(), strFilter));
}

UniValue listmasternodes(const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() > 1))
        throw std::runtime_error(
            "listmasternodes ( \"filter\" )\n"
            "\nGet a list of deterministic masternodes\n"

            "\nArguments:\n"
            "1. \"filter\"    (string, optional) Filter search text. Partial match by txhash, status, or addr.\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"proTxHash\": \"xxxx\",           (string) ProTx transaction hash\n"
            "    \"collateralHash\": \"xxxx\",      (string) Collateral transaction hash\n"
            "    \"collateralIndex\": n,            (numeric) Collateral output index\n"
            "    \"collateralAddress\": \"xxxx\",   (string) Collateral address\n"
            "    \"operatorReward\": n,             (numeric) Operator reward percentage\n"
            "    \"dmnstate\": {                    (object) Current DMN state\n"
            "      \"service\": \"xxxx\",           (string) Masternode IP:Port\n"
            "      \"registeredHeight\": n,         (numeric) Block height of registration\n"
            "      \"lastPaidHeight\": n,           (numeric) Block height of last payment\n"
            "      \"PoSePenalty\": n,              (numeric) PoSe penalty score\n"
            "      \"PoSeRevivedHeight\": n,        (numeric) Block height of PoSe revival\n"
            "      \"PoSeBanHeight\": n,            (numeric) Block height of PoSe ban (-1 if not banned)\n"
            "      \"revocationReason\": n,         (numeric) Revocation reason (0=not revoked)\n"
            "      \"ownerAddress\": \"xxxx\",      (string) Owner address\n"
            "      \"votingAddress\": \"xxxx\",     (string) Voting address\n"
            "      \"payoutAddress\": \"xxxx\",     (string) Payout address\n"
            "      \"operatorPubKey\": \"xxxx\"     (string) Operator public key\n"
            "    }\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("listmasternodes", "") + HelpExampleRpc("listmasternodes", ""));


    const std::string& strFilter = request.params.size() > 0 ? request.params[0].get_str() : "";
    UniValue ret(UniValue::VARR);

    auto mnList = deterministicMNManager->GetListAtChainTip();
    mnList.ForEachMN(false, [&](const CDeterministicMNCPtr& dmn) {
        UniValue obj = DmnToJson(dmn);
        if (filterMasternode(obj, strFilter, !dmn->IsPoSeBanned())) {
            ret.push_back(obj);
        }
    });
    return ret;
}

UniValue getmasternodecount(const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() > 0))
        throw std::runtime_error(
            "getmasternodecount\n"
            "\nGet masternode count values\n"

            "\nResult:\n"
            "{\n"
            "  \"total\": n,        (numeric) Total masternodes\n"
            "  \"enabled\": n,      (numeric) Enabled masternodes\n"
            "  \"ipv4\": n,         (numeric) Number of IPv4 masternodes\n"
            "  \"ipv6\": n,         (numeric) Number of IPv6 masternodes\n"
            "  \"onion\": n         (numeric) Number of Tor masternodes\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getmasternodecount", "") + HelpExampleRpc("getmasternodecount", ""));

    UniValue obj(UniValue::VOBJ);
    auto infoMNs = mnodeman.getMNsInfo();

    obj.pushKV("total", infoMNs.total);
    obj.pushKV("enabled", infoMNs.enabledSize);
    obj.pushKV("ipv4", infoMNs.ipv4);
    obj.pushKV("ipv6", infoMNs.ipv6);
    obj.pushKV("onion", infoMNs.onion);

    return obj;
}

UniValue getmasternodestatus(const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() != 0))
        throw std::runtime_error(
            "getmasternodestatus\n"
            "\nPrint deterministic masternode status\n"

            "\nResult:\n"
            "{\n"
            "  \"proTxHash\": \"xxxx\",       (string) ProTx transaction hash\n"
            "  \"collateralHash\": \"xxxx\",  (string) Collateral transaction hash\n"
            "  \"collateralIndex\": n,        (numeric) Collateral output index\n"
            "  \"dmnstate\": { ... },         (object) Current DMN state\n"
            "  \"netaddr\": \"xxxx\",         (string) Masternode network address\n"
            "  \"status\": \"xxxx\"           (string) Masternode status message\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getmasternodestatus", "") + HelpExampleRpc("getmasternodestatus", ""));

    if (!fMasterNode)
        throw JSONRPCError(RPC_MISC_ERROR, _("This is not a masternode."));

    if (!activeMasternodeManager) {
        throw JSONRPCError(RPC_MISC_ERROR, _("Active Masternode not initialized."));
    }

    if (!deterministicMNManager->IsDIP3Enforced()) {
        throw JSONRPCError(RPC_MISC_ERROR, _("Deterministic masternodes are not enforced yet"));
    }

    const CActiveMasternodeInfo* amninfo = activeMasternodeManager->GetInfo();
    UniValue mnObj(UniValue::VOBJ);
    auto dmn = deterministicMNManager->GetListAtChainTip().GetMNByOperatorKey(amninfo->pubKeyOperator);
    if (dmn) {
        dmn->ToJson(mnObj);
    }
    mnObj.pushKV("netaddr", amninfo->service.ToString());
    mnObj.pushKV("status", activeMasternodeManager->GetStatus());
    return mnObj;
}

// clang-format off
// PIV2-Core: Only DMN/DIP3 compatible RPC commands
// Legacy masternode commands removed (startmasternode, createmasternodebroadcast, etc.)
// Use ProTx/EVO commands (protx_register_fund, generateoperatorkeypair) for masternode management
static const CRPCCommand commands[] =
{ //  category              name                         actor (function)            okSafe argNames
  //  --------------------- ---------------------------  --------------------------  ------ --------
    { "masternode",         "getmasternodecount",        &getmasternodecount,        true,  {} },
    { "masternode",         "getmasternodestatus",       &getmasternodestatus,       true,  {} },
    { "masternode",         "initmasternode",            &initmasternode,            true,  {"privkey","address"} },
    { "masternode",         "listmasternodes",           &listmasternodes,           true,  {"filter"} },

    /** Not shown in help */
    { "hidden",             "getcachedblockhashes",      &getcachedblockhashes,      true,  {} },
    { "hidden",             "mnping",                    &mnping,                    true,  {} },
};
// clang-format on

void RegisterMasternodeRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
