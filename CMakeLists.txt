cmake_minimum_required(VERSION 3.16)
project(quick_game CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(raylib REQUIRED)
find_package(tinyxml2 REQUIRED)
find_package(cereal REQUIRED)

# ENet doesn't have a CMake config, use pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(ENET REQUIRED libenet)

# Shared code
add_library(qg_common
  src/commmon/sprites.cpp
  src/commmon/room.cpp
  src/commmon/entity.cpp
  src/commmon/character.cpp
  src/client/room_render.cpp
)

target_include_directories(qg_common PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/commmon
)

target_link_libraries(qg_common PUBLIC
  tinyxml2::tinyxml2
)

# ----- Client
add_executable(client
    src/client/client.cpp
)

target_link_libraries(client PRIVATE
    ${ENET_LIBRARIES}
    qg_common
    raylib
)

target_include_directories(client PRIVATE
    ${ENET_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client
    ${CMAKE_CURRENT_SOURCE_DIR}/src/commmon
)

#
# # ----- Server
# add_executable(server
#     src/server/server.cpp
#     src/server/server_net.cpp
#     src/server/world.cpp
#     src/server/fs_db.cpp
# )
#
# target_link_libraries(server PRIVATE
#     ${ENET_LIBRARIES}
#     qg_common
#     raylib
# )
#
# target_include_directories(server PRIVATE
#     ${ENET_INCLUDE_DIRS}
#     ${CMAKE_CURRENT_SOURCE_DIR}/src/client
#     ${CMAKE_CURRENT_SOURCE_DIR}/src/commmon
# )

# ----- Tests
add_executable(test_room
    src/tests/test_room.cpp
    src/client/room_render.cpp
)

target_include_directories(test_room PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client
)

target_link_libraries(test_room PRIVATE qg_common raylib)

add_executable(test_sprites src/tests/test_sprites.cpp)
target_link_libraries(test_sprites PRIVATE qg_common raylib)

# ---- test-client/server
add_executable(client_server_ser_de
    src/tests/client_server_ser_de.cpp
    src/client/room_render.cpp
)
target_link_libraries(client_server_ser_de PRIVATE
    ${ENET_LIBRARIES}
    qg_common
    cereal::cereal
    raylib
)
target_include_directories(client_server_ser_de PRIVATE
    ${ENET_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/commmon
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client
)
