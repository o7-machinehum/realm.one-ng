# Character Sprite Layout Guide

## Core Rules

- Tile size is `16x16`.
- A sprite id is discovered from tile property: `name`
- The labeled tile is the **top-left anchor** for that full sprite block.

## Sprite Size

Player currently uses `1x2` (configured in code).
One frame rectangle is: width `W * 16`, height `H * 16`.

## Movement Block Layout

4 directional strips, each with 4 frames:

- Direction row order (top -> bottom): South, East, North, West
- Each strip has 4 frames laid out left -> right
- Frame width in tiles: `W`

## Action Block Layout (Optional)

- Same direction order: South, East, North, West
- Same 4 frames per direction
- Placed in a second 4-row block (the "bottom half")
- **Action frame width is `2 * W` tiles** (twice as wide on X)
- Action frame height is still `H` tiles

If action strips are missing, engine falls back to movement animation.

## Death Block Layout (Optional)

A dead creature can be shown as the inverse size, right (and lower) to the South facing last animation.

## Anchor Positioning (Required)

Label the top-left tile of the **full block** (movement + action).

## Practical Example (`1x2` sprite)

- Movement frame size: `1x2` tiles
- Action frame size: `2x2` tiles
- Movement block rows: 4 strips * 2 tiles high = 8 tile rows
- Action block rows: 4 strips * 2 tiles high = 8 tile rows
- Total vertical block: 16 tile rows

## Naming

Character sheet clip name should match what client uses (`player_1`, etc.).
