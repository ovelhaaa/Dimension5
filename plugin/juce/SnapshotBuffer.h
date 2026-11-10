#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

namespace dimension_ipc {

/**
 * Robust, linearizable multi-producer multi-reader (MPMC) snapshot buffer.
 *
 * Architecture & Concurrency Guarantees:
 * 1. Single-Word 64-bit Publication Descriptor:
 *    - High 56 bits: Monotonic Generation (1 .. 2^56 - 1)
 *    - Low 8 bits: Published Slot Index (0 .. 255)
 *    Linearization Point of publish(): atomic CAS updating publishedDescriptor.
 *    Generation and slot index are bound atomically in a single bus transaction;
 *    it is impossible for generation and slot index to become disjoint or out-of-order.
 *
 * 2. Slot State Lifecycle:
 *    - Bit 31 (kBusyWriteBit): Owned exclusively by an active writer.
 *    - Bit 30 (kPublishedBit): Actively published snapshot. Writers CANNOT claim it.
 *    - Bits 0..29 (kReaderMask): Active reader reference count.
 *    A slot can ONLY be claimed for writing if state == 0 (FREE).
 *
 * 3. Premature Reuse Protection:
 *    A published slot retains kPublishedBit as long as it is published. Even if a
 *    concurrent writer holds a stale view of the publication descriptor and targets
 *    that slot, the CAS(0, kBusyWriteBit) fails deterministically.
 *
 * 4. Reader Protection:
 *    Readers atomically reserve the published slot (state -> state + 1) before copying,
 *    preventing writers from reusing the slot while the copy is in progress.
 *    Copies to local temporaries first and commits to output only after generation validation.
 *
 * 5. Realtime Safety:
 *    Zero heap allocations, zero locks, zero unbounded spin loops.
 *    Single-producer audio thread runs wait-free (O(1)).
 */
template <typename T, size_t NumSlots = 8>
class SnapshotBuffer {
public:
    static_assert(NumSlots >= 3, "NumSlots must be >= 3 for race-free buffering with active reader");
    static_assert(NumSlots <= 256, "NumSlots must fit in 8-bit slot index");

    enum class PublishResult {
        Success = 0,
        StaleGeneration,
        SlotsUnavailable,
        ContentionLimitReached
    };

    static constexpr int kMaxPublishCasAttempts = 8;

    SnapshotBuffer() {
        publishedDescriptor.store(0, std::memory_order_relaxed);
        autoGen.store(0, std::memory_order_relaxed);
        for (size_t i = 0; i < NumSlots; ++i) {
            slots[i].generation.store(0, std::memory_order_relaxed);
            slots[i].state.store(0, std::memory_order_relaxed);
        }
    }

    // Test hooks (nullptr in production; set by deterministic unit tests)
    std::function<void(uint32_t slot)> testHookPreReserve { nullptr };
    std::function<void(uint32_t slot)> testHookDuringRead { nullptr };
    std::function<void(uint32_t cand, uint64_t gen)> testHookPostGenCAS { nullptr };
    std::function<void(uint32_t cand, uint64_t gen)> testHookBeforePublishCAS { nullptr };
    std::function<void(uint32_t cand, uint64_t gen)> testHookPreSlotClaim { nullptr };

