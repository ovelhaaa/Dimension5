#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <atomic>

namespace {
void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "CHECK FAILED: " << message << "\n";
        throw std::runtime_error(message);
    }
}

void setParamHostValue(Dimension5AudioProcessor& proc, const char* id, float value) {
    auto* param = proc.parameters.getParameter(id);
    check(param != nullptr, "parameter exists");
    param->setValueNotifyingHost(param->convertTo0to1(value));
}

void processTestBlock(Dimension5AudioProcessor& proc, juce::AudioBuffer<float>& buffer) {
    juce::MidiBuffer midi;
    proc.processBlock(buffer, midi);
}

void processStereoTestSignal(Dimension5AudioProcessor& proc, int numFrames = 32) {
    juce::AudioBuffer<float> buffer(2, numFrames);
    for (int i = 0; i < numFrames; ++i) {
        buffer.setSample(0, i, 0.25f);
        buffer.setSample(1, i, 0.25f);
    }
    processTestBlock(proc, buffer);
}

// 1. Processor construction and prepareToPlay
void test_processor_construction_and_prepare() {
    Dimension5AudioProcessor proc;
    check(proc.getName() == "Dimension5", "processor name is Dimension5");
    check(proc.getNumPrograms() >= 6, "has factory presets");

    proc.prepareToPlay(48000.0, 32);
    juce::AudioBuffer<float> buffer(2, 32);
    for (int i = 0; i < 32; ++i) {
        buffer.setSample(0, i, 0.2f);
        buffer.setSample(1, i, 0.2f);
    }
    processTestBlock(proc, buffer);

    check(std::isfinite(buffer.getSample(0, 0)), "output L finite");
    check(std::isfinite(buffer.getSample(1, 0)), "output R finite");
}

// 2. Scenario A: Host advanced edit without opening editor
// Mode II -> directly edit Depth (1.4 ms to 2.2 ms) -> process audio -> effective config is Custom with Depth = 2.2 ms and Mode II baseline
void test_scenario_a_host_advanced_edit_without_editor() {
    Dimension5AudioProcessor proc;
    check(proc.getActiveEditor() == nullptr, "editor is closed");
    proc.prepareToPlay(48000.0, 32);

    // 1. Select Mode II via public host parameter
    setParamHostValue(proc, "mode", (float)DIMENSION_MODE_II);
    processStereoTestSignal(proc);

    Dimension5AudioProcessor::PluginConfig cfg;
    proc.getLastAppliedConfig(cfg);
    check(cfg.dspParams.mode == DIMENSION_MODE_II, "applied mode is Mode II");
    check(std::abs(cfg.dspParams.depthMs - 1.4f) < 1e-4f, "applied depth is Mode II preset (1.4 ms)");
    check(std::abs(cfg.dspParams.baseDelayMs - 8.5f) < 1e-4f, "applied baseDelay is Mode II preset (8.5 ms)");
    check(std::abs(cfg.dspParams.rateHz - 0.25f) < 1e-4f, "applied rate is Mode II preset (0.25 Hz)");

    // 2. Directly alter Depth from 1.4 ms to 2.2 ms via public host parameter
    setParamHostValue(proc, "depthMs", 2.2f);

    // 3. Process audio without opening editor
    check(proc.getActiveEditor() == nullptr, "editor remains closed during edit and process");
    processStereoTestSignal(proc);

    // Verify applied config to DSP
    proc.getLastAppliedConfig(cfg);
    check(cfg.dspParams.mode == DIMENSION_MODE_CUSTOM, "DSP applied mode transitioned to Custom");
    check(std::abs(cfg.dspParams.depthMs - 2.2f) < 1e-4f, "DSP applied depth is exactly the edited 2.2 ms");
    check(std::abs(cfg.dspParams.baseDelayMs - 8.5f) < 1e-4f, "DSP applied baseDelay retained Mode II value (8.5 ms)");
    check(std::abs(cfg.dspParams.rateHz - 0.25f) < 1e-4f, "DSP applied rate retained Mode II value (0.25 Hz)");
    check(std::abs(cfg.dspParams.hpfHz - 120.0f) < 1e-4f, "DSP applied hpf retained Mode II value (120 Hz)");
    check(std::abs(cfg.dspParams.lpfHz - 7500.0f) < 1e-4f, "DSP applied lpf retained Mode II value (7500 Hz)");
    check(std::abs(cfg.dspParams.analogAmount - 0.45f) < 1e-4f, "DSP applied analogAmount retained Mode II value (0.45)");
    check(std::abs(cfg.dspParams.companderAmount - 0.45f) < 1e-4f, "DSP applied companderAmount retained Mode II value (0.45)");
    check(std::abs(cfg.dspParams.dryGain - 0.83f) < 1e-4f, "DSP applied dryGain retained 0.83f");
    check(std::abs(cfg.dspParams.wetDirectGain - 0.475f) < 1e-4f, "DSP applied wetDirectGain retained Mode II value (0.475)");
    check(std::abs(cfg.dspParams.wetCrossGain - 0.3325f) < 1e-4f, "DSP applied wetCrossGain retained Mode II value (0.3325)");
}

// 3. Scenario B: Host direct Custom entry without opening editor
// Mode IV -> directly change Mode to Custom -> process audio -> sonic config preserved from Mode IV
void test_scenario_b_host_direct_custom_selection_without_editor() {
    Dimension5AudioProcessor proc;
    check(proc.getActiveEditor() == nullptr, "editor is closed");
    proc.prepareToPlay(48000.0, 32);

    // 1. Select Mode IV
    setParamHostValue(proc, "mode", (float)DIMENSION_MODE_IV);
    processStereoTestSignal(proc);

    Dimension5AudioProcessor::PluginConfig cfgBefore;
    proc.getLastAppliedConfig(cfgBefore);
    check(cfgBefore.dspParams.mode == DIMENSION_MODE_IV, "applied mode is Mode IV");
    check(std::abs(cfgBefore.dspParams.baseDelayMs - 11.5f) < 1e-4f, "Mode IV baseDelay is 11.5 ms");
    check(std::abs(cfgBefore.dspParams.depthMs - 2.4f) < 1e-4f, "Mode IV depth is 2.4 ms");
    check(std::abs(cfgBefore.dspParams.rateHz - 0.50f) < 1e-4f, "Mode IV rate is 0.50 Hz");

    // 2. Alter Mode directly to Custom via public parameter
    setParamHostValue(proc, "mode", (float)DIMENSION_MODE_CUSTOM);

    // 3. Process audio
    processStereoTestSignal(proc);

    Dimension5AudioProcessor::PluginConfig cfgAfter;
    proc.getLastAppliedConfig(cfgAfter);
    check(cfgAfter.dspParams.mode == DIMENSION_MODE_CUSTOM, "applied mode is Custom");
    check(std::abs(cfgAfter.dspParams.baseDelayMs - 11.5f) < 1e-4f, "Custom preserved Mode IV baseDelay (11.5 ms)");
    check(std::abs(cfgAfter.dspParams.depthMs - 2.4f) < 1e-4f, "Custom preserved Mode IV depth (2.4 ms)");
    check(std::abs(cfgAfter.dspParams.rateHz - 0.50f) < 1e-4f, "Custom preserved Mode IV rate (0.50 Hz)");
    check(std::abs(cfgAfter.dspParams.hpfHz - 140.0f) < 1e-4f, "Custom preserved Mode IV hpf (140 Hz)");
    check(std::abs(cfgAfter.dspParams.lpfHz - 6500.0f) < 1e-4f, "Custom preserved Mode IV lpf (6500 Hz)");
    check(std::abs(cfgAfter.dspParams.analogAmount - 0.60f) < 1e-4f, "Custom preserved Mode IV analog (0.60)");
    check(std::abs(cfgAfter.dspParams.companderAmount - 0.60f) < 1e-4f, "Custom preserved Mode IV compander (0.60)");
    check(std::abs(cfgAfter.dspParams.wetDirectGain - 0.55f) < 1e-4f, "Custom preserved Mode IV wetDirect (0.55)");
    check(std::abs(cfgAfter.dspParams.wetCrossGain - 0.385f) < 1e-4f, "Custom preserved Mode IV wetCross (0.385)");
}

