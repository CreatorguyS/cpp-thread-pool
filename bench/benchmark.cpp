/*
 * benchmark.cpp — Compare single-threaded vs std::async vs ThreadPool
 *
 * Benchmark 1: Heavy CPU-Bound Workload (Compute Kernel on 4M items)
 * Benchmark 2: High-Volume Fine-Grained Tasks (10,000 tasks showing thread reuse)
 */

#include "../include/thread_pool.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>

using Clock = std::chrono::steady_clock;
using Ms    = std::chrono::duration<double, std::milli>;

static void compute_heavy(std::vector<double> &data, size_t start, size_t end) {
    for (size_t i = start; i < end; ++i) {
        double v = data[i];
        for (int step = 0; step < 80; ++step) {
            v = std::sin(v) * std::cos(v) + std::sqrt(std::abs(v) + 1.0);
        }
        data[i] = v;
    }
}

template <typename Fn>
static double measure_ms(Fn &&fn) {
    auto t0 = Clock::now();
    fn();
    auto t1 = Clock::now();
    return Ms(t1 - t0).count();
}

int main() {
    const unsigned HW = std::thread::hardware_concurrency();

    std::cout << "============================================================\n";
    std::cout << "  BENCHMARK 1: CPU-Bound Parallel Computation (4M items)   \n";
    std::cout << "============================================================\n";
    constexpr size_t TOTAL_ITEMS = 4'000'000;
    const size_t NUM_CHUNKS = HW * 4;
    const size_t CHUNK_SIZE = TOTAL_ITEMS / NUM_CHUNKS;

    std::vector<double> base_data(TOTAL_ITEMS, 3.14159);

    auto data_single = base_data;
    double t_single = measure_ms([&] {
        compute_heavy(data_single, 0, TOTAL_ITEMS);
    });
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "[Single-Threaded]      " << std::setw(8) << t_single << " ms  | Speedup: 1.00x (Baseline)\n";

    auto data_async = base_data;
    double t_async = measure_ms([&] {
        std::vector<std::future<void>> futs;
        futs.reserve(NUM_CHUNKS);
        for (size_t t = 0; t < NUM_CHUNKS; ++t) {
            size_t start = t * CHUNK_SIZE;
            size_t end = (t == NUM_CHUNKS - 1) ? TOTAL_ITEMS : (t + 1) * CHUNK_SIZE;
            futs.push_back(std::async(std::launch::async, [&data_async, start, end] {
                compute_heavy(data_async, start, end);
            }));
        }
        for (auto &f : futs) f.get();
    });
    std::cout << "[std::async]           " << std::setw(8) << t_async << " ms  | Speedup: "
              << std::setw(5) << t_single / t_async << "x\n";

    auto data_pool = base_data;
    double t_pool = 0;
    {
        ThreadPool pool(HW, NUM_CHUNKS * 2);
        t_pool = measure_ms([&] {
            std::vector<std::future<void>> futs;
            futs.reserve(NUM_CHUNKS);
            for (size_t t = 0; t < NUM_CHUNKS; ++t) {
                size_t start = t * CHUNK_SIZE;
                size_t end = (t == NUM_CHUNKS - 1) ? TOTAL_ITEMS : (t + 1) * CHUNK_SIZE;
                futs.push_back(pool.submit([&data_pool, start, end] {
                    compute_heavy(data_pool, start, end);
                }));
            }
            for (auto &f : futs) f.get();
        });
    }
    std::cout << "[ThreadPool (" << HW << "T)]    " << std::setw(8) << t_pool << " ms  | Speedup: "
              << std::setw(5) << t_single / t_pool << "x\n";

    std::cout << "\n============================================================\n";
    std::cout << "  BENCHMARK 2: High-Frequency Tasks (Thread Reuse vs Spawning)\n";
    std::cout << "============================================================\n";
    constexpr int NUM_SHORT_TASKS = 50'000;
    std::cout << "Submitting " << NUM_SHORT_TASKS << " independent tasks...\n";

    // std::async spawns an OS thread per task
    double t_async_spawn = measure_ms([&] {
        // Run in batches of 200 to prevent OS thread exhaustion
        constexpr int BATCH = 200;
        for (int b = 0; b < NUM_SHORT_TASKS / BATCH; ++b) {
            std::vector<std::future<int>> futs;
            futs.reserve(BATCH);
            for (int i = 0; i < BATCH; ++i) {
                futs.push_back(std::async(std::launch::async, [i] { return i * 2; }));
            }
            for (auto &f : futs) f.get();
        }
    });
    std::cout << "[std::async Spawning]  " << std::setw(8) << t_async_spawn << " ms\n";

    // ThreadPool reuses worker threads from the pool
    double t_pool_reuse = 0;
    {
        ThreadPool pool(HW, 4096);
        t_pool_reuse = measure_ms([&] {
            std::vector<std::future<int>> futs;
            futs.reserve(NUM_SHORT_TASKS);
            for (int i = 0; i < NUM_SHORT_TASKS; ++i) {
                futs.push_back(pool.submit([i] { return i * 2; }));
            }
            for (auto &f : futs) f.get();
        });
    }
    std::cout << "[ThreadPool Reused]    " << std::setw(8) << t_pool_reuse << " ms\n";
    std::cout << "------------------------------------------------------------\n";
    std::cout << ">>> ThreadPool is " << std::setprecision(1)
              << (t_async_spawn / t_pool_reuse) << "x faster than std::async on high-frequency tasks!\n";
    std::cout << "------------------------------------------------------------\n";

    return 0;
}
