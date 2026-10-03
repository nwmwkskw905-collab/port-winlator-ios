cmake_minimum_required(VERSION 3.22)

project(WinlatariOSPhase02 C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Warnings are promoted deliberately: Phase 02 Section 30 forbids hiding them behind flags.
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Wpointer-arith
        -Wcast-align
        -Wstrict-prototypes
        -Wmissing-prototypes
        -Wno-unused-parameter
    )
endif()

if(APPLE)
    find_library(FOUNDATION_LIBRARY Foundation)
    # sys_icache_invalidate() lives in libSystem and needs no explicit link on Apple platforms.
endif()

add_library(phase02_runtime_core STATIC
    RuntimeCore/src/linux_platform.c
    RuntimeCore/src/darwin_platform.c
    RuntimeCore/src/runtime_memory.c
    RuntimeCore/src/runtime_dual_mapping.c
    RuntimeCore/src/runtime_jit.c
    RuntimeCore/src/runtime_cpu_abi.c
    RuntimeCore/src/runtime_threads.c
    RuntimeCore/src/runtime_signals.c
    RuntimeCore/src/runtime_filesystem.c
    RuntimeCore/src/runtime_ipc.c
    RuntimeCore/src/runtime_context.c
    RuntimeCore/src/runtime_loader.c
    Diagnostics/src/phase02_log.c
    Diagnostics/src/phase02_harness.c
    Diagnostics/src/phase02_progress.c
    Diagnostics/src/phase02_execution_policy.c
)

target_include_directories(phase02_runtime_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/RuntimeCore/include
    ${CMAKE_CURRENT_SOURCE_DIR}/Diagnostics/include
)

find_package(Threads REQUIRED)
target_link_libraries(phase02_runtime_core PUBLIC Threads::Threads)

if(APPLE)
    target_link_libraries(phase02_runtime_core PUBLIC ${FOUNDATION_LIBRARY})
endif()

add_executable(phase02_poc RuntimePoC/main.c)
target_link_libraries(phase02_poc PRIVATE phase02_runtime_core)

add_executable(phase02_unit_tests Tests/test_runtime_core.c)
target_link_libraries(phase02_unit_tests PRIVATE phase02_runtime_core)

# Pass 04: the selected-suite regression. It drives the same two entry points the app's
# "Run Selected" button drives, one press per phase02_log_t, and is the test that would have
# caught a run-03-class termination that the per-suite host runs never exercised.
add_executable(phase02_run_selected_tests Tests/test_run_selected.c)
target_link_libraries(phase02_run_selected_tests PRIVATE phase02_runtime_core)

enable_testing()
add_test(NAME phase02_unit_tests COMMAND phase02_unit_tests)
add_test(NAME phase02_run_selected_tests COMMAND phase02_run_selected_tests)
add_test(NAME phase02_suite_all COMMAND phase02_poc --suite all --quiet)

# The AArch64 Linux cross build of the very same translation units lives in a separate
# toolchain-driven configure step (tools/aarch64-linux-toolchain.cmake, driven by
# tools/build_aarch64_cross.sh) so that no target is ever silently built for the wrong
# architecture - a mistake this file had in an earlier revision and which the AArch64 codegen
# check exists precisely to prevent.
