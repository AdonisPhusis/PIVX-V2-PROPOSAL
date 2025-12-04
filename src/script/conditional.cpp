// Copyright (c) 2025 The PIV2 developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "script/conditional.h"
#include "hash.h"

CScript CreateConditionalScript(
    const uint256& hashlock,
    uint32_t timelock,
    const CKeyID& destA,
    const CKeyID& destB)
{
    CScript script;

    // Branch A: secret + signature
    script << OP_IF;
    script << OP_SIZE;
    script << 32;
    script << OP_EQUALVERIFY;
    script << OP_SHA256;
    script << ToByteVector(hashlock);
    script << OP_EQUALVERIFY;
    script << OP_DUP;
    script << OP_HASH160;
    script << ToByteVector(destA);

    // Branch B: timeout + signature
    script << OP_ELSE;
    script << CScriptNum(timelock);
    script << OP_CHECKLOCKTIMEVERIFY;
    script << OP_DROP;
    script << OP_DUP;
    script << OP_HASH160;
    script << ToByteVector(destB);

    script << OP_ENDIF;
    script << OP_EQUALVERIFY;
    script << OP_CHECKSIG;

    return script;
}

bool IsConditionalScript(const CScript& script)
{
    uint256 h;
    uint32_t t;
    CKeyID a, b;
    return DecodeConditionalScript(script, h, t, a, b);
}

bool DecodeConditionalScript(
    const CScript& script,
    uint256& hashlock,
    uint32_t& timelock,
    CKeyID& destA,
    CKeyID& destB)
{
    std::vector<unsigned char> data;
    opcodetype opcode;
    CScript::const_iterator it = script.begin();

    // OP_IF
    if (!script.GetOp(it, opcode) || opcode != OP_IF)
        return false;

    // OP_SIZE
    if (!script.GetOp(it, opcode) || opcode != OP_SIZE)
        return false;

    // 32 (size check)
    if (!script.GetOp(it, opcode, data))
        return false;
    if (data.empty() || CScriptNum(data, true).getint() != 32)
        return false;

    // OP_EQUALVERIFY
    if (!script.GetOp(it, opcode) || opcode != OP_EQUALVERIFY)
        return false;

    // OP_SHA256
    if (!script.GetOp(it, opcode) || opcode != OP_SHA256)
        return false;

    // hashlock (32 bytes)
    if (!script.GetOp(it, opcode, data) || data.size() != 32)
        return false;
    memcpy(hashlock.begin(), data.data(), 32);

    // OP_EQUALVERIFY
    if (!script.GetOp(it, opcode) || opcode != OP_EQUALVERIFY)
        return false;

    // OP_DUP
    if (!script.GetOp(it, opcode) || opcode != OP_DUP)
        return false;

    // OP_HASH160
    if (!script.GetOp(it, opcode) || opcode != OP_HASH160)
        return false;

    // destA (20 bytes)
    if (!script.GetOp(it, opcode, data) || data.size() != 20)
        return false;
    destA = CKeyID(uint160(data));

    // OP_ELSE
    if (!script.GetOp(it, opcode) || opcode != OP_ELSE)
        return false;

    // timelock (use 5-byte max like CLTV, validate positive)
    if (!script.GetOp(it, opcode, data))
        return false;
    try {
        CScriptNum num(data, true, 5);  // 5-byte max like CHECKLOCKTIMEVERIFY
        int lockValue = num.getint();
        if (lockValue <= 0)
            return false;
        timelock = static_cast<uint32_t>(lockValue);
    } catch (const scriptnum_error&) {
        return false;
    }

    // OP_CHECKLOCKTIMEVERIFY
    if (!script.GetOp(it, opcode) || opcode != OP_CHECKLOCKTIMEVERIFY)
        return false;

    // OP_DROP
    if (!script.GetOp(it, opcode) || opcode != OP_DROP)
        return false;

    // OP_DUP
    if (!script.GetOp(it, opcode) || opcode != OP_DUP)
        return false;

    // OP_HASH160
    if (!script.GetOp(it, opcode) || opcode != OP_HASH160)
        return false;

    // destB (20 bytes)
    if (!script.GetOp(it, opcode, data) || data.size() != 20)
        return false;
    destB = CKeyID(uint160(data));

    // OP_ENDIF
    if (!script.GetOp(it, opcode) || opcode != OP_ENDIF)
        return false;

    // OP_EQUALVERIFY
    if (!script.GetOp(it, opcode) || opcode != OP_EQUALVERIFY)
        return false;

    // OP_CHECKSIG
    if (!script.GetOp(it, opcode) || opcode != OP_CHECKSIG)
        return false;

    // Verify we've reached the end of the script (no trailing garbage)
    if (it != script.end())
        return false;

    return true;
}

CScript CreateConditionalSpendA(
    const std::vector<unsigned char>& sig,
    const CPubKey& pubkey,
    const std::vector<unsigned char>& secret,
    const CScript& redeemScript)
{
    CScript scriptSig;
    scriptSig << sig;
    scriptSig << ToByteVector(pubkey);
    scriptSig << secret;
    scriptSig << OP_TRUE;
    scriptSig << std::vector<unsigned char>(redeemScript.begin(), redeemScript.end());
    return scriptSig;
}

CScript CreateConditionalSpendB(
    const std::vector<unsigned char>& sig,
    const CPubKey& pubkey,
    const CScript& redeemScript)
{
    CScript scriptSig;
    scriptSig << sig;
    scriptSig << ToByteVector(pubkey);
    scriptSig << OP_FALSE;
    scriptSig << std::vector<unsigned char>(redeemScript.begin(), redeemScript.end());
    return scriptSig;
}
