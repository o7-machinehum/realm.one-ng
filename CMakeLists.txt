
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
target_link_libraries(qg_common PUBLIC enet)


add_executable(test
  src/test.cpp
  src/sprites.cpp
  src/entity.cpp
)

target_link_libraries(test PRIVATE raylib tinyxml2::tinyxml2 qg_common)
