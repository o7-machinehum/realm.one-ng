cmake_minimum_required(VERSION 3.16)
project(quick_game CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_SERVER "Build the server executable" ON)
option(BUILD_CLIENT "Build the networked client executable" ON)
option(BUILD_VIEWER "Build the standalone cube viewer" ON)

# ----- Common (voxel world + auth crypto + net protocol headers)
find_package(OpenSSL REQUIRED)
add_library(qg_common
  src/common/voxel_world.cpp
  src/common/auth_crypto.cpp
)
target_include_directories(qg_common PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)
target_link_libraries(qg_common PUBLIC OpenSSL::Crypto)

# ----- Worldgen tool (no raylib, no enet)
find_package(tinyxml2 REQUIRED)
add_executable(worldgen
  src/worldgen/worldgen.cpp
)
target_link_libraries(worldgen PRIVATE qg_common tinyxml2::tinyxml2)

# ----- Renderer lib (raylib-based)
if(BUILD_VIEWER OR BUILD_CLIENT)
    find_package(raylib REQUIRED)
    add_library(qg_render
        src/render/cube_renderer.cpp
    )
    target_link_libraries(qg_render PUBLIC qg_common raylib)
    target_include_directories(qg_render PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/render
    )
endif()

# ----- ENet + sqlite3 via pkg-config
if(BUILD_SERVER OR BUILD_CLIENT)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ENET REQUIRED libenet)
endif()
if(BUILD_SERVER)
    pkg_check_modules(SQLITE3 REQUIRED sqlite3)
endif()

# ----- Standalone offline viewer
if(BUILD_VIEWER)
    add_executable(viewer
        src/viewer/viewer.cpp
    )
    target_link_libraries(viewer PRIVATE qg_render)
endif()

# ----- Networked client (cube renderer + auth + sync)
if(BUILD_CLIENT)
    add_executable(client
        src/client/client.cpp
        src/client/game_client.cpp
        src/client/net_client.cpp
        src/client/auth_screen.cpp
        src/client/local_keys.cpp
    )
    target_link_libraries(client PRIVATE qg_render ${ENET_LINK_LIBRARIES})
    target_include_directories(client PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/client
        ${ENET_INCLUDE_DIRS}
    )
endif()

# ----- Game server (auth + world + sync)
if(BUILD_SERVER)
    add_executable(server
        src/server/server.cpp
        src/server/game_server.cpp
        src/server/auth_db.cpp
    )
    target_link_libraries(server PRIVATE
        qg_common
        ${ENET_LINK_LIBRARIES}
        ${SQLITE3_LINK_LIBRARIES}
    )
    target_include_directories(server PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/server
        ${ENET_INCLUDE_DIRS}
        ${SQLITE3_INCLUDE_DIRS}
    )
endif()
