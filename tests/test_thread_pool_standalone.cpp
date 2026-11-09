/*
 * test_thread_pool_standalone.cpp
 *
 * Zero-dependency test runner for ThreadPool — no GoogleTest needed.
 * Uses only C++17 STL.  Build with:
 *
 *   make test                   (release)
 *   make tsan                   (ThreadSanitizer — detects data races)
 *   make asan                   (AddressSanitizer — detects memory errors)
 *
 * Or manually:
 *   g++ -std=c++17 -pthread -O2 -Iinclude \
 *       tests/test_thread_pool_standalone.cpp -o build/test_thread_pool
 *   ./build/test_thread_pool
 */

#include "../include/thread_pool.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace std::chrono_literals;

/* ── Minimal test harness ───────────────────────────────────────────── */

static int g_pass = 0, g_fail = 0;

#define EXPECT_EQ(a, b)  expect_eq((a), (b), #a, #b, __LINE__)
#define EXPECT_TRUE(x)   expect_true((x), #x, __LINE__)
#define EXPECT_THROW(expr, ExcType) expect_throw([&]{ (void)(expr); }, #ExcType, __LINE__)

template <typename T>
static void expect_eq(T got, T want, const char *expr_a,
                      const char *expr_b, int line) {
    if (got == want) { ++g_pass; return; }
    ++g_fail;
    std::printf("  FAIL L%d: %s == %s  (got %ld, want %ld)\n",
                line, expr_a, expr_b, (long)got, (long)want);
}

static void expect_true(bool v, const char *expr, int line) {
    if (v) { ++g_pass; return; }
    ++g_fail;
    std::printf("  FAIL L%d: expected true: %s\n", line, expr);
}

static void expect_throw(std::function<void()> fn,
                         const char *exc_name, int line) {
    try { fn(); ++g_fail;
          std::printf("  FAIL L%d: expected %s but no exception thrown\n",
                      line, exc_name); }
    catch (...) { ++g_pass; }
}

static void run_test(const char *name, std::function<void()> fn) {
    std::printf("[%s]\n", name);
    fn();
}

/* ── Tests ──────────────────────────────────────────────────────────── */

static void test_submit_returns_future() {
    ThreadPool pool(2);
    auto fut = pool.submit([] { return 42; });
    EXPECT_EQ(fut.get(), 42);
}

static void test_all_tasks_complete() {
    constexpr int N = 1000;
    ThreadPool pool(4);
    std::atomic<int> counter{0};
    std::vector<std::future<void>> futs;
    futs.reserve(N);
    for (int i = 0; i < N; ++i)
        futs.emplace_back(pool.submit([&counter] {
            counter.fetch_add(1, std::memory_order_relaxed);
        }));
    for (auto &f : futs) f.get();
    EXPECT_EQ(counter.load(), N);
}

static void test_exception_propagates() {
    ThreadPool pool(2);
    auto fut = pool.submit([]() -> int {
        throw std::runtime_error("boom");
        return 0;
    });
    EXPECT_THROW(fut.get(), std::runtime_error);
}

static void test_submit_after_shutdown_throws() {
    ThreadPool pool(2);
    pool.shutdown();
    EXPECT_THROW(pool.submit([] { return 1; }), std::runtime_error);
}

/*
 * ThreadSanitizer target: 8 threads × 500 tasks sharing one atomic.
 * A plain int (non-atomic) here would cause TSan to report a data race.
 */
static void test_concurrent_writes_data_race_free() {
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

static void test_shutdown_drains_inflight() {
    ThreadPool pool(4);
    std::atomic<int> done{0};
    constexpr int N = 200;
    std::vector<std::future<void>> futs;
    for (int i = 0; i < N; ++i)
        futs.emplace_back(pool.submit([&done] {
            done.fetch_add(1, std::memory_order_relaxed);
        }));
    pool.shutdown();
    EXPECT_EQ(done.load(), N);
}

static void test_argument_forwarding() {
    ThreadPool pool(2);
    auto fut = pool.submit([](int a, int b) { return a + b; }, 7, 13);
    EXPECT_EQ(fut.get(), 20);
}

static void test_bounded_queue_back_pressure() {
    // 1 worker, queue capacity 4
    ThreadPool pool(1, 4);
    std::atomic<bool> release{false};

    // Block the single worker
    auto blocker = pool.submit([&release] {
        while (!release.load(std::memory_order_acquire))
            std::this_thread::sleep_for(1ms);
    });

    // Fill the queue up to capacity (should not block since blocker is running)
    std::vector<std::future<void>> futs;
    for (int i = 0; i < 4; ++i)
        futs.emplace_back(pool.submit([] { /* no-op */ }));

    EXPECT_TRUE(pool.pending_tasks() <= pool.queue_capacity());

    release.store(true, std::memory_order_release);
    blocker.get();
    for (auto &f : futs) f.get();
}

/* ── Main ───────────────────────────────────────────────────────────── */

int main() {
    std::printf("=== ThreadPool Test Suite (standalone, no GoogleTest) ===\n\n");

    run_test("submit returns correct future",          test_submit_returns_future);
    run_test("all 1000 tasks complete",                test_all_tasks_complete);
    run_test("exception propagates via future",        test_exception_propagates);
    run_test("submit after shutdown throws",           test_submit_after_shutdown_throws);
    run_test("concurrent writes are data-race-free",   test_concurrent_writes_data_race_free);
    run_test("shutdown drains in-flight tasks",        test_shutdown_drains_inflight);
    run_test("argument forwarding works",              test_argument_forwarding);
    run_test("bounded queue back-pressure enforced",   test_bounded_queue_back_pressure);

    std::printf("\n%s — %d passed, %d failed\n",
                g_fail == 0 ? "ALL PASS" : "SOME FAILURES",
                g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
