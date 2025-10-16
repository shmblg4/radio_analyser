cmake_minimum_required(VERSION 3.10)
cmake_policy(SET CMP0072 NEW)
project(radio_analyser)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    ${PROJECT_SOURCE_DIR}/include
    /usr/local/include/libhackrf
)

link_directories(/usr/local/lib)

set(SOURCES main.cc)
file(GLOB_RECURSE SRC_FILES ${PROJECT_SOURCE_DIR}/src/*.cc)

add_executable(radio_analyser ${SOURCES} ${SRC_FILES})

find_package(spdlog REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Widgets)

target_link_libraries(radio_analyser PRIVATE hackrf spdlog::spdlog Qt6::Widgets)
