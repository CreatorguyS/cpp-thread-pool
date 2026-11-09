cmake_minimum_required(VERSION 3.16)
project(thread_pool CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Sanitizer options (pass -DSANITIZE_THREAD=ON or -DSANITIZE_ADDRESS=ON) ──
option(SANITIZE_THREAD  "Build with ThreadSanitizer"  OFF)
option(SANITIZE_ADDRESS "Build with AddressSanitizer" OFF)

function(add_sanitizers target)
    if(SANITIZE_THREAD)
        target_compile_options(${target} PRIVATE -fsanitize=thread -g -O1)
        target_link_options   (${target} PRIVATE -fsanitize=thread)
    elseif(SANITIZE_ADDRESS)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -g -O1)
        target_link_options   (${target} PRIVATE -fsanitize=address,undefined)
    else()
        target_compile_options(${target} PRIVATE -O2 -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# ── GoogleTest (fetched automatically — no manual install needed) ──────────
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ── Tests ─────────────────────────────────────────────────────────────────
enable_testing()
add_executable(test_thread_pool tests/test_thread_pool.cpp)
target_include_directories(test_thread_pool PRIVATE include)
target_link_libraries(test_thread_pool PRIVATE GTest::gtest GTest::gtest_main pthread)
add_sanitizers(test_thread_pool)
add_test(NAME ThreadPoolTests COMMAND test_thread_pool)

# ── Benchmark ──────────────────────────────────────────────────────────────
add_executable(benchmark bench/benchmark.cpp)
target_include_directories(benchmark PRIVATE include)
target_link_libraries(benchmark PRIVATE pthread)
target_compile_options(benchmark PRIVATE -O3 -march=native)
