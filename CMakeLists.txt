cmake_minimum_required(VERSION 3.25)

project(dux-lang
    VERSION     0.1.0
    DESCRIPTION "The Dux programming language compiler"
    LANGUAGES   CXX
)

# ─── C++ standard ─────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD          23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# Export compile_commands.json for clangd / editors
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# ─── Dependencies ─────────────────────────────────────────────────────────────
find_package(FLEX  2.6  REQUIRED)
find_package(BISON 3.8  REQUIRED)

# ─── Code generation ──────────────────────────────────────────────────────────
set(GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GEN_DIR})

FLEX_TARGET(DuxLexer
    src/lexer/dux.l
    ${GEN_DIR}/lexer.cpp
    DEFINES_FILE ${GEN_DIR}/lexer.hpp
)

BISON_TARGET(DuxParser
    src/parser/dux.y
    ${GEN_DIR}/parser.cpp
    DEFINES_FILE ${GEN_DIR}/parser.hpp
    COMPILE_FLAGS "-Wcounterexamples"
)

ADD_FLEX_BISON_DEPENDENCY(DuxLexer DuxParser)

# ─── Executable ───────────────────────────────────────────────────────────────
add_executable(dux
    src/main.cpp
    src/ast/ast.cpp
    src/driver/driver.cpp
    ${FLEX_DuxLexer_OUTPUTS}
    ${BISON_DuxParser_OUTPUTS}
)

target_include_directories(dux PRIVATE
    src
    ${GEN_DIR}
)

target_compile_definitions(dux PRIVATE
    DUX_VERSION="${PROJECT_VERSION}"
)

# Warnings
target_compile_options(dux PRIVATE
    -Wall -Wextra -Wpedantic -Wshadow -Wno-unused-function
    $<$<CONFIG:Debug>:   -g3 -O0>
    $<$<CONFIG:Release>: -O3 -DNDEBUG -march=native>
)

# Sanitisers in debug builds
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(dux PRIVATE -fsanitize=address,undefined)
    target_link_options(dux   PRIVATE -fsanitize=address,undefined)
endif()

# ─── Install ──────────────────────────────────────────────────────────────────
include(GNUInstallDirs)
install(TARGETS dux RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# ─── Tests ────────────────────────────────────────────────────────────────────
enable_testing()

# Smoke test: parse all example files
file(GLOB DUX_EXAMPLES "${CMAKE_SOURCE_DIR}/examples/*.dux")
foreach(EX ${DUX_EXAMPLES})
    cmake_path(GET EX STEM name)
    add_test(
        NAME    parse_${name}
        COMMAND dux --dump-ast ${EX}
    )
endforeach()