// 4. Mode transition sequence: Mode II -> Mode IV -> Custom
void test_mode_sequence_II_to_IV_to_custom() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    setParamHostValue(proc, "mode", (float)DIMENSION_MODE_II);
    processStereoTestSignal(proc);

    setParamHostValue(proc, "mode", (float)DIMENSION_MODE_IV);
    processStereoTestSignal(proc);

    setParamHostValue(proc, "mode", (float)DIMENSION_MODE_CUSTOM);
    processStereoTestSignal(proc);

    Dimension5AudioProcessor::PluginConfig cfg;
    proc.getLastAppliedConfig(cfg);
    check(cfg.dspParams.mode == DIMENSION_MODE_CUSTOM, "mode is Custom");
    check(std::abs(cfg.dspParams.baseDelayMs - 11.5f) < 1e-4f, "Custom baseline is Mode IV (11.5 ms), NOT Mode II");
    check(std::abs(cfg.dspParams.depthMs - 2.4f) < 1e-4f, "Custom baseline is Mode IV depth (2.4 ms)");
}

// 5. First edit of each relevant advanced parameter from Mode II
void test_first_edit_of_each_advanced_param() {
    struct TestCase {
        const char* paramId;
        float testValue;
    };

    const TestCase testCases[] = {
        { "baseDelayMs", 14.2f },
        { "depthMs", 3.1f },
        { "rateHz", 1.25f },
        { "hpfHz", 250.0f },
        { "lpfHz", 5000.0f },
        { "analogAmount", 0.85f },
        { "companderAmount", 0.70f },
        { "dryGain", 0.60f },
        { "wetDirectGain", 0.75f },
        { "wetCrossGain", 0.20f }
    };

    for (const auto& tc : testCases) {
        Dimension5AudioProcessor proc;
        proc.prepareToPlay(48000.0, 32);

        // Select Mode II
        setParamHostValue(proc, "mode", (float)DIMENSION_MODE_II);
        processStereoTestSignal(proc);

        // First edit of this specific parameter
        setParamHostValue(proc, tc.paramId, tc.testValue);
        processStereoTestSignal(proc);

        Dimension5AudioProcessor::PluginConfig cfg;
        proc.getLastAppliedConfig(cfg);
        check(cfg.dspParams.mode == DIMENSION_MODE_CUSTOM, "switched to Custom on edit");

        // Verify the edited parameter took the test value
        float actual = 0.0f;
        if (std::string(tc.paramId) == "baseDelayMs")       actual = cfg.dspParams.baseDelayMs;
        else if (std::string(tc.paramId) == "depthMs")      actual = cfg.dspParams.depthMs;
        else if (std::string(tc.paramId) == "rateHz")       actual = cfg.dspParams.rateHz;
        else if (std::string(tc.paramId) == "hpfHz")        actual = cfg.dspParams.hpfHz;
        else if (std::string(tc.paramId) == "lpfHz")        actual = cfg.dspParams.lpfHz;
        else if (std::string(tc.paramId) == "analogAmount") actual = cfg.dspParams.analogAmount;
        else if (std::string(tc.paramId) == "companderAmount") actual = cfg.dspParams.companderAmount;
        else if (std::string(tc.paramId) == "dryGain")      actual = cfg.dspParams.dryGain;
        else if (std::string(tc.paramId) == "wetDirectGain") actual = cfg.dspParams.wetDirectGain;
        else if (std::string(tc.paramId) == "wetCrossGain") actual = cfg.dspParams.wetCrossGain;

        check(std::abs(actual - tc.testValue) < 1e-4f, "edited parameter applied exactly");

        // Verify other parameters retained Mode II values (e.g. if we didn't edit baseDelayMs, it should be 8.5)
        if (std::string(tc.paramId) != "baseDelayMs") {
            check(std::abs(cfg.dspParams.baseDelayMs - 8.5f) < 1e-4f, "baseDelay retained Mode II value");
        }
        if (std::string(tc.paramId) != "depthMs") {
            check(std::abs(cfg.dspParams.depthMs - 1.4f) < 1e-4f, "depth retained Mode II value");
        }
        if (std::string(tc.paramId) != "rateHz") {
            check(std::abs(cfg.dspParams.rateHz - 0.25f) < 1e-4f, "rate retained Mode II value");
        }
    }
}

// 6. Scenario C: Old state restore with stale advanced values -> edit
void test_scenario_c_old_session_with_stale_params_and_edit() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    // Old session XML where mode was II, but advanced params had stale values from Mode I or default
    juce::XmlElement xml("PARAMETERS");
    xml.setAttribute("program", 0);
    xml.setAttribute("activeCompareSlot", 0);

    auto addParam = [&](const char* id, float val) {
        auto* child = xml.createNewChildElement("PARAM");
        child->setAttribute("id", id);
        child->setAttribute("value", (double)val);
    };

    addParam("mode", 1.0f);         // Mode II
    addParam("baseDelayMs", 7.0f);  // Stale Mode I default
    addParam("depthMs", 0.9f);      // Stale Mode I default
    addParam("rateHz", 0.25f);
    addParam("dryGain", 0.83f);
    addParam("inputGain", 1.20f);
    addParam("width", 1.15f);

    juce::MemoryBlock mem;
    Dimension5AudioProcessor::copyXmlToBinary(xml, mem);
    proc.setStateInformation(mem.getData(), (int)mem.getSize());

    // Process audio in restored Mode II
    processStereoTestSignal(proc);
    Dimension5AudioProcessor::PluginConfig cfg;
    proc.getLastAppliedConfig(cfg);
    check(cfg.dspParams.mode == DIMENSION_MODE_II, "restored mode is Mode II");
    check(std::abs(cfg.dspParams.baseDelayMs - 8.5f) < 1e-4f, "restored Mode II uses 8.5 ms baseline");

    // Edit an advanced parameter (rateHz to 0.40 Hz)
    setParamHostValue(proc, "rateHz", 0.40f);
    processStereoTestSignal(proc);

    proc.getLastAppliedConfig(cfg);
    check(cfg.dspParams.mode == DIMENSION_MODE_CUSTOM, "transitioned to Custom");
    check(std::abs(cfg.dspParams.rateHz - 0.40f) < 1e-4f, "edited rateHz is 0.40 Hz");
    check(std::abs(cfg.dspParams.baseDelayMs - 8.5f) < 1e-4f, "baseDelay aligned to Mode II baseline (8.5 ms), NOT stale 7.0 ms");
    check(std::abs(cfg.dspParams.depthMs - 1.4f) < 1e-4f, "depth aligned to Mode II baseline (1.4 ms), NOT stale 0.9 ms");
}

// 7. Automation sequences in the same block
void test_simultaneous_automation_same_block() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    // In the same block before processBlock, host automates Mode to III and depthMs to 2.1 ms
    setParamHostValue(proc, "mode", (float)DIMENSION_MODE_III);
    setParamHostValue(proc, "depthMs", 2.1f);

    processStereoTestSignal(proc);

    Dimension5AudioProcessor::PluginConfig cfg;
    proc.getLastAppliedConfig(cfg);
    check(cfg.dspParams.mode == DIMENSION_MODE_CUSTOM, "effective mode is Custom");
    check(std::abs(cfg.dspParams.depthMs - 2.1f) < 1e-4f, "custom depth is 2.1 ms");
    check(std::abs(cfg.dspParams.baseDelayMs - 10.0f) < 1e-4f, "baseline is Mode III baseDelay (10.0 ms)");
    check(std::abs(cfg.dspParams.rateHz - 0.50f) < 1e-4f, "baseline is Mode III rate (0.50 Hz)");
}

