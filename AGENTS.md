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

### Collision semantics
- Default tiles are walkable.
- Blocking is explicit via tile property `non_walkable=true` (legacy `not_walkable=true` still accepted).
- `Block` layer with non-zero gids also blocks.
- Absence of `non_walkable` does not imply blocked.

### Monster spawn + behavior
- Monster spawn source is tile layer named `Monsters` in each TMX.
- Spawn marker tiles are matched by TSX tile property `monster_name` (fallbacks: `monster_id`, `monster`).
- Marker text is matched against monster template filename stem from `game/monsters/*.toml`.
- Monster id matching is normalized (trim + lowercase) on server side.
- Monster blocking footprint is anchor tile only (`x,y`); large sprites do not block their upper/side pixels.
- Monsters wander server-side and avoid blocked tiles, players, and other monster anchors.
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

### Networking/data model notes
- `MonsterStateMsg` includes:
  - `sprite_tileset`, `sprite_name`
  - `sprite_w_tiles`, `sprite_h_tiles`
- Any protocol field changes in `src/common/msg.h` require rebuilding both client and server together.

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
