cmake_minimum_required(VERSION 3.25)

project(dux-lang
    VERSION     0.1.0
    DESCRIPTION "The Dux programming language compiler"
    LANGUAGES   C CXX
)

# ─── C++ standard ─────────────────────────────────────────────────────────────
set(CMAKE_C_STANDARD            11)
set(CMAKE_C_STANDARD_REQUIRED   ON)
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

# Satisfy LLVM's dependency on zstd (needed when LLVM is built with zstd support)
find_library(ZSTD_LIBRARY zstd)
if(ZSTD_LIBRARY AND NOT TARGET zstd::libzstd_shared)
    add_library(zstd::libzstd_shared SHARED IMPORTED)
    set_target_properties(zstd::libzstd_shared PROPERTIES
        IMPORTED_LOCATION "${ZSTD_LIBRARY}")
endif()

# LLVM ≥ 17 required (opaque-pointer IR by default)
find_package(LLVM CONFIG REQUIRED)
if(LLVM_VERSION_MAJOR LESS 17)
    message(FATAL_ERROR "LLVM >= 17 required, found ${LLVM_PACKAGE_VERSION}")
endif()
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION} at ${LLVM_DIR}")
# Use native target so the compiler works on x86, ARM, RISC-V, etc.
llvm_map_components_to_libnames(LLVM_LIBS
    core support irreader passes
    native nativecodegen
    linker bitreader
)

# ─── duxrt: runtime library ───────────────────────────────────────────────────
add_library(duxrt STATIC
    src/runtime/alloc.c
    src/runtime/io.c
    src/runtime/str.c
    src/runtime/list.c
    src/runtime/dict.c
    src/runtime/range.c
    src/runtime/math_rt.c
    src/runtime/exceptions.cpp
    src/runtime/path_rt.c
    src/runtime/fs_rt.c
    src/runtime/file_rt.c
    src/runtime/time_rt.c
    src/runtime/timefmt_rt.c
    src/runtime/sys_rt.c
    src/runtime/random_rt.c
    src/runtime/thread_rt.c
    src/runtime/tls_rt.c
    src/runtime/process_rt.c
    src/runtime/signal_rt.c
    src/runtime/bytes_rt.c
    src/runtime/json_rt.c
    src/runtime/regex_rt.c
    src/runtime/socket_rt.c
    src/runtime/net_tls.c
    src/runtime/net_http.c
    src/runtime/ws_rt.c
    src/runtime/async_rt.c
    src/runtime/achan_rt.c
    src/runtime/future_rt.c
    src/runtime/pool_rt.c
)

target_include_directories(duxrt PUBLIC src/runtime)

target_compile_options(duxrt PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:   -g3 -O0>
    $<$<CONFIG:Release>: -O3 -DNDEBUG -march=native>
)
# duxrt must NOT be instrumented with sanitizers: user programs link against
# it and are not compiled with sanitizer runtimes.

# OpenSSL for net.tls
find_package(OpenSSL REQUIRED)

target_link_libraries(duxrt PUBLIC m stdc++ pthread OpenSSL::SSL OpenSSL::Crypto)  # math + C++ EH ABI + pthreads + TLS

