# AGENTS.md

## Repo overview
- Language: C++23
- Build system: CMake (primary), Makefile helpers
- Main executables:
  - `client` (`src/client`)
  - `server` (`src/server`)
- Shared game code: `src/common`
- Tests live in: `src/tests`

## Project structure
- `src/client`: rendering and network client code
- `src/server`: server/network/world logic
- `src/common`: shared types for messages, rooms, entities, sprites
- `src/tests`: test and serialization checks
- `data/`: gameplay data (`rooms`, `characters`)
- `data/worlds`: Tiled `.world` files (multi-world layout)
- `game/assets`: art and tiled project files
- `game/monsters`: monster template TOML files

## Build and run
- Configure:
  - `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`
- Build:
  - `cmake --build build --parallel`
- Run server:
  - `./build/server 7777`
- Run client:
  - `./build/client`
- Makefile shortcuts:
  - `make build`
  - `make run-server PORT=7777`
  - `make run-client`

## Test commands
CMake currently defines these test binaries:
- `test_room`
- `test_sprites`
- `client_server_ser_de`

Run them directly after build:
- `./build/test_room`
- `./build/test_sprites`
- `./build/client_server_ser_de`

Note: `ctest` currently reports no registered tests; run test binaries directly.

## Dependencies
Required CMake packages/modules:
- `raylib`
- `tinyxml2`
- `cereal`
- `libenet` via `pkg-config` (`PkgConfig` + `libenet`)

## Editing guidelines for agents
- Keep shared data-model changes in `src/common` synchronized with both client and server callers.
- For protocol/network changes, verify client/server compatibility and update serialization tests.
- Prefer small, targeted edits and re-run the affected binary/tests.
- Do not add heavy new dependencies without user approval.

## Agent memory (project-specific)
- World loading is data-driven from `data/worlds/*.world` (not a single `data/world.world`).
- Room ids are world-qualified at runtime (`worldName:roomFile.tmx`), with alias resolution in `World::resolveRoomName`.
- Portal triggers are parsed from object layer named `Portals`.
- Portal resolution supports nested class properties and optional `world` override.
- Portal teleport is skipped if destination tile is not walkable.
- Movement and state sync are server-authoritative.
- Multi-world topology uses position-based packs `data/worlds/<x>_<y>/` plus `data/worlds/globe.world`.

### World travel semantics
- Gameplay model is classic top-down Zelda-like screen transitions.
- One world corresponds to one screen-sized map.
- Players can walk to any map edge (left/right/top/bottom) to move into an adjacent world/screen.
- Inside a single world, there are vertical levels/floors and players can move up/down between those levels.
- Cross-world movement is edge-based overworld traversal, not staircase/floor/level-based travel.
- Cross-world edge traversal should happen on the world's overworld level (`level 0`).
- World adjacency is authored in `data/worlds/globe.world` (global placement of level-0 screens).
- Edge transitions must remain server-authoritative (never client-trusted).
- There is no separate standalone "overworld" TMX: the stitched set of all worlds' `level 0` screens is the overworld.
- `data/worlds/globe.world` should point directly to each world's `rooms/0.tmx` (not nested `.world` files).
- Each world pack remains independently editable/owned under `data/worlds/<x>_<y>/` without requiring edits to other world packs.
- Folder naming convention encodes world position:
  - `500_500` center
  - `500_501` north
  - `501_500` east
  - `499_500` south

### Collision semantics
- Default tiles are walkable.
- Blocking is explicit via tile property `non_walkable=true` (legacy `not_walkable=true` still accepted).
- `Block` layer with non-zero gids also blocks.
- Tile-property walkability resolves top-down by layer order (top-most non-empty tile wins), so bridge-like overlays can be walkable over blocked base tiles.
- Absence of `non_walkable` does not imply blocked.

