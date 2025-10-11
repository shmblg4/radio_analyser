cmake_minimum_required(VERSION 3.10)
project(radio_analyser)

# Указать стандарт C++
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Пути к заголовочным файлам
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(/usr/local/include/libhackrf) # Путь к hackrf.h

# Пути к поиску библиотек
link_directories(/usr/local/lib) # Путь к libhackrf.so

# Источники проекта
set(SOURCES main.cc)
file(GLOB_RECURSE SRC_FILES ${PROJECT_SOURCE_DIR}/src/*.cc)

# Собираем исполняемый файл
add_executable(radio_analyser ${SOURCES} ${SRC_FILES})

find_package(spdlog REQUIRED)

target_link_libraries(radio_analyser PRIVATE hackrf spdlog::spdlog)
