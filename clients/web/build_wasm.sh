#!/usr/bin/env bash
# Builds unison_core.js from wasm_bridge.c + core/ -- run this after
# changing wasm_bridge.c or core/ itself; a normal checkout doesn't need to
# (unison_core.js is committed, see this directory's README).
#
# Needs the emsdk submodule checked out first (one-time):
#   git submodule add https://github.com/emscripten-core/emsdk.git clients/web/emsdk
#   git submodule update --init clients/web/emsdk
# ($EMSDK_DIR below assumes that path -- see this script's own history for
# why: this client used to live inside a Dolphin fork, where the emsdk
# submodule was a sibling under that repo's own Externals/, not here).
#
# Activates the vendored emsdk toolchain (installing it on first run --
# network access needed exactly once, then it's cached under emsdk/, which
# .gitignore excludes from the repo itself, see its own comment there),
# configures+builds unison/core against that toolchain, links this
# directory's bridge (wasm_bridge.c) against it as one self-contained JS
# file (-s SINGLE_FILE=1 -- the .wasm binary is base64-embedded inside,
# matching how this client has no separate static file serving for it,
# just the one committed unison_core.js index.html <script>s directly),
# verifies the result with bridge_test.mjs (real unison_core behavior, not
# just "it compiled").
#
# $1 (optional): output directory for intermediate build artifacts (the
# nested unison_core wasm build, mainly). Defaults to this script's own
# directory. unison_core.js is always written into this script's own
# directory regardless -- it's a committed source file (index.html loads it
# directly), not a build artifact kept out of git.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
EMSDK_DIR="$SCRIPT_DIR/emsdk"
UNISON_CORE_DIR="$REPO_ROOT/core"
EMSDK_VERSION="6.0.4" # keep in sync with the version this was last verified against

OUT_DIR="${1:-$SCRIPT_DIR}"
mkdir -p "$OUT_DIR"

if [ ! -x "$EMSDK_DIR/emsdk" ]; then
  echo "== emsdk not found at $EMSDK_DIR --" \
       "run: git submodule add https://github.com/emscripten-core/emsdk.git clients/web/emsdk =="
  exit 1
fi
if [ ! -x "$EMSDK_DIR/upstream/emscripten/emcc" ]; then
  echo "== emsdk toolchain not installed, installing $EMSDK_VERSION (one-time, needs network) =="
  "$EMSDK_DIR/emsdk" install "$EMSDK_VERSION"
fi
"$EMSDK_DIR/emsdk" activate "$EMSDK_VERSION" > /dev/null
# shellcheck disable=SC1091
source "$EMSDK_DIR/emsdk_env.sh" > /dev/null

echo "== configuring+building unison_core for wasm32 =="
CORE_BUILD_DIR="$OUT_DIR/unison_core_wasm"
emcmake cmake -S "$UNISON_CORE_DIR" -B "$CORE_BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release -DUNISON_BUILD_TESTS=OFF > /dev/null
cmake --build "$CORE_BUILD_DIR" -j"$(nproc)"

echo "== building the wasm bridge =="
# Every unison_wasm_* function wasm_bridge.c defines with EMSCRIPTEN_KEEPALIVE
# -- kept as one explicit list (not -s EXPORT_ALL=1) so an unused/typo'd
# export fails the link loudly instead of silently doing nothing.
EXPORTED_FUNCTIONS=_unison_wasm_parse_hello,_unison_wasm_hello_protocol_version,_unison_wasm_hello_stream_type,_unison_wasm_hello_input_encoding,_unison_wasm_hello_video_width,_unison_wasm_hello_video_height,_unison_wasm_hello_video_fps,_unison_wasm_hello_has_audio,_unison_wasm_hello_audio_sample_rate,_unison_wasm_hello_audio_channels,_unison_wasm_hello_slot_count,_unison_wasm_hello_slot_index,_unison_wasm_hello_slot_label,_unison_wasm_hello_slot_occupied,_unison_wasm_build_hello_ack,_unison_wasm_parse_session_ready,_unison_wasm_ready_slot,_unison_wasm_ready_video_width,_unison_wasm_ready_video_height,_unison_wasm_ready_video_fps,_unison_wasm_ready_has_audio,_unison_wasm_ready_audio_sample_rate,_unison_wasm_ready_audio_channels,_unison_wasm_ready_has_redirect,_unison_wasm_ready_redirect_host,_unison_wasm_ready_redirect_port,_unison_wasm_ready_video_mode,_unison_wasm_parse_handshake_error,_unison_wasm_error_code,_unison_wasm_error_detail,_unison_wasm_peek_handshake_message,_unison_wasm_parse_video_header,_unison_wasm_video_width,_unison_wasm_video_height,_unison_wasm_video_format,_unison_wasm_video_compressed_offset,_unison_wasm_video_compressed_size,_unison_wasm_video_max_inflated_size,_unison_wasm_decode_video_frame,_unison_wasm_parse_audio_frame,_unison_wasm_audio_sample_rate,_unison_wasm_audio_channels,_unison_wasm_audio_sample_count,_unison_wasm_audio_samples_offset,_unison_wasm_build_input_frame,_unison_wasm_build_touch_and_buttons_frame,_unison_wasm_build_extended_input_frame,_malloc,_free

emcc "$SCRIPT_DIR/wasm_bridge.c" \
  -I "$UNISON_CORE_DIR/include" \
  "$CORE_BUILD_DIR/libunison_core.a" \
  -o "$SCRIPT_DIR/unison_core.js" \
  -s MODULARIZE=1 -s EXPORT_NAME=UnisonCore -s SINGLE_FILE=1 \
  -s EXPORTED_FUNCTIONS="$EXPORTED_FUNCTIONS" \
  -s EXPORTED_RUNTIME_METHODS=ccall,cwrap,UTF8ToString,HEAPU8,HEAP32 \
  -O2

echo "== running the Node.js bridge test =="
(cd "$SCRIPT_DIR" && "$EMSDK_DIR/node/"*/bin/node bridge_test.mjs)

echo "== OK: $SCRIPT_DIR/unison_core.js ($(wc -c < "$SCRIPT_DIR/unison_core.js") bytes) =="
