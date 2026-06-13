cmake_minimum_required(VERSION 3.20)
project(pisya-code VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

add_executable(pisya
    src/main.cpp
    src/config.cpp
    src/ai_client.cpp
    src/tools.cpp
    src/agent.cpp
    src/ui.cpp
    src/session.cpp
    src/lang.cpp
    src/rc.cpp
)

target_include_directories(pisya PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
)

target_link_libraries(pisya PRIVATE
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_compile_definitions(pisya PRIVATE
    CPPHTTPLIB_OPENSSL_SUPPORT
)

target_compile_options(pisya PRIVATE
    -Wall -Wextra -Wpedantic
)
