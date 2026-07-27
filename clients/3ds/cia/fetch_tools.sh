#!/usr/bin/env bash
# Downloads makerom and bannertool (prebuilt Linux x86_64 binaries) into
# $1 if not already present there. Neither tool is packaged by devkitPro
# (CIA building/signing tooling is maintained separately from the core
# homebrew SDK) -- same reasoning as this project's build_wasm.sh fetching
# emsdk itself rather than assuming it's preinstalled.
#
# makerom: github.com/3DSGuy/Project_CTR (the actively maintained fork --
# the original profi200/Project_CTR repo has moved there).
# bannertool: github.com/carstene1ns/3ds-bannertool -- a community mirror/
# fork with a working release pipeline; the original Steveice10/bannertool
# repo no longer exists on GitHub.
set -euo pipefail

TOOLS_DIR="${1:?usage: fetch_tools.sh <output-dir>}"
mkdir -p "$TOOLS_DIR"

MAKEROM_VERSION="makerom-v0.19.0"
BANNERTOOL_VERSION="1.2.3"

if [ ! -x "$TOOLS_DIR/makerom" ]; then
    echo "Fetching makerom ${MAKEROM_VERSION}..."
    curl -sL -o "$TOOLS_DIR/makerom.zip" \
        "https://github.com/3DSGuy/Project_CTR/releases/download/${MAKEROM_VERSION}/${MAKEROM_VERSION}-ubuntu_x86_64.zip"
    unzip -o -d "$TOOLS_DIR" "$TOOLS_DIR/makerom.zip" >/dev/null
    rm -f "$TOOLS_DIR/makerom.zip"
    chmod +x "$TOOLS_DIR/makerom"
fi

if [ ! -x "$TOOLS_DIR/bannertool" ]; then
    echo "Fetching bannertool ${BANNERTOOL_VERSION}..."
    curl -sL -o "$TOOLS_DIR/bannertool.tar.gz" \
        "https://github.com/carstene1ns/3ds-bannertool/releases/download/${BANNERTOOL_VERSION}/bannertool-${BANNERTOOL_VERSION}-linux.tar.gz"
    tar -xzf "$TOOLS_DIR/bannertool.tar.gz" -C "$TOOLS_DIR"
    mv "$TOOLS_DIR/bannertool-${BANNERTOOL_VERSION}-linux/bannertool" "$TOOLS_DIR/bannertool"
    rm -rf "$TOOLS_DIR/bannertool-${BANNERTOOL_VERSION}-linux" "$TOOLS_DIR/bannertool.tar.gz"
    chmod +x "$TOOLS_DIR/bannertool"
fi

echo "makerom and bannertool ready in $TOOLS_DIR"