# Record absolute path so the compiler driver can find it at runtime
set(DUXRT_LIB_PATH "$<TARGET_FILE:duxrt>")
set(DUXRT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime")

# ─── Runtime bitcode for LTO ──────────────────────────────────────────────────
# Compile the pure-C runtime files (no C++ EH code) to a single LLVM bitcode
# bundle.  The driver links this into the user module before running the
# optimiser, giving LLVM full visibility of all runtime function bodies.
# LTO is silently disabled if clang or llvm-link are absent.

find_program(LLVM_CLANG_EXE
    NAMES "clang-${LLVM_VERSION_MAJOR}" clang
    HINTS "${LLVM_TOOLS_BINARY_DIR}"
    DOC "clang for building runtime bitcode")

find_program(LLVM_LINK_EXE
    NAMES "llvm-link-${LLVM_VERSION_MAJOR}" llvm-link
    HINTS "${LLVM_TOOLS_BINARY_DIR}"
    DOC "llvm-link for merging bitcode files")

set(DUXRT_BC_PATH "")   # default: LTO disabled

if(LLVM_CLANG_EXE AND LLVM_LINK_EXE)
    message(STATUS "LTO enabled: ${LLVM_CLANG_EXE}  ${LLVM_LINK_EXE}")

    # Pure C runtime sources — exceptions.cpp excluded (C++ EH ABI stays opaque).
    set(DUXRT_C_SRCS
        src/runtime/alloc.c
        src/runtime/io.c
        src/runtime/str.c
        src/runtime/list.c
        src/runtime/dict.c
        src/runtime/range.c
        src/runtime/math_rt.c
        src/runtime/tls_rt.c
        src/runtime/ws_rt.c
    )

    set(DUXRT_BC_DIR "${CMAKE_CURRENT_BINARY_DIR}/runtime_bc")
    file(MAKE_DIRECTORY "${DUXRT_BC_DIR}")

    set(DUXRT_BC_FILES "")
    foreach(src IN LISTS DUXRT_C_SRCS)
        get_filename_component(stem "${src}" NAME_WE)
        set(bc "${DUXRT_BC_DIR}/${stem}.bc")
        add_custom_command(
            OUTPUT  "${bc}"
            COMMAND "${LLVM_CLANG_EXE}"
                    -c -emit-llvm -O0
                    -I "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime"
                    "${CMAKE_CURRENT_SOURCE_DIR}/${src}"
                    -o "${bc}"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${src}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/duxrt.h"
            COMMENT "Bitcode: ${src}")
        list(APPEND DUXRT_BC_FILES "${bc}")
    endforeach()

    set(DUXRT_BC_PATH "${DUXRT_BC_DIR}/duxrt.bc")
    add_custom_command(
        OUTPUT  "${DUXRT_BC_PATH}"
        COMMAND "${LLVM_LINK_EXE}" ${DUXRT_BC_FILES} -o "${DUXRT_BC_PATH}"
        DEPENDS ${DUXRT_BC_FILES}
        COMMENT "Bitcode: linking → duxrt.bc")
    add_custom_target(duxrt_bc ALL DEPENDS "${DUXRT_BC_PATH}")
else()
    message(STATUS "LTO disabled: clang or llvm-link not found")
endif()

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
    src/sema/types.cpp
    src/sema/symbol.cpp
    src/sema/sema.cpp
    src/sema/generics.cpp
    src/codegen/codegen.cpp
    ${FLEX_DuxLexer_OUTPUTS}
    ${BISON_DuxParser_OUTPUTS}
)

target_include_directories(dux PRIVATE
    src
    ${GEN_DIR}
    ${LLVM_INCLUDE_DIRS}
)

# Bake duxrt paths and stdlib dir into the compiler
target_compile_definitions(dux PRIVATE
    DUX_VERSION="${PROJECT_VERSION}"
    ${LLVM_DEFINITIONS}
    DUXRT_LIB_PATH="${DUXRT_LIB_PATH}"
    DUXRT_BC_PATH="${DUXRT_BC_PATH}"
    DUXRT_INCLUDE_DIR="${DUXRT_INCLUDE_DIR}"
    DUXSTDLIB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/stdlib"
    DUX_STDLIB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/stdlib"
)

# Ensure duxrt.bc is built before the dux compiler (when LTO is enabled)
if(TARGET duxrt_bc)
    add_dependencies(dux duxrt_bc)
endif()

target_link_libraries(dux PRIVATE ${LLVM_LIBS})

# Warnings — suppress LLVM header noise
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
install(TARGETS dux    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS duxrt  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES   src/runtime/duxrt.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/duxrt)
install(DIRECTORY stdlib/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/dux/stdlib
        FILES_MATCHING PATTERN "*.dux")

