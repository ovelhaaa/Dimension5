#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include <JuceHeader.h>

extern "C" {
#include "dimension_dsp.h"
}

namespace dimension_ipc {

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
 * Fallback mailbox to guarantee the latest configuration is never dropped
 * even under extreme burst queue saturation.
 */
template <typename T>
struct FallbackMailbox {
    alignas(64) T slots[2] {};
    alignas(64) std::atomic<uint32_t> writeSlot { 0 };
    alignas(64) std::atomic<bool> hasUpdate { false };

    void publish(const T& val) {
        const uint32_t target = 1U - writeSlot.load(std::memory_order_relaxed);
        slots[target] = val;
        writeSlot.store(target, std::memory_order_release);
        hasUpdate.store(true, std::memory_order_release);
    }

    bool consume(T& out) {
        if (!hasUpdate.exchange(false, std::memory_order_acq_rel)) {
            return false;
        }
        const uint32_t current = writeSlot.load(std::memory_order_acquire);
        out = slots[current];
        return true;
    }

    void reset() {
        hasUpdate.store(false, std::memory_order_relaxed);
    }
};

/**
 * Snapshot buffer for lock-free inspection reader (getLastAppliedConfig).
 * Audio thread publishes applied configs wait-free without locks or allocations.
 * Reader acquires a reference count on a published slot so audio never reuses it
 * while it is actively being copied.
 */
template <typename T, size_t NumSlots = 8>
class SnapshotBuffer {
public:
    SnapshotBuffer() = default;

    void publish(const T& val) {
        const uint32_t cur = currentPublishedSlot.load(std::memory_order_relaxed);
        uint32_t target = (cur + 1) % NumSlots;
        for (size_t i = 0; i < NumSlots; ++i) {
            const uint32_t cand = (uint32_t)((cur + 1 + i) % NumSlots);
            if (cand != cur && slots[cand].readerCount.load(std::memory_order_acquire) == 0) {
                target = cand;
                break;
            }
        }
        slots[target].data = val;
        currentPublishedSlot.store(target, std::memory_order_release);
    }

    void read(T& out) const {
        const uint32_t idx = currentPublishedSlot.load(std::memory_order_acquire);
        slots[idx].readerCount.fetch_add(1, std::memory_order_acquire);
        out = slots[idx].data;
        slots[idx].readerCount.fetch_sub(1, std::memory_order_release);
    }

private:
    struct Slot {
        T data {};
        mutable std::atomic<uint32_t> readerCount { 0 };
    };

    alignas(64) Slot slots[NumSlots];
    alignas(64) std::atomic<uint32_t> currentPublishedSlot { 0 };
};

} // namespace dimension_ipc

class Dimension5AudioProcessor final : public juce::AudioProcessor,
                                       private juce::AudioProcessorValueTreeState::Listener,
                                       private juce::AsyncUpdater {
public:
    using ValueTreeState = juce::AudioProcessorValueTreeState;

    struct PluginConfig {
        DimensionParams dspParams {};
        float mix = 1.0f;
        float bypassTarget = 1.0f;
    };

    Dimension5AudioProcessor();
    ~Dimension5AudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

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
    void switchToCustomMode();
    void selectCompareSlot(int slot);
    void copyCompareToOtherSlot();
    int getActiveCompareSlot() const;

    void applyModePresetToAdvancedParameters(DimensionMode mode);
    bool isUpdatingModePreset() const { return isInternalPresentationSync.load(std::memory_order_relaxed); }
    void getEffectiveConfig(PluginConfig& outConfig) const;
    void getLastAppliedConfig(PluginConfig& outConfig) const;
    uint32_t getSaturationCount() const;
    void deliverPendingUpdates() { handleUpdateNowIfNeeded(); }
    bool hasPendingUpdates() const { return isUpdatePending(); }
    float getFloatParam(const char* id) const;

private:
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;

    void captureCompareSlot(int slot);
    void recallCompareSlot(int slot);
    void captureCurrentConfig(PluginConfig& outConfig) const;
    float getMix() const;
    float getBypassTarget() const;
    void applyFactoryPreset(int index);
    void setFloatParamHost(const char* id, float value);
    void setInternalParamValue(const char* id, float value);
    void syncAdvancedParamsInternal(const DimensionParams& p);
    void publishConfigToAudio(const PluginConfig& cfg);

    mutable std::recursive_mutex controlMutex;

    DimensionDSP dsp;
    std::array<float, DIMENSION_MAX_BLOCK_SIZE> inL {};
    std::array<float, DIMENSION_MAX_BLOCK_SIZE> inR {};
    std::array<float, DIMENSION_MAX_BLOCK_SIZE> outL {};
    std::array<float, DIMENSION_MAX_BLOCK_SIZE> outR {};
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
    std::atomic<float> currentSampleRate { DIMENSION_SAMPLE_RATE_DEFAULT };

    dimension_ipc::BoundedMPSCQueue<PluginConfig, 64> configQueue;
    dimension_ipc::FallbackMailbox<PluginConfig> fallbackMailbox;
    dimension_ipc::SnapshotBuffer<PluginConfig, 8> appliedSnapshotBuffer;
    std::atomic<uint32_t> saturationCount { 0 };

    std::atomic<int> lastPresetMode { (int)DIMENSION_MODE_I };
    std::atomic<int> effectiveMode { (int)DIMENSION_MODE_I };
    std::atomic<uint64_t> stateGeneration { 1 };
    std::atomic<uint64_t> pendingModeSyncGeneration { 0 };
    std::atomic<int> pendingHostMode { -1 };
    std::atomic<uint64_t> pendingPresetSyncGeneration { 0 };
    std::atomic<int> pendingPresetSyncMode { -1 };
    std::atomic<bool> isInternalPresentationSync { false };
    std::atomic<bool> isBatchRestoring { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Dimension5AudioProcessor)
};
