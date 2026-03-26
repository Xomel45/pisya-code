cmake_minimum_required(VERSION 3.20)
project(pisya-code VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)

add_executable(pisya
    src/main.cpp
    src/config.cpp
    src/ai_client.cpp
    src/tools.cpp
    src/agent.cpp
    src/ui.cpp
    src/session.cpp
    src/lang.cpp
)

target_include_directories(pisya PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
)

target_link_libraries(pisya PRIVATE
    Threads::Threads
)

target_compile_options(pisya PRIVATE
    -Wall -Wextra -Wpedantic
)
