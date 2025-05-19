cmake_minimum_required(VERSION 3.20)
project(RustAlkadCheat2588 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Указываем MinGW для GitHub Actions (можно переопределить при сборке)
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)

# Проверяем наличие всех зависимостей
find_file(IMGUI_H "imgui.h" PATHS "${CMAKE_SOURCE_DIR}/../imgui" NO_DEFAULT_PATH)
if(NOT IMGUI_H)
    message(FATAL_ERROR "imgui.h not found in ${CMAKE_SOURCE_DIR}/../imgui. Please ensure ImGui is placed in the correct directory.")
endif()

find_file(MINHOOK_H "MinHook.h" PATHS "${CMAKE_SOURCE_DIR}/../minhook/include" NO_DEFAULT_PATH)
if(NOT MINHOOK_H)
    message(FATAL_ERROR "MinHook.h not found in ${CMAKE_SOURCE_DIR}/../minhook/include. Please ensure MinHook is placed in the correct directory.")
endif()

find_file(JSON_HPP "json.hpp" PATHS "${CMAKE_SOURCE_DIR}/../json/single_include/nlohmann" NO_DEFAULT_PATH)
if(NOT JSON_HPP)
    message(FATAL_ERROR "json.hpp not found in ${CMAKE_SOURCE_DIR}/../json/single_include/nlohmann. Please ensure nlohmann-json is placed in the correct directory.")
endif()

# Добавляем исходники
add_library(RustAlkadCheat2588 SHARED
    RustAlkadCheat2588.cpp
    ../imgui/imgui.cpp
    ../imgui/imgui_draw.cpp
    ../imgui/imgui_widgets.cpp
    ../imgui/imgui_tables.cpp
    ../imgui/imgui_impl_win32.cpp
    ../minhook/src/buffer.c
    ../minhook/src/hook.c
    ../minhook/src/trampoline.c
)

# Путь к заголовкам
target_include_directories(RustAlkadCheat2588 PRIVATE
    ${CMAKE_SOURCE_DIR}/../imgui
    ${CMAKE_SOURCE_DIR}/../minhook/include
    ${CMAKE_SOURCE_DIR}/../json/single_include
)

# Линкуем библиотеки
target_link_libraries(RustAlkadCheat2588 PRIVATE
    -lpsapi
)

# Имя выходного файла
set_target_properties(RustAlkadCheat2588 PROPERTIES OUTPUT_NAME "RustAlkadCheat2588")

# Статическая линковка и флаги для MSVC
if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /D_CRT_SECURE_NO_WARNINGS /W0")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static")
endif()
