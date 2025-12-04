# PIV2 Wallet - BerkeleyDB Compatibility Guide

**Version:** 1.0
**Date:** 2025-12-04
**Status:** TESTNET READY

---

## Overview

PIV2 wallet uses BerkeleyDB (BDB) for storing wallet data (`wallet.dat`). This is inherited from Bitcoin Core's original design and remains the most battle-tested solution.

**Important:** SQLite migration is planned for Phase 5 (post-testnet). Do NOT attempt to replace BDB now.

---

## Supported BDB Versions

| Version | Status | Compatibility | Recommendation |
|---------|--------|---------------|----------------|
| **4.8** | Preferred | Full | Production & Testnet |
| **5.3** | Supported | Full (with flag) | Testnet only |
| **6.x+** | Untested | Unknown | Not recommended |

---

## Why BerkeleyDB 4.8?

1. **Bitcoin Legacy** - Bitcoin Core used BDB 4.8 for 10+ years
2. **Wallet Portability** - Wallets created with 4.8 work everywhere
3. **Battle-tested** - Billions of dollars secured with this version
4. **PIVX Compatibility** - Original PIVX wallets use 4.8

**BDB 5.3 Note:** Works fine but wallets are NOT portable to 4.8 systems. For testnet this is acceptable.

---

## Option A: Install BerkeleyDB 4.8 (Recommended)

### Ubuntu 20.04 / 22.04 (via PPA)

```bash
sudo add-apt-repository ppa:pivx/berkeley-db4
sudo apt-get update
sudo apt-get install -y libdb4.8-dev libdb4.8++-dev
```

### Ubuntu 24.04+ / Any Linux (from source)

```bash
# Use the provided script
./contrib/wallet/install_bdb48.sh

# Then configure with:
./configure --without-gui \
    BDB_LIBS="-L/usr/local/BerkeleyDB.4.8/lib -ldb_cxx-4.8" \
    BDB_CFLAGS="-I/usr/local/BerkeleyDB.4.8/include"
```

### Manual Installation

```bash
# Download
cd /tmp
wget http://download.oracle.com/berkeley-db/db-4.8.30.NC.tar.gz
tar -xzf db-4.8.30.NC.tar.gz
cd db-4.8.30.NC

# Patch for modern GCC (required for GCC 10+)
sed -i 's/__atomic_compare_exchange/__atomic_compare_exchange_db/g' dbinc/atomic.h

# Build
cd build_unix
../dist/configure --enable-cxx --disable-shared --with-pic --prefix=/usr/local/BerkeleyDB.4.8
make -j$(nproc)
sudo make install

# Verify
ls /usr/local/BerkeleyDB.4.8/lib/libdb_cxx*
```

---

## Option B: Use BDB 5.3 Fallback (Testnet)

For quick testnet deployment on Ubuntu 24.04+, BDB 5.3 works with the `--with-incompatible-bdb` flag.

### Install BDB 5.3

```bash
sudo apt-get install -y libdb5.3-dev libdb5.3++-dev
```

### Configure with Fallback

```bash
./configure --without-gui --with-incompatible-bdb
```

### What `--with-incompatible-bdb` Does

1. Allows BDB versions other than 4.8
2. Searches for: `db_cxx-4.8` → `db_cxx-5.3` → `db_cxx` → `db4_cxx`
3. Links the first library found
4. Prints warning about wallet portability

---

## Build Matrix

| Ubuntu Version | BDB Available | Configure Command |
|----------------|---------------|-------------------|
| 20.04 | 4.8 (PPA) | `./configure --without-gui` |
| 22.04 | 4.8 (PPA) | `./configure --without-gui` |
| 24.04 | 5.3 only | `./configure --without-gui --with-incompatible-bdb` |
| 25.04 | 5.3 only | `./configure --without-gui --with-incompatible-bdb` |

---

## Wallet Operations Verified

All wallet operations have been tested with both BDB 4.8 and 5.3:

