# Monster Definitions

Each monster template is one `.toml` file in this folder.

- Unique monster id: filename stem (`rat.toml` -> `rat`)
- Author-facing display name: `name`
- Stats: `max_hp`, `strength`, `speed_ms`, `exp_reward`
- Optional loot table: repeated `[[drops]]` entries with `item` + `chance`

## Sprite Connection

Use two fields:

- `sprite_tileset`: TSX filename under `game/assets/art` (example: `character.tsx`)
- `sprite_name`: logical sprite clip name in that TSX (the tile `name` property consumed by `Sprites`)

This keeps data stable if tile ids move around in Tiled.

## Spawn Flow (Tiled -> TOML -> Game)

1. In a room `.tmx`, create a tile layer named `Monsters`.
2. Paint tiles on that layer where monsters should spawn.
3. Those tiles must come from a TSX tile that has property `monster_name`.
   Example: `monster_name = "rat"` resolves to `game/monsters/rat.toml`.
4. The painted tile is treated as the monster anchor tile (bottom-left).

The server reads the `Monsters` layer and instantiates runtime monsters from the matched TOML template.

## Size + Animation Convention

- In TOML, set `monster_size = "WxH"` (tile units, each tile = 16px), e.g. `1x1`, `1x2`, `2x2`.
- Gameplay/collision uses this size (anchor = bottom-left tile).
- Best place for animation metadata is the art tileset (`.tsx`) because direction/frame order is visual data:
  - Keep per-frame `name` + `dir` + `seq` properties in TSX (current engine path), or
  - Enforce a strict sheet convention and generate clips automatically in sprite loader.

## Notes

- Parser currently uses a flat TOML subset (`key = value`, quoted strings, ints, `# comments`).
- Supported table form: `[[drops]]` with `item` and `chance` (0.0..1.0).
- Unknown keys are ignored safely.
