cmake_minimum_required(VERSION 3.1)
project(bleb)

set(CMAKE_CXX_STANDARD 14)

# Also add headers so that they're included in generated projects
file(GLOB sources
    ${PROJECT_SOURCE_DIR}/src/bleb/*.cpp
    ${PROJECT_SOURCE_DIR}/src/bleb/*.hpp

    ${PROJECT_SOURCE_DIR}/include/bleb/*.hpp
)

file(GLOB test_sources
    ${PROJECT_SOURCE_DIR}/test/*.hpp
    ${PROJECT_SOURCE_DIR}/test/*.cpp
)

add_library(${PROJECT_NAME} ${sources})
target_include_directories(${PROJECT_NAME} PUBLIC include)

add_executable(${PROJECT_NAME}_test ${sources} ${test_sources})
target_include_directories(${PROJECT_NAME}_test PUBLIC include)

# AddressSanitizer (disable if it's giving you trouble)
target_compile_options(${PROJECT_NAME}_test PRIVATE -fsanitize=address)
target_link_libraries(${PROJECT_NAME}_test PUBLIC -fsanitize=address)

add_executable(blebtool ${sources} src/blebtool.cpp)
target_include_directories(blebtool PUBLIC include)

# AddressSanitizer (disable if it's giving you trouble)
target_compile_options(blebtool PRIVATE -fsanitize=address)
target_link_libraries(blebtool PUBLIC -fsanitize=address)
