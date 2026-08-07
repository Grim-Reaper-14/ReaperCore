#!/usr/bin/env python3

import json
import pathlib
import sys

TARGETS = {
    "PLAYER_ID",
    "PLAYER_PED_ID",
    "GET_GAME_TIMER",
}


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: generate_native_indices.py <natives_gen9.json> <output.hpp>", file=sys.stderr)
        return 2

    source = pathlib.Path(sys.argv[1])
    output = pathlib.Path(sys.argv[2])

    with source.open("r", encoding="utf-8") as stream:
        natives = json.load(stream)

    found: