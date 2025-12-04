// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef HU_RPC_REGISTER_H
#define HU_RPC_REGISTER_H

/** These are in one header file to avoid creating tons of single-function
 * headers for everything under src/rpc/ */
class CRPCTable;

/** Register block chain RPC commands */
void RegisterBlockchainRPCCommands(CRPCTable& tableRPC);
/** Register P2P networking RPC commands */
void RegisterNetRPCCommands(CRPCTable& tableRPC);
/** Register miscellaneous RPC commands */
void RegisterMiscRPCCommands(CRPCTable& tableRPC);
/** Register mining RPC commands */
void RegisterMiningRPCCommands(CRPCTable& tableRPC);
/** Register raw transaction RPC commands */
void RegisterRawTransactionRPCCommands(CRPCTable& tableRPC);
/** Register masternode RPC commands */
void RegisterMasternodeRPCCommands(CRPCTable& tableRPC);
/** Register Evo RPC commands */
void RegisterEvoRPCCommands(CRPCTable &tableRPC);
// HU: RegisterQuorumsRPCCommands removed
/** Register KHU RPC commands */
void RegisterHURPCCommands(CRPCTable &tableRPC);
/** Register DAO RPC commands (PIVHU monthly governance) */
void RegisterDAORPCCommands(CRPCTable &tableRPC);
/** Register Conditional Scripts RPC commands */
void RegisterConditionalRPCCommands(CRPCTable &tableRPC);

static inline void RegisterAllCoreRPCCommands(CRPCTable& tableRPC)
{
    RegisterBlockchainRPCCommands(tableRPC);
    RegisterNetRPCCommands(tableRPC);
    RegisterMiscRPCCommands(tableRPC);
    RegisterMiningRPCCommands(tableRPC);
    RegisterRawTransactionRPCCommands(tableRPC);
    RegisterMasternodeRPCCommands(tableRPC);
    RegisterEvoRPCCommands(tableRPC);
    // HU: RegisterQuorumsRPCCommands removed
    RegisterHURPCCommands(tableRPC);
    RegisterDAORPCCommands(tableRPC);
    RegisterConditionalRPCCommands(tableRPC);
}

#endif // HU_RPC_REGISTER_H
