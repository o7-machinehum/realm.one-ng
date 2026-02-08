# AGENTS.md

## Repo overview
- Language: C++17
- Build system: CMake (primary), Makefile helpers
- Main executables:
  - `client` (`src/client`)
  - `server` (`src/server`)
- Shared game code: `src/commmon` (note the triple `m` in folder name)
- Tests live in: `src/tests`

## Project structure
- `src/client`: rendering and network client code
- `src/server`: server/network/world logic
- `src/commmon`: shared types for messages, rooms, entities, sprites
- `src/tests`: test and serialization checks
- `data/`: gameplay data (`rooms`, `characters`)
- `game/assets`: art and tiled project files

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

Note: `src/tests/test_world.cpp` exists but is not currently wired into `CMakeLists.txt`.

## Dependencies
Required CMake packages/modules:
- `raylib`
- `tinyxml2`
- `cereal`
- `libenet` via `pkg-config` (`PkgConfig` + `libenet`)

## Editing guidelines for agents
- Preserve the existing directory naming (including `src/commmon` typo), unless explicitly asked to refactor it everywhere.
- Keep shared data-model changes in `src/commmon` synchronized with both client and server callers.
- For protocol/network changes, verify client/server compatibility and update serialization tests.
- Prefer small, targeted edits and re-run the affected binary/tests.
- Do not add heavy new dependencies without user approval.

## Typical workflow
1. Inspect impacted files in `src/client`, `src/server`, and `src/commmon`.
2. Implement minimal changes.
3. Build with CMake.
4. Run relevant test binaries.
5. If networking logic changed, run server + client smoke check.