    PublishResult publishDetailed(const T& val, uint64_t gen = 0) {
        uint64_t effectiveGen = 0;
        if (gen != 0) {
            effectiveGen = gen;
            // Advance autoGen so subsequent automatic publications stay strictly ahead of explicit generations
            uint64_t curAuto = autoGen.load(std::memory_order_relaxed);
            while (effectiveGen > curAuto &&
                   !autoGen.compare_exchange_weak(curAuto, effectiveGen,
                                                  std::memory_order_relaxed,
                                                  std::memory_order_relaxed)) {
            }
        } else {
            // Automatic monotonic generation: must be strictly greater than both autoGen
            // and the currently published generation.
            const uint64_t curDescSnapshot = publishedDescriptor.load(std::memory_order_acquire);
            const uint64_t lastPubGen = (curDescSnapshot != 0) ? extractGen(curDescSnapshot) : 0;
            uint64_t curAuto = autoGen.load(std::memory_order_relaxed);
            uint64_t nextGen = (curAuto > lastPubGen ? curAuto : lastPubGen) + 1;
            while (true) {
                if (nextGen <= curAuto) {
                    nextGen = curAuto + 1;
                }
                if (autoGen.compare_exchange_weak(curAuto, nextGen,
                                                  std::memory_order_relaxed,
                                                  std::memory_order_relaxed)) {
                    effectiveGen = nextGen;
                    break;
                }
            }
        }

        // Fast reject if incoming generation is already stale
        uint64_t curDesc = publishedDescriptor.load(std::memory_order_acquire);
        if (curDesc != 0 && effectiveGen <= extractGen(curDesc)) {
            return PublishResult::StaleGeneration;
        }

        const uint32_t curSlot = (curDesc != 0) ? extractSlot(curDesc) : 0;

        for (size_t i = 0; i < NumSlots; ++i) {
            const uint32_t cand = (uint32_t)((curSlot + 1 + i) % NumSlots);

            // Re-check current published descriptor: never target currently published slot
            curDesc = publishedDescriptor.load(std::memory_order_acquire);
            if (curDesc != 0 && cand == extractSlot(curDesc)) {
                continue;
            }

            // Test hook: called after candidate slot is selected but before attempting claim
            if (testHookPreSlotClaim) {
                testHookPreSlotClaim(cand, effectiveGen);
            }

            // A candidate slot can ONLY be claimed if state == 0 (FREE).
            // Published slots have kPublishedBit; reading slots have readerCount > 0;
            // writing slots have kBusyWriteBit. All non-zero states fail here.
            uint32_t expected = 0;
            if (!slots[cand].state.compare_exchange_strong(expected, kBusyWriteBit,
                                                           std::memory_order_acquire,
                                                           std::memory_order_relaxed)) {
                continue;
            }

            // Post-claim validation: verify cand didn't become published while we claimed it
            curDesc = publishedDescriptor.load(std::memory_order_acquire);
            if (curDesc != 0 && cand == extractSlot(curDesc)) {
                slots[cand].state.store(kPublishedBit, std::memory_order_release);
                continue;
            }

            // Format payload and generation into the privately claimed slot
            slots[cand].data = val;
            slots[cand].generation.store(effectiveGen, std::memory_order_release);

            // Mark slot as ready/published before attempting publication CAS.
            // Readers cannot read it yet because publishedDescriptor does not point to it.
            slots[cand].state.store(kPublishedBit, std::memory_order_release);

            // Bounded monotonic CAS loop to update publishedDescriptor
            for (int attempt = 0; attempt < kMaxPublishCasAttempts; ++attempt) {
                if (curDesc != 0 && effectiveGen <= extractGen(curDesc)) {
                    // Stale generation: another writer published a newer or equal generation
                    // Revert candidate slot back to FREE
                    slots[cand].state.store(0, std::memory_order_release);
                    return PublishResult::StaleGeneration;
                }

                const uint64_t newDesc = makeDescriptor(effectiveGen, cand);

                if (testHookBeforePublishCAS) {
                    testHookBeforePublishCAS(cand, effectiveGen);
                }

                // LINEARIZATION POINT of publish()
                if (publishedDescriptor.compare_exchange_weak(curDesc, newDesc,
                                                              std::memory_order_acq_rel,
                                                              std::memory_order_acquire)) {
                    // Test hook to simulate legacy post-generation race condition if enabled
                    if (testHookPostGenCAS) {
                        testHookPostGenCAS(cand, effectiveGen);
                    }

                    // Successfully published! Retire the previously published slot
                    if (curDesc != 0) {
                        const uint32_t oldSlot = extractSlot(curDesc);
                        if (oldSlot != cand) {
                            slots[oldSlot].state.fetch_and(~kPublishedBit, std::memory_order_acq_rel);
                        }
                    }
                    return PublishResult::Success;
                }
                // compare_exchange_weak updated curDesc with the freshest publishedDescriptor
            }

            // CAS attempts exhausted under contention: revert candidate slot back to FREE
            slots[cand].state.store(0, std::memory_order_release);
            return PublishResult::ContentionLimitReached;
        }

        // All candidate slots were unavailable (saturation)
        return PublishResult::SlotsUnavailable;
    }

    bool publish(const T& val, uint64_t gen = 0) {
        return publishDetailed(val, gen) == PublishResult::Success;
    }

    bool read(T& out) const {
        uint64_t ignoredGen = 0;
        return read(out, ignoredGen);
    }

