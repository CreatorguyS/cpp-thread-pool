# Multithreaded Thread Pool — Modern C++17

A production-quality fixed-size worker pool with a **bounded blocking task queue**, `std::future`-based task submission, and graceful two-phase shutdown — built to demonstrate low-level systems programming, OS synchronization primitives, and memory safety.

---

## Architecture

```
                        Caller Thread(s)
              pool.submit(f, args...) -> std::future<T>
                                 │
                                 ▼
         ┌──────────────────────────────────────────────┐
         │         Bounded Blocking Task Queue          │
         │   std::queue + std::mutex + CVs (Full/Empty) │
         │   (Blocks if capacity reached: Back-pressure)│
         └──────┬───────────────┬──────────────┬────────┘
                │               │              │
                ▼               ▼              ▼
           Worker 0        Worker 1  ...  Worker N-1
          (std::thread)   (std::thread)  (std::thread)
                │               │              │
                └───────────────┴──────────────┘
                    Execute tasks outside lock
```

### Core Systems Engineering Highlights
* **Bounded Queue & Back-Pressure:** Hard capacity limit prevents unbounded heap consumption when producer throughput exceeds consumer throughput.
* **Dual Condition Variables:** Uses separate `not_empty_` and `not_full_` condition variables to eliminate spurious wakeups and busy spinning.
* **Lock Granularity:** Tasks are popped under `std::unique_lock`, but task execution occurs strictly *outside* the lock to guarantee true multi-core parallel execution.
* **Verified Race-Free:** Tested and verified clean under **ThreadSanitizer (TSan)** with zero data races and zero deadlocks.
* **Graceful Two-Phase Teardown:** Signals shutdown flag under lock, notifies all sleeping workers via `notify_all()`, drains remaining in-flight tasks, and joins all threads via RAII in the destructor.

---

## Verified Benchmark Results

*Platform: Linux 5.15 (x86_64), GCC 13.3, 12 Hardware Threads (`-O3 -march=native`)*

### Benchmark 1: CPU-Bound Computation (4,000,000 Elements)
| Execution Mode | Wall Time | Speedup | Notes |
| :--- | :---: | :---: | :--- |
| **Single-Threaded** | 6,758.5 ms | **1.00x** | Baseline serial execution |
| **`std::async`** | 735.5 ms | 9.19x | Spawns OS threads dynamically |
| **ThreadPool (12 Threads)** | **744.7 ms** | **9.08x** | Near-linear scaling across 12 cores |

### Benchmark 2: High-Frequency Task Throughput (50,000 Short Tasks)
| Execution Mode | Execution Time | Overhead Analysis |
| :--- | :---: | :--- |
| **`std::async` (OS Thread Spawning)** | 3,989.0 ms | Heavy syscall and thread allocation overhead |
| **ThreadPool (Thread Reuse)** | **1,255.9 ms** | **3.2x faster** — zero per-task kernel thread allocation |

---

## Unit Testing & Verification

Includes a comprehensive test suite covering concurrency edge cases:
- `SubmitAndGetFuture`: Validates future extraction and packaged task execution.
- `AllTasksComplete`: 1,000 parallel tasks updating atomic counters with `std::memory_order_relaxed`.
- `ExceptionPropagates`: Verifies exceptions thrown inside worker threads propagate cleanly to caller upon `future.get()`.
- `SubmitAfterShutdownThrows`: Confirms post-shutdown task rejection.
- `ConcurrentWritesDataRaceFree`: 8 threads × 500 tasks stress-tested under ThreadSanitizer.
- `BoundedQueueBackPressure`: Asserts queue capacity bounding under worker starvation.
- `ShutdownDrainsInFlight`: Guarantees zero task loss during teardown.

---

## Build & Run

```bash
# Build and run standalone unit tests
make test

# Build and run with ThreadSanitizer (TSan data-race validation)
make tsan

# Build and run with AddressSanitizer (ASan memory error check)
make asan

# Run performance benchmarks
make bench
```