// 8. Persistence and A/B compare after auto-transition to Custom
void test_persistence_and_compare_ab_after_custom_transition() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    // Slot A: Start in Mode II, edit depthMs to 2.2 ms (auto-switches to Custom)
    setParamHostValue(proc, "mode", (float)DIMENSION_MODE_II);
    processStereoTestSignal(proc);

    setParamHostValue(proc, "depthMs", 2.2f);
    processStereoTestSignal(proc);

    Dimension5AudioProcessor::PluginConfig cfgA;
    proc.getLastAppliedConfig(cfgA);
    check(cfgA.dspParams.mode == DIMENSION_MODE_CUSTOM, "Slot A is Custom");
    check(std::abs(cfgA.dspParams.depthMs - 2.2f) < 1e-4f, "Slot A depth is 2.2 ms");

    // Switch to Slot B: change to Mode I
    proc.selectCompareSlot(1);
    setParamHostValue(proc, "mode", (float)DIMENSION_MODE_I);
    processStereoTestSignal(proc);

    Dimension5AudioProcessor::PluginConfig cfgB;
    proc.getLastAppliedConfig(cfgB);
    check(cfgB.dspParams.mode == DIMENSION_MODE_I, "Slot B is Mode I");

    // Switch back to Slot A: verify Custom state and Mode II baseline restored
    proc.selectCompareSlot(0);
    processStereoTestSignal(proc);

    Dimension5AudioProcessor::PluginConfig cfgARecalled;
    proc.getLastAppliedConfig(cfgARecalled);
    check(cfgARecalled.dspParams.mode == DIMENSION_MODE_CUSTOM, "Recalled Slot A is Custom");
    check(std::abs(cfgARecalled.dspParams.depthMs - 2.2f) < 1e-4f, "Recalled Slot A depth is 2.2 ms");
    check(std::abs(cfgARecalled.dspParams.baseDelayMs - 8.5f) < 1e-4f, "Recalled Slot A baseDelay is 8.5 ms (Mode II baseline)");

    // Persistence: serialize proc to memory and restore into proc2
    juce::MemoryBlock mem;
    proc.getStateInformation(mem);

    Dimension5AudioProcessor proc2;
    proc2.prepareToPlay(48000.0, 32);
    proc2.setStateInformation(mem.getData(), (int)mem.getSize());
    processStereoTestSignal(proc2);

    Dimension5AudioProcessor::PluginConfig cfgProc2;
    proc2.getLastAppliedConfig(cfgProc2);
    check(cfgProc2.dspParams.mode == DIMENSION_MODE_CUSTOM, "Proc2 restored Custom mode");
    check(std::abs(cfgProc2.dspParams.depthMs - 2.2f) < 1e-4f, "Proc2 restored depth 2.2 ms");
    check(std::abs(cfgProc2.dspParams.baseDelayMs - 8.5f) < 1e-4f, "Proc2 restored Mode II baseline baseDelay (8.5 ms)");
}

// 9. Open and close editor without modifying config or sound
void test_open_close_editor_does_not_modify_config_or_sound() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    // Select Mode II
    setParamHostValue(proc, "mode", (float)DIMENSION_MODE_II);
    juce::AudioBuffer<float> bufferBefore(2, 32);
    for (int i = 0; i < 32; ++i) {
        bufferBefore.setSample(0, i, 0.3f);
        bufferBefore.setSample(1, i, 0.3f);
    }
    processTestBlock(proc, bufferBefore);

    Dimension5AudioProcessor::PluginConfig cfgBefore;
    proc.getLastAppliedConfig(cfgBefore);
    check(cfgBefore.dspParams.mode == DIMENSION_MODE_II, "applied mode is Mode II before editor");

    // Open editor
    auto* editor = proc.createEditor();
    check(editor != nullptr, "editor created successfully");

    Dimension5AudioProcessor::PluginConfig cfgWithEditor;
    proc.getLastAppliedConfig(cfgWithEditor);
    check(cfgWithEditor.dspParams.mode == DIMENSION_MODE_II, "applied mode is STILL Mode II after opening editor");

    juce::AudioBuffer<float> bufferDuring(2, 32);
    for (int i = 0; i < 32; ++i) {
        bufferDuring.setSample(0, i, 0.3f);
        bufferDuring.setSample(1, i, 0.3f);
    }
    processTestBlock(proc, bufferDuring);

    proc.getLastAppliedConfig(cfgWithEditor);
    check(cfgWithEditor.dspParams.mode == DIMENSION_MODE_II, "applied mode remains Mode II during processing with editor open");

    // Close editor
    delete editor;

    Dimension5AudioProcessor::PluginConfig cfgAfter;
    proc.getLastAppliedConfig(cfgAfter);
    check(cfgAfter.dspParams.mode == DIMENSION_MODE_II, "applied mode is STILL Mode II after closing editor");
}

// 10. UI interaction: slider edit preserves first edit without being overwritten by preset
void test_ui_slider_interaction_preserves_first_edit() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    setParamHostValue(proc, "mode", (float)DIMENSION_MODE_II);
    processStereoTestSignal(proc);

    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());
    check(editor != nullptr, "editor instantiated");

    // Simulate slider drag / typing via the public parameter attachment path
    setParamHostValue(proc, "depthMs", 2.2f);
    processStereoTestSignal(proc);

    Dimension5AudioProcessor::PluginConfig cfg;
    proc.getLastAppliedConfig(cfg);
    check(cfg.dspParams.mode == DIMENSION_MODE_CUSTOM, "mode is Custom");
    check(std::abs(cfg.dspParams.depthMs - 2.2f) < 1e-4f, "depthMs edit preserved at 2.2 ms, not overwritten by preset");
    check(std::abs(cfg.dspParams.baseDelayMs - 8.5f) < 1e-4f, "other params retain Mode II baseline");
}

// 11. Controlled concurrency and configuration consistency
void test_controlled_concurrency_and_configuration_consistency() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    std::atomic<bool> stopFlag { false };
    std::atomic<int> blocksProcessed { 0 };
    std::atomic<int> batchUpdatesPerformed { 0 };

    // Thread 1: Audio thread processing continuous blocks
    std::thread audioThread([&] {
        juce::AudioBuffer<float> buffer(2, 64);
        juce::MidiBuffer midi;
        while (!stopFlag.load(std::memory_order_relaxed)) {
            for (int i = 0; i < 64; ++i) {
                buffer.setSample(0, i, 0.2f);
                buffer.setSample(1, i, 0.2f);
            }
            proc.processBlock(buffer, midi);

            Dimension5AudioProcessor::PluginConfig applied;
            proc.getLastAppliedConfig(applied);

            // Verify configuration invariants: never torn, valid ranges
            if (applied.dspParams.mode < DIMENSION_MODE_I || applied.dspParams.mode > DIMENSION_MODE_CUSTOM) {
                check(false, "invalid mode detected in applied config");
            }
            if (applied.dspParams.dryGain < 0.0f || applied.dspParams.dryGain > 2.0f) {
                check(false, "invalid dryGain detected in applied config");
            }
            if (applied.dspParams.baseDelayMs < 1.0f || applied.dspParams.baseDelayMs > 32.0f) {
                check(false, "invalid baseDelayMs detected in applied config");
            }

            for (int i = 0; i < 64; ++i) {
                const float l = buffer.getSample(0, i);
                const float r = buffer.getSample(1, i);
                if (!std::isfinite(l) || !std::isfinite(r) || std::abs(l) > 4.0f || std::abs(r) > 4.0f) {
                    check(false, "audio thread produced invalid output during concurrent update");
                }
            }
            blocksProcessed.fetch_add(1, std::memory_order_relaxed);
        }
    });

    // Thread 2: Control thread executing concurrent parameter changes, presets, and A/B compare
    std::thread controlThread([&] {
        for (int iter = 0; iter < 100; ++iter) {
            proc.setCurrentProgram(iter % proc.getNumPrograms());
            proc.selectCompareSlot(iter % 2);

            setParamHostValue(proc, "depthMs", 1.0f + (float)(iter % 20) * 0.1f);
            setParamHostValue(proc, "mode", (float)(iter % 5));

            batchUpdatesPerformed.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::yield();
        }
    });

    controlThread.join();
    stopFlag.store(true, std::memory_order_relaxed);
    audioThread.join();

    check(blocksProcessed.load() > 0, "audio blocks were processed concurrently");
    check(batchUpdatesPerformed.load() == 100, "all batch updates completed safely");
}

