#pragma once

#include <array>
#include <atomic>
#include <mutex>

#include <JuceHeader.h>

extern "C" {
#include "dimension_dsp.h"
}

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
    bool isUpdatingModePreset() const { return isSyncingModePreset.load(std::memory_order_relaxed); }
    void getEffectiveConfig(PluginConfig& outConfig) const;
    void getLastAppliedConfig(PluginConfig& outConfig) const;
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
    void setFloatParam(const char* id, float value);

    mutable std::mutex controlMutex;

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
    PluginConfig lastAppliedConfig {};
    bool hasActiveConfig = false;
    std::atomic<float> currentSampleRate { DIMENSION_SAMPLE_RATE_DEFAULT };

    mutable std::atomic<uint32_t> presetUpdateSeq { 0 };
    mutable std::atomic<uint32_t> appliedConfigSeq { 0 };
    std::atomic<int> batchDepth { 0 };
    std::atomic<int> lastPresetMode { (int)DIMENSION_MODE_I };
    std::atomic<int> effectiveMode { (int)DIMENSION_MODE_I };
    std::atomic<bool> isSyncingModePreset { false };
    std::atomic<bool> isBatchRestoring { false };
    std::atomic<bool> modeNeedsSyncToCustom { false };
    std::atomic<bool> presetNeedsSyncToParams { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Dimension5AudioProcessor)
};
