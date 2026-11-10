#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <thread>
#include <vector>

#include "SnapshotBuffer.h"

static void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

struct BenchmarkMetrics {
    double meanNs;
    double p95Ns;
    double p99Ns;
    double worstNs;
};

static BenchmarkMetrics computeMetrics(std::vector<double>& latencies) {
    if (latencies.empty()) return { 0, 0, 0, 0 };
    std::sort(latencies.begin(), latencies.end());
    const double sum = std::accumulate(latencies.begin(), latencies.end(), 0.0);
    const double mean = sum / (double)latencies.size();
    const size_t idx95 = (size_t)((double)latencies.size() * 0.95);
    const size_t idx99 = (size_t)((double)latencies.size() * 0.99);
    const double p95 = latencies[std::min(idx95, latencies.size() - 1)];
    const double p99 = latencies[std::min(idx99, latencies.size() - 1)];
    const double worst = latencies.back();
    return { mean, p95, p99, worst };
}

// 1. Deterministic Out-of-Order Publication
void test_standalone_out_of_order() {
    dimension_ipc::SnapshotBuffer<int, 8> buf;

    std::atomic<bool> p1Paused { false };
    std::atomic<bool> p2Done { false };
    std::atomic<bool> res1 { true };
    std::atomic<bool> res2 { false };

    buf.testHookBeforePublishCAS = [&](uint32_t /*slot*/, uint64_t gen) {
        if (gen == 10) {
            p1Paused.store(true, std::memory_order_release);
            while (!p2Done.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
        }
    };

    std::thread t1([&]() {
        res1.store(buf.publish(100, 10));
    });

    std::thread t2([&]() {
        while (!p1Paused.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        res2.store(buf.publish(200, 11));
        p2Done.store(true, std::memory_order_release);
    });

    t1.join();
    t2.join();

    check(res2.load(), "P2 (gen 11) published successfully");
    check(!res1.load(), "P1 (gen 10) was dropped as stale");

    int outVal = 0;
    uint64_t outGen = 0;
    check(buf.read(outVal, outGen), "Read succeeded");
    check(outGen == 11, "Latest generation is 11");
    check(outVal == 200, "Payload is 200");
}

// 2. Deterministic Slot Reuse Protection
void test_standalone_slot_reuse_protection() {
    dimension_ipc::SnapshotBuffer<int, 4> buf;
    buf.publish(10, 1);

    std::atomic<bool> writerAStarted { false };
    std::atomic<bool> writerBFinished { false };

    std::thread wA([&]() {
        writerAStarted.store(true, std::memory_order_release);
        while (!writerBFinished.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        buf.publish(20, 2);
    });

    std::thread wB([&]() {
        while (!writerAStarted.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        buf.publish(30, 3);
        writerBFinished.store(true, std::memory_order_release);
    });

    wB.join();

    int readVal = 0;
    uint64_t readGen = 0;
    check(buf.read(readVal, readGen), "Read succeeded during active writer");
    check(readGen >= 3 && readVal == 30, "Published snapshot remains protected");

    wA.join();
}

// 3. Multi-Producer Multi-Reader Stress Test
void test_standalone_stress_mpmc() {
    struct TestData {
        uint64_t seq;
        double values[4];
    };

    dimension_ipc::SnapshotBuffer<TestData, 8> buf;
    std::atomic<bool> running { true };
    std::atomic<int> errors { 0 };

    constexpr int kOperationsPerProducer = 5000;

    auto producer = [&](uint64_t baseGen) {
        for (int i = 0; i < kOperationsPerProducer && running.load(std::memory_order_relaxed); ++i) {
            TestData d;
            d.seq = baseGen + (uint64_t)i;
            for (int k = 0; k < 4; ++k) {
                d.values[k] = (double)d.seq * 2.5;
            }
            buf.publish(d, d.seq);
        }
    };

    auto consumer = [&]() {
        uint64_t lastGen = 0;
        int reads = 0;
        while (running.load(std::memory_order_relaxed) && reads < 8000) {
            TestData d {};
            uint64_t g = 0;
            if (buf.read(d, g)) {
                if (g < lastGen) {
                    errors.fetch_add(1); // Generation regressed
                }
                lastGen = g;
                if (d.seq != g) {
                    errors.fetch_add(1); // Generation/payload mismatch
                }
                for (int k = 0; k < 4; ++k) {
                    if (std::abs(d.values[k] - (double)g * 2.5) > 1e-4) {
                        errors.fetch_add(1); // Torn payload
                    }
                }
                ++reads;
            }
        }
    };

    std::thread p1([&]() { producer(100000); });
    std::thread p2([&]() { producer(200000); });
    std::thread p3([&]() { producer(300000); });
    std::thread p4([&]() { producer(400000); });

    std::thread c1(consumer);
    std::thread c2(consumer);
    std::thread c3(consumer);
    std::thread c4(consumer);

    p1.join();
    p2.join();
    p3.join();
    p4.join();
    running.store(false, std::memory_order_release);

    c1.join();
    c2.join();
    c3.join();
    c4.join();

    check(errors.load() == 0, "No tearing, corruption, or non-monotonic ordering in MPMC stress");
}

// 4. Performance Benchmarks
void run_performance_benchmarks() {
    using Clock = std::chrono::high_resolution_clock;

    // Benchmark 1: Low-contention single-producer publish()
    {
        dimension_ipc::SnapshotBuffer<double, 8> buf;
        constexpr int N = 50000;
        std::vector<double> latencies;
        latencies.reserve(N);

        for (int i = 0; i < N; ++i) {
            const auto t0 = Clock::now();
            buf.publish((double)i);
            const auto t1 = Clock::now();
            latencies.push_back((double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        }

        auto m = computeMetrics(latencies);
        std::cout << "[Benchmark] publish() Low Contention: Mean = " << m.meanNs
                  << " ns, p95 = " << m.p95Ns << " ns, p99 = " << m.p99Ns
                  << " ns, worst = " << m.worstNs << " ns\n";
    }

    // Benchmark 2: Low-contention single-consumer read()
    {
        dimension_ipc::SnapshotBuffer<double, 8> buf;
        buf.publish(42.0);
        constexpr int N = 50000;
        std::vector<double> latencies;
        latencies.reserve(N);

        double out = 0.0;
        uint64_t gen = 0;
        for (int i = 0; i < N; ++i) {
            const auto t0 = Clock::now();
            buf.read(out, gen);
            const auto t1 = Clock::now();
            latencies.push_back((double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        }

        auto m = computeMetrics(latencies);
        std::cout << "[Benchmark] read() Low Contention:    Mean = " << m.meanNs
                  << " ns, p95 = " << m.p95Ns << " ns, p99 = " << m.p99Ns
                  << " ns, worst = " << m.worstNs << " ns\n";
    }

    // Benchmark 3: High contention (4 producers, 4 consumers)
    {
        dimension_ipc::SnapshotBuffer<double, 8> buf;
        buf.publish(1.0);
        std::atomic<bool> startBench { false };

        constexpr int opsPerThread = 10000;
        std::vector<double> pubLatencies[4];
        std::vector<double> readLatencies[4];
        for (int i = 0; i < 4; ++i) {
            pubLatencies[i].reserve(opsPerThread);
            readLatencies[i].reserve(opsPerThread);
        }

        auto pubWorker = [&](int id) {
            while (!startBench.load(std::memory_order_acquire)) std::this_thread::yield();
            for (int i = 0; i < opsPerThread; ++i) {
                const auto t0 = Clock::now();
                buf.publish((double)i);
                const auto t1 = Clock::now();
                pubLatencies[id].push_back((double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
            }
        };

        auto readWorker = [&](int id) {
            while (!startBench.load(std::memory_order_acquire)) std::this_thread::yield();
            double val = 0;
            uint64_t g = 0;
            for (int i = 0; i < opsPerThread; ++i) {
                const auto t0 = Clock::now();
                buf.read(val, g);
                const auto t1 = Clock::now();
                readLatencies[id].push_back((double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
            }
        };

        std::thread pThreads[4];
        std::thread rThreads[4];
        for (int i = 0; i < 4; ++i) {
            pThreads[i] = std::thread(pubWorker, i);
            rThreads[i] = std::thread(readWorker, i);
        }

        startBench.store(true, std::memory_order_release);
        for (int i = 0; i < 4; ++i) {
            pThreads[i].join();
            rThreads[i].join();
        }

        std::vector<double> allPub;
        std::vector<double> allRead;
        for (int i = 0; i < 4; ++i) {
            allPub.insert(allPub.end(), pubLatencies[i].begin(), pubLatencies[i].end());
            allRead.insert(allRead.end(), readLatencies[i].begin(), readLatencies[i].end());
        }

        auto pubM = computeMetrics(allPub);
        auto readM = computeMetrics(allRead);
        std::cout << "[Benchmark] publish() High Contention: Mean = " << pubM.meanNs
                  << " ns, p95 = " << pubM.p95Ns << " ns, p99 = " << pubM.p99Ns
                  << " ns, worst = " << pubM.worstNs << " ns\n";
        std::cout << "[Benchmark] read()    High Contention: Mean = " << readM.meanNs
                  << " ns, p95 = " << readM.p95Ns << " ns, p99 = " << readM.p99Ns
                  << " ns, worst = " << readM.worstNs << " ns\n";
    }

    // Benchmark 4: Saturation behavior
    {
        dimension_ipc::SnapshotBuffer<double, 4> buf;
        buf.publish(1.0);
        for (size_t i = 0; i < 4; ++i) buf.forceSlotBusyForTest(i, true);

        constexpr int N = 20000;
        std::vector<double> latencies;
        latencies.reserve(N);

        for (int i = 0; i < N; ++i) {
            const auto t0 = Clock::now();
            buf.publish(99.0);
            const auto t1 = Clock::now();
            latencies.push_back((double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        }

        for (size_t i = 0; i < 4; ++i) buf.forceSlotBusyForTest(i, false);

        auto m = computeMetrics(latencies);
        std::cout << "[Benchmark] publish() Saturation Drop: Mean = " << m.meanNs
                  << " ns, p95 = " << m.p95Ns << " ns, p99 = " << m.p99Ns
                  << " ns, worst = " << m.worstNs << " ns\n";
    }
}

int main() {
    std::cout << "=== Dimension5 Standalone SnapshotBuffer Test Suite ===\n";
    test_standalone_out_of_order();
    std::cout << "Test 1: Out-of-order publication PASSED\n";

    test_standalone_slot_reuse_protection();
    std::cout << "Test 2: Slot reuse protection PASSED\n";

    test_standalone_stress_mpmc();
    std::cout << "Test 3: MPMC stress test PASSED\n";

    std::cout << "\n=== Running Performance Benchmarks ===\n";
    run_performance_benchmarks();

    std::cout << "\nAll Standalone SnapshotBuffer Tests PASSED successfully!\n";
    return 0;
}
