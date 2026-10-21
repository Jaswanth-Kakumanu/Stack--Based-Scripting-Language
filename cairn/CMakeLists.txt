cmake_minimum_required(VERSION 3.16)

project(cairn
    VERSION 1.0.0
    DESCRIPTION "A stack-based scripting language and virtual machine"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Core library: everything except the CLI entry point, so the tests can link
# against the same objects the binary uses.
# ---------------------------------------------------------------------------
add_library(cairn_core
    src/token.cpp
    src/source.cpp
    src/diagnostic.cpp
    src/lexer.cpp
    src/ast.cpp
    src/parser.cpp
    src/value.cpp
    src/opcode.cpp
    src/chunk.cpp
    src/compiler.cpp
    src/vm.cpp)

target_include_directories(cairn_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

if(MSVC)
    target_compile_options(cairn_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(cairn_core PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
endif()

# ---------------------------------------------------------------------------
# CLI driver
# ---------------------------------------------------------------------------
add_executable(cairn src/main.cpp)
target_link_libraries(cairn PRIVATE cairn_core)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
# Forces the VM to dispatch through a switch instead of a computed-goto table.
# Build both ways and compare with --bench; that comparison is the point.
option(CAIRN_FORCE_SWITCH "Use switch dispatch in the VM instead of computed goto" OFF)
if(CAIRN_FORCE_SWITCH)
    target_compile_definitions(cairn_core PUBLIC CAIRN_FORCE_SWITCH)
endif()

option(CAIRN_BUILD_TESTS "Build the Cairn test suite" ON)

if(CAIRN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
