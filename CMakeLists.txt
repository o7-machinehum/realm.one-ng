
cmake_minimum_required(VERSION 3.16)
project(quick_game CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(raylib REQUIRED)
find_package(tinyxml2 REQUIRED)

# Shared code
add_library(qg_common
  src/commmon/sprites.cpp
  src/commmon/room.cpp
  src/commmon/entity.cpp
  src/commmon/character.cpp
)

target_include_directories(qg_common PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/commmon
)

target_link_libraries(qg_common PUBLIC
  tinyxml2::tinyxml2
)

# Tests
add_executable(test_room src/tests/test_room.cpp)
target_link_libraries(test_room PRIVATE qg_common raylib)

add_executable(test_sprites src/tests/test_sprites.cpp)
target_link_libraries(test_sprites PRIVATE qg_common raylib)

enable_testing()
add_test(NAME test_room COMMAND test_room WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME test_sprites COMMAND test_sprites WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
