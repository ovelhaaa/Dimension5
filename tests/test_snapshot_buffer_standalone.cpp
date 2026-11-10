#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mutex>
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

// =============================================================================
// Standalone SnapshotBuffer Verification Suite (Phase 1.2.2 Hardening)
// =============================================================================

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

// 2. Deterministic Slot Reuse Protection with Real Stale Descriptor Interleaving (Section 6)
void test_standalone_slot_reuse_protection() {
    dimension_ipc::SnapshotBuffer<int, 4> buf;
    // Step 1: Initial published snapshot (Slot 0, Gen 1)
    check(buf.publish(10, 1), "Initial publication gen 1");

    std::atomic<bool> writerAInHook { false };
    std::atomic<bool> writerBFinished { false };
    std::atomic<dimension_ipc::SnapshotBuffer<int, 4>::PublishResult> resultA {};

    // Hook: called after Writer A selected its candidate slot, but before claiming it!
    buf.testHookPreSlotClaim = [&](uint32_t /*cand*/, uint64_t gen) {
        if (gen == 2) {
            writerAInHook.store(true, std::memory_order_release);
            while (!writerBFinished.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
        }
    };

    // Producer A enters publishDetailed with gen 2
    std::thread wA([&]() {
        resultA.store(buf.publishDetailed(20, 2));
    });

    // Producer B enters after A has chosen candidate 1, and publishes gen 3 to slot 1!
    std::thread wB([&]() {
        while (!writerAInHook.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        // Producer B claims slot 1 and publishes gen 3
        check(buf.publish(30, 3), "Producer B published gen 3");
        writerBFinished.store(true, std::memory_order_release);
    });

    wB.join();
    wA.join();

    // Verify invariants:
    // A could not overwrite slot 1 while B published it
    // A's attempt must be rejected (StaleGeneration), not masquerading as success
    check(resultA.load() == dimension_ipc::SnapshotBuffer<int, 4>::PublishResult::StaleGeneration,
          "Producer A was rejected as StaleGeneration");

    int readVal = 0;
    uint64_t readGen = 0;
    check(buf.read(readVal, readGen), "Read succeeded after concurrent collision");
    check(readGen == 3, "Published generation is strictly B's generation 3");
    check(readVal == 30, "Published payload is strictly B's payload 30 (not torn or overwritten)");
}

// 3. Bounded CAS Loop and Contention Limit Reached (Section 3)
void test_standalone_bounded_cas_contention_limit() {
    dimension_ipc::SnapshotBuffer<int, 4> buf;
    check(buf.publish(10, 1), "Initial publish");

    check(dimension_ipc::SnapshotBuffer<int, 4>::kMaxPublishCasAttempts == 8,
          "kMaxPublishCasAttempts is 8");

    int attempts = 0;
    buf.testHookBeforePublishCAS = [&](uint32_t cand, uint64_t /*gen*/) {
        attempts++;
        // Force CAS failure by changing publishedDescriptor before every CAS attempt
        // Keep gen < 100 so it is not marked stale!
        buf.forcePublishedDescriptorForTest(attempts, (cand + 1) % 4);
    };

    auto res = buf.publishDetailed(99, 100);
    check(res == dimension_ipc::SnapshotBuffer<int, 4>::PublishResult::ContentionLimitReached,
          "publishDetailed returns ContentionLimitReached after 8 failed attempts");
    check(attempts == 8, "Exactly 8 CAS attempts were executed");

    // Clear hook: verify subsequent publication succeeds and candidate slot was properly freed
    buf.testHookBeforePublishCAS = nullptr;
    auto res2 = buf.publishDetailed(101, 101);
    check(res2 == dimension_ipc::SnapshotBuffer<int, 4>::PublishResult::Success,
          "Subsequent publish succeeds: candidate slot was correctly freed without leaks");

    int val = 0;
    uint64_t g = 0;
    check(buf.read(val, g), "Read succeeded after recovery");
    check(val == 101 && g == 101, "Confirmed payload and generation 101");
}

// 4. Mixed Explicit and Automatic Generations — Scenario A (Section 5)
void test_standalone_mixed_generations_scenario_a() {
    dimension_ipc::SnapshotBuffer<int, 4> buf;

    // 1. Publish explicitly generation 1
    check(buf.publish(10, 1), "Explicit publish gen 1");

    // 2. Publish automatically another payload
    check(buf.publish(20), "Automatic publish after explicit gen 1");

    // 3. Read snapshot: must return 20 and generation >= 2
    int outVal = 0;
    uint64_t outGen = 0;
    check(buf.read(outVal, outGen), "Read succeeded");
    check(outVal == 20, "Read payload must be 20");
    check(outGen == 2, "Read generation must be 2 (monotonically advanced from 1)");
}

// 5. Mixed Explicit and Automatic Generations — Scenario B (Section 5)
void test_standalone_mixed_generations_scenario_b() {
    dimension_ipc::SnapshotBuffer<int, 4> buf;

    // 1. Publish explicitly generation 100
    check(buf.publish(100, 100), "Explicit publish gen 100");

    // 2. Publish automatically another payload
    check(buf.publish(200), "Automatic publish after explicit gen 100");

    // 3. Read snapshot: must return 200 and generation 101
    int outVal = 0;
    uint64_t outGen = 0;
    check(buf.read(outVal, outGen), "Read succeeded");
    check(outVal == 200, "Read payload must be 200");
    check(outGen == 101, "Read generation must be 101 (strictly monotonic after 100)");
}

// 6. Stale Publication Fast Rejection
void test_standalone_stale_publication() {
    dimension_ipc::SnapshotBuffer<int, 4> buf;
    check(buf.publish(50, 50), "Publish gen 50");

    auto res = buf.publishDetailed(30, 30);
    check(res == dimension_ipc::SnapshotBuffer<int, 4>::PublishResult::StaleGeneration,
          "Older generation 30 rejected as StaleGeneration");

    auto resEq = buf.publishDetailed(50, 50);
    check(resEq == dimension_ipc::SnapshotBuffer<int, 4>::PublishResult::StaleGeneration,
          "Equal generation 50 rejected as StaleGeneration");

    int outVal = 0;
    uint64_t outGen = 0;
    check(buf.read(outVal, outGen), "Read succeeded");
    check(outVal == 50 && outGen == 50, "Snapshot 50 remains intact");
}

// 7. Saturation and Recovery
void test_standalone_saturation_and_recovery() {
    dimension_ipc::SnapshotBuffer<int, 4> buf;
    check(buf.publish(1, 1), "Publish gen 1");

    // Force remaining candidate slots busy
    for (size_t s = 0; s < 4; ++s) {
        buf.forceSlotBusyForTest(s, true);
    }

    auto res = buf.publishDetailed(99, 2);
    check(res == dimension_ipc::SnapshotBuffer<int, 4>::PublishResult::SlotsUnavailable,
          "SlotsUnavailable returned on full saturation");

    // Clear busy flags
    for (size_t s = 0; s < 4; ++s) {
        buf.forceSlotBusyForTest(s, false);
    }

    // Now publication should succeed normally
    check(buf.publish(100, 3), "Publish gen 3 succeeded after recovery");

    int outVal = 0;
    uint64_t outGen = 0;
    check(buf.read(outVal, outGen), "Read succeeded");
    check(outVal == 100 && outGen == 3, "New snapshot confirmed");
}

// 8. Reset Lifecycle and Precondition Contract (Section 8)
void test_standalone_reset_lifecycle() {
    dimension_ipc::SnapshotBuffer<int, 4> buf;
    buf.publish(10, 1);
    buf.publish(20, 2);

    int val = 0;
    uint64_t gen = 0;
    check(buf.read(val, gen), "Read before reset");
    check(gen == 2, "Generation is 2 before reset");

    // Call reset
    buf.reset();

    // After reset: buffer is empty
    check(!buf.read(val, gen), "Read fails after reset (buffer empty)");
    check(buf.getLatestPublishedGeneration() == 0, "Latest generation is 0 after reset");

    // Can publish afresh
    check(buf.publish(30, 1), "Fresh publish gen 1 after reset");
    check(buf.read(val, gen), "Read succeeded after fresh publish");
    check(val == 30 && gen == 1, "Payload and gen intact after reset");
}

// 9. Three Concurrent Producers Stress Test
void test_standalone_three_concurrent_producers() {
    struct Correlated {
        int producerId;
        uint64_t seq;
        double checksum;
    };

    dimension_ipc::SnapshotBuffer<Correlated, 8> buf;
    std::atomic<bool> running { true };
    std::atomic<int> errors { 0 };

    constexpr int kOps = 4000;

    auto worker = [&](int pId, uint64_t baseGen) {
        for (int i = 0; i < kOps && running.load(std::memory_order_relaxed); ++i) {
            Correlated c;
            c.producerId = pId;
            c.seq = baseGen + (uint64_t)i;
            c.checksum = (double)c.seq * 3.14159;
            buf.publish(c, c.seq);
        }
    };

    auto reader = [&]() {
        uint64_t lastGen = 0;
        int reads = 0;
        while (running.load(std::memory_order_relaxed) && reads < 6000) {
            Correlated c {};
            uint64_t g = 0;
            if (buf.read(c, g)) {
                if (g < lastGen) errors.fetch_add(1); // non-monotonic
                lastGen = g;
                if (c.seq != g) errors.fetch_add(1); // payload/gen mismatch
                if (std::abs(c.checksum - (double)g * 3.14159) > 1e-4) errors.fetch_add(1); // torn
                ++reads;
            }
        }
    };

    std::thread p1([&]() { worker(1, 10000); });
    std::thread p2([&]() { worker(2, 20000); });
    std::thread p3([&]() { worker(3, 30000); });
    std::thread r1(reader);
    std::thread r2(reader);

    p1.join();
    p2.join();
    p3.join();
    running.store(false, std::memory_order_release);
    r1.join();
    r2.join();

    check(errors.load() == 0, "Zero errors observed in 3-producer 2-reader stress test");
}

// 10. Multi-Producer Multi-Reader Stress Test (MPMC)
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

// =============================================================================
// Phase 1.2.3 — Deterministic Generation Hardening Suite (Section 4 Tests A..F)
// =============================================================================

// 11. Test A — Explicit Generation under Contention
void test_phase123_test_a_explicit_gen_contention() {
    dimension_ipc::SnapshotBuffer<int, 8> buf;
    std::atomic<bool> start { false };
    std::atomic<int> completed { 0 };

    auto worker = [&](uint64_t baseGen) {
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        for (int i = 0; i < 200; ++i) {
            buf.publish(i, baseGen + (uint64_t)i);
        }
        completed.fetch_add(1, std::memory_order_release);
    };

    std::thread t1(worker, 1000);
    std::thread t2(worker, 2000);
    std::thread t3(worker, 3000);
    std::thread t4(worker, 4000);

    start.store(true, std::memory_order_release);
    t1.join();
    t2.join();
    t3.join();
    t4.join();

    check(completed.load() == 4, "All 4 explicit generation producers completed without unbounded loops");
    int val = 0;
    uint64_t g = 0;
    check(buf.read(val, g), "Read succeeded");
    check(g >= 4000, "Published generation monotonically advanced to at least 4000");
}

// 12. Test B — Automatic Generation under Contention
void test_phase123_test_b_auto_gen_contention() {
    dimension_ipc::SnapshotBuffer<int, 8> buf;
    std::atomic<bool> start { false };
    std::vector<uint64_t> acceptedGenerations[4];
    std::mutex genMutex;

    auto worker = [&](int threadId) {
        std::vector<uint64_t> threadGens;
        threadGens.reserve(200);
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        for (int i = 0; i < 200; ++i) {
            if (buf.publish(threadId * 1000 + i)) {
                uint64_t curPub = buf.getLatestPublishedGeneration();
                if (curPub > 0) {
                    threadGens.push_back(curPub);
                }
            }
        }
        std::lock_guard<std::mutex> lock(genMutex);
        acceptedGenerations[threadId] = std::move(threadGens);
    };

    std::thread t0(worker, 0);
    std::thread t1(worker, 1);
    std::thread t2(worker, 2);
    std::thread t3(worker, 3);

    start.store(true, std::memory_order_release);
    t0.join();
    t1.join();
    t2.join();
    t3.join();

    std::vector<uint64_t> allGens;
    for (int i = 0; i < 4; ++i) {
        allGens.insert(allGens.end(), acceptedGenerations[i].begin(), acceptedGenerations[i].end());
    }
    check(!allGens.empty(), "Automatic publications were accepted");
    std::sort(allGens.begin(), allGens.end());
    auto it = std::adjacent_find(allGens.begin(), allGens.end());
    check(it == allGens.end(), "Zero duplicate generations accepted under concurrent automatic publication");
}

// 13. Test C — Mixed Generations Sequence (Section 4 Scenario C)
// Sequence: Explicit 1 -> Auto -> Explicit 100 -> Auto -> Explicit 99 -> Auto
void test_phase123_test_c_mixed_generations_sequence() {
    dimension_ipc::SnapshotBuffer<int, 4> buf;
    int readVal = 0;
    uint64_t readGen = 0;

    // 1. Explicit 1
    auto r1 = buf.publishDetailed(10, 1);
    check(r1 == dimension_ipc::SnapshotBuffer<int, 4>::PublishResult::Success, "Explicit 1 succeeded");
    check(buf.read(readVal, readGen) && readGen == 1 && readVal == 10, "Read gen 1 with val 10");

    // 2. Auto -> expected gen 2
    auto r2 = buf.publishDetailed(20);
    check(r2 == dimension_ipc::SnapshotBuffer<int, 4>::PublishResult::Success, "Auto after 1 succeeded");
    check(buf.read(readVal, readGen) && readGen == 2 && readVal == 20, "Read gen 2 with val 20");

    // 3. Explicit 100 -> expected gen 100
    auto r3 = buf.publishDetailed(100, 100);
    check(r3 == dimension_ipc::SnapshotBuffer<int, 4>::PublishResult::Success, "Explicit 100 succeeded");
    check(buf.read(readVal, readGen) && readGen == 100 && readVal == 100, "Read gen 100 with val 100");

    // 4. Auto -> expected gen 101
    auto r4 = buf.publishDetailed(101);
    check(r4 == dimension_ipc::SnapshotBuffer<int, 4>::PublishResult::Success, "Auto after 100 succeeded");
    check(buf.read(readVal, readGen) && readGen == 101 && readVal == 101, "Read gen 101 with val 101");

    // 5. Explicit 99 -> expected REJECTION (StaleGeneration) because 99 <= 101!
    auto r5 = buf.publishDetailed(99, 99);
    check(r5 == dimension_ipc::SnapshotBuffer<int, 4>::PublishResult::StaleGeneration, "Explicit 99 rejected as StaleGeneration");
    // Ensure published state was NOT overwritten by stale generation
    check(buf.read(readVal, readGen) && readGen == 101 && readVal == 101, "State preserved at gen 101");

    // 6. Auto -> expected gen 102
    auto r6 = buf.publishDetailed(102);
    check(r6 == dimension_ipc::SnapshotBuffer<int, 4>::PublishResult::Success, "Auto after rejected 99 succeeded");
    check(buf.read(readVal, readGen) && readGen == 102 && readVal == 102, "Read gen 102 with val 102");
}

// 14. Test D — Generation Reservation Contention Limit (Section 4 Scenario D)
void test_phase123_test_d_gen_reservation_contention_limit() {
    // 1. Explicit generation reservation forced contention limit
    {
        dimension_ipc::SnapshotBuffer<int, 4> buf;
        int hookCalls = 0;
        buf.testHookBeforeGenCAS = [&](int attempt) {
            hookCalls++;
            // Modify autoGen before each CAS attempt so expected value never matches
            buf.forceAutoGenForTest((uint64_t)(attempt + 1));
        };

        auto res = buf.publishDetailed(42, 1000);
        check(res == dimension_ipc::SnapshotBuffer<int, 4>::PublishResult::ContentionLimitReached,
              "Explicit gen reservation returns ContentionLimitReached under forced contention");
        check(hookCalls == dimension_ipc::SnapshotBuffer<int, 4>::kMaxGenCasAttempts,
              "Exactly kMaxGenCasAttempts (8) reservation attempts were executed");
    }

    // 2. Automatic generation reservation forced contention limit
    {
        dimension_ipc::SnapshotBuffer<int, 4> buf;
        int hookCalls = 0;
        buf.testHookBeforeGenCAS = [&](int attempt) {
            hookCalls++;
            // Invalidate expected curAuto on each attempt
            buf.forceAutoGenForTest((uint64_t)(attempt + 50));
        };

        auto res = buf.publishDetailed(99);
        check(res == dimension_ipc::SnapshotBuffer<int, 4>::PublishResult::ContentionLimitReached,
              "Auto gen reservation returns ContentionLimitReached under forced contention");
        check(hookCalls == dimension_ipc::SnapshotBuffer<int, 4>::kMaxGenCasAttempts,
              "Exactly kMaxGenCasAttempts (8) auto reservation attempts were executed");
    }
}

// 15. Test E — Numerical Limits (56-bit capacity, overflow boundary, no silent wraparound)
void test_phase123_test_e_numerical_limits_56bit_capacity() {
    dimension_ipc::SnapshotBuffer<int, 4> buf;
    constexpr uint64_t kMaxGen = 0x00FFFFFFFFFFFFFFULL;

    // Explicit generation exceeding 56 bits must be rejected as StaleGeneration/overflow
    auto r1 = buf.publishDetailed(1, kMaxGen + 1ULL);
    check(r1 == dimension_ipc::SnapshotBuffer<int, 4>::PublishResult::StaleGeneration,
          "Explicit gen > 2^56 - 1 is rejected");

    // Force autoGen to kMaxGen and attempt automatic publication
    buf.forceAutoGenForTest(kMaxGen);
    auto r2 = buf.publishDetailed(2);
    check(r2 == dimension_ipc::SnapshotBuffer<int, 4>::PublishResult::StaleGeneration,
          "Auto gen at 56-bit capacity is safely rejected without silent wraparound");

    // Verify valid generation at kMaxGen - 1 succeeds
    buf.reset();
    auto r3 = buf.publishDetailed(3, kMaxGen - 1ULL);
    check(r3 == dimension_ipc::SnapshotBuffer<int, 4>::PublishResult::Success,
          "Publication at boundary (kMaxGen - 1) succeeds");
    int outVal = 0;
    uint64_t outGen = 0;
    check(buf.read(outVal, outGen), "Read at boundary succeeded");
    check(outGen == kMaxGen - 1ULL, "Boundary generation retrieved accurately");
}

// 16. Test F — Slot Preservation Suite
void test_phase123_test_f_slot_preservation_suite() {
    dimension_ipc::SnapshotBuffer<int, 4> buf;
    // Step 1: Initial publish to slot 0
    check(buf.publish(10, 1), "Publish gen 1");

    // Step 2: Simulate active reader reserving slot 0
    std::atomic<bool> readerInSlot { false };
    std::atomic<bool> writerFinished { false };

    buf.testHookDuringRead = [&](uint32_t /*slot*/) {
        readerInSlot.store(true, std::memory_order_release);
        while (!writerFinished.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    };

    std::thread readerThread([&]() {
        int v = 0;
        uint64_t g = 0;
        check(buf.read(v, g), "Reader read gen 1");
        check(v == 10 && g == 1, "Reader payload matches gen 1");
    });

    std::thread writerThread([&]() {
        while (!readerInSlot.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        // Slot 0 is protected by active reader count. Writer must use slot 1!
        check(buf.publish(20, 2), "Writer publish gen 2 to available slot");
        writerFinished.store(true, std::memory_order_release);
    });

    readerThread.join();
    writerThread.join();

    int v = 0;
    uint64_t g = 0;
    check(buf.read(v, g), "Read gen 2");
    check(v == 20 && g == 2, "Latest payload is gen 2");
}

// 17. Performance Benchmarks (Section 11)
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

    // Benchmark 3: Audio Telemetry Snapshot Cost (struct equivalent to StereoTelemetrySnapshot)
    {
        struct TelemetrySim {
            float rmsL, rmsR, peakL, peakR;
            float correlation, balance, widthEstimate;
            float energyHistory[32];
        };

        dimension_ipc::SnapshotBuffer<TelemetrySim, 4> teleBuf;
        constexpr int N = 50000;
        std::vector<double> latencies;
        latencies.reserve(N);

        TelemetrySim t {};
        t.correlation = 0.85f;
        t.widthEstimate = 1.2f;

        for (int i = 0; i < N; ++i) {
            const auto t0 = Clock::now();
            teleBuf.publish(t);
            const auto t1 = Clock::now();
            latencies.push_back((double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        }

        auto m = computeMetrics(latencies);
        std::cout << "[Benchmark] Audio Telemetry Snapshot: Mean = " << m.meanNs
                  << " ns, p95 = " << m.p95Ns << " ns, p99 = " << m.p99Ns
                  << " ns, worst = " << m.worstNs << " ns\n";
    }

    // Benchmark 4: High contention (4 producers, 4 consumers) with metrics on Stale, SlotsUnavailable, ContentionLimit
    {
        dimension_ipc::SnapshotBuffer<double, 8> buf;
        buf.publish(1.0);
        std::atomic<bool> startBench { false };

        std::atomic<uint64_t> successCount { 0 };
        std::atomic<uint64_t> staleCount { 0 };
        std::atomic<uint64_t> slotsUnavailCount { 0 };
        std::atomic<uint64_t> contentionLimitCount { 0 };

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
                auto res = buf.publishDetailed((double)i);
                const auto t1 = Clock::now();
                pubLatencies[id].push_back((double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());

                if (res == dimension_ipc::SnapshotBuffer<double, 8>::PublishResult::Success) {
                    successCount.fetch_add(1, std::memory_order_relaxed);
                } else if (res == dimension_ipc::SnapshotBuffer<double, 8>::PublishResult::StaleGeneration) {
                    staleCount.fetch_add(1, std::memory_order_relaxed);
                } else if (res == dimension_ipc::SnapshotBuffer<double, 8>::PublishResult::SlotsUnavailable) {
                    slotsUnavailCount.fetch_add(1, std::memory_order_relaxed);
                } else if (res == dimension_ipc::SnapshotBuffer<double, 8>::PublishResult::ContentionLimitReached) {
                    contentionLimitCount.fetch_add(1, std::memory_order_relaxed);
                }
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
        std::cout << "            Success: " << successCount.load()
                  << ", Stale: " << staleCount.load()
                  << ", SlotsUnavail: " << slotsUnavailCount.load()
                  << ", ContentionLimit: " << contentionLimitCount.load() << "\n";
        std::cout << "[Benchmark] read()    High Contention: Mean = " << readM.meanNs
                  << " ns, p95 = " << readM.p95Ns << " ns, p99 = " << readM.p99Ns
                  << " ns, worst = " << readM.worstNs << " ns\n";
    }

    // Benchmark 5: Saturation behavior
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
    std::cout << "=== Dimension5 Standalone SnapshotBuffer Test Suite (Phase 1.2.2 Hardening) ===" << std::endl;

    std::cout << "[Running] Test 1: test_standalone_out_of_order..." << std::endl;
    test_standalone_out_of_order();
    std::cout << "Test 1: Out-of-order publication PASSED" << std::endl;

    std::cout << "[Running] Test 2: test_standalone_slot_reuse_protection..." << std::endl;
    test_standalone_slot_reuse_protection();
    std::cout << "Test 2: Deterministic slot reuse with stale descriptor PASSED" << std::endl;

    std::cout << "[Running] Test 3: test_standalone_bounded_cas_contention_limit..." << std::endl;
    test_standalone_bounded_cas_contention_limit();
    std::cout << "Test 3: Bounded CAS loop and contention limit PASSED" << std::endl;

    std::cout << "[Running] Test 4: test_standalone_mixed_generations_scenario_a..." << std::endl;
    test_standalone_mixed_generations_scenario_a();
    std::cout << "Test 4: Mixed generations Scenario A PASSED" << std::endl;

    std::cout << "[Running] Test 5: test_standalone_mixed_generations_scenario_b..." << std::endl;
    test_standalone_mixed_generations_scenario_b();
    std::cout << "Test 5: Mixed generations Scenario B PASSED" << std::endl;

    std::cout << "[Running] Test 6: test_standalone_stale_publication..." << std::endl;
    test_standalone_stale_publication();
    std::cout << "Test 6: Stale generation fast rejection PASSED" << std::endl;

    std::cout << "[Running] Test 7: test_standalone_saturation_and_recovery..." << std::endl;
    test_standalone_saturation_and_recovery();
    std::cout << "Test 7: Saturation and recovery PASSED" << std::endl;

    std::cout << "[Running] Test 8: test_standalone_reset_lifecycle..." << std::endl;
    test_standalone_reset_lifecycle();
    std::cout << "Test 8: Reset lifecycle and precondition PASSED" << std::endl;

    std::cout << "[Running] Test 9: test_standalone_three_concurrent_producers..." << std::endl;
    test_standalone_three_concurrent_producers();
    std::cout << "Test 9: Three concurrent producers PASSED" << std::endl;

    std::cout << "[Running] Test 10: test_standalone_stress_mpmc..." << std::endl;
    test_standalone_stress_mpmc();
    std::cout << "Test 10: MPMC stress test PASSED" << std::endl;

    std::cout << "[Running] Test 11: test_phase123_test_a_explicit_gen_contention..." << std::endl;
    test_phase123_test_a_explicit_gen_contention();
    std::cout << "Test 11: Explicit generation under contention PASSED" << std::endl;

    std::cout << "[Running] Test 12: test_phase123_test_b_auto_gen_contention..." << std::endl;
    test_phase123_test_b_auto_gen_contention();
    std::cout << "Test 12: Automatic generation under contention PASSED" << std::endl;

    std::cout << "[Running] Test 13: test_phase123_test_c_mixed_generations_sequence..." << std::endl;
    test_phase123_test_c_mixed_generations_sequence();
    std::cout << "Test 13: Mixed generations sequence PASSED" << std::endl;

    std::cout << "[Running] Test 14: test_phase123_test_d_gen_reservation_contention_limit..." << std::endl;
    test_phase123_test_d_gen_reservation_contention_limit();
    std::cout << "Test 14: Generation reservation contention limit PASSED" << std::endl;

    std::cout << "[Running] Test 15: test_phase123_test_e_numerical_limits_56bit_capacity..." << std::endl;
    test_phase123_test_e_numerical_limits_56bit_capacity();
    std::cout << "Test 15: Numerical limits (56-bit capacity) PASSED" << std::endl;

    std::cout << "[Running] Test 16: test_phase123_test_f_slot_preservation_suite..." << std::endl;
    test_phase123_test_f_slot_preservation_suite();
    std::cout << "Test 16: Slot preservation suite PASSED" << std::endl;

    std::cout << "\n=== Running Performance Benchmarks ===" << std::endl;
    run_performance_benchmarks();

    std::cout << "\nAll Standalone SnapshotBuffer Tests PASSED successfully!" << std::endl;
    return 0;
}
