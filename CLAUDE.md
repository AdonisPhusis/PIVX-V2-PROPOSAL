# PIV2-Core Claude Instructions

## Testnet Quality Standards

**IMPORTANT**: The testnet MUST reflect the future mainnet. Never make "quick fixes" or "hacky workarounds" for testnet issues.

### Rules for testnet development:

1. **No quick fixes** - Every change to the testnet must be production-quality code that would be acceptable on mainnet
2. **Proper consensus** - Any consensus rule must be deterministic and identical on all nodes
3. **Clean restarts** - If the testnet state is corrupted or has issues from bad code, restart with a fresh genesis rather than patching around problems
4. **Test from genesis** - New nodes must be able to sync from genesis block to tip without issues

### New Testnet Genesis Parameters

When starting a new testnet with fresh genesis:
- Dev wallet premine: **98,850,000 PIV2** (block 1)
- This allows testing of all economic parameters at scale

### Architecture Notes

- **DMM (Deterministic Masternode Miner)**: Block production layer
- **HU (Horizontal Utility)**: BFT finality layer with 2/3 quorum
- Production and verification use IDENTICAL deterministic formulas based on block data only
- No local clock dependencies for consensus rules

### VPS Nodes

- Seed: 57.131.33.151
- MN1: 162.19.251.75
- MN2: 57.131.33.152
- MN3: 57.131.33.214
- MN4: 51.75.31.44

Deploy script: `contrib/testnet/deploy_to_vps.sh`
