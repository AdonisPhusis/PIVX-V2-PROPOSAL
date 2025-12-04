#!/usr/bin/env python3
# =============================================================================
# PIV2 Genesis Block Generator
# =============================================================================
# Copyright (c) 2025 The PIV2/PIVHU developers
# Distributed under the MIT software license
#
# This script generates genesis block parameters for PIV2/PIVHU networks.
# It mines a valid nonce that meets the difficulty target.
#
# Usage:
#   python3 genesis_piv2.py --network testnet
#   python3 genesis_piv2.py --network mainnet
#   python3 genesis_piv2.py --network regtest
#
# Output:
#   - hashGenesisBlock
#   - hashMerkleRoot
#   - nNonce
#   - nTime
#   - nBits
# =============================================================================

import hashlib
import struct
import time
import argparse
from binascii import hexlify, unhexlify

# =============================================================================
# Constants
# =============================================================================

# Network configurations
NETWORKS = {
    'mainnet': {
        'timestamp': "PIVHU Genesis Nov 2025 - Knowledge Hedge Unit - MN Consensus - Zero Block Reward",
        'nTime': 1732924800,  # Nov 30, 2025 00:00:00 UTC
        'nBits': 0x1e0ffff0,  # Standard testnet difficulty
        'nVersion': 1,
    },
    'testnet': {
        'timestamp': "PIVHU Testnet Dec 2025 - Knowledge Hedge Unit - 3 MN DMM Genesis",
        'nTime': 1733270400,  # Dec 4, 2025 00:00:00 UTC
        'nBits': 0x1e0ffff0,  # Standard testnet difficulty
        'nVersion': 1,
    },
    'regtest': {
        'timestamp': "PIVHU Regtest Dec 2025 - Knowledge Hedge Unit - Test Genesis v2",
        'nTime': 1732924800,
        'nBits': 0x207fffff,  # Very easy difficulty for regtest
        'nVersion': 1,
    }
}

# Genesis outputs for testnet (P2PKH)
TESTNET_OUTPUTS = [
    # Output 0: MN1 Collateral (10,000 PIV2)
    {'value': 10000 * 100000000, 'pubkeyhash': '87060609b12d797fd2396629957fde4a3d3adbff'},
    # Output 1: MN2 Collateral (10,000 PIV2)
    {'value': 10000 * 100000000, 'pubkeyhash': '2563dfb22c186e7d2741ed6d785856f7f17e187a'},
    # Output 2: MN3 Collateral (10,000 PIV2)
    {'value': 10000 * 100000000, 'pubkeyhash': 'dd2ba22aec7280230ff03da61b7141d7acf12edd'},
    # Output 3: Dev Wallet (50,000,000 PIV2)
    {'value': 50000000 * 100000000, 'pubkeyhash': '197cf6a11f4214b4028389c77b90f27bc90dc839'},
    # Output 4: Faucet (50,000,000 PIV2)
    {'value': 50000000 * 100000000, 'pubkeyhash': 'ec1ab14139850ef2520199c49ba1e46656c9e84f'},
]

# =============================================================================
# Hash Functions
# =============================================================================

def double_sha256(data):
    """Bitcoin-style double SHA256"""
    return hashlib.sha256(hashlib.sha256(data).digest()).digest()

def reverse_bytes(data):
    """Reverse byte order (for display)"""
    return data[::-1]

# =============================================================================
# Serialization
# =============================================================================

def serialize_varint(n):
    """Serialize variable-length integer"""
    if n < 0xfd:
        return struct.pack('<B', n)
    elif n <= 0xffff:
        return struct.pack('<BH', 0xfd, n)
    elif n <= 0xffffffff:
        return struct.pack('<BI', 0xfe, n)
    else:
        return struct.pack('<BQ', 0xff, n)

def create_coinbase_script(timestamp, nbits=486604799, nnumber=4):
    """Create coinbase scriptSig"""
    # Encode nBits as push data
    nbits_bytes = struct.pack('<I', nbits)
    # Encode number as CScriptNum
    nnumber_bytes = bytes([nnumber])
    # Timestamp as bytes
    ts_bytes = timestamp.encode('utf-8')

    script = b''
    # Push nBits (4 bytes)
    script += bytes([len(nbits_bytes)]) + nbits_bytes
    # Push nnumber
    script += bytes([len(nnumber_bytes)]) + nnumber_bytes
    # Push timestamp
    script += bytes([len(ts_bytes)]) + ts_bytes

    return script

def create_p2pkh_script(pubkeyhash):
    """Create P2PKH output script"""
    # OP_DUP OP_HASH160 <20 bytes> OP_EQUALVERIFY OP_CHECKSIG
    return bytes([0x76, 0xa9, 0x14]) + unhexlify(pubkeyhash) + bytes([0x88, 0xac])

