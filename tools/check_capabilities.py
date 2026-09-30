#!/usr/bin/env python3
"""Lints docs/capabilities.md's machine-readable ``clients`` block against
the actual client source it describes -- part of the Unison CI pipeline's
build-smoke stage (see .github/workflows/build.yml), added specifically
because feature work (video modes so far) has landed unevenly across
clients and nothing previously caught the doc/code pair drifting apart.

Only the ``clients`` block is checked: the ``hosts`` block describes the
four emulator forks, which live in separate repos not checked out here
(see docs/capabilities.md's own note) and stay a manually maintained,
informational-only record for this pass.

Three extraction strategies, named directly in each client's JSON entry:

- ``video_mode_option_kotlin``: Android's Prefs.kt declares its supported
  modes as a Kotlin list of ``VideoModeOption("id", ...)`` calls -- pull
  every id out via regex and compare the resulting set exactly against
  the declared ``video_modes``.
- ``format_dispatch_h264_h265``: "tiles" is always assumed present (the
  universal fallback for a client that never sets hello_ack.video_mode).
  Beyond that, requires the literal enum constant name
  (``UNISON_VIDEO_FORMAT_H264``/``_H265``) to appear combined with
  ``format`` via a bitwise AND (``hdr.format & UNISON_VIDEO_FORMAT_H264``,
  or the same inside an ``|``'d parenthesized group) -- i.e. it has to
  appear in what looks like real dispatch code deciding what to do with a
  received frame, not just anywhere in the source tree. Replaces an
  earlier, naive ``grep_h264_h265`` strategy (bare substring search for
  "h264"/"h265" anywhere at all) that produced a real false positive for
  nds: that client's own code comments explain *why* it has no H.264/H.265
  path at all, and a comment doing that necessarily still contains the
  word "h264" -- caught live when this script flagged 3ds/switch/nds
  together and a closer read showed 3ds/switch have real decoders
  (MVD hardware / ffmpeg software respectively) but nds categorically
  doesn't (its own `unison_decode_video_frame()` call rejects anything but
  TILES/INDEXED outright). It deliberately does NOT try to detect "legacy"
  support this way -- unlike h264/h265, that string has no equivalently
  unambiguous signal to grep for.
- ``grep_h264_h265``: the older, naive version described above -- kept
  (not yet migrated) for any client not confirmed to need the stricter
  check.

Exit code is non-zero (and every mismatch is printed) if anything
disagrees; this is meant to be cheap and static, no build step involved.
"""
import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CAPABILITIES_MD = REPO_ROOT / "docs" / "capabilities.md"

JSON_BLOCK_RE = re.compile(r"```json\n(.*?)\n```", re.DOTALL)
# Matches both a literal string id ("h264") and a reference to Android's
# own VIDEO_MODE_DEFAULT constant (its one entry that isn't a literal --
# see Prefs.kt, VIDEO_MODE_DEFAULT = "tiles").
KOTLIN_VIDEO_MODE_RE = re.compile(r'VideoModeOption\(\s*(?:"([a-z0-9]+)"|VIDEO_MODE_DEFAULT)')
VIDEO_MODE_DEFAULT_RE = re.compile(r'VIDEO_MODE_DEFAULT\s*=\s*"([a-z0-9]+)"')

# Every client's own generate.py-produced i18n file (e.g.
# strings_generated.hpp/.cpp/.h/.js) carries a "video_mode_h264"/
# "video_mode_h265" *label string key* regardless of whether that client's
# UI actually has a picker exposing it (see i18n/strings.json -- it's
# generated uniformly across all five clients). That's just an unused
# resource on the four clients without a picker, not a sign the feature is
# implemented there -- excluded here so it doesn't look like one.
GENERATED_STRINGS_FILE_RE = re.compile(r"strings_generated\.")


def load_declared_capabilities() -> dict:
    text = CAPABILITIES_MD.read_text(encoding="utf-8")
    match = JSON_BLOCK_RE.search(text)
    if not match:
        sys.exit(f"error: no ```json block found in {CAPABILITIES_MD}")
    return json.loads(match.group(1))


