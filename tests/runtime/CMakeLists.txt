# tests/runtime/CMakeLists.txt
# Compile and register C-level runtime tests for duxrt.

cmake_minimum_required(VERSION 3.25)

# ── Compile options shared by all runtime tests ────────────────────────────
set(RUNTIME_TEST_COMPILE_FLAGS
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:   -g3 -O0>
    $<$<CONFIG:Release>: -O2>
)

# ── Helper: add a single runtime test ─────────────────────────────────────
# Usage: add_runtime_test(<stem>)
# Creates target test_<stem> from test_<stem>.c, links against duxrt, and
# registers it with CTest.
function(add_runtime_test stem)
    set(tgt "rt_${stem}")
    add_executable(${tgt} "test_${stem}.c")
    target_link_libraries(${tgt} PRIVATE duxrt)
    target_compile_options(${tgt} PRIVATE ${RUNTIME_TEST_COMPILE_FLAGS})
    # Place test binaries alongside the library in the build tree
    set_target_properties(${tgt} PROPERTIES
        OUTPUT_NAME "test_${stem}"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )
    add_test(
        NAME    "runtime_${stem}"
        COMMAND ${tgt}
    )
endfunction()

# ── Test targets ──────────────────────────────────────────────────────────
add_runtime_test(str)
add_runtime_test(dict)
add_runtime_test(list)
add_runtime_test(lifetime)

# ── Optional: valgrind variants ───────────────────────────────────────────
find_program(VALGRIND_EXECUTABLE valgrind)
if(VALGRIND_EXECUTABLE)
    message(STATUS "Valgrind found: ${VALGRIND_EXECUTABLE} - adding valgrind CTest variants")

    set(VALGRIND_FLAGS
        --leak-check=full
        --show-leak-kinds=all
        --track-origins=yes
        --error-exitcode=1
    )

    foreach(stem str dict list lifetime)
        add_test(
            NAME    "runtime_valgrind_${stem}"
            COMMAND ${VALGRIND_EXECUTABLE} ${VALGRIND_FLAGS}
                    $<TARGET_FILE:rt_${stem}>
        )
        # Mark as depending on the normal test (so normal run happens first)
        set_tests_properties("runtime_valgrind_${stem}" PROPERTIES
            DEPENDS "runtime_${stem}"
        )
    endforeach()
else()
    message(STATUS "Valgrind not found - skipping valgrind CTest variants")
endif()
