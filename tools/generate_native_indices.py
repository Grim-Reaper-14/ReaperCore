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

    found: dict[str, int] = {}
    current_index = 0

    # Preserve the exact insertion order used by YimMenuV2's generator:
    # namespace order first, then native hash/object order within each namespace.
    for _namespace, native_group in natives.items():
        for _hash, native_data in native_group.items():
            name = native_data.get("name")
            if name in TARGETS and name not in found:
                found[name] = current_index
            current_index += 1

    missing = sorted(TARGETS.difference(found))
    if missing:
        print(
            "missing required native definitions: " + ", ".join(missing),
            file=sys.stderr,
        )
        return 1

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        "#pragma once\n\n"
        "#include <cstddef>\n\n"
        "namespace reapercore::generated_natives\n"
        "{\n"
        f"    inline constexpr std::size_t native_count = {current_index};\n"
        f"    inline constexpr std::size_t player_id = {found['PLAYER_ID']};\n"
        f"    inline constexpr std::size_t player_ped_id = {found['PLAYER_PED_ID']};\n"
        f"    inline constexpr std::size_t get_game_timer = {found['GET_GAME_TIMER']};\n"
        "}\n",
        encoding="utf-8",
    )

    print(f"generated {output} ({current_index} natives)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