def extract_video_mode_option_kotlin(source_glob: str) -> set:
    path = REPO_ROOT / source_glob
    text = path.read_text(encoding="utf-8")
    default_match = VIDEO_MODE_DEFAULT_RE.search(text)
    default_mode = default_match.group(1) if default_match else None

    modes = set()
    for match in KOTLIN_VIDEO_MODE_RE.finditer(text):
        modes.add(match.group(1) if match.group(1) is not None else default_mode)
    return modes


def extract_grep_h264_h265(source_glob: str) -> set:
    modes = {"tiles"}  # universal fallback, see this script's docstring
    for path in REPO_ROOT.glob(source_glob):
        if not path.is_file() or GENERATED_STRINGS_FILE_RE.search(path.name):
            continue
        text = path.read_text(encoding="utf-8", errors="ignore").lower()
        if "h264" in text:
            modes.add("h264")
        if "h265" in text:
            modes.add("h265")
    return modes


# Requires the enum constant combined with `format` via a bitwise AND --
# real dispatch code deciding what to do with a received frame
# (`hdr.format & UNISON_VIDEO_FORMAT_H264`, or the same name inside an
# `|`'d parenthesized group, as clients/switch/source/session.cpp's own
# `hdr.format & (UNISON_VIDEO_FORMAT_H264 | UNISON_VIDEO_FORMAT_H265)`
# does) -- never just the bare constant name on its own line, which a
# comment explaining the format's *absence* would also contain. See this
# script's own docstring for why this replaced a naive substring grep.
FORMAT_DISPATCH_H264_RE = re.compile(r"format\s*&\s*\(?[^)\n]*UNISON_VIDEO_FORMAT_H264")
FORMAT_DISPATCH_H265_RE = re.compile(r"format\s*&\s*\(?[^)\n]*UNISON_VIDEO_FORMAT_H265")


def extract_format_dispatch_h264_h265(source_glob: str) -> set:
    modes = {"tiles"}  # universal fallback, see this script's docstring
    for path in REPO_ROOT.glob(source_glob):
        if not path.is_file() or GENERATED_STRINGS_FILE_RE.search(path.name):
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        if FORMAT_DISPATCH_H264_RE.search(text):
            modes.add("h264")
        if FORMAT_DISPATCH_H265_RE.search(text):
            modes.add("h265")
    return modes


EXTRACTORS = {
    "video_mode_option_kotlin": extract_video_mode_option_kotlin,
    "format_dispatch_h264_h265": extract_format_dispatch_h264_h265,
    "grep_h264_h265": extract_grep_h264_h265,
}


def main() -> int:
    capabilities = load_declared_capabilities()
    clients = capabilities.get("clients", {})
    if not clients:
        sys.exit(f"error: no 'clients' block found in {CAPABILITIES_MD}")

    failures = []
    for name, entry in clients.items():
        extractor = EXTRACTORS.get(entry["extract"])
        if extractor is None:
            failures.append(f"{name}: unknown extract strategy {entry['extract']!r}")
            continue

        declared = set(entry["video_modes"])
        actual = extractor(entry["source_glob"])

        missing_in_code = declared - actual  # doc claims a mode the code doesn't have
        extra_in_code = actual - declared    # code has a mode the doc doesn't mention

        if missing_in_code:
            failures.append(
                f"{name}: docs/capabilities.md claims {sorted(missing_in_code)} "
                f"but {entry['source_glob']} shows no sign of it"
            )
        if extra_in_code:
            failures.append(
                f"{name}: {entry['source_glob']} shows {sorted(extra_in_code)} "
                f"but docs/capabilities.md doesn't list it as supported"
            )

    if failures:
        print("Capability matrix drift detected:\n")
        for failure in failures:
            print(f"  - {failure}")
        print(f"\nFix by updating {CAPABILITIES_MD} or the client code, whichever is stale.")
        return 1

    print(f"OK: docs/capabilities.md matches all {len(clients)} clients' source.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