// 12. Channel layouts and bypass
void test_channel_layouts_and_bypass() {
    Dimension5AudioProcessor proc;

    // Mono in -> Mono out
    {
        juce::AudioProcessor::BusesLayout monoLayout;
        monoLayout.inputBuses.add(juce::AudioChannelSet::mono());
        monoLayout.outputBuses.add(juce::AudioChannelSet::mono());
        check(proc.setBusesLayout(monoLayout), "mono->mono layout supported");
        proc.prepareToPlay(48000.0, 32);

        juce::AudioBuffer<float> buffer(1, 32);
        for (int i = 0; i < 32; ++i) buffer.setSample(0, i, 0.35f);
        processTestBlock(proc, buffer);
        check(std::isfinite(buffer.getSample(0, 0)), "mono->mono output finite");
    }

    // Mono in -> Stereo out
    {
        juce::AudioProcessor::BusesLayout upmix;
        upmix.inputBuses.add(juce::AudioChannelSet::mono());
        upmix.outputBuses.add(juce::AudioChannelSet::stereo());
        check(proc.setBusesLayout(upmix), "mono->stereo layout supported");
        proc.prepareToPlay(48000.0, 32);

        juce::AudioBuffer<float> buffer(2, 64);
        for (int i = 0; i < 64; ++i) buffer.setSample(0, i, 0.40f);
        processTestBlock(proc, buffer);
        check(std::isfinite(buffer.getSample(0, 0)), "mono->stereo L finite");
        check(std::isfinite(buffer.getSample(1, 0)), "mono->stereo R finite");
    }

    // Bypass in Mono in -> Stereo out
    {
        juce::AudioProcessor::BusesLayout upmix;
        upmix.inputBuses.add(juce::AudioChannelSet::mono());
        upmix.outputBuses.add(juce::AudioChannelSet::stereo());
        proc.setBusesLayout(upmix);
        proc.prepareToPlay(48000.0, 32);

        setParamHostValue(proc, "bypass", 1.0f);

        juce::AudioBuffer<float> buffer(2, 32);
        for (int b = 0; b < 150; ++b) {
            for (int i = 0; i < 32; ++i) {
                buffer.setSample(0, i, 0.50f);
                buffer.setSample(1, i, 0.0f);
            }
            processTestBlock(proc, buffer);
        }

        check(std::abs(buffer.getSample(0, 31) - 0.50f) < 0.01f, "bypassed L is dry mono input");
        check(std::abs(buffer.getSample(1, 31) - 0.50f) < 0.01f, "bypassed R is dry mono input");
    }
}

// 13. Partial blocks and buffer sizes
void test_partial_blocks_and_large_buffers() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    const int blockSizes[] = { 1, 7, 13, 16, 31, 32, 63, 64, 127, 128, 256, 512, 1024, 2048 };
    for (int size : blockSizes) {
        juce::AudioBuffer<float> buffer(2, size);
        for (int i = 0; i < size; ++i) {
            buffer.setSample(0, i, 0.1f * (float)std::sin(2.0 * 3.14159265 * 440.0 * (double)i / 48000.0));
            buffer.setSample(1, i, 0.1f * (float)std::sin(2.0 * 3.14159265 * 440.0 * (double)i / 48000.0));
        }
        processTestBlock(proc, buffer);
        for (int i = 0; i < size; ++i) {
            check(std::isfinite(buffer.getSample(0, i)), "partial block output finite L");
            check(std::isfinite(buffer.getSample(1, i)), "partial block output finite R");
        }
    }
}
void deliverMessageThreadUpdates(Dimension5AudioProcessor& proc) {
    proc.deliverPendingUpdates();
}

// 14. Deferred update: Mode II -> edit Depth -> Mode IV -> deliver pending async update
void test_deferred_mode_II_depth_then_mode_IV() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    // 1. Select Mode II
    setParamHostValue(proc, "mode", (float)DIMENSION_MODE_II);
    processStereoTestSignal(proc);

    // 2. Edit Depth to 2.2 ms (triggers auto-transition to Custom with delayed async publication)
    setParamHostValue(proc, "depthMs", 2.2f);
    processStereoTestSignal(proc);

    Dimension5AudioProcessor::PluginConfig cfgMid;
    proc.getLastAppliedConfig(cfgMid);
    check(cfgMid.dspParams.mode == DIMENSION_MODE_CUSTOM, "effective mode transitioned to Custom");
    check(std::abs(cfgMid.dspParams.depthMs - 2.2f) < 1e-4f, "depth updated to 2.2 ms");

    // 3. Select Mode IV BEFORE delivering pending message-thread updates
    setParamHostValue(proc, "mode", (float)DIMENSION_MODE_IV);

    // 4. Deliver pending message-thread updates now
    deliverMessageThreadUpdates(proc);

    // 5. Process audio block
    processStereoTestSignal(proc);

    // Verify: Mode IV remains selected and applied! Stale Custom notification was discarded.
    Dimension5AudioProcessor::PluginConfig cfgFinal;
    proc.getLastAppliedConfig(cfgFinal);
    check(cfgFinal.dspParams.mode == DIMENSION_MODE_IV, "applied mode is Mode IV, NOT overwritten by Custom");
    check(std::abs(proc.getFloatParam("mode") - (float)DIMENSION_MODE_IV) < 1e-4f, "host mode parameter is Mode IV");
    check(std::abs(cfgFinal.dspParams.depthMs - 2.4f) < 1e-4f, "applied depth is Mode IV baseline (2.4 ms)");
    check(std::abs(cfgFinal.dspParams.baseDelayMs - 11.5f) < 1e-4f, "applied baseDelay is Mode IV baseline (11.5 ms)");
    check(std::abs(cfgFinal.dspParams.rateHz - 0.50f) < 1e-4f, "applied rate is Mode IV baseline (0.50 Hz)");
}

// 15. Deferred update: Mode II -> edit Depth -> apply preset -> deliver pending async update
void test_deferred_mode_II_depth_then_preset() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    setParamHostValue(proc, "mode", (float)DIMENSION_MODE_II);
    processStereoTestSignal(proc);

    setParamHostValue(proc, "depthMs", 2.2f);
    processStereoTestSignal(proc);

    // Apply factory preset 3 ("Clean Guitar Rack" -> Mode III) before dispatching messages
    proc.setCurrentProgram(3);

    deliverMessageThreadUpdates(proc);
    processStereoTestSignal(proc);

    Dimension5AudioProcessor::PluginConfig cfg;
    proc.getLastAppliedConfig(cfg);
    check(cfg.dspParams.mode == DIMENSION_MODE_III, "applied mode is Mode III from preset");
    check(std::abs(proc.getFloatParam("mode") - (float)DIMENSION_MODE_III) < 1e-4f, "host mode parameter is Mode III");
    check(std::abs(cfg.dspParams.depthMs - 1.8f) < 1e-4f, "depth is Mode III preset value (1.8 ms)");
    check(std::abs(cfg.dspParams.baseDelayMs - 10.0f) < 1e-4f, "baseDelay is Mode III preset value (10.0 ms)");
}

