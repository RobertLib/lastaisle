cmake_minimum_required(VERSION 3.20)
project(LastAisle C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(SDL3 REQUIRED CONFIG)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

file(GLOB ART_SOURCES ${CMAKE_SOURCE_DIR}/art/*.art)
set(ATLAS_OUTPUTS
    ${CMAKE_SOURCE_DIR}/assets/atlas.png
    ${CMAKE_SOURCE_DIR}/src/gen/atlas.h
    ${CMAKE_SOURCE_DIR}/src/gen/atlas.c)

# hand-authored pixel art -> packed atlas + generated sprite table
add_custom_command(
    OUTPUT ${ATLAS_OUTPUTS}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build_atlas.py
    DEPENDS ${ART_SOURCES} ${CMAKE_SOURCE_DIR}/art/manifest.txt ${CMAKE_SOURCE_DIR}/art/palette.txt
            ${CMAKE_SOURCE_DIR}/tools/build_atlas.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Compiling pixel art atlas")

file(GLOB GAME_SOURCES ${CMAKE_SOURCE_DIR}/src/*.c)
add_executable(lastaisle ${GAME_SOURCES} ${CMAKE_SOURCE_DIR}/src/gen/atlas.c)
target_include_directories(lastaisle PRIVATE src src/gen)
target_link_libraries(lastaisle PRIVATE SDL3::SDL3)
if(NOT MSVC)
    target_link_libraries(lastaisle PRIVATE m)
    target_compile_options(lastaisle PRIVATE -Wall -Wextra -Wno-unused-parameter
                           -Wno-missing-field-initializers -Wno-sign-compare)
endif()

# the game looks for assets/ next to the executable
add_custom_command(TARGET lastaisle POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:lastaisle>/assets
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/assets/atlas.png $<TARGET_FILE_DIR:lastaisle>/assets/atlas.png)
