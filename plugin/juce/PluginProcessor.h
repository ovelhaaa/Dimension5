#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>

#include <JuceHeader.h>

extern "C" {
#include "dimension_dsp.h"
}

#include "StereoAnalyzer.h"

namespace dimension_ipc {

/**
 * Envelope pairing an atomic monotonic generation with payload.
 * Guarantees a strict total ordering across all publication channels.
 */
template <typename T>
struct Envelope {
    uint64_t generation { 0 };
    T payload {};
};

/**
 * Bounded Multi-Producer Single-Consumer (MPSC) lock-free queue.
 * Realtime audio consumer runs wait-free (O(1), zero locks, zero allocs, zero spin).
 * Producers are serialized with turn sequences; returns false on saturation.
 */
template <typename T, size_t Capacity = 64>
class BoundedMPSCQueue {
public:
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");

    BoundedMPSCQueue() {
        for (size_t i = 0; i < Capacity; ++i) {
            buffer[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    bool tryEnqueue(const T& item) {
        size_t pos = enqueuePos.load(std::memory_order_relaxed);
        for (;;) {
            auto& node = buffer[pos & (Capacity - 1)];
            const size_t seq = node.sequence.load(std::memory_order_acquire);
            const intptr_t diff = (intptr_t)seq - (intptr_t)pos;
            if (diff == 0) {
                if (enqueuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    node.data = item;
                    node.sequence.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false; // Queue full (saturation)
            } else {
                pos = enqueuePos.load(std::memory_order_relaxed);
            }
        }
    }

    bool tryDequeue(T& item) {
        auto& node = buffer[dequeuePos & (Capacity - 1)];
        const size_t seq = node.sequence.load(std::memory_order_acquire);
        const intptr_t diff = (intptr_t)seq - (intptr_t)(dequeuePos + 1);
        if (diff == 0) {
            item = node.data;
            node.sequence.store(dequeuePos + Capacity, std::memory_order_release);
            ++dequeuePos;
            return true;
        }
        return false;
    }

    void reset() {
        dequeuePos = 0;
        enqueuePos.store(0, std::memory_order_relaxed);
        for (size_t i = 0; i < Capacity; ++i) {
            buffer[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

private:
    struct Node {
        std::atomic<size_t> sequence { 0 };
        T data {};
    };

    alignas(64) Node buffer[Capacity];
    alignas(64) std::atomic<size_t> enqueuePos { 0 };
    alignas(64) size_t dequeuePos { 0 };
};

/**
 * Safe multi-slot fallback mailbox.
 * Solves the 2-slot race condition where a producer could reuse a slot while consumer is reading.
 * Each slot has an atomic reservation state:
 * - Bit 31: kBusyWriteBit (set while writer is populating payload).
 * - Lower 31 bits: readerCount (incremented before reading; writer CANNOT claim slot if > 0).
 */
template <typename T, size_t NumSlots = 4>
class SafeMailbox {
public:
    static_assert(NumSlots >= 3, "NumSlots must be >= 3 for race-free buffering with active reader");

    SafeMailbox() = default;

    bool publish(const T& val) {
        const uint32_t cur = publishedSlot.load(std::memory_order_relaxed);
        for (size_t i = 0; i < NumSlots; ++i) {
            const uint32_t cand = (uint32_t)((cur + 1 + i) % NumSlots);
            uint32_t expected = 0;
            if (slots[cand].state.compare_exchange_strong(expected, kBusyWriteBit,
                                                         std::memory_order_acquire,
                                                         std::memory_order_relaxed)) {
                slots[cand].data = val;
                slots[cand].state.store(0, std::memory_order_release);
                publishedSlot.store(cand, std::memory_order_release);
                hasUpdate.store(true, std::memory_order_release);
                return true;
            }
        }
        return false; // All slots busy
    }

    bool consume(T& out) {
        if (!hasUpdate.exchange(false, std::memory_order_acq_rel)) {
            return false;
        }
        for (int retry = 0; retry < 4; ++retry) {
            const uint32_t idx = publishedSlot.load(std::memory_order_acquire);
            uint32_t s = slots[idx].state.load(std::memory_order_acquire);
            if (s & kBusyWriteBit) {
                continue;
            }
            if (slots[idx].state.compare_exchange_weak(s, s + 1,
                                                      std::memory_order_acquire,
                                                      std::memory_order_relaxed)) {
                out = slots[idx].data;
                slots[idx].state.fetch_sub(1, std::memory_order_release);
                return true;
            }
        }
        return false;
    }

    void reset() {
        hasUpdate.store(false, std::memory_order_relaxed);
    }

private:
    static constexpr uint32_t kBusyWriteBit = 0x80000000U;

    struct alignas(64) Slot {
        T data {};
        mutable std::atomic<uint32_t> state { 0 };
    };

    alignas(64) Slot slots[NumSlots];
    alignas(64) std::atomic<uint32_t> publishedSlot { 0 };
    alignas(64) std::atomic<bool> hasUpdate { false };
};

/**
 * Robust snapshot buffer for single-writer (audio thread) multi-reader (inspection).
 * Guarantees:
 * 1. Writer claims a slot with compare_exchange(0, kBusyWriteBit). If any reader is reading
 *    (state > 0) or slot is currently published, writer skips it.
 * 2. If all candidate slots are unavailable, writer safely preserves the last published snapshot.
 * 3. Reader atomically reserves the published slot before reading (state -> state + 1),
 *    preventing writer reuse during read.
 * 4. Deterministic test hooks allow unit tests to verify race window immunity.
 */
template <typename T, size_t NumSlots = 8>
class SnapshotBuffer {
public:
    static_assert(NumSlots >= 3, "NumSlots must be >= 3");

    SnapshotBuffer() = default;

    // Test hooks (empty in production, set by tests to force interleavings)
    std::function<void(uint32_t slot)> testHookPreReserve { nullptr };
    std::function<void(uint32_t slot)> testHookDuringRead { nullptr };

    bool publish(const T& val) {
        const uint32_t cur = currentPublishedSlot.load(std::memory_order_relaxed);
        for (size_t i = 0; i < NumSlots; ++i) {
            const uint32_t cand = (uint32_t)((cur + 1 + i) % NumSlots);
            if (cand == cur) {
                continue; // Do not overwrite currently published slot
            }
            uint32_t expected = 0;
            if (slots[cand].state.compare_exchange_strong(expected, kBusyWriteBit,
                                                         std::memory_order_acquire,
                                                         std::memory_order_relaxed)) {
                slots[cand].data = val;
                slots[cand].state.store(0, std::memory_order_release);
                currentPublishedSlot.store(cand, std::memory_order_release);
                return true;
            }
        }
        // Policy: preserve the last published snapshot if all other slots are busy
        return false;
    }

    bool read(T& out) const {
        for (int attempt = 0; attempt < 8; ++attempt) {
            const uint32_t idx = currentPublishedSlot.load(std::memory_order_acquire);

            if (testHookPreReserve) {
                testHookPreReserve(idx);
            }

            uint32_t s = slots[idx].state.load(std::memory_order_acquire);
            if (s & kBusyWriteBit) {
                continue;
            }

            if (slots[idx].state.compare_exchange_weak(s, s + 1,
                                                      std::memory_order_acquire,
                                                      std::memory_order_relaxed)) {
                if (testHookDuringRead) {
                    testHookDuringRead(idx);
                }

                out = slots[idx].data;
                slots[idx].state.fetch_sub(1, std::memory_order_release);
                return true;
            }
        }
        return false;
    }

    void forceSlotBusyForTest(size_t slot, bool busy) {
        slots[slot % NumSlots].state.store(busy ? kBusyWriteBit : 0, std::memory_order_release);
    }

private:
    static constexpr uint32_t kBusyWriteBit = 0x80000000U;

    struct alignas(64) Slot {
        T data {};
        mutable std::atomic<uint32_t> state { 0 };
    };

    alignas(64) Slot slots[NumSlots];
    alignas(64) std::atomic<uint32_t> currentPublishedSlot { 0 };
};

} // namespace dimension_ipc

class Dimension5AudioProcessor final : public juce::AudioProcessor,
                                       private juce::AudioProcessorValueTreeState::Listener,
                                       private juce::Timer {
public:
    using ValueTreeState = juce::AudioProcessorValueTreeState;

    struct PluginConfig {
        DimensionParams dspParams {};
        float mix = 1.0f;
        float bypassTarget = 1.0f;
    };

    using ConfigEnvelope = dimension_ipc::Envelope<PluginConfig>;

    Dimension5AudioProcessor();
    ~Dimension5AudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    ValueTreeState parameters;

    static ValueTreeState::ParameterLayout createParameterLayout();
    static juce::StringArray factoryPresetNames();

    float getOutputMeterLeft() const;
    float getOutputMeterRight() const;
    float getInputMeterLeft() const;
    float getInputMeterRight() const;
    bool getStereoTelemetry(StereoTelemetrySnapshot& out) const;
    void switchToCustomMode();
    void selectCompareSlot(int slot);
    void copyCompareToOtherSlot();
    int getActiveCompareSlot() const;

    void applyModePresetToAdvancedParameters(DimensionMode mode);
    bool isUpdatingModePreset() const;
    void getEffectiveConfig(PluginConfig& outConfig) const;
    void getLastAppliedConfig(PluginConfig& outConfig) const;
    uint32_t getSaturationCount() const;
    void deliverPendingUpdates();
    bool hasPendingUpdates() const;
    float getFloatParam(const char* id) const;

    dimension_ipc::SnapshotBuffer<PluginConfig, 8>& getAppliedSnapshotBufferForTesting() { return appliedSnapshotBuffer; }
    dimension_ipc::SnapshotBuffer<PluginConfig, 8>& getStagedConfigBufferForTesting() { return stagedConfigBuffer; }
    dimension_ipc::BoundedMPSCQueue<ConfigEnvelope, 64>& getConfigQueueForTesting() { return configQueue; }
    dimension_ipc::SafeMailbox<ConfigEnvelope, 4>& getFallbackMailboxForTesting() { return fallbackMailbox; }

private:
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void timerCallback() override;
    void handlePresentationUpdate();

    void captureCompareSlot(int slot);
    void recallCompareSlot(int slot);
    void captureCurrentConfig(PluginConfig& outConfig) const;
    float getMix() const;
    float getBypassTarget() const;
    void applyFactoryPreset(int index);
    void setFloatParamHost(const char* id, float value);
    void setInternalParamValue(const char* id, float value);
    void syncAdvancedParamsInternal(const DimensionParams& p);
    void publishConfigToAudio(const PluginConfig& cfg, uint64_t gen = 0);

    mutable std::recursive_mutex controlMutex;

    DimensionDSP dsp;
    StereoAnalyzer stereoAnalyzer;
    dimension_ipc::SnapshotBuffer<StereoTelemetrySnapshot, 4> telemetryBuffer;
    std::array<float, DIMENSION_MAX_BLOCK_SIZE> inL {};
    std::array<float, DIMENSION_MAX_BLOCK_SIZE> inR {};
    std::array<float, DIMENSION_MAX_BLOCK_SIZE> outL {};
    std::array<float, DIMENSION_MAX_BLOCK_SIZE> outR {};
    std::atomic<float> inputMeterLeft { 0.0f };
    std::atomic<float> inputMeterRight { 0.0f };
    std::atomic<float> outputMeterLeft { 0.0f };
    std::atomic<float> outputMeterRight { 0.0f };
    float bypassEffectLevel = 1.0f;
    float bypassSmoothCoeff = 1.0f;
    float smoothMix = 1.0f;
    float mixSmoothCoeff = 1.0f;

    std::array<juce::ValueTree, 2> compareStates;
    std::atomic<int> currentProgram { 0 };
    std::atomic<int> activeCompareSlot { 0 };

    PluginConfig stagedConfig {};
    PluginConfig activeConfig {};
    uint64_t lastAppliedGeneration { 0 };
    std::atomic<float> currentSampleRate { DIMENSION_SAMPLE_RATE_DEFAULT };

    dimension_ipc::BoundedMPSCQueue<ConfigEnvelope, 64> configQueue;
    dimension_ipc::SafeMailbox<ConfigEnvelope, 4> fallbackMailbox;
    dimension_ipc::SnapshotBuffer<PluginConfig, 8> appliedSnapshotBuffer;
    dimension_ipc::SnapshotBuffer<PluginConfig, 8> stagedConfigBuffer;
    std::atomic<uint32_t> saturationCount { 0 };

    std::atomic<int> lastPresetMode { (int)DIMENSION_MODE_I };
    std::atomic<int> effectiveMode { (int)DIMENSION_MODE_I };
    std::atomic<uint64_t> stateGeneration { 1 };
    std::atomic<uint64_t> pendingModeSyncGeneration { 0 };
    std::atomic<int> pendingHostMode { -1 };
    std::atomic<uint64_t> pendingPresetSyncGeneration { 0 };
    std::atomic<int> pendingPresetSyncMode { -1 };
    std::atomic<bool> presentationUpdateNeeded { false };
    std::atomic<bool> isPresentationSyncActive { false };
    std::atomic<bool> isBatchRestoring { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Dimension5AudioProcessor)
};