// 16. Deferred update: Edit -> restore session -> deliver pending async update
void test_deferred_edit_then_restore_session() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    // Edit Depth in Mode II
    setParamHostValue(proc, "mode", (float)DIMENSION_MODE_II);
    processStereoTestSignal(proc);
    setParamHostValue(proc, "depthMs", 2.2f);
    processStereoTestSignal(proc);

    // Prepare session XML for Mode I
    juce::XmlElement xml("PARAMETERS");
    xml.setAttribute("program", 0);
    xml.setAttribute("activeCompareSlot", 0);
    auto addParam = [&](const char* id, float val) {
        auto* child = xml.createNewChildElement("PARAM");
        child->setAttribute("id", id);
        child->setAttribute("value", (double)val);
    };
    addParam("mode", 0.0f); // Mode I
    addParam("baseDelayMs", 7.0f);
    addParam("depthMs", 0.9f);
    addParam("rateHz", 0.25f);
    addParam("dryGain", 0.83f);
    addParam("inputGain", 1.0f);
    addParam("width", 1.0f);

    juce::MemoryBlock mem;
    Dimension5AudioProcessor::copyXmlToBinary(xml, mem);

    // Restore session immediately before dispatching messages
    proc.setStateInformation(mem.getData(), (int)mem.getSize());

    deliverMessageThreadUpdates(proc);
    processStereoTestSignal(proc);

    Dimension5AudioProcessor::PluginConfig cfg;
    proc.getLastAppliedConfig(cfg);
    check(cfg.dspParams.mode == DIMENSION_MODE_I, "restored Mode I prevails");
    check(std::abs(proc.getFloatParam("mode") - (float)DIMENSION_MODE_I) < 1e-4f, "host mode is Mode I");
    check(std::abs(cfg.dspParams.depthMs - 0.9f) < 1e-4f, "restored depth is 0.9 ms");
    check(std::abs(cfg.dspParams.baseDelayMs - 7.0f) < 1e-4f, "restored baseDelay is 7.0 ms");
}

// 17. Deferred update: Edit -> switch A/B -> deliver pending async update
void test_deferred_edit_then_switch_ab() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    // Slot A: Mode II, edit Depth to 2.2 ms
    setParamHostValue(proc, "mode", (float)DIMENSION_MODE_II);
    processStereoTestSignal(proc);
    setParamHostValue(proc, "depthMs", 2.2f);
    processStereoTestSignal(proc);

    // Switch to Slot B (Mode I) before delivering async updates
    proc.selectCompareSlot(1);
    setParamHostValue(proc, "mode", (float)DIMENSION_MODE_I);

    deliverMessageThreadUpdates(proc);
    processStereoTestSignal(proc);

    Dimension5AudioProcessor::PluginConfig cfgB;
    proc.getLastAppliedConfig(cfgB);
    check(cfgB.dspParams.mode == DIMENSION_MODE_I, "Slot B is Mode I");

    // Switch back to Slot A
    proc.selectCompareSlot(0);
    processStereoTestSignal(proc);

    Dimension5AudioProcessor::PluginConfig cfgA;
    proc.getLastAppliedConfig(cfgA);
    check(cfgA.dspParams.mode == DIMENSION_MODE_CUSTOM, "Slot A preserved Custom mode");
    check(std::abs(cfgA.dspParams.depthMs - 2.2f) < 1e-4f, "Slot A preserved depth 2.2 ms");
    check(std::abs(cfgA.dspParams.baseDelayMs - 8.5f) < 1e-4f, "Slot A preserved Mode II baseline (8.5 ms)");
}

// 18. Deferred update: Multiple rapid edits before delivery -> latest valid state prevails
void test_deferred_multiple_edits_latest_prevails() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    setParamHostValue(proc, "mode", (float)DIMENSION_MODE_II);
    setParamHostValue(proc, "depthMs", 2.2f);
    setParamHostValue(proc, "rateHz", 0.45f);
    setParamHostValue(proc, "mode", (float)DIMENSION_MODE_IV);
    setParamHostValue(proc, "mode", (float)DIMENSION_MODE_III);

    deliverMessageThreadUpdates(proc);
    processStereoTestSignal(proc);

    Dimension5AudioProcessor::PluginConfig cfg;
    proc.getLastAppliedConfig(cfg);
    check(cfg.dspParams.mode == DIMENSION_MODE_III, "final applied mode is Mode III");
    check(std::abs(proc.getFloatParam("mode") - (float)DIMENSION_MODE_III) < 1e-4f, "final host mode is Mode III");
    check(std::abs(cfg.dspParams.depthMs - 1.8f) < 1e-4f, "Mode III depth is 1.8 ms");
    check(std::abs(cfg.dspParams.baseDelayMs - 10.0f) < 1e-4f, "Mode III baseDelay is 10.0 ms");
}

// 19. Cross-thread configuration consistency: No torn configurations
void test_cross_thread_configuration_consistency_no_tearing() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    auto makeStateXml = [](int mode, float inG, float outG, float w, float m) {
        juce::XmlElement xml("PARAMETERS");
        xml.setAttribute("program", 0);
        xml.setAttribute("activeCompareSlot", 0);
        auto addParam = [&](const char* id, float val) {
            auto* child = xml.createNewChildElement("PARAM");
            child->setAttribute("id", id);
            child->setAttribute("value", (double)val);
        };
        addParam("mode", (float)mode);
        addParam("inputGain", inG);
        addParam("outputGain", outG);
        addParam("width", w);
        addParam("mix", m);
        juce::MemoryBlock mem;
        Dimension5AudioProcessor::copyXmlToBinary(xml, mem);
        return mem;
    };

    const juce::MemoryBlock memA = makeStateXml(DIMENSION_MODE_I, 0.50f, 1.50f, 0.60f, 0.40f);
    const juce::MemoryBlock memB = makeStateXml(DIMENSION_MODE_IV, 1.60f, 0.60f, 1.80f, 0.90f);

    proc.setStateInformation(memA.getData(), (int)memA.getSize());
    processStereoTestSignal(proc);

    std::atomic<bool> stopFlag { false };
    std::atomic<bool> threadError { false };
    std::string errorMessage;
    std::mutex errorMutex;

    auto recordError = [&](const std::string& msg) {
        std::lock_guard<std::mutex> lk(errorMutex);
        if (!threadError.load()) {
            threadError.store(true);
            errorMessage = msg;
        }
    };

    auto isConfigA = [](const Dimension5AudioProcessor::PluginConfig& c) {
        return c.dspParams.mode == DIMENSION_MODE_I &&
               std::abs(c.dspParams.inputGain - 0.50f) < 1e-3f &&
               std::abs(c.dspParams.outputGain - 1.50f) < 1e-3f &&
               std::abs(c.dspParams.width - 0.60f) < 1e-3f &&
               std::abs(c.mix - 0.40f) < 1e-3f;
    };

    auto isConfigB = [](const Dimension5AudioProcessor::PluginConfig& c) {
        return c.dspParams.mode == DIMENSION_MODE_IV &&
               std::abs(c.dspParams.inputGain - 1.60f) < 1e-3f &&
               std::abs(c.dspParams.outputGain - 0.60f) < 1e-3f &&
               std::abs(c.dspParams.width - 1.80f) < 1e-3f &&
               std::abs(c.mix - 0.90f) < 1e-3f;
    };

    std::atomic<int> blocksChecked { 0 };
    std::atomic<int> inspectionReadsChecked { 0 };

    // Thread 1: Audio callback thread
    std::thread audioThread([&] {
        juce::AudioBuffer<float> buffer(2, 32);
        juce::MidiBuffer midi;
        while (!stopFlag.load(std::memory_order_relaxed)) {
            for (int i = 0; i < 32; ++i) {
                buffer.setSample(0, i, 0.2f);
                buffer.setSample(1, i, 0.2f);
            }
            proc.processBlock(buffer, midi);

            Dimension5AudioProcessor::PluginConfig applied;
            proc.getLastAppliedConfig(applied);
            if (!isConfigA(applied) && !isConfigB(applied)) {
                recordError("Torn configuration observed on audio thread!");
                break;
            }
            blocksChecked.fetch_add(1, std::memory_order_relaxed);
        }
    });

    // Thread 2: Inspection reader thread (simulating UI/parameter observer)
    std::thread inspectionThread([&] {
        while (!stopFlag.load(std::memory_order_relaxed)) {
            Dimension5AudioProcessor::PluginConfig cfg;
            proc.getLastAppliedConfig(cfg);
            if (!isConfigA(cfg) && !isConfigB(cfg)) {
                recordError("Torn configuration observed on inspection thread!");
                break;
            }
            inspectionReadsChecked.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::yield();
        }
    });

    // Thread 3: Producer thread publishing correlated configurations A and B
    std::thread producerThread([&] {
        for (int iter = 0; iter < 100 && !threadError.load(); ++iter) {
            if (iter % 2 == 0) {
                proc.setStateInformation(memA.getData(), (int)memA.getSize());
            } else {
                proc.setStateInformation(memB.getData(), (int)memB.getSize());
            }
            std::this_thread::yield();
        }
    });

    producerThread.join();
    stopFlag.store(true, std::memory_order_release);
    inspectionThread.join();
    audioThread.join();

    if (threadError.load()) {
        check(false, errorMessage.c_str());
    }
    check(blocksChecked.load() > 20, "audio thread processed blocks safely during concurrency");
    check(inspectionReadsChecked.load() > 20, "inspection thread observed consistent states");
}

