cmake_minimum_required(VERSION 3.10)
project(HandTennisGame C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -Wall -Wextra")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -Wall -Wextra")

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(SDL2_TTF REQUIRED IMPORTED_TARGET SDL2_ttf)
pkg_check_modules(OPENCV REQUIRED IMPORTED_TARGET opencv4)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${SDL2_INCLUDE_DIRS})
include_directories(${OPENCV_INCLUDE_DIRS})

# Source files
set(SOURCES
    main.c
    hand_tracker.cpp
    game.c
    physics.c
    ai_opponent.c
    rendering.c
)

# Create executable
add_executable(hand_tennis ${SOURCES})

# Link libraries
target_link_libraries(hand_tennis
    ${SDL2_LIBRARIES}
    ${SDL2_TTF_LIBRARIES}
    opencv_core
    opencv_imgproc
    opencv_highgui
    opencv_videoio
    m
)
