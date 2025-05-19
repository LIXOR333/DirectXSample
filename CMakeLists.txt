cmake_minimum_required(VERSION 3.20)
project(RustAlkadCheat2588 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Указываем MinGW
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)

# Находим зависимости
find_package(imgui CONFIG REQUIRED)
find_package(minhook CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# Добавляем исходник
add_library(RustAlkadCheat2588 SHARED src/RustAlkadCheat2588.cpp)

# Линкуем библиотеки (убрали ZLIB)
target_link_libraries(RustAlkadCheat2588 PRIVATE
    imgui::imgui
    minhook::minhook
    nlohmann_json::nlohmann_json
    -lpsapi
)

# Путь к заголовкам
target_include_directories(RustAlkadCheat2588 PRIVATE
    ${CMAKE_SOURCE_DIR}/DirectXSample/src
)

# Имя выходного файла
set_target_properties(RustAlkadCheat2588 PROPERTIES OUTPUT_NAME "RustAlkadCheat2588")

# Статическая линковка
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static")
