#!/usr/bin/env python3
"""Checks that every console-client's fixed GBA-button mapping table covers
all 10 finlink_key bits (finlink/protocol.h) exactly once -- the
console-client "Button-Mapping-Vollständigkeit" test category. Static/regex,
same reasoning as check_capabilities.py: Switch's table needs borealis
headers, 3DS's needs libctru's <3ds.h>, neither of which builds on a plain
host without the respective devkitPro cross-toolchain (see
clients/3ds/tests/, clients/nds/arm9/tests/ for where that line gets drawn
the other way, for logic that genuinely doesn't need any SDK header) --
parsing the actual committed source text directly is cheap, no build step,
and still catches the real failure mode this check exists for: a button
silently missing from (or duplicated in) one client's table.

NDS has no separate table file (see clients/nds/arm9/source/main.c) -- its
mapping is a flat `if (keys & KEY_X) mask |= FINLINK_KEY_X;` chain instead,
extracted the same way.
"""
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
PROTOCOL_H = REPO_ROOT / "core" / "include" / "finlink" / "protocol.h"

# The canonical set of button bits every console-client mapping table must
# cover -- derived from finlink_key's own definition, not hardcoded here, so
# a future protocol.h change (new button added/removed) can't silently drift
# out of sync with this list.
FINLINK_KEY_ENUM_RE = re.compile(r"typedef enum \{(.*?)\} finlink_key;", re.DOTALL)
FINLINK_KEY_NAME_RE = re.compile(r"\b(FINLINK_KEY_[A-Z]+)\b")

TABLE_ENTRY_KEY_RE = re.compile(r"\b(FINLINK_KEY_[A-Z]+)\b")


def load_canonical_keys() -> set:
    text = PROTOCOL_H.read_text(encoding="utf-8")
    enum_match = FINLINK_KEY_ENUM_RE.search(text)
    if not enum_match:
        sys.exit(f"error: no 'finlink_key' enum found in {PROTOCOL_H}")
    return set(FINLINK_KEY_NAME_RE.findall(enum_match.group(1)))


def extract_table_keys(path: Path, start_marker: str, end_marker: str) -> list:
    """Returns every FINLINK_KEY_* occurrence between the first line
    containing start_marker and the next line containing end_marker
    (inclusive range), in source order, WITH duplicates -- duplicate
    detection is the caller's job, not this extractor's."""
    text = path.read_text(encoding="utf-8")
    lines = text.splitlines()
    start = next((i for i, line in enumerate(lines) if start_marker in line), None)
    if start is None:
        sys.exit(f"error: marker {start_marker!r} not found in {path}")
    end = next((i for i in range(start + 1, len(lines)) if end_marker in lines[i]), None)
    if end is None:
        sys.exit(f"error: marker {end_marker!r} (after {start_marker!r}) not found in {path}")
    segment = "\n".join(lines[start:end + 1])
    return TABLE_ENTRY_KEY_RE.findall(segment)


# Each client: (display name, source path, start marker, end marker).
CLIENTS = [
    (
        "switch",
        REPO_ROOT / "clients" / "switch" / "source" / "gba_buttons.hpp",
        "GBA_BUTTONS = {",
        "} };",
    ),
    (
        "3ds",
        REPO_ROOT / "clients" / "3ds" / "source" / "gba_buttons.hpp",
        "GBA_BUTTONS = {",
        "} };",
    ),
    (
        "nds",
        REPO_ROOT / "clients" / "nds" / "arm9" / "source" / "main.c",
        "if (keys & KEY_A) mask |= FINLINK_KEY_A;",
        "if (keys & KEY_L) mask |= FINLINK_KEY_L;",
    ),
]


def main() -> int:
    canonical = load_canonical_keys()
    if len(canonical) != 10:
        sys.exit(f"error: expected 10 finlink_key entries in {PROTOCOL_H}, found {len(canonical)}: {sorted(canonical)}")

    failures = []
    for name, path, start_marker, end_marker in CLIENTS:
        if not path.is_file():
            failures.append(f"{name}: {path} does not exist")
            continue

        found = extract_table_keys(path, start_marker, end_marker)
        found_set = set(found)

        missing = canonical - found_set
        extra = found_set - canonical
        duplicates = {key for key in found_set if found.count(key) > 1}

        if missing:
            failures.append(f"{name}: {path} is missing {sorted(missing)}")
        if extra:
            failures.append(f"{name}: {path} references unknown key(s) {sorted(extra)}")
        if duplicates:
            failures.append(f"{name}: {path} maps {sorted(duplicates)} more than once")

    if failures:
        print("Button-mapping completeness check failed:\n")
        for failure in failures:
            print(f"  - {failure}")
        return 1

    print(f"OK: all {len(CLIENTS)} console-clients map all {len(canonical)} finlink_key bits exactly once.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
