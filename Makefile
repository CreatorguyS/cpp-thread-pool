# Makefile — Thread Pool (no CMake required)
# Requires only: g++ (GCC 10+ or Clang 11+)
#
# Targets:
#   make                 — build tests + benchmark (Release)
#   make test            — build and run all tests
#   make bench           — build and run benchmark
#   make tsan            — build with ThreadSanitizer and run tests
#   make asan            — build with AddressSanitizer and run tests
#   make clean           — remove all build artifacts

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -pthread
INC      := -Iinclude

BUILD    := build
TEST_BIN := $(BUILD)/test_thread_pool
BENCH_BIN:= $(BUILD)/benchmark

.PHONY: all test bench tsan asan clean

all: $(TEST_BIN) $(BENCH_BIN)

$(BUILD):
	mkdir -p $(BUILD)

$(TEST_BIN): tests/test_thread_pool_standalone.cpp include/thread_pool.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -O2 $(INC) $< -o $@

$(BENCH_BIN): bench/benchmark.cpp include/thread_pool.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -O3 -march=native $(INC) $< -o $@

test: $(TEST_BIN)
	@echo "\n=== Running Tests ==="
	./$(TEST_BIN)

bench: $(BENCH_BIN)
	@echo "\n=== Running Benchmark ==="
	./$(BENCH_BIN)

tsan: tests/test_thread_pool_standalone.cpp include/thread_pool.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -g -O1 -fsanitize=thread $(INC) \
		tests/test_thread_pool_standalone.cpp -o $(BUILD)/test_tsan
	@echo "\n=== Running with ThreadSanitizer ==="
	./$(BUILD)/test_tsan

asan: tests/test_thread_pool_standalone.cpp include/thread_pool.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -g -O1 -fsanitize=address,undefined $(INC) \
		tests/test_thread_pool_standalone.cpp -o $(BUILD)/test_asan
	@echo "\n=== Running with AddressSanitizer ==="
	./$(BUILD)/test_asan

clean:
	rm -rf $(BUILD)
