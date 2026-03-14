# Container Definitions

Each `.toml` file defines a UI container: where slots are positioned within a texture,
what item types they accept, and how they're laid out.

The client loads these at startup and uses them for hit-testing and icon placement.
Slot coordinates are **pixel offsets from the top-left corner** of the texture.

## Top-level fields

```toml
texture = "widgets/backpack.png"   # path relative to game/assets/art/
```

## Manual slots

Use `[[slots]]` to define individual slots with exact positions:

```toml
[[slots]]
x = 8
y = 8
w = 32
h = 32
item_type = "Weapon"   # optional type constraint
```

`item_type` restricts the slot to a specific equipment type. Valid values:
`Weapon`, `Armor`, `Shield`, `Legs`, `Boots`, `Helmet`, `Ring`, `Necklace`.
Omit `item_type` for slots that accept any item.

Slots are ordered — their index in the file determines their inventory mapping.

## Grid layout

Use `[grid]` to generate a uniform grid of slots:

```toml
[grid]
start_x = 5       # x offset of the first slot (top-left corner)
start_y = 5       # y offset of the first slot (top-left corner)
slot_w = 32        # width of each slot in pixels
slot_h = 32        # height of each slot in pixels
gap_x = 4          # horizontal gap between slots
gap_y = 4          # vertical gap between slots
cols = 5           # number of columns
rows = 4           # number of rows
```

Grid slots are generated left-to-right, top-to-bottom and appended after
any manually defined `[[slots]]`. Grid slots have no type constraint.

## Files

| File | Used for | Slot count |
|------|----------|------------|
| `equipment_bar.toml` | Bottom HUD equipment bar | 8 (each type-constrained) |
| `hotbar.toml` | Bottom HUD hotbar | 10 |
| `backpack.toml` | Inventory overlay (I key) | 35 (5x7 grid) |

## Inventory mapping

The flat inventory array is split across containers by index:

- **Hotbar**: inventory indices `0` to `hotbar_slots - 1`
- **Backpack**: inventory indices `hotbar_slots` to `hotbar_slots + backpack_slots - 1`
- **Equipment**: references items by instance ID, not by inventory index

The server-side inventory limit (`kInventoryLimit` in `server_constants.h`) must be
at least `hotbar_slots + backpack_slots` for all slots to be usable.
