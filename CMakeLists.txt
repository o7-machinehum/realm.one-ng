cmake_minimum_required(VERSION 3.20)
project(quick_game LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

find_package(raylib REQUIRED)
find_package(tinyxml2 REQUIRED)

add_executable(quick_game
  src/main.cpp
  src/sprites.cpp
  src/room.cpp
  src/character.cpp
  src/text_input.cpp
)

target_link_libraries(quick_game PRIVATE raylib)
target_link_libraries(quick_game PRIVATE tinyxml2)

# Raylib needs these on some platforms
if (APPLE)
  target_link_libraries(quick_game PRIVATE "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
endif()

# Avoid console window suppression on Windows (keep terminal)
if (WIN32)
  target_compile_definitions(quick_game PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
