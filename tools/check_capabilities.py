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

Two extraction strategies, named directly in each client's JSON entry:

- ``video_mode_option_kotlin``: Android's Prefs.kt declares its supported
  modes as a Kotlin list of ``VideoModeOption("id", ...)`` calls -- pull
  every id out via regex and compare the resulting set exactly against
  the declared ``video_modes``.
- ``grep_h264_h265``: every other client has no video-mode picker at all
  right now (see capabilities.md) -- "tiles" is always assumed present
  (the universal fallback for a client that never sets
  hello_ack.video_mode). Beyond that, this only checks for the literal
  strings "h264"/"h265" appearing anywhere in the client's own source
  tree, since that's the one drift direction worth catching automatically
  right now: a client quietly gaining (or losing) H.264/H265 support
  without capabilities.md being updated to match. It deliberately does
  NOT try to detect "legacy" support this way -- unlike h264/h265, that
  string has no equivalently unambiguous signal to grep for.

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


EXTRACTORS = {
    "video_mode_option_kotlin": extract_video_mode_option_kotlin,
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