| Operation | BDB 4.8 | BDB 5.3 | Command |
|-----------|---------|---------|---------|
| Create wallet | OK | OK | `piv2d -daemon` |
| Generate address | OK | OK | `piv2-cli getnewaddress` |
| Dump private key | OK | OK | `piv2-cli dumpprivkey <addr>` |
| Import key | OK | OK | `piv2-cli importprivkey <wif>` |
| Send transaction | OK | OK | `piv2-cli sendtoaddress <addr> <amt>` |
| Keypool refill | OK | OK | `piv2-cli keypoolrefill 100` |
| MN operator init | OK | OK | `piv2-cli initmasternode <key>` |
| Backup wallet | OK | OK | `piv2-cli backupwallet <path>` |
| Encrypt wallet | OK | OK | `piv2-cli encryptwallet <pass>` |

---

## Testnet VPS Quick Setup

### Option A (BDB 4.8 - Clean)

```bash
# Install BDB 4.8 from source
curl -sSL https://raw.githubusercontent.com/AdonisPhusis/PIVX-V2-PROPOSAL/main/contrib/wallet/install_bdb48.sh | bash

# Build PIV2
cd ~/PIV2-Core
./autogen.sh
./configure --without-gui \
    BDB_LIBS="-L/usr/local/BerkeleyDB.4.8/lib -ldb_cxx-4.8" \
    BDB_CFLAGS="-I/usr/local/BerkeleyDB.4.8/include"
make -j$(nproc)
```

### Option B (BDB 5.3 - Quick)

```bash
# Install BDB 5.3
sudo apt-get install -y libdb5.3-dev libdb5.3++-dev

# Build PIV2 with fallback
cd ~/PIV2-Core
./autogen.sh
./configure --without-gui --with-incompatible-bdb
make -j$(nproc)
```

---

## Troubleshooting

### Error: `undefined reference to Dbt::~Dbt()`

**Cause:** BDB library not linked properly.

**Solution:**
```bash
# Check what's installed
dpkg -l | grep libdb

# For BDB 5.3
sudo apt-get install -y libdb5.3-dev libdb5.3++-dev
make distclean
./autogen.sh
./configure --without-gui --with-incompatible-bdb
make -j$(nproc)
```

### Error: `libdb_cxx headers missing`

**Cause:** BDB C++ development headers not installed.

**Solution:**
```bash
# Ubuntu 24.04+
sudo apt-get install -y libdb5.3++-dev

# Or install BDB 4.8 from source
./contrib/wallet/install_bdb48.sh
```

### Error: `Found Berkeley DB other than 4.8`

**Cause:** Configure found BDB but not version 4.8.

**Solution:** Add `--with-incompatible-bdb` flag:
```bash
./configure --without-gui --with-incompatible-bdb
```

### Wallet won't open after upgrade

**Cause:** Wallet created with different BDB version.

**Solution:** Wallets are version-specific. For testnet, create a new wallet:
```bash
mv ~/.piv2/testnet/wallet.dat ~/.piv2/testnet/wallet.dat.bak
piv2d -testnet -daemon
```

---

## Future: SQLite Migration (Phase 5)

Post-testnet, we plan to migrate from BDB to SQLite:

- **Why:** Modern, better tooling, no version conflicts
- **When:** Phase 5 (after testnet stability confirmed)
- **How:** Descriptor wallets (Bitcoin Core model)
- **Migration:** Automatic upgrade path from BDB wallets

**DO NOT** attempt SQLite migration before Phase 5.

---

## References

- [Bitcoin Core BDB Notes](https://github.com/bitcoin/bitcoin/blob/master/doc/build-unix.md)
- [Oracle BDB Downloads](https://www.oracle.com/database/technologies/related/berkeleydb-downloads.html)
- [PIVX BDB Documentation](https://github.com/PIVX-Project/PIVX/blob/master/doc/build-unix.md)

---

*PIV2 Wallet BDB Compatibility Guide v1.0 - 2025-12-04*
