#!/usr/bin/env python3
"""
Generate data/worlds/globe.world from position-coded world folders.

Expected folder naming:
  data/worlds/<grid_x>_<grid_y>/

Expected level-0 map in each folder:
  0.tmx
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


GRID_DIR_RE = re.compile(r"^(-?\d+)_(-?\d+)$")


def read_l0_pixel_size(tmx_path: Path) -> tuple[int, int]:
    try:
        root = ET.parse(tmx_path).getroot()
    except ET.ParseError as exc:
        raise ValueError(f"failed to parse TMX '{tmx_path}': {exc}") from exc

    width = int(root.attrib.get("width", "0"))
    height = int(root.attrib.get("height", "0"))
    tile_w = int(root.attrib.get("tilewidth", "0"))
    tile_h = int(root.attrib.get("tileheight", "0"))
    if width <= 0 or height <= 0 or tile_w <= 0 or tile_h <= 0:
        raise ValueError(
            f"invalid size attrs in '{tmx_path}' "
            f"(width={width}, height={height}, tilewidth={tile_w}, tileheight={tile_h})"
        )
    return width * tile_w, height * tile_h


def discover_worlds(worlds_root: Path) -> list[dict]:
    worlds: list[dict] = []
    for entry in sorted(worlds_root.iterdir()):
        if not entry.is_dir():
            continue
        m = GRID_DIR_RE.match(entry.name)
        if not m:
            continue

        gx = int(m.group(1))
        gy = int(m.group(2))
        l0 = entry / "rooms/0.tmx"
        if not l0.exists():
            continue

        pixel_w, pixel_h = read_l0_pixel_size(l0)
        worlds.append(
            {
                "name": entry.name,
                "gx": gx,
                "gy": gy,
                "pixel_w": pixel_w,
                "pixel_h": pixel_h,
            }
        )
    return worlds


def build_globe_data(worlds: list[dict]) -> dict:
    if not worlds:
        raise ValueError("no worlds found (expected folders like 500_500 with rooms/0.tmx)")

    # Anchor at 500_500 when present; otherwise use the first discovered world.
    origin = next((w for w in worlds if w["gx"] == 500 and w["gy"] == 500), worlds[0])
    step_w = origin["pixel_w"]
    step_h = origin["pixel_h"]

    maps = []
    for w in sorted(worlds, key=lambda x: (x["gy"], x["gx"])):
        maps.append(
            {
                "fileName": f'{w["name"]}/rooms/0.tmx',
                "x": (w["gx"] - origin["gx"]) * step_w,
                "y": (origin["gy"] - w["gy"]) * step_h,
                "width": w["pixel_w"],
                "height": w["pixel_h"],
            }
        )

    return {"type": "world", "maps": maps}


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate globe.world from world folders")
    parser.add_argument(
        "--worlds-root",
        default="data/worlds",
        help="Worlds root directory (default: data/worlds)",
    )
    parser.add_argument(
        "--output",
        default="data/worlds/globe.world",
        help="Output .world path (default: data/worlds/globe.world)",
    )
    args = parser.parse_args()

    worlds_root = Path(args.worlds_root)
    output_path = Path(args.output)

    if not worlds_root.exists() or not worlds_root.is_dir():
        print(f"error: worlds root not found: {worlds_root}", file=sys.stderr)
        return 1

    try:
        worlds = discover_worlds(worlds_root)
        data = build_globe_data(worlds)
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(data, indent=4) + "\n", encoding="utf-8")
    print(f"wrote {output_path} with {len(data['maps'])} maps")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