# ─── Packaging (CPack) (#38) ──────────────────────────────────────────────────
set(CPACK_PACKAGE_NAME        "dux-lang")
set(CPACK_PACKAGE_VERSION     "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "The Dux programming language compiler")
set(CPACK_PACKAGE_VENDOR      "Dux Lang")
set(CPACK_PACKAGE_CONTACT     "vorj.dux@gmail.com")

# Source strip prefix so installed paths are relative
set(CPACK_STRIP_FILES         TRUE)

# Debian package
set(CPACK_DEBIAN_PACKAGE_SECTION    "devel")
set(CPACK_DEBIAN_PACKAGE_DEPENDS    "libc6 (>= 2.17), libstdc++6 (>= 9)")
set(CPACK_DEBIAN_FILE_NAME          "DEB-DEFAULT")

# Generators: .deb and .tar.gz
set(CPACK_GENERATOR             "TGZ;DEB")
set(CPACK_SOURCE_GENERATOR      "TGZ")
set(CPACK_SOURCE_IGNORE_FILES   "/\\.git/;/build/;/\\.cache/")

include(CPack)

# ─── Tests ────────────────────────────────────────────────────────────────────
enable_testing()

add_subdirectory(tests/runtime)

# Smoke test: parse all example files
file(GLOB DUX_EXAMPLES "${CMAKE_SOURCE_DIR}/examples/*.dux")
foreach(EX ${DUX_EXAMPLES})
    cmake_path(GET EX STEM name)
    add_test(
        NAME    example_${name}
        COMMAND dux --dump-ast ${EX}
    )
endforeach()

# parse_ok: files that must parse cleanly (exit 0)
file(GLOB DUX_PARSE_OK "${CMAKE_SOURCE_DIR}/tests/parse_ok/*.dux")
foreach(F ${DUX_PARSE_OK})
    cmake_path(GET F STEM name)
    add_test(
        NAME    parse_ok_${name}
        COMMAND dux --dump-ast ${F}
    )
endforeach()

# parse_fail: files that must fail to parse (exit non-zero)
file(GLOB DUX_PARSE_FAIL "${CMAKE_SOURCE_DIR}/tests/parse_fail/*.dux")
foreach(F ${DUX_PARSE_FAIL})
    cmake_path(GET F STEM name)
    add_test(
        NAME    parse_fail_${name}
        COMMAND dux ${F}
    )
    set_tests_properties(parse_fail_${name} PROPERTIES
        WILL_FAIL TRUE
    )
endforeach()

# sema_ok: files that must pass semantic analysis (exit 0 with --check)
file(GLOB DUX_SEMA_OK "${CMAKE_SOURCE_DIR}/tests/sema_ok/*.dux")
foreach(F ${DUX_SEMA_OK})
    cmake_path(GET F STEM name)
    add_test(
        NAME    sema_ok_${name}
        COMMAND dux --check ${F}
    )
endforeach()

# sema_fail: files that must fail semantic analysis (exit non-zero with --check)
file(GLOB DUX_SEMA_FAIL "${CMAKE_SOURCE_DIR}/tests/sema_fail/*.dux")
foreach(F ${DUX_SEMA_FAIL})
    cmake_path(GET F STEM name)
    add_test(
        NAME    sema_fail_${name}
        COMMAND dux --check ${F}
    )
    set_tests_properties(sema_fail_${name} PROPERTIES
        WILL_FAIL TRUE
    )
endforeach()

# codegen_ok: files that must compile to IR (exit 0 with --emit-ir)
file(GLOB DUX_CODEGEN_OK "${CMAKE_SOURCE_DIR}/tests/codegen_ok/*.dux")
foreach(F ${DUX_CODEGEN_OK})
    cmake_path(GET F STEM name)
    add_test(
        NAME    codegen_ok_${name}
        COMMAND dux --emit-ir ${F} -o /dev/null
    )
endforeach()

# run_ok: files that must compile and run, producing correct output
# Each test has a .dux source and a .expected file with expected stdout.
file(GLOB DUX_RUN_OK "${CMAKE_SOURCE_DIR}/tests/run_ok/*.dux")
foreach(F ${DUX_RUN_OK})
    cmake_path(GET F STEM name)
    set(EXPECTED "${CMAKE_SOURCE_DIR}/tests/run_ok/${name}.expected")
    if(EXISTS "${EXPECTED}")
        add_test(
            NAME    run_ok_${name}
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_test.sh
                    $<TARGET_FILE:dux>
                    ${F}
                    ${EXPECTED}
        )
        set_tests_properties(run_ok_${name} PROPERTIES
            DEPENDS duxrt
        )
    endif()
endforeach()

# run_fail: files that must compile but exit non-zero at runtime.
# Used for testing assert failures, runtime panics, etc.
file(GLOB DUX_RUN_FAIL "${CMAKE_SOURCE_DIR}/tests/run_fail/*.dux")
foreach(F ${DUX_RUN_FAIL})
    cmake_path(GET F STEM name)
    add_test(
        NAME    run_fail_${name}
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_fail_test.sh
                $<TARGET_FILE:dux>
                ${F}
    )
    set_tests_properties(run_fail_${name} PROPERTIES
        DEPENDS duxrt
    )
endforeach()

# bootstrap: compile stage2+stage3 from Dux source, run stage3 round-trip suite.
# This verifies the compiler can compile a realistic self-hosting workload and
# that the resulting binaries produce correct output (13 round-trip tests).
add_test(
    NAME    bootstrap_stage3_suite
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/bootstrap_test.sh
            $<TARGET_FILE:dux>
            ${CMAKE_SOURCE_DIR}
)
set_tests_properties(bootstrap_stage3_suite PROPERTIES
    DEPENDS duxrt
    TIMEOUT 120
)