### Monster spawn + behavior
- Monster spawn source is tile layer named `Monsters` in each TMX.
- Spawn marker tiles are matched by TSX tile property `monster_name` (fallbacks: `monster_id`, `monster`).
- Marker text is matched against monster template filename stem from `game/monsters/*.toml`.
- Monster id matching is normalized (trim + lowercase) on server side.
- Monster blocking footprint is anchor tile only (`x,y`); large sprites do not block their upper/side pixels.
- Monsters chase the nearest player in the same room (1-tile step AI each movement tick), and fall back to wandering when no player is present.
- Monster movement avoids blocked tiles, players, and other monster anchors.
- Monsters can melee-attack nearby players server-side (same room, tile-adjacent).
- Monster melee damage uses the monster TOML `strength` stat (minimum 1).
- On player defeat, server resets hp/mana to max and respawns on a walkable tile in the current room.
- `monster_size` in TOML (`"WxH"`) is used for sprite dimensions and server-side monster metadata.

### Sprite loader contract
- Sprite loader has been rewritten to not require TSX `dir`, `seq`, or `size` properties.
- Loader assumes anchor layout:
  - Anchor tile (`name` or `monster_name`) is bottom-left of the full directional block.
  - Direction rows from top to bottom: South, East, North, West.
  - Each direction has 4 frames laid out to the right.
  - Frame width/height comes from size override (`monster_size` from server data, player hardcoded map).
- Client loads sprite sheets per `sprite_tileset` and passes per-sprite size overrides into `Sprites::loadTSX(...)`.
- If a sheet does not provide all 4 direction strips, loader currently falls back to any available strip.

### Rendering behavior
- `Monsters` layer is metadata-only and is not drawn by `RoomRenderer`.
- Scene renderer depth-sorts monsters and players by feet Y to approximate top-down perspective.
- Attack marker for targeted monster is a feet-area ground indicator (bottom tile-height band), drawn behind entities.
- Player movement uses client-side interpolation; room transitions clear interpolation caches to avoid cross-room slide artifacts.
- Combat outcome text (`hit` / `whiffed` / `blocked`) is driven by per-entity outcome sequence counters and rendered as short-lived floating text in scene space.

### Networking/data model notes
- `MonsterStateMsg` includes:
  - `sprite_tileset`, `sprite_name`
  - `sprite_w_tiles`, `sprite_h_tiles`
- `PlayerStateMsg` and `MonsterStateMsg` include:
  - `combat_outcome`, `combat_outcome_seq` (for combat feedback animations)
- `GameStateMsg` includes:
  - level + EXP progress fields
  - levelable skills (`exp`, `melee`, `distance`, `magic`, `shielding`, `evasion`)
  - derived non-levelable stats (`derived_defence`, `derived_offence`, `derived_evasion`)
- Any protocol field changes in `src/common/msg.h` require rebuilding both client and server together.

### Progression + combat notes
- Global progression is data-driven from `data/global.toml` `[progression]`:
  - `exp_per_level_a`
  - `exp_per_level_b`
  - `exp_per_level_c`
  - Server uses `need(L) = a*L*L + b*L + c`.
- Skill XP is persisted in auth DB (`players.skills`) for:
  - `melee`, `distance`, `magic`, `shielding`, `evasion`
- Combat swing resolution is outcome-based (`hit` / `missed` / `blocked`) instead of always-hit fixed damage.
- Dead monsters respawn server-side from their authored spawn anchor after `data/global.toml` `[gameplay].monster_respawn_ms` (with retry if spawn tile is occupied).
- Derived stats use skills plus equipped item bonuses:
  - `defence = shielding_level + equipped_defense`
  - `offence = melee_level + distance_level/2 + magic_level/2 + equipped_attack`
  - `evasion = evasion_level + equipped_evasion`

### Frequent gotchas
- Tiled tileset paths in TMX/`.world` can be relative; world loader includes fallback path resolution.
- If portal appears "broken", check destination walkability first.
- If monster does not render correctly, verify:
  - `sprite_tileset` path exists under `game/assets/art`
  - anchor tile position follows the loader contract
  - `monster_size` matches sheet layout
  - `monster_name` casing in TSX (server id matching is case-insensitive; sprite-name lookup may still depend on sheet naming).

## Typical workflow
1. Inspect impacted files in `src/client`, `src/server`, and `src/common`.
2. Implement minimal changes.
3. Build with CMake.
4. Run relevant test binaries.
5. If networking logic changed, run server + client smoke check.
