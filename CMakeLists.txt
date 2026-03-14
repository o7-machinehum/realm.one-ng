cmake_minimum_required(VERSION 3.16)
project(quick_game CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_SERVER "Build the server executable" ON)

find_package(raylib REQUIRED)
find_package(tinyxml2 REQUIRED)
find_package(cereal REQUIRED)
find_package(OpenSSL REQUIRED)

# ENet doesn't have a CMake config, use pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(ENET REQUIRED libenet)
if(BUILD_SERVER)
    pkg_check_modules(SQLITE3 REQUIRED sqlite3)
endif()

# Shared code
add_library(qg_common
  src/common/auth_crypto.cpp
  src/common/sprites.cpp
  src/common/room.cpp
  src/common/entity.cpp
  src/common/string_util.cpp
  src/client/room_render.cpp
)

target_include_directories(qg_common PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)

target_link_libraries(qg_common PUBLIC
  tinyxml2::tinyxml2
  OpenSSL::Crypto
)

# ----- Client
add_executable(client
    src/client/client.cpp
    src/client/auth_ui.cpp
    src/client/client_game_ui.cpp
    src/client/client_ui_primitives.cpp
    src/client/client_layout.cpp
    src/client/client_support.cpp
    src/client/ui_settings.cpp
    src/client/local_keys.cpp
    src/client/client_sheet_cache.cpp
    src/client/client_scene_renderer.cpp
    src/client/client_windows.cpp
    src/client/client_hud.cpp
    src/client/client_inventory_overlay.cpp
    src/client/container_def.cpp
    src/client/net_client.cpp
    src/client/speech_bubble.cpp
    src/client/combat_fx.cpp
    src/server/monster_defs.cpp
)

target_link_libraries(client PRIVATE
    ${ENET_LINK_LIBRARIES}
    qg_common
    raylib
)

target_include_directories(client PRIVATE
    ${ENET_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server
)

if(BUILD_SERVER)
# ----- Server
add_executable(server
    src/server/server.cpp
    src/server/net_server.cpp
    src/server/server_util.cpp
    src/server/server_occupancy.cpp
    src/server/server_spawning.cpp
    src/server/server_msg_handlers.cpp
    src/server/server_tick.cpp
    src/server/world.cpp
    src/server/monster_defs.cpp
    src/server/npc_defs.cpp
    src/server/global_settings.cpp
    src/server/item_defs.cpp
    src/server/auth_db.cpp
    src/server/combat.cpp
    src/server/equipment.cpp
)

target_link_libraries(server PRIVATE
    ${ENET_LINK_LIBRARIES}
    ${SQLITE3_LINK_LIBRARIES}
    qg_common
    raylib
)

target_include_directories(server PRIVATE
    ${ENET_INCLUDE_DIRS}
    ${SQLITE3_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)
endif()
