# World Layout Spec

## Core model
- Game style is top-down (old Zelda-like).
- One world is one screen.
- Each world is independently editable in its own folder.
- There is no separate standalone overworld TMX.
- The overworld is the stitched set of all worlds' level-0 screens.

## Directory model
- Worlds live under `data/worlds/<x>_<y>/`.
- Folder names encode world position on the overworld grid.
- Examples:
  - `500_500` = center
  - `500_501` = north of center
  - `501_500` = east of center
  - `499_500` = below/south of center

## Per-world files
- `<x>_<y>.toml` contains world metadata/rules.
- `<x>_<y>.world` arranges all levels inside that world.
- Room files live in a `rooms/` subfolder and use level-based TMX names:
  - `rooms/0.tmx` for level 0
  - `rooms/p1.tmx` for level +1
  - `rooms/n1.tmx` for level -1
  - additional levels follow the same pattern.

## Cross-world composition
- `data/worlds/globe.world` defines level-0 adjacency.
- `globe.world` should reference level-0 TMX files directly (for example `500_500/rooms/0.tmx`), not nested `.world` files.
- Walking off screen edges (left/right/top/bottom) moves players to adjacent worlds on level 0.

## Vertical movement
- Within a single world, players can move up/down between levels.
- Non-zero levels are internal to that world and do not define global overworld adjacency.
