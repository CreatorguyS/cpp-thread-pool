/*
 * test_thread_pool.cpp — GoogleTest suite for ThreadPool
 *
 * Run with:   cmake --build build && ctest --test-dir build --output-on-failure
 * Or directly: ./build/tests/test_thread_pool
 *
 * ThreadSanitizer:
 *   cmake -DSANITIZE_THREAD=ON -B build_tsan && cmake --build build_tsan
 *   ./build_tsan/tests/test_thread_pool
 */

#include "../include/thread_pool.hpp"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <numeric>
#include <vector>

using namespace std::chrono_literals;

/* ------------------------------------------------------------------ */
/* Basic correctness                                                    */
/* ------------------------------------------------------------------ */

TEST(ThreadPoolTest, SubmitAndGetFuture) {
    ThreadPool pool(2);
    auto fut = pool.submit([] { return 42; });
    EXPECT_EQ(fut.get(), 42);
}

TEST(ThreadPoolTest, AllTasksComplete) {
    constexpr int N = 1000;
    ThreadPool pool(4);
    std::atomic<int> counter{0};

    std::vector<std::future<void>> futs;
    futs.reserve(N);
    for (int i = 0; i < N; ++i)
        futs.emplace_back(pool.submit([&counter] { counter.fetch_add(1, std::memory_order_relaxed); }));

    for (auto &f : futs) f.get();
    EXPECT_EQ(counter.load(), N);
}

TEST(ThreadPoolTest, ExceptionPropagatesViaFuture) {
    ThreadPool pool(2);
    auto fut = pool.submit([] -> int {
        throw std::runtime_error("test exception");
        return 0;
    });
    EXPECT_THROW(fut.get(), std::runtime_error);
}

TEST(ThreadPoolTest, SubmitAfterShutdownThrows) {
    ThreadPool pool(2);
    pool.shutdown();
    EXPECT_THROW(pool.submit([] { return 1; }), std::runtime_error);
}

/* ------------------------------------------------------------------ */
/* Concurrency: no data races on shared state                           */
/* (This is what ThreadSanitizer monitors at runtime)                   */
/* ------------------------------------------------------------------ */

TEST(ThreadPoolTest, ConcurrentWritesAreDataRaceFree) {
    // 8 threads, each submitting 500 increments of a shared atomic.
    // If there were a data race on a plain int, TSan would catch it.
    ThreadPool pool(8);
    std::atomic<int> sum{0};
    constexpr int TASKS = 4000;

    std::vector<std::future<void>> futs;
    futs.reserve(TASKS);
    for (int i = 0; i < TASKS; ++i)
        futs.emplace_back(pool.submit([&sum] {
            sum.fetch_add(1, std::memory_order_relaxed);
        }));

    for (auto &f : futs) f.get();
    EXPECT_EQ(sum.load(), TASKS);
}

/* ------------------------------------------------------------------ */
/* Back-pressure: bounded queue blocks, then resumes                    */
/* ------------------------------------------------------------------ */

TEST(ThreadPoolTest, BoundedQueueDoesNotExceedCapacity) {
    // Queue capacity = 4, only 1 worker so tasks pile up.
    // We rely on pending_tasks() staying <= capacity (4).
    ThreadPool pool(1, /*queue_capacity=*/4);

    std::atomic<bool> release{false};
    // Submit a long-running task to keep the worker busy.
    auto blocker = pool.submit([&release] {
        while (!release.load(std::memory_order_acquire))
            std::this_thread::sleep_for(1ms);
    });

    // Fill the queue to exactly capacity.
    std::vector<std::future<void>> futs;
    for (int i = 0; i < 4; ++i)
        futs.emplace_back(pool.submit([] { /* no-op */ }));

    // Queue should be at or under capacity.
    EXPECT_LE(pool.pending_tasks(), pool.queue_capacity());

    // Unblock the worker so we can drain and join cleanly.
    release.store(true, std::memory_order_release);
    blocker.get();
    for (auto &f : futs) f.get();
}

/* ------------------------------------------------------------------ */
/* Graceful shutdown drains in-flight work                              */
/* ------------------------------------------------------------------ */

TEST(ThreadPoolTest, ShutdownDrainsInFlightTasks) {
    ThreadPool pool(4);
    std::atomic<int> done{0};
    constexpr int N = 200;

    std::vector<std::future<void>> futs;
    for (int i = 0; i < N; ++i)
        futs.emplace_back(pool.submit([&done] { done.fetch_add(1, std::memory_order_relaxed); }));

    pool.shutdown(); // Should block until every task finishes.
    EXPECT_EQ(done.load(), N);
}

/* ------------------------------------------------------------------ */
/* Return value forwarding                                              */
/* ------------------------------------------------------------------ */

TEST(ThreadPoolTest, ArgumentForwarding) {
    ThreadPool pool(2);
    auto fut = pool.submit([](int a, int b) { return a + b; }, 7, 13);
    EXPECT_EQ(fut.get(), 20);
}

/* ------------------------------------------------------------------ */
/* Entry point                                                          */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
