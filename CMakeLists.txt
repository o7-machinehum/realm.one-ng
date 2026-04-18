cmake_minimum_required(VERSION 3.16)
project(quick_game CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_SERVER "Build the server executable" ON)
option(BUILD_VIEWER "Build the standalone cube viewer" ON)

# ----- Shared common lib (voxel world format)
add_library(qg_common
  src/common/voxel_world.cpp
)
target_include_directories(qg_common PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)

# ----- Worldgen tool
find_package(tinyxml2 REQUIRED)
add_executable(worldgen
  src/worldgen/worldgen.cpp
)
target_link_libraries(worldgen PRIVATE qg_common tinyxml2::tinyxml2)

# ----- Standalone cube viewer (renders a world.dat file; no networking)
if(BUILD_VIEWER)
    find_package(raylib REQUIRED)
    add_executable(viewer
        src/viewer/viewer.cpp
        src/viewer/cube_renderer.cpp
    )
    target_link_libraries(viewer PRIVATE qg_common raylib)
    target_include_directories(viewer PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/viewer
    )
endif()

# ----- Server (world loader stub; networking will be re-added later)
if(BUILD_SERVER)
    add_executable(server
        src/server/server.cpp
    )
    target_link_libraries(server PRIVATE qg_common)
    target_include_directories(server PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/server
    )
endif()
