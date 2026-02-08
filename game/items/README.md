# Item Definitions

Each item template is one `.toml` file in this folder.

- Unique item id: filename stem (`wooden_sword.toml` -> `wooden_sword`)
- Display name: `name`
- Sprite linkage:
  - `sprite_tileset`: TSX filename under `game/assets/art`
  - `sprite_name`: tile property name in that TSX (typically `name` or `item_name`)

## Spawn Flow (Tiled -> TOML -> Game)

1. In a room `.tmx`, paint item marker tiles on tile layer `Items`.
2. Marker tile should have one of these TSX properties: `item_name`, `item_id`, `item`, or `name`.
3. That value resolves to `game/items/<value>.toml`.
4. Server spawns runtime items from those markers.

Unknown keys in TOML are ignored, so designers can add extra stats safely.
