cmake_minimum_required(VERSION 3.10)
cmake_policy(SET CMP0072 NEW)
project(radio_analyser)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

include_directories(
    ${PROJECT_SOURCE_DIR}/include
    /usr/local/include/libhackrf
)

link_directories(/usr/local/lib)

set(SOURCES main.cc)
file(GLOB_RECURSE SRC_FILES ${PROJECT_SOURCE_DIR}/src/*.cc)

add_executable(
    radio_analyser 
    include/MainWindow.hpp 
    include/AudioProcessorThread.hpp ${SOURCES} ${SRC_FILES})

find_package(spdlog REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport)
find_package(QCustomPlot REQUIRED)

# Try to find Qt6Multimedia (optional)
find_package(Qt6 QUIET COMPONENTS Multimedia)
if(Qt6Multimedia_FOUND)
    message(STATUS "Qt6Multimedia found: ${Qt6Multimedia_DIR}")
else()
    message(WARNING "Qt6Multimedia not found - audio support will be disabled")
    message(STATUS "To enable audio, install: qt6-multimedia-dev (Ubuntu/Debian) or qt6-multimedia-devel (Fedora)")
endif()
find_package(PkgConfig REQUIRED)
find_library(FFTW3_LIB NAMES fftw3)

if(NOT FFTW3_LIB)
    message(FATAL_ERROR "FFTW3 library not found")
endif()

target_link_libraries(radio_analyser PRIVATE
    hackrf
    spdlog::spdlog
    Qt6::Widgets
    QCustomPlot::qcustomplot
    ${FFTW3_LIB}
)

if(TARGET Qt6::Multimedia)
    target_link_libraries(radio_analyser PRIVATE Qt6::Multimedia)
    target_compile_definitions(radio_analyser PRIVATE HAVE_QT_AUDIO=1)
endif()
