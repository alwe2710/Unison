#!/usr/bin/env python3
"""Cross-repo check for the "generell: Protokoll-Implementierung
einheitlich über alle Server und Clients" test category: each of the four
host forks (Cemu, azahar, melonDS, dolphin-gba-stream) hand-rolls its own
handshake_error `code` string mapping from a local HandshakeErrorCode enum
(VersionMismatch/SlotUnavailable/MalformedRequest) -- each repo's own test
suite already round-trips its own mapping through unison_core's reference
parser (catches "is this valid JSON unison_core can parse"), but nothing
previously checked that all four repos agree on the same string for the
same semantic error. A host silently drifting to e.g. "slot_taken" instead
of "slot_unavailable" would pass its own tests (unison_core's parser
doesn't care what the string actually says) while breaking every client
that pattern-matches on the documented value from docs/protocol.md.

Static/regex over shallow clones of the four host repos (this script's
only cross-repo check, hence living here in Unison -- the coordinating
hub all four already depend on -- rather than in any single host repo's
own CI, which only ever checks out itself), same reasoning as
check_capabilities.py/check_button_mapping.py for staying static rather
than needing a build.
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

# (display name, clone URL, branch, path to the file with the mapping)
HOSTS = [
    ("Cemu", "https://github.com/alwe2710/cemu-screen-stream.git", "transcoding",
     "src/Cemu/unisonStream/UnisonMessages.cpp"),
    ("azahar", "https://github.com/alwe2710/azahar-screen-stream.git", "master",
     "src/core/streaming/handshake_messages.cpp"),
    ("melonDS", "https://github.com/alwe2710/melonds-screen-stream.git", "master",
     "src/streaming/UnisonMessages.cpp"),
    ("dolphin-gba-stream", "https://github.com/alwe2710/dolphin-gba-stream.git", "master",
     "Source/Core/Core/HW/GBAStreamHandshake.cpp"),
]

# The three known HandshakeErrorCode enumerators every host defines --
# hardcoded here (not derived from any one host's own enum) since the
# whole point is checking they all still agree with each other, not with
# whichever host happened to be read first.
ERROR_CODES = ["VersionMismatch", "SlotUnavailable", "MalformedRequest"]

CASE_STRING_RE = re.compile(
    r'case HandshakeErrorCode::(\w+)\s*:\s*(?:\n\s*)?return\s*"([^"]*)"'
)


def extract_mapping(text: str) -> dict:
    return {name: value for name, value in CASE_STRING_RE.findall(text)}


def main() -> int:
    failures = []
    mappings = {}  # host name -> {enumerator: string}

    with tempfile.TemporaryDirectory() as tmp:
        for name, url, branch, rel_path in HOSTS:
            dest = Path(tmp) / name
            result = subprocess.run(
                ["git", "clone", "--depth", "1", "--branch", branch, url, str(dest)],
                capture_output=True, text=True,
            )
            if result.returncode != 0:
                failures.append(f"{name}: clone failed -- {result.stderr.strip()}")
                continue
            path = dest / rel_path
            if not path.is_file():
                failures.append(f"{name}: {rel_path} does not exist on {branch}")
                continue
            mapping = extract_mapping(path.read_text(encoding="utf-8"))
            missing = [code for code in ERROR_CODES if code not in mapping]
            if missing:
                failures.append(f"{name}: {rel_path} has no case for {missing}")
                continue
            mappings[name] = mapping

    if len(mappings) >= 2:
        reference_host = next(iter(mappings))
        reference = mappings[reference_host]
        for code in ERROR_CODES:
            values = {host: m[code] for host, m in mappings.items()}
            distinct = set(values.values())
            if len(distinct) > 1:
                failures.append(
                    f"{code}: hosts disagree on the wire string -- {values} "
                    f"(every host must use the exact same string for the same semantic error)"
                )

    if failures:
        print("Cross-host protocol consistency check failed:\n")
        for failure in failures:
            print(f"  - {failure}")
        return 1

    checked = ", ".join(mappings.keys())
    print(f"OK: {checked} all agree on the same handshake_error code strings for {ERROR_CODES}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