    bool read(T& out, uint64_t& outGen) const {
        constexpr int kMaxAttempts = 8;
        for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
            const uint64_t desc = publishedDescriptor.load(std::memory_order_acquire);
            if (desc == 0) {
                return false; // No snapshot published yet
            }

            const uint32_t slotIdx = extractSlot(desc);
            const uint64_t expectedGen = extractGen(desc);

            if (testHookPreReserve) {
                testHookPreReserve(slotIdx);
            }

            uint32_t s = slots[slotIdx].state.load(std::memory_order_acquire);

            // Slot must be actively published and not undergoing write
            if ((s & kPublishedBit) == 0 || (s & kBusyWriteBit) != 0) {
                continue;
            }

            // Reader count protection against overflow
            if ((s & kReaderMask) >= (kReaderMask - 1)) {
                continue;
            }

            // LINEARIZATION POINT of reader reservation
            if (slots[slotIdx].state.compare_exchange_weak(s, s + 1,
                                                           std::memory_order_acquire,
                                                           std::memory_order_relaxed)) {
                if (testHookDuringRead) {
                    testHookDuringRead(slotIdx);
                }

                // Copy to local temporaries first to prevent partial/torn outputs on failure
                T tempVal = slots[slotIdx].data;
                const uint64_t slotGen = slots[slotIdx].generation.load(std::memory_order_acquire);

                // Release reservation
                slots[slotIdx].state.fetch_sub(1, std::memory_order_release);

                // Validate that the slot payload matches the generation we acquired
                if (slotGen == expectedGen) {
                    out = std::move(tempVal);
                    outGen = slotGen;
                    return true;
                }
            }
        }
        return false;
    }

    uint64_t getLatestPublishedGeneration() const noexcept {
        return extractGen(publishedDescriptor.load(std::memory_order_acquire));
    }

    void forceSlotBusyForTest(size_t slot, bool busy) {
        const uint64_t curDesc = publishedDescriptor.load(std::memory_order_acquire);
        const bool isPublished = (curDesc != 0 && (slot % NumSlots) == extractSlot(curDesc));
        uint32_t val = 0;
        if (busy) {
            val = kBusyWriteBit;
        } else if (isPublished) {
            val = kPublishedBit;
        }
        slots[slot % NumSlots].state.store(val, std::memory_order_release);
    }

    void forcePublishedDescriptorForTest(uint64_t gen, uint32_t slot) {
        publishedDescriptor.store(makeDescriptor(gen, slot % NumSlots), std::memory_order_release);
    }

    /**
     * Resets the buffer to its empty initial state.
     *
     * CONCURRENCY CONTRACT / PRECONDITION:
     * reset() is NOT thread-safe with respect to concurrent calls to publish() or read().
     * Callers must ensure external mutual exclusion or invoke reset() only during
     * lifecycle transitions (e.g. processor construction, destruction, or offline prepareToPlay)
     * when no producers or consumers are actively calling publish() or read().
     */
    void reset() {
        publishedDescriptor.store(0, std::memory_order_release);
        for (size_t i = 0; i < NumSlots; ++i) {
            slots[i].generation.store(0, std::memory_order_release);
            slots[i].state.store(0, std::memory_order_release);
        }
        autoGen.store(0, std::memory_order_release);
    }

private:
    static constexpr uint32_t kBusyWriteBit = 0x80000000U;
    static constexpr uint32_t kPublishedBit = 0x40000000U;
    static constexpr uint32_t kReaderMask   = 0x3FFFFFFFU;

    static constexpr uint64_t kSlotMask = 0xFFULL;
    static constexpr uint64_t kGenShift = 8ULL;
    static constexpr uint64_t kMaxGen   = 0x00FFFFFFFFFFFFFFULL;

    static inline uint64_t makeDescriptor(uint64_t gen, uint32_t slot) noexcept {
        return ((gen & kMaxGen) << kGenShift) | (uint64_t)(slot & kSlotMask);
    }

    static inline uint64_t extractGen(uint64_t desc) noexcept {
        return (desc >> kGenShift) & kMaxGen;
    }

    static inline uint32_t extractSlot(uint64_t desc) noexcept {
        return (uint32_t)(desc & kSlotMask);
    }

    struct alignas(64) Slot {
        T data {};
        std::atomic<uint64_t> generation { 0 };
        mutable std::atomic<uint32_t> state { 0 };
    };

    alignas(64) Slot slots[NumSlots];
    alignas(64) std::atomic<uint64_t> publishedDescriptor { 0 };
    alignas(64) std::atomic<uint64_t> autoGen { 0 };
};

} // namespace dimension_ipc
