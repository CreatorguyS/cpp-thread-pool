#pragma once

/*
 * thread_pool.hpp  —  C++17 fixed-size thread pool
 *
 * Design decisions (mention these in Qualcomm interviews):
 *  - Bounded blocking queue prevents unbounded memory growth (back-pressure).
 *  - std::promise / std::future decouple task submission from result retrieval.
 *  - Two-phase shutdown: stop accepting new tasks, then drain the queue.
 *  - std::condition_variable used for both "not-full" and "not-empty" signals.
 */

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

class ThreadPool {
public:
    /*
     * Construct a pool with `num_threads` workers and a task queue that
     * holds at most `queue_capacity` pending tasks.  Callers that submit
     * when the queue is full will block until a slot is free.
     */
    explicit ThreadPool(std::size_t num_threads,
                        std::size_t queue_capacity = 1024)
        : capacity_(queue_capacity), stopped_(false) {

        if (num_threads == 0)
            throw std::invalid_argument("ThreadPool: num_threads must be > 0");

        workers_.reserve(num_threads);
        for (std::size_t i = 0; i < num_threads; ++i)
            workers_.emplace_back(&ThreadPool::worker_loop, this);
    }

    /* Non-copyable, non-movable — threads hold a raw `this` pointer. */
    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;

    /*
     * Destructor triggers graceful shutdown:
     *  1. Signal all workers to stop after draining remaining tasks.
     *  2. Wake every blocked worker so it can exit its loop.
     *  3. Join all threads before releasing resources.
     */
    ~ThreadPool() { shutdown(); }

    /*
     * submit(f, args...) — enqueue callable f with forwarded arguments.
     *
     * Returns std::future<ReturnType> so the caller can retrieve the result
     * or catch exceptions thrown inside the task.
     *
     * Blocks if the queue is at capacity (back-pressure).
     * Throws std::runtime_error if called after shutdown.
     */
    template <typename F, typename... Args>
    auto submit(F &&f, Args &&...args)
        -> std::future<std::invoke_result_t<F, Args...>> {

        using ReturnType = std::invoke_result_t<F, Args...>;

        // Wrap the callable in a packaged_task so we can extract a future.
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));

        std::future<ReturnType> result = task->get_future();

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);

            // Block if queue is full — provides back-pressure.
            not_full_.wait(lock, [this] {
                return tasks_.size() < capacity_ || stopped_;
            });

            if (stopped_)
                throw std::runtime_error(
                    "ThreadPool::submit called after shutdown");

            // Wrap in a type-erased std::function so the queue holds a
            // uniform type.
            tasks_.emplace([task] { (*task)(); });
        }

        not_empty_.notify_one(); // wake exactly one idle worker
        return result;
    }

    /*
     * shutdown() — idempotent, safe to call more than once.
     * Signals all workers to exit after finishing in-flight tasks,
     * then joins every thread.
     */
    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (stopped_) return;
            stopped_ = true;
        }
        // Wake all workers so they can observe stopped_ == true.
        not_empty_.notify_all();
        not_full_.notify_all();

        for (std::thread &t : workers_)
            if (t.joinable()) t.join();
    }

    /* Accessors — useful for benchmarking and testing. */
    std::size_t thread_count() const noexcept { return workers_.size(); }
    std::size_t queue_capacity() const noexcept { return capacity_; }
    std::size_t pending_tasks() const {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return tasks_.size();
    }

private:
    /*
     * Each worker runs this loop.
     * It blocks on not_empty_ when idle and exits when stopped_ is set
     * and the queue is drained — ensuring no task is ever dropped.
     */
    void worker_loop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);

                // Wait until there is work to do, or the pool is shut down.
                not_empty_.wait(lock, [this] {
                    return !tasks_.empty() || stopped_;
                });

                // Exit only after all tasks are drained.
                if (stopped_ && tasks_.empty()) return;

                task = std::move(tasks_.front());
                tasks_.pop();
            }
            // Notify a potential blocked submit() that a slot freed up.
            not_full_.notify_one();

            task(); // Execute outside the lock — true parallelism.
        }
    }

    const std::size_t              capacity_;
    bool                           stopped_;
    mutable std::mutex             queue_mutex_;
    std::condition_variable        not_empty_;
    std::condition_variable        not_full_;
    std::queue<std::function<void()>> tasks_;
    std::vector<std::thread>       workers_;
};
