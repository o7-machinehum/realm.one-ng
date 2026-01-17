
cmake_minimum_required(VERSION 3.16)
project(quick_game CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(raylib REQUIRED)
find_package(tinyxml2 REQUIRED)

add_library(qg_common STATIC
  src/text_input.cpp
)

target_include_directories(qg_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(qg_common PUBLIC enet)  # <- literally -lenet

add_executable(quick_game_client
  src/main.cpp
  src/sprites.cpp
  src/room.cpp
  src/character.cpp
  src/client_net.cpp
)
target_link_libraries(quick_game_client PRIVATE raylib tinyxml2::tinyxml2 qg_common enet)

add_executable(quick_game_server
  src/server_main.cpp
  src/server_net.cpp
  src/fs_db.cpp
)
target_link_libraries(quick_game_server PRIVATE qg_common enet)

