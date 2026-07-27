#!/usr/bin/env bash
# Fetches and builds hacbrewpack (NCA/NSP packer) from source into $1.
#
# Not packaged by devkitPro, and unlike this project's other fetched build
# tools (emsdk for the web client, makerom/bannertool for the 3DS CIA),
# there's no single well-established prebuilt binary to fetch: the
# original repo (The-4n/hacBrewPack) has been deleted from GitHub, and
# what's left are a handful of low-star, unverified forks -- not something
# to trust as an opaque binary. Building from source instead (this fork:
# github.com/dragonflylee/hacBrewPack, chosen for being the most recently
# maintained of the available copies and for having a source tree that
# matches the well-known hacbrewpack structure file-for-file) means the
# actual code that runs is at least inspectable, not just trusted blindly.
#
# This script fetches no cryptographic key material -- see this
# directory's README for why that's the *user's* responsibility, not
# something this repo bundles or downloads.
set -euo pipefail

OUT_DIR="${1:?usage: build_hacbrewpack.sh <output-dir>}"
mkdir -p "$OUT_DIR"

if [ -x "$OUT_DIR/hacbrewpack" ]; then
    echo "hacbrewpack already built in $OUT_DIR"
    exit 0
fi

SRC_DIR="$OUT_DIR/src"
if [ ! -d "$SRC_DIR" ]; then
    echo "Fetching hacbrewpack source..."
    git clone --depth 1 https://github.com/dragonflylee/hacBrewPack.git "$SRC_DIR"
fi

echo "Building hacbrewpack..."
cp "$SRC_DIR/config.mk.template" "$SRC_DIR/config.mk"
make -C "$SRC_DIR" -j"$(nproc)"
cp "$SRC_DIR/hacbrewpack" "$OUT_DIR/hacbrewpack"

echo "hacbrewpack ready at $OUT_DIR/hacbrewpack"
