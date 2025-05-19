cmake_minimum_required(VERSION 3.20)
project(RustAlkadCheat2588 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Указываем MinGW для GitHub Actions
if(DEFINED ENV{GITHUB_ACTIONS})
    set(CMAKE_SYSTEM_NAME Windows)
    set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
    set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
endif()

# Проверяем наличие nlohmann-json
find_file(JSON_HPP "json.hpp" PATHS "${CMAKE_SOURCE_DIR}/../json/single_include/nlohmann" NO_DEFAULT_PATH)
if(NOT JSON_HPP)
    message(FATAL_ERROR "json.hpp not found in ${CMAKE_SOURCE_DIR}/../json/single_include/nlohmann. Please ensure nlohmann-json is downloaded by build.yml.")
endif()

# Добавляем основной файл
add_library(RustAlkadCheat2588 SHARED
    RustAlkadCheat2588.cpp
)

# Путь к заголовкам nlohmann-json
target_include_directories(RustAlkadCheat2588 PRIVATE
    ${CMAKE_SOURCE_DIR}/../json/single_include
)

# Линкуем библиотеки Windows и DirectX
target_link_libraries(RustAlkadCheat2588 PRIVATE
    -lgdi32
    -ld3d9
    -ld3dx9
)

# Имя выходного файла
set_target_properties(RustAlkadCheat2588 PROPERTIES OUTPUT_NAME "RustAlkadCheat2588")

# Статическая линковка и флаги для MinGW
if(CMAKE_CXX_COMPILER MATCHES "mingw")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static -Wall -Wno-unknown-pragmas")
endif()

# Флаги для MSVC
if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /D_CRT_SECURE_NO_WARNINGS /W0")
endif()
