# Art Alignment Guide (Characters + Monsters)

This explains how to lay out monster/player sprites so the engine parses them correctly.

## Core Rules

- Tile size is `16x16`.
- A sprite id is discovered from tile property:
  - `name`
- The labeled tile is the **top-left anchor** for that full sprite block.

## Sprite Size

Each sprite has logical size `WxH` tiles.

- Player currently uses `1x2` (configured in code).
- Monsters use `monster_size = "WxH"` from `game/monsters/*.toml`.

So one frame rectangle is:

- width: `W * 16`
- height: `H * 16`

## Movement Block Layout

Movement uses 4 directional strips, each with 4 frames.

- Direction row order (top -> bottom):
  1. South
  2. East
  3. North
  4. West
- Each strip has 4 frames laid out left -> right.
- Frame width in tiles for movement: `W`.

## Action Block Layout (Optional)

Action strips (attack/swing) are optional.

- Same direction order: South, East, North, West.
- Same 4 frames per direction.
- Placed in a second 4-row block (the “bottom half”).
- **Action frame width is `2 * W` tiles** (twice as wide on X).
- Action frame height is still `H` tiles.

If action strips are missing, engine falls back to movement animation.


## Death Block Layout (Optional)
A dead creature can be shown as the inverse size, right (and lower) to the South facing last animation. So for a 1x2 monsters, you will find the dead creature at (64,16) and (80,16).


## Anchor Positioning (Required)

Label the top-left tile of the **full block** (movement + action).

## Practical Example (`1x2` sprite)

- Movement frame size: `1x2` tiles.
- Action frame size: `2x2` tiles.
- Movement block rows: 4 strips * 2 tiles high = 8 tile rows.
- Action block rows: 4 strips * 2 tiles high = 8 tile rows.

Total vertical block: 16 tile rows.

## Naming Consistency

- Character sheet clip name should match what client uses (`player_1`, etc.).
- Monster art clip name should match monster id (TOML filename stem, e.g. `rat`).
- Item sprites are different: item clip name is always item id (filename stem).

## Common Mistakes

- Wrong direction row order.
- Label placed on the wrong anchor tile (must be top-left of full block).
- Action strip using movement width (`W`) instead of `2W`.
- Labeling only random tiles instead of the anchor tile.
