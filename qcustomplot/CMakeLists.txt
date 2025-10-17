cmake_minimum_required(VERSION 3.14)

project(QCustomPlot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport)

add_library(qcustomplot SHARED qcustomplot.cpp qcustomplot.h)

target_link_libraries(qcustomplot PRIVATE Qt6::Widgets Qt6::PrintSupport)

target_include_directories(qcustomplot
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/qcustomplot>
)

install(
    TARGETS qcustomplot
    EXPORT QCustomPlotTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(
    FILES qcustomplot.h
    DESTINATION include/qcustomplot
)

install(
    EXPORT QCustomPlotTargets
    FILE QCustomPlotTargets.cmake
    NAMESPACE QCustomPlot::
    DESTINATION lib/cmake/QCustomPlot
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/QCustomPlotConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/QCustomPlotConfig.cmake"
    INSTALL_DESTINATION lib/cmake/QCustomPlot
)

install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/QCustomPlotConfig.cmake"
    DESTINATION lib/cmake/QCustomPlot
)