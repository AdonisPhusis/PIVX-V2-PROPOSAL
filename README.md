PIVX V2 Core Repository
=======================

PIVX V2 is a fork of PIVX implementing a new economic model with DMM (Deterministic Masternode Management) consensus.

## Key Features

- **DMM Consensus**: Deterministic masternode-based block production (no PoW/PoS mining)
- **KHU Economic Layer**: Collateralized yield system with transparent (KHU) and shielded (ZKHU) components
- **SHIELD Privacy**: zk-SNARKs based privacy protocol (Sapling)
- **No Block Rewards**: Zero inflation post-V6 activation
- **ECDSA Finality**: 12-block finality with ECDSA signatures

## Building

PIVX V2 Core is daemon-only (no Qt GUI). See `doc/build-unix.md` for build instructions.

```bash
./autogen.sh
./configure --without-gui --with-incompatible-bdb
make -j$(nproc)
```

## Binaries

- `piv2d` - PIVX V2 daemon
- `piv2-cli` - RPC command-line interface
- `piv2-tx` - Transaction utility

## Testing

```bash
# Unit tests
./src/test/test_pivx --run_test=hu_*

# Regtest block generation
./src/piv2-cli -regtest generate 10
```

## Documentation

- `/docs/00-NOMENCLATURE.md` - Official nomenclature
- `/docs/01-ARCHITECTURE.md` - System architecture
- `/docs/02-SPEC.md` - Technical specification
- `/docs/blueprints/` - Detailed implementation docs

## License

PIVX V2 Core is released under the MIT license. See [COPYING](COPYING) for details.

Based on PIVX Core (https://pivx.org) and Bitcoin Core.