def serialize_transaction(outputs, timestamp):
    """Serialize coinbase transaction"""
    tx = b''

    # Version (4 bytes)
    tx += struct.pack('<I', 1)

    # Number of inputs (varint)
    tx += serialize_varint(1)

    # Input: coinbase
    tx += b'\x00' * 32  # Previous txid (null)
    tx += struct.pack('<I', 0xffffffff)  # Previous vout (-1)

    # scriptSig
    scriptsig = create_coinbase_script(timestamp)
    tx += serialize_varint(len(scriptsig))
    tx += scriptsig

    # Sequence
    tx += struct.pack('<I', 0xffffffff)

    # Number of outputs
    tx += serialize_varint(len(outputs))

    # Outputs
    for out in outputs:
        tx += struct.pack('<Q', out['value'])  # Value (8 bytes)
        script = create_p2pkh_script(out['pubkeyhash'])
        tx += serialize_varint(len(script))
        tx += script

    # Locktime
    tx += struct.pack('<I', 0)

    return tx

def compute_merkle_root(txids):
    """Compute merkle root from transaction IDs"""
    if len(txids) == 0:
        return b'\x00' * 32

    while len(txids) > 1:
        if len(txids) % 2 == 1:
            txids.append(txids[-1])

        new_level = []
        for i in range(0, len(txids), 2):
            new_level.append(double_sha256(txids[i] + txids[i+1]))
        txids = new_level

    return txids[0]

def serialize_block_header(version, prev_hash, merkle_root, ntime, nbits, nonce):
    """Serialize block header"""
    header = b''
    header += struct.pack('<I', version)       # Version (4 bytes)
    header += prev_hash                         # Previous block hash (32 bytes)
    header += merkle_root                       # Merkle root (32 bytes)
    header += struct.pack('<I', ntime)         # Timestamp (4 bytes)
    header += struct.pack('<I', nbits)         # nBits (4 bytes)
    header += struct.pack('<I', nonce)         # Nonce (4 bytes)
    return header

# =============================================================================
# Mining
# =============================================================================

def nbits_to_target(nbits):
    """Convert nBits to target"""
    exponent = nbits >> 24
    mantissa = nbits & 0x00ffffff

    if exponent <= 3:
        target = mantissa >> (8 * (3 - exponent))
    else:
        target = mantissa << (8 * (exponent - 3))

    return target

def mine_genesis(network_config, outputs, verbose=True):
    """Mine genesis block"""
    timestamp = network_config['timestamp']
    ntime = network_config['nTime']
    nbits = network_config['nBits']
    version = network_config['nVersion']

    # Create coinbase transaction
    coinbase_tx = serialize_transaction(outputs, timestamp)
    txid = double_sha256(coinbase_tx)

    # Compute merkle root
    merkle_root = compute_merkle_root([txid])

    # Previous block hash (null for genesis)
    prev_hash = b'\x00' * 32

    # Target
    target = nbits_to_target(nbits)

    if verbose:
        print("=" * 70)
        print("PIV2 Genesis Mining")
        print("=" * 70)
        print(f"Timestamp: {timestamp}")
        print(f"nTime: {ntime}")
        print(f"nBits: 0x{nbits:08x}")
        print(f"Target: {target:064x}")
        print(f"Merkle Root: {hexlify(reverse_bytes(merkle_root)).decode()}")
        print("=" * 70)
        print("Mining...")

    start_time = time.time()
    nonce = 0

    while nonce < 0xffffffff:
        header = serialize_block_header(version, prev_hash, merkle_root, ntime, nbits, nonce)
        hash_result = double_sha256(header)
        hash_int = int.from_bytes(hash_result, 'little')

        if hash_int <= target:
            elapsed = time.time() - start_time

            if verbose:
                print()
                print("=" * 70)
                print("GENESIS FOUND!")
                print("=" * 70)
                print(f"nNonce: {nonce}")
                print(f"Hash: {hexlify(reverse_bytes(hash_result)).decode()}")
                print(f"Merkle Root: {hexlify(reverse_bytes(merkle_root)).decode()}")
                print(f"Time: {elapsed:.2f} seconds")
                print("=" * 70)
                print()
                print("C++ Code:")
                print(f'    assert(consensus.hashGenesisBlock == uint256S("0x{hexlify(reverse_bytes(hash_result)).decode()}"));')
                print(f'    assert(genesis.hashMerkleRoot == uint256S("0x{hexlify(reverse_bytes(merkle_root)).decode()}"));')
                print()

            return {
                'nonce': nonce,
                'hash': hexlify(reverse_bytes(hash_result)).decode(),
                'merkle_root': hexlify(reverse_bytes(merkle_root)).decode(),
                'ntime': ntime,
                'nbits': nbits,
            }

        if verbose and nonce % 100000 == 0:
            print(f"  Mining... nNonce={nonce}")

        nonce += 1

    raise Exception("Genesis not found!")

# =============================================================================
# Main
# =============================================================================

def main():
    parser = argparse.ArgumentParser(description='PIV2 Genesis Block Generator')
    parser.add_argument('--network', choices=['mainnet', 'testnet', 'regtest'],
                        default='testnet', help='Network type')
    parser.add_argument('--quiet', action='store_true', help='Quiet mode')
    args = parser.parse_args()

    network_config = NETWORKS[args.network]

    # Use testnet outputs for all networks (customize as needed)
    outputs = TESTNET_OUTPUTS

    result = mine_genesis(network_config, outputs, verbose=not args.quiet)

    if args.quiet:
        print(f"nNonce={result['nonce']}")
        print(f"hash={result['hash']}")
        print(f"merkle={result['merkle_root']}")

if __name__ == '__main__':
    main()