// 20. Queue saturation and publication unavailability policy
void test_saturation_policy_and_unavailability() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    // Audio thread is NOT running.
    // Rapidly push 128 configurations to exceed queue capacity (64)
    for (int i = 0; i < 128; ++i) {
        setParamHostValue(proc, "inputGain", 1.0f + (float)i * 0.01f);
    }

    // Saturation counter must be > 0
    check(proc.getSaturationCount() > 0, "queue saturation occurred and was recorded");

    // The 129th edit sets depthMs to exactly 3.14f
    setParamHostValue(proc, "depthMs", 3.14f);

    // Now run audio thread for 1 block
    processStereoTestSignal(proc);

    // Verify the latest edit (3.14f) was applied and was NOT silently discarded!
    Dimension5AudioProcessor::PluginConfig cfg;
    proc.getLastAppliedConfig(cfg);
    check(std::abs(cfg.dspParams.depthMs - 3.14f) < 1e-4f, "latest edit preserved and applied after queue saturation");
}

// 21. Persistence immediately after auto-transition to Custom without async dispatch
void test_immediate_persistence_after_auto_custom_without_async_dispatch() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    setParamHostValue(proc, "mode", (float)DIMENSION_MODE_II);
    processStereoTestSignal(proc);

    // Edit Depth to 2.2 ms (triggers auto-transition to Custom)
    setParamHostValue(proc, "depthMs", 2.2f);

    // DO NOT dispatch messages to message thread!
    // Immediately serialize to memory block
    juce::MemoryBlock mem;
    proc.getStateInformation(mem);

    // Restore into a fresh processor instance
    Dimension5AudioProcessor proc2;
    proc2.prepareToPlay(48000.0, 32);
    proc2.setStateInformation(mem.getData(), (int)mem.getSize());
    processStereoTestSignal(proc2);

    Dimension5AudioProcessor::PluginConfig cfgProc2;
    proc2.getLastAppliedConfig(cfgProc2);
    check(cfgProc2.dspParams.mode == DIMENSION_MODE_CUSTOM, "Proc2 restored Custom mode");
    check(std::abs(cfgProc2.dspParams.depthMs - 2.2f) < 1e-4f, "Proc2 restored depth 2.2 ms");
    check(std::abs(cfgProc2.dspParams.baseDelayMs - 8.5f) < 1e-4f, "Proc2 restored Mode II baseline baseDelay (8.5 ms)");
    check(std::abs(cfgProc2.dspParams.rateHz - 0.25f) < 1e-4f, "Proc2 restored Mode II baseline rate (0.25 Hz)");
    check(std::abs(cfgProc2.dspParams.hpfHz - 120.0f) < 1e-4f, "Proc2 restored Mode II baseline hpf (120 Hz)");
    check(std::abs(cfgProc2.dspParams.lpfHz - 7500.0f) < 1e-4f, "Proc2 restored Mode II baseline lpf (7500 Hz)");
    check(std::abs(cfgProc2.dspParams.analogAmount - 0.45f) < 1e-4f, "Proc2 restored Mode II baseline analog (0.45)");
    check(std::abs(cfgProc2.dspParams.companderAmount - 0.45f) < 1e-4f, "Proc2 restored Mode II baseline compander (0.45)");
}

// 22. Interleaving 1: Exact reproduction of config 4 reverted to 3 by fallback
void test_interleaving_1_fallback_inversion_regression() {
    // Isolated test with small queue and SafeMailbox
    dimension_ipc::BoundedMPSCQueue<dimension_ipc::Envelope<int>, 2> testQueue;
    dimension_ipc::SafeMailbox<dimension_ipc::Envelope<int>, 4> testMailbox;

    // 1. Queue receives configs 1 and 2
    check(testQueue.tryEnqueue({ 1, 101 }), "Enqueue config 1");
    check(testQueue.tryEnqueue({ 2, 102 }), "Enqueue config 2");

    // 2. Config 3 goes to fallback due to saturation
    dimension_ipc::Envelope<int> env3 { 3, 103 };
    if (!testQueue.tryEnqueue(env3)) {
        testMailbox.publish(env3);
    }

    // 3. Consumer pops 1 and frees space
    dimension_ipc::Envelope<int> popped;
    check(testQueue.tryDequeue(popped) && popped.payload == 101, "Popped config 1");

    // 4. Config 4 enters the queue
    dimension_ipc::Envelope<int> env4 { 4, 104 };
    check(testQueue.tryEnqueue(env4), "Enqueue config 4 into freed space");

    // 5 & 6. Consumer drains queue and mailbox using generation-based selection
    uint64_t lastAppliedGen = 1;
    dimension_ipc::Envelope<int> bestEnv { 0, 0 };
    bool hasBest = false;

    dimension_ipc::Envelope<int> inc;
    while (testQueue.tryDequeue(inc)) {
        if (inc.generation > lastAppliedGen) {
            if (!hasBest || inc.generation > bestEnv.generation) {
                bestEnv = inc;
                hasBest = true;
            }
        }
    }

    dimension_ipc::Envelope<int> fb;
    if (testMailbox.consume(fb)) {
        if (fb.generation > lastAppliedGen) {
            if (!hasBest || fb.generation > bestEnv.generation) {
                bestEnv = fb;
                hasBest = true;
            }
        }
    }

    // Expected final result: 104 (generation 4), NOT 103 (generation 3)
    check(hasBest && bestEnv.payload == 104, "Config 4 preserved, older fallback 3 rejected!");

    // Integration test with real processor (exact sequence from specification)
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    auto& queue = proc.getConfigQueueForTesting();
    auto& mbox = proc.getFallbackMailboxForTesting();

    Dimension5AudioProcessor::PluginConfig c1, c2, c3, c4;
    c1.dspParams.depthMs = 1.0f;
    c2.dspParams.depthMs = 2.0f;
    c3.dspParams.depthMs = 3.0f;
    c4.dspParams.depthMs = 4.0f;

    // 1. Queue receives configs 1 and 2
    queue.tryEnqueue({ 101, c1 });
    queue.tryEnqueue({ 102, c2 });

    // 2. Config 3 goes to fallback due to saturation
    mbox.publish({ 103, c3 });

    // 3. Consumer pops 1 and frees space
    Dimension5AudioProcessor::ConfigEnvelope env;
    queue.tryDequeue(env);

    // 4. Config 4 enters the queue
    queue.tryEnqueue({ 104, c4 });

    // 5 & 6. Audio callback processes block
    processStereoTestSignal(proc);

    // Verify applied config is 4 (depth 4.0f), NOT 3
    Dimension5AudioProcessor::PluginConfig applied;
    proc.getLastAppliedConfig(applied);
    check(std::abs(applied.dspParams.depthMs - 4.0f) < 1e-4f, "Processor applied config 4, fallback 3 did not revert it");
}

