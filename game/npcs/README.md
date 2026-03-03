# NPC Definitions

Each NPC template is one `.toml` file in this folder.

- Unique NPC id: filename stem (`old_man.toml` -> `old_man`)
- Display name: `name`
- Sprite source: `sprite_tileset` under `game/assets/art`
- Sprite clip key is NPC id (filename stem)
- Optional: `npc_size = "WxH"`, `speed_ms`, `wander_radius`

## Spawn Flow

1. In a room `.tmx`, create tile layer `NPCs`.
2. Paint NPC marker tiles on that layer.
3. Marker tiles must come from a TSX tile with `npc_name`.
   Example: `npc_name = "old_man"` resolves to `game/npcs/old_man.toml`.

## Dialogue

Use repeated `[[dialogue]]` blocks:

- `question` (or `questions`): comma-separated keywords/phrases
- `response`: NPC reply text

Example:

[[dialogue]]
question = "hi, hello, hey"
response = "Hello there."
