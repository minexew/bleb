cmake_minimum_required(VERSION 3.1)
project(bleb)

set(CMAKE_CXX_STANDARD 14)
set(library ${PROJECT_NAME})

# Also add headers so that they're included in generated projects
file(GLOB sources
    ${PROJECT_SOURCE_DIR}/src/bleb/*.cpp
    ${PROJECT_SOURCE_DIR}/src/bleb/*.hpp

    ${PROJECT_SOURCE_DIR}/include/bleb/*.hpp
)

add_library(${library} ${sources})

target_include_directories(${PROJECT_NAME} PUBLIC include)
