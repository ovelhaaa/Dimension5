#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {
constexpr const char* kModeParamId = "mode";

juce::StringArray modeChoices() {
    return { "I", "II", "III", "IV", "Custom" };
}

juce::NormalisableRange<float> rangeFor(const DimensionParamDescriptor& desc) {
    return { desc.minValue, desc.maxValue };
}
}

Dimension5AudioProcessor::Dimension5AudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMETERS", createParameterLayout()) {
    Dimension_Init(&dsp, DIMENSION_SAMPLE_RATE_DEFAULT);
}

void Dimension5AudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    juce::ignoreUnused(samplesPerBlock);
    Dimension_Init(&dsp, (float)sampleRate);
    syncParametersToDsp();
}

void Dimension5AudioProcessor::releaseResources() {
}

bool Dimension5AudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& mainIn = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();
    return mainIn == mainOut
        && (mainOut == juce::AudioChannelSet::mono() || mainOut == juce::AudioChannelSet::stereo());
}

void Dimension5AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    syncParametersToDsp();

    const int totalFrames = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();
    int pos = 0;

    while (pos < totalFrames) {
        const int n = juce::jmin<int>(DIMENSION_MAX_BLOCK_SIZE, totalFrames - pos);
        const float* srcL = buffer.getReadPointer(0, pos);
        const float* srcR = (channels > 1) ? buffer.getReadPointer(1, pos) : srcL;

        for (int i = 0; i < n; ++i) {
            inL[(size_t)i] = srcL[i];
            inR[(size_t)i] = srcR[i];
        }

        Dimension_ProcessBlock(&dsp, inL.data(), inR.data(), outL.data(), outR.data(), (uint32_t)n);

        float* dstL = buffer.getWritePointer(0, pos);
        float* dstR = (channels > 1) ? buffer.getWritePointer(1, pos) : nullptr;
        for (int i = 0; i < n; ++i) {
            dstL[i] = outL[(size_t)i];
            if (dstR != nullptr) {
                dstR[i] = outR[(size_t)i];
            }
        }

        pos += n;
    }
}

juce::AudioProcessorEditor* Dimension5AudioProcessor::createEditor() {
    return new Dimension5AudioProcessorEditor(*this);
}

bool Dimension5AudioProcessor::hasEditor() const {
    return true;
}

const juce::String Dimension5AudioProcessor::getName() const {
    return JucePlugin_Name;
}

bool Dimension5AudioProcessor::acceptsMidi() const {
    return false;
}

bool Dimension5AudioProcessor::producesMidi() const {
    return false;
}

bool Dimension5AudioProcessor::isMidiEffect() const {
    return false;
}

double Dimension5AudioProcessor::getTailLengthSeconds() const {
    return DIMENSION_DELAY_MAX_MS * 0.001;
}

int Dimension5AudioProcessor::getNumPrograms() {
    return 1;
}

int Dimension5AudioProcessor::getCurrentProgram() {
    return 0;
}

void Dimension5AudioProcessor::setCurrentProgram(int index) {
    juce::ignoreUnused(index);
}

const juce::String Dimension5AudioProcessor::getProgramName(int index) {
    juce::ignoreUnused(index);
    return {};
}

void Dimension5AudioProcessor::changeProgramName(int index, const juce::String& newName) {
    juce::ignoreUnused(index, newName);
}

void Dimension5AudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    if (auto state = parameters.copyState(); state.isValid()) {
        std::unique_ptr<juce::XmlElement> xml(state.createXml());
        copyXmlToBinary(*xml, destData);
    }
}

void Dimension5AudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName(parameters.state.getType())) {
        parameters.replaceState(juce::ValueTree::fromXml(*xml));
        syncParametersToDsp();
    }
}

Dimension5AudioProcessor::ValueTreeState::ParameterLayout Dimension5AudioProcessor::createParameterLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(kModeParamId, 1), "Mode", modeChoices(), DIMENSION_MODE_I));

    for (uint32_t i = 0; i < (uint32_t)DIMENSION_PARAM_COUNT; ++i) {
        const DimensionParamDescriptor* desc = Dimension_GetParamDescriptor((DimensionParamId)i);
        if (desc == nullptr) {
            continue;
        }

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(desc->stableId, 1),
            desc->displayName,
            rangeFor(*desc),
            desc->defaultValue,
            juce::String(),
            juce::AudioProcessorParameter::genericParameter,
            nullptr,
            nullptr));
    }

    return { params.begin(), params.end() };
}

void Dimension5AudioProcessor::syncParametersToDsp() {
    const auto modeValue = parameters.getRawParameterValue(kModeParamId);
    const int nextMode = modeValue != nullptr ? juce::roundToInt(modeValue->load()) : DIMENSION_MODE_I;

    if (nextMode != currentMode) {
        currentMode = juce::jlimit((int)DIMENSION_MODE_I, (int)DIMENSION_MODE_CUSTOM, nextMode);
        Dimension_SetMode(&dsp, (DimensionMode)currentMode);
    }

    if (currentMode == DIMENSION_MODE_CUSTOM) {
        DimensionParams p;
        Dimension_GetParams(&dsp, &p);
        p.mode = DIMENSION_MODE_CUSTOM;
        p.inputGain = getFloatParam("inputGain");
        p.outputGain = getFloatParam("outputGain");
        p.dryGain = getFloatParam("dryGain");
        p.wetDirectGain = getFloatParam("wetDirectGain");
        p.wetCrossGain = getFloatParam("wetCrossGain");
        p.baseDelayMs = getFloatParam("baseDelayMs");
        p.depthMs = getFloatParam("depthMs");
        p.rateHz = getFloatParam("rateHz");
        p.hpfHz = getFloatParam("hpfHz");
        p.lpfHz = getFloatParam("lpfHz");
        p.analogAmount = getFloatParam("analogAmount");
        p.companderAmount = getFloatParam("companderAmount");
        p.width = getFloatParam("width");
        Dimension_SetParams(&dsp, &p);
    } else {
        DimensionParams p;
        Dimension_GetParams(&dsp, &p);
        p.inputGain = getFloatParam("inputGain");
        p.outputGain = getFloatParam("outputGain");
        p.width = getFloatParam("width");
        Dimension_SetParams(&dsp, &p);
    }
}

float Dimension5AudioProcessor::getFloatParam(const char* id) const {
    const auto* value = parameters.getRawParameterValue(id);
    return value != nullptr ? value->load() : 0.0f;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new Dimension5AudioProcessor();
}