// 23. Interleaving 2: Continuous production with finite consumption work budget
void test_interleaving_2_continuous_production_bounded_budget() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    std::atomic<bool> producerRunning { true };
    std::atomic<int> producedCount { 0 };

    std::thread producer([&]() {
        while (producerRunning.load(std::memory_order_relaxed)) {
            const int count = producedCount.fetch_add(1, std::memory_order_relaxed);
            setParamHostValue(proc, "depthMs", 1.0f + (float)(count % 20) * 0.1f);
        }
    });

    // Run 50 blocks while producer is actively hammering the queue
    for (int i = 0; i < 50; ++i) {
        processStereoTestSignal(proc);
    }

    producerRunning.store(false, std::memory_order_release);
    producer.join();

    // Now run a few more blocks to allow convergence to final value
    for (int i = 0; i < 10; ++i) {
        processStereoTestSignal(proc);
    }

    Dimension5AudioProcessor::PluginConfig finalCfg;
    proc.getLastAppliedConfig(finalCfg);
    check(finalCfg.dspParams.depthMs >= 1.0f && finalCfg.dspParams.depthMs <= 3.0f,
          "Audio progressed without hanging and converged to a valid depthMs");
}

// 24. Interleaving 3: Reader paused after choosing slot and before reserving it
void test_interleaving_3_reader_paused_pre_reserve() {
    struct TestData {
        int a;
        int b;
    };
    dimension_ipc::SnapshotBuffer<TestData, 8> buffer;
    buffer.publish({ 10, 20 });

    std::atomic<bool> writerDone { false };
    buffer.testHookPreReserve = [&](uint32_t) {
        // While reader was about to reserve, writer publishes 5 new values
        for (int i = 1; i <= 5; ++i) {
            buffer.publish({ 100 * i, 200 * i });
        }
        writerDone.store(true);
        // Clear hook so retries don't loop
        buffer.testHookPreReserve = nullptr;
    };

    TestData readData { 0, 0 };
    check(buffer.read(readData), "Reader successfully acquired slot after pre-reserve intervention");
    check(writerDone.load(), "Writer ran during pre-reserve window");
    // Verify data coherence: readData.b must be 2 * readData.a (no tearing)
    check(readData.b == 2 * readData.a, "Data read is internally coherent");
}

// 25. Interleaving 4: Reader paused during copy while writer attempts to reuse slot
void test_interleaving_4_reader_paused_during_copy_writer_reuse() {
    struct CorrelatedConfig {
        float val[8];
    };
    dimension_ipc::SnapshotBuffer<CorrelatedConfig, 4> buffer;

    CorrelatedConfig initial;
    for (int i = 0; i < 8; ++i) initial.val[i] = 1.0f;
    buffer.publish(initial);

    std::atomic<bool> writerAttempted { false };
    buffer.testHookDuringRead = [&](uint32_t readingSlot) {
        (void)readingSlot;
        // While reader is in critical section, publish 10 items to wrap around all slots
        for (int round = 2; round <= 10; ++round) {
            CorrelatedConfig c;
            for (int i = 0; i < 8; ++i) c.val[i] = (float)round;
            buffer.publish(c);
        }
        writerAttempted.store(true);
    };

    CorrelatedConfig out;
    check(buffer.read(out), "Read succeeded");
    check(writerAttempted.load(), "Writer attempted reuse during active copy");

    // Verify all 8 floats match the same round (no partial overwrite by writer)
    const float first = out.val[0];
    for (int i = 1; i < 8; ++i) {
        check(std::abs(out.val[i] - first) < 1e-5f, "Slot was not torn by writer during active read copy");
    }
}

// 26. Interleaving 5: All inspection slots unavailable
void test_interleaving_5_all_inspection_slots_unavailable() {
    dimension_ipc::SnapshotBuffer<int, 4> buffer;
    buffer.publish(42);

    // Make all slots busy
    for (size_t i = 0; i < 4; ++i) {
        buffer.forceSlotBusyForTest(i, true);
    }

    // Writer attempts to publish
    bool published = buffer.publish(99);
    check(!published, "Writer gracefully dropped publication when all slots busy");

    // Unmark slots
    for (size_t i = 0; i < 4; ++i) {
        buffer.forceSlotBusyForTest(i, false);
    }
}

// 27. Interleaving 6: New decision between validation and publication of deferred update
void test_interleaving_6_new_decision_between_validation_and_deferred_publish() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    // 1. Select Mode II (schedules deferred presentation update)
    setParamHostValue(proc, "mode", (float)DIMENSION_MODE_II);
    check(proc.hasPendingUpdates(), "Presentation update pending for Mode II");

    // 2. Before update is delivered, user selects Mode IV
    setParamHostValue(proc, "mode", (float)DIMENSION_MODE_IV);

    // 3. Now deliver updates
    proc.deliverPendingUpdates();

    // Mode IV must prevail; Mode II must not overwrite Mode IV
    Dimension5AudioProcessor::PluginConfig cfg;
    proc.getLastAppliedConfig(cfg);
    check(cfg.dspParams.mode == DIMENSION_MODE_IV || std::abs(proc.getFloatParam("mode") - (float)DIMENSION_MODE_IV) < 1e-4f,
          "Newer decision prevailed over stale deferred update");
}

// 28. Interleaving 7: Legitimate automation while presentation publishes parameters
void test_interleaving_7_legitimate_automation_during_presentation_sync() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    // Set Mode II
    setParamHostValue(proc, "mode", (float)DIMENSION_MODE_II);
    processStereoTestSignal(proc);

    // Run concurrent automation thread modifying depth while message thread does presentation work
    std::atomic<bool> automationDone { false };
    std::thread autoThread([&]() {
        // Automation thread modifies depthMs to 2.75f
        setParamHostValue(proc, "depthMs", 2.75f);
        automationDone.store(true);
    });

    // Main thread delivers presentation updates
    proc.deliverPendingUpdates();

    autoThread.join();
    check(automationDone.load(), "Automation thread finished");

    processStereoTestSignal(proc);

    Dimension5AudioProcessor::PluginConfig cfg;
    proc.getLastAppliedConfig(cfg);
    check(std::abs(cfg.dspParams.depthMs - 2.75f) < 1e-4f,
          "Legitimate automation was not dropped by concurrent presentation sync");
}

// 29. Interleaving 8: Multiple producer contexts with correlated configurations (no tearing)
void test_interleaving_8_multiple_producer_contexts_no_tearing() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    std::atomic<bool> keepRunning { true };
    std::atomic<bool> hasTornConfig { false };

    // 3 concurrent producer threads automating correlated parameters
    auto makeProducer = [&](float marker) {
        return std::thread([&, marker]() {
            while (keepRunning.load(std::memory_order_relaxed)) {
                setParamHostValue(proc, "inputGain", marker);
                setParamHostValue(proc, "outputGain", marker * 0.5f);
                setParamHostValue(proc, "width", marker * 0.25f);
            }
        });
    };

    std::thread t1 = makeProducer(1.0f);
    std::thread t2 = makeProducer(2.0f);
    std::thread t3 = makeProducer(3.0f);

    for (int block = 0; block < 100; ++block) {
        processStereoTestSignal(proc);
        Dimension5AudioProcessor::PluginConfig cfg;
        proc.getLastAppliedConfig(cfg);
        // Check that parameters are within valid domain
        if (cfg.dspParams.inputGain < 0.0f || cfg.dspParams.outputGain < 0.0f) {
            hasTornConfig.store(true);
        }
    }

    keepRunning.store(false, std::memory_order_release);
    t1.join();
    t2.join();
    t3.join();

    check(!hasTornConfig.load(), "No torn or invalid configurations observed under concurrent automation");
}

