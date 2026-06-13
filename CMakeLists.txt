cmake_minimum_required(VERSION 3.10)
cmake_policy(SET CMP0072 NEW)
project(radio_analyser VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Export compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find required packages
find_package(PkgConfig REQUIRED)
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

# Find FFTW3 using pkg-config
pkg_check_modules(FFTW3 REQUIRED fftw3)

# Find HackRF using pkg-config
pkg_check_modules(HACKRF libhackrf)

# If pkg-config didn't find HackRF, try manual search
if(NOT HACKRF_FOUND)
    message(STATUS "pkg-config didn't find libhackrf, trying manual search...")
    
    find_path(HACKRF_INCLUDE_DIR 
        NAMES hackrf.h
        PATHS 
            /usr/include
            /usr/local/include
            /usr/include/libhackrf
            /usr/local/include/libhackrf
        PATH_SUFFIXES libhackrf
    )
    
    find_library(HACKRF_LIBRARY
        NAMES hackrf
        PATHS
            /usr/lib
            /usr/local/lib
            /usr/lib/x86_64-linux-gnu
    )
    
    if(HACKRF_INCLUDE_DIR AND HACKRF_LIBRARY)
        set(HACKRF_FOUND TRUE)
        set(HACKRF_INCLUDE_DIRS ${HACKRF_INCLUDE_DIR})
        set(HACKRF_LIBRARIES ${HACKRF_LIBRARY})
        message(STATUS "Found HackRF: ${HACKRF_LIBRARY}")
        message(STATUS "HackRF include: ${HACKRF_INCLUDE_DIR}")
    else()
        message(FATAL_ERROR "HackRF library not found. Install libhackrf-dev (Ubuntu/Debian) or hackrf-devel (Fedora)")
    endif()
endif()

# Collect source files
set(SOURCES main.cc)
file(GLOB_RECURSE SRC_FILES ${PROJECT_SOURCE_DIR}/src/*.cc)

# Create executable
add_executable(radio_analyser 
    include/MainWindow.hpp 
    include/AudioProcessorThread.hpp 
    include/SpectrumWorker.hpp 
    ${SOURCES} 
    ${SRC_FILES}
)

# Include directories
target_include_directories(radio_analyser PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${HACKRF_INCLUDE_DIRS}
    ${FFTW3_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(radio_analyser PRIVATE
    ${HACKRF_LIBRARIES}
    spdlog::spdlog
    Qt6::Widgets
    QCustomPlot::qcustomplot
    ${FFTW3_LIBRARIES}
)

# Add Qt6 Multimedia if available
if(TARGET Qt6::Multimedia)
    target_link_libraries(radio_analyser PRIVATE Qt6::Multimedia)
    target_compile_definitions(radio_analyser PRIVATE HAVE_QT_AUDIO=1)
endif()

# Compiler warnings
target_compile_options(radio_analyser PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

# Testing
enable_testing()

add_executable(dsp_self_test tests/dsp_self_test.cc src/AnalysisParams.cc)
target_include_directories(dsp_self_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${FFTW3_INCLUDE_DIRS}
)
target_link_libraries(dsp_self_test PRIVATE ${FFTW3_LIBRARIES})
add_test(NAME dsp_self_test COMMAND dsp_self_test)
