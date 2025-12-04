#!/bin/sh
# Copyright (c) 2013-2016 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C
set -e
srcdir="$(dirname $0)"
cd "$srcdir"

# Initialize submodules if needed
if [ ! -d "src/secp256k1/.git" ] || [ ! -d "src/leveldb/.git" ]; then
  echo "Initializing git submodules..."
  git submodule update --init --recursive 2>/dev/null || true
fi

# Fix leveldb if submodule is incomplete (missing source files)
if [ ! -f "src/leveldb/db/builder.cc" ]; then
  echo "Leveldb submodule incomplete, cloning manually..."
  rm -rf src/leveldb
  git clone --depth 1 --branch 1.22 https://github.com/google/leveldb.git src/leveldb
  echo "Leveldb 1.22 installed"
fi

if [ -z ${LIBTOOLIZE} ] && GLIBTOOLIZE="$(command -v glibtoolize)"; then
  LIBTOOLIZE="${GLIBTOOLIZE}"
  export LIBTOOLIZE
fi
command -v autoreconf >/dev/null || \
  (echo "configuration failed, please install autoconf first" && exit 1)
autoreconf --install --force --warnings=all