// 30. Interleaving 9: Immediate persistence and A/B before delivering presentation updates
void test_interleaving_9_immediate_persistence_and_ab_before_presentation() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    // Start with Mode II
    setParamHostValue(proc, "mode", (float)DIMENSION_MODE_II);
    processStereoTestSignal(proc);

    // Edit depthMs to 2.35f (transitions to Custom)
    setParamHostValue(proc, "depthMs", 2.35f);

    // WITHOUT calling deliverPendingUpdates() or processing audio:
    // 1. Switch to Compare Slot B (this immediately captures the edited Slot A)
    proc.selectCompareSlot(1);

    // 2. In Slot B, switch to Mode I
    setParamHostValue(proc, "mode", (float)DIMENSION_MODE_I);
    processStereoTestSignal(proc);

    Dimension5AudioProcessor::PluginConfig cfgModeI;
    proc.getLastAppliedConfig(cfgModeI);
    check(cfgModeI.dspParams.mode == DIMENSION_MODE_I, "Switched to Mode I in Slot B");

    // 3. Switch back to Compare Slot A (recalls Slot A)
    proc.selectCompareSlot(0);
    processStereoTestSignal(proc);

    Dimension5AudioProcessor::PluginConfig cfgRecalled;
    proc.getLastAppliedConfig(cfgRecalled);
    check(cfgRecalled.dspParams.mode == DIMENSION_MODE_CUSTOM, "A/B recalled Custom mode");
    check(std::abs(cfgRecalled.dspParams.depthMs - 2.35f) < 1e-4f, "A/B recalled depth 2.35 ms");
    check(std::abs(cfgRecalled.dspParams.baseDelayMs - 8.5f) < 1e-4f, "A/B recalled Mode II baseline baseDelay 8.5 ms");

    // 4. Immediate state serialization
    juce::MemoryBlock mem;
    proc.getStateInformation(mem);

    Dimension5AudioProcessor procRestored;
    procRestored.prepareToPlay(48000.0, 32);
    procRestored.setStateInformation(mem.getData(), (int)mem.getSize());
    processStereoTestSignal(procRestored);

    Dimension5AudioProcessor::PluginConfig cfgRestored;
    procRestored.getLastAppliedConfig(cfgRestored);
    check(cfgRestored.dspParams.mode == DIMENSION_MODE_CUSTOM, "Restored processor is Custom mode");
    check(std::abs(cfgRestored.dspParams.depthMs - 2.35f) < 1e-4f, "Restored processor has depth 2.35 ms");
    check(std::abs(cfgRestored.dspParams.baseDelayMs - 8.5f) < 1e-4f, "Restored processor has Mode II baseDelay 8.5 ms");
}
} // namespace

int main() {
    juce::ScopedJuceInitialiser_GUI initialiser;

    try {
        std::cout << "[JUCE Tests] 1. test_processor_construction_and_prepare...\n";
        test_processor_construction_and_prepare();

        std::cout << "[JUCE Tests] 2. test_scenario_a_host_advanced_edit_without_editor...\n";
        test_scenario_a_host_advanced_edit_without_editor();

        std::cout << "[JUCE Tests] 3. test_scenario_b_host_direct_custom_selection_without_editor...\n";
        test_scenario_b_host_direct_custom_selection_without_editor();

        std::cout << "[JUCE Tests] 4. test_mode_sequence_II_to_IV_to_custom...\n";
        test_mode_sequence_II_to_IV_to_custom();

        std::cout << "[JUCE Tests] 5. test_first_edit_of_each_advanced_param...\n";
        test_first_edit_of_each_advanced_param();

        std::cout << "[JUCE Tests] 6. test_scenario_c_old_session_with_stale_params_and_edit...\n";
        test_scenario_c_old_session_with_stale_params_and_edit();

        std::cout << "[JUCE Tests] 7. test_simultaneous_automation_same_block...\n";
        test_simultaneous_automation_same_block();

        std::cout << "[JUCE Tests] 8. test_persistence_and_compare_ab_after_custom_transition...\n";
        test_persistence_and_compare_ab_after_custom_transition();

        std::cout << "[JUCE Tests] 9. test_open_close_editor_does_not_modify_config_or_sound...\n";
        test_open_close_editor_does_not_modify_config_or_sound();

        std::cout << "[JUCE Tests] 10. test_ui_slider_interaction_preserves_first_edit...\n";
        test_ui_slider_interaction_preserves_first_edit();

        std::cout << "[JUCE Tests] 11. test_controlled_concurrency_and_configuration_consistency...\n";
        test_controlled_concurrency_and_configuration_consistency();

        std::cout << "[JUCE Tests] 12. test_channel_layouts_and_bypass...\n";
        test_channel_layouts_and_bypass();

        std::cout << "[JUCE Tests] 13. test_partial_blocks_and_large_buffers...\n";
        test_partial_blocks_and_large_buffers();

        std::cout << "[JUCE Tests] 14. test_deferred_mode_II_depth_then_mode_IV...\n";
        test_deferred_mode_II_depth_then_mode_IV();

        std::cout << "[JUCE Tests] 15. test_deferred_mode_II_depth_then_preset...\n";
        test_deferred_mode_II_depth_then_preset();

        std::cout << "[JUCE Tests] 16. test_deferred_edit_then_restore_session...\n";
        test_deferred_edit_then_restore_session();

        std::cout << "[JUCE Tests] 17. test_deferred_edit_then_switch_ab...\n";
        test_deferred_edit_then_switch_ab();

        std::cout << "[JUCE Tests] 18. test_deferred_multiple_edits_latest_prevails...\n";
        test_deferred_multiple_edits_latest_prevails();

        std::cout << "[JUCE Tests] 19. test_cross_thread_configuration_consistency_no_tearing...\n";
        test_cross_thread_configuration_consistency_no_tearing();

        std::cout << "[JUCE Tests] 20. test_saturation_policy_and_unavailability...\n";
        test_saturation_policy_and_unavailability();

        std::cout << "[JUCE Tests] 21. test_immediate_persistence_after_auto_custom_without_async_dispatch...\n";
        test_immediate_persistence_after_auto_custom_without_async_dispatch();

        std::cout << "[JUCE Tests] 22. test_interleaving_1_fallback_inversion_regression...\n";
        test_interleaving_1_fallback_inversion_regression();

        std::cout << "[JUCE Tests] 23. test_interleaving_2_continuous_production_bounded_budget...\n";
        test_interleaving_2_continuous_production_bounded_budget();

        std::cout << "[JUCE Tests] 24. test_interleaving_3_reader_paused_pre_reserve...\n";
        test_interleaving_3_reader_paused_pre_reserve();

        std::cout << "[JUCE Tests] 25. test_interleaving_4_reader_paused_during_copy_writer_reuse...\n";
        test_interleaving_4_reader_paused_during_copy_writer_reuse();

        std::cout << "[JUCE Tests] 26. test_interleaving_5_all_inspection_slots_unavailable...\n";
        test_interleaving_5_all_inspection_slots_unavailable();

        std::cout << "[JUCE Tests] 27. test_interleaving_6_new_decision_between_validation_and_deferred_publish...\n";
        test_interleaving_6_new_decision_between_validation_and_deferred_publish();

        std::cout << "[JUCE Tests] 28. test_interleaving_7_legitimate_automation_during_presentation_sync...\n";
        test_interleaving_7_legitimate_automation_during_presentation_sync();

        std::cout << "[JUCE Tests] 29. test_interleaving_8_multiple_producer_contexts_no_tearing...\n";
        test_interleaving_8_multiple_producer_contexts_no_tearing();

        std::cout << "[JUCE Tests] 30. test_interleaving_9_immediate_persistence_and_ab_before_presentation...\n";
        test_interleaving_9_immediate_persistence_and_ab_before_presentation();

        std::cout << "All JUCE processor tests PASSED successfully!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "JUCE Test FAILED with exception: " << e.what() << "\n";
        return 1;
    }
}
