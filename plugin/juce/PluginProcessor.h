#pragma once

#include <array>
#include <atomic>

#include <JuceHeader.h>

extern "C" {
#include "dimension_dsp.h"
}

class Dimension5AudioProcessor final : public juce::AudioProcessor {
public:
    using ValueTreeState = juce::AudioProcessorValueTreeState;

    Dimension5AudioProcessor();
    ~Dimension5AudioProcessor() override = default;

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

private:
    void captureCompareSlot(int slot);
    void recallCompareSlot(int slot);
    void syncParametersToDsp();
    float getFloatParam(const char* id) const;
    float getMix() const;
    float getBypassTarget() const;
    void applyFactoryPreset(int index);
    void setFloatParam(const char* id, float value);

    DimensionDSP dsp;
    std::array<float, DIMENSION_MAX_BLOCK_SIZE> inL {};
    std::array<float, DIMENSION_MAX_BLOCK_SIZE> inR {};
    std::array<float, DIMENSION_MAX_BLOCK_SIZE> outL {};
    std::array<float, DIMENSION_MAX_BLOCK_SIZE> outR {};
    std::atomic<float> outputMeterLeft { 0.0f };
    std::atomic<float> outputMeterRight { 0.0f };
    float bypassEffectLevel = 1.0f;
    float bypassSmoothCoeff = 1.0f;
    std::array<juce::ValueTree, 2> compareStates;
    int currentMode = DIMENSION_MODE_I;
    int currentProgram = 0;
    int activeCompareSlot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Dimension5AudioProcessor)
};
