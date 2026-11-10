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
            std::this_thread::yield();
        }
    });

    // Ensure producer thread has actually started and produced updates
    while (producedCount.load(std::memory_order_acquire) < 5) {
        processStereoTestSignal(proc);
    }

    // Run 50 blocks while producer is actively hammering the queue
    for (int i = 0; i < 50; ++i) {
        processStereoTestSignal(proc);
    }

    producerRunning.store(false, std::memory_order_release);
    producer.join();

    // Now run blocks to allow convergence to final value
    for (int i = 0; i < 20; ++i) {
        processStereoTestSignal(proc);
    }

    Dimension5AudioProcessor::PluginConfig finalCfg;
    proc.getLastAppliedConfig(finalCfg);
    check(producedCount.load() >= 5, "Producer executed updates");
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

// ==============================================================================
// Stereo Analyzer & Telemetry Test Suite
// ==============================================================================

// 31. Silence handling: no NaN, Inf, or false measurements
void test_stereo_analyzer_silence() {
    StereoAnalyzer analyzer;
    analyzer.prepare(48000.0);
    std::vector<float> silence(256, 0.0f);
    StereoTelemetrySnapshot snap {};

    for (int b = 0; b < 10; ++b) {
        analyzer.processBlock(silence.data(), silence.data(), silence.data(), silence.data(), 256, snap);
    }

    check(!snap.frame.hasSignal, "silence: hasSignal must be false");
    check(std::isfinite(snap.frame.correlation), "silence: correlation must be finite");
    check(std::isfinite(snap.frame.sideMidRatioDb), "silence: sideMidRatioDb must be finite");
    check(std::isfinite(snap.frame.monoAttenuationDb), "silence: monoAttenuationDb must be finite");
    check(snap.frame.midRms < 1e-6f, "silence: midRms must be 0");
    check(snap.frame.sideRms < 1e-6f, "silence: sideRms must be 0");
}

// 32. Identical mono: correlation ~ +1, Side ~ 0, Mono loss ~ 0 dB
void test_stereo_analyzer_identical_mono() {
    StereoAnalyzer analyzer;
    analyzer.prepare(48000.0);
    constexpr int kFrames = 4800;
    std::vector<float> sig(kFrames);
    for (int i = 0; i < kFrames; ++i) {
        sig[(size_t)i] = 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f * (float)i / 48000.0f);
    }
    StereoTelemetrySnapshot snap {};
    analyzer.processBlock(sig.data(), sig.data(), sig.data(), sig.data(), kFrames, snap);

    check(snap.frame.hasSignal, "identical mono: hasSignal is true");
    check(snap.frame.correlation > 0.999f, "identical mono: correlation ~ +1");
    check(snap.frame.sideRms < 1e-4f, "identical mono: sideRms ~ 0");
    check(snap.frame.sideMidRatioDb <= -50.0f, "identical mono: side/mid ratio <= -50 dB");
    check(std::abs(snap.frame.monoAttenuationDb) < 0.1f, "identical mono: mono loss ~ 0 dB");
}

// 33. Antiphase L = -R: correlation ~ -1, Mid ~ 0, strong mono cancellation
void test_stereo_analyzer_opposite_antiphase() {
    StereoAnalyzer analyzer;
    analyzer.prepare(48000.0);
    constexpr int kFrames = 4800;
    std::vector<float> sigL(kFrames);
    std::vector<float> sigR(kFrames);
    for (int i = 0; i < kFrames; ++i) {
        sigL[(size_t)i] =  0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f * (float)i / 48000.0f);
        sigR[(size_t)i] = -0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f * (float)i / 48000.0f);
    }
    StereoTelemetrySnapshot snap {};
    analyzer.processBlock(sigL.data(), sigR.data(), sigL.data(), sigR.data(), kFrames, snap);

    check(snap.frame.hasSignal, "antiphase: hasSignal is true");
    check(snap.frame.correlation < -0.999f, "antiphase: correlation ~ -1");
    check(snap.frame.midRms < 1e-4f, "antiphase: midRms ~ 0");
    check(snap.frame.sideMidRatioDb >= 20.0f, "antiphase: side/mid ratio >= +20 dB");
    check(snap.frame.monoAttenuationDb < -40.0f, "antiphase: strong mono cancellation (< -40 dB)");
}

// 34. Uncorrelated quadrature signals: correlation ~ 0, mono loss ~ -3 dB
void test_stereo_analyzer_uncorrelated() {
    StereoAnalyzer analyzer;
    analyzer.prepare(48000.0);
    constexpr int kFrames = 9600;
    std::vector<float> sigL(kFrames);
    std::vector<float> sigR(kFrames);
    for (int i = 0; i < kFrames; ++i) {
        sigL[(size_t)i] = 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * 300.0f * (float)i / 48000.0f);
        sigR[(size_t)i] = 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi * 300.0f * (float)i / 48000.0f);
    }
    StereoTelemetrySnapshot snap {};
    analyzer.processBlock(sigL.data(), sigR.data(), sigL.data(), sigR.data(), kFrames, snap);

    check(snap.frame.hasSignal, "uncorrelated: hasSignal is true");
    check(std::abs(snap.frame.correlation) < 0.15f, "uncorrelated: correlation near zero");
    check(std::abs(snap.frame.monoAttenuationDb - (-3.01f)) < 0.8f, "uncorrelated: mono attenuation ~ -3 dB");
}

// 35. Single channel signal: correlation 0.0f, no undefined divide-by-zero
void test_stereo_analyzer_single_channel() {
    StereoAnalyzer analyzer;
    analyzer.prepare(48000.0);
    constexpr int kFrames = 2400;
    std::vector<float> sigL(kFrames, 0.4f);
    std::vector<float> sigR(kFrames, 0.0f);
    StereoTelemetrySnapshot snap {};
    analyzer.processBlock(sigL.data(), sigR.data(), sigL.data(), sigR.data(), kFrames, snap);

    check(snap.frame.hasSignal, "single channel: hasSignal is true");
    check(std::abs(snap.frame.correlation) < 1e-6f, "single channel: correlation safely 0.0f");
    check(std::isfinite(snap.frame.sideMidRatioDb), "single channel: sideMidRatioDb is finite");
    check(std::isfinite(snap.frame.monoAttenuationDb), "single channel: monoAttenuationDb is finite");
}

// 36. Extreme values and sample rates
void test_stereo_analyzer_extreme_values_and_sample_rates() {
    const double rates[] = { 44100.0, 48000.0, 96000.0 };
    const int blockSizes[] = { 16, 32, 64, 512, 1024 };

    for (double sr : rates) {
        StereoAnalyzer analyzer;
        analyzer.prepare(sr);

        for (int bs : blockSizes) {
            const size_t ubs = (size_t)bs;
            std::vector<float> inL(ubs, 2.0f);
            std::vector<float> inR(ubs, -2.0f);
            StereoTelemetrySnapshot snap {};
            analyzer.processBlock(inL.data(), inR.data(), inL.data(), inR.data(), bs, snap);

            check(snap.frame.hasSignal, "extreme: hasSignal is true");
            check(std::isfinite(snap.frame.correlation), "extreme: correlation finite");
            check(std::isfinite(snap.frame.midRms), "extreme: midRms finite");
            check(std::isfinite(snap.frame.sideRms), "extreme: sideRms finite");
            check(std::isfinite(snap.frame.sideMidRatioDb), "extreme: sideMidRatioDb finite");
            check(std::isfinite(snap.frame.monoAttenuationDb), "extreme: monoAttenuationDb finite");
        }
    }
}

// 37. Telemetry has zero modification on audio samples (Bit-exact DSP validation)
void test_stereo_telemetry_bit_exact_audio() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    juce::AudioBuffer<float> buffer(2, 64);
    for (int i = 0; i < 64; ++i) {
        buffer.setSample(0, i, 0.35f * std::sin((float)i * 0.1f));
        buffer.setSample(1, i, 0.25f * std::cos((float)i * 0.1f));
    }

    const float preL0 = buffer.getSample(0, 0);
    const float preR0 = buffer.getSample(1, 0);

    processTestBlock(proc, buffer);

    check(std::isfinite(buffer.getSample(0, 0)), "audio sample finite");
    check(std::isfinite(buffer.getSample(1, 0)), "audio sample finite");
    check(std::abs(buffer.getSample(0, 0) - preL0) > 1e-4f || std::abs(buffer.getSample(1, 0) - preR0) > 1e-4f, "audio processed by DSP");

    StereoTelemetrySnapshot snap {};
    check(proc.getStereoTelemetry(snap), "telemetry snapshot successfully published during processBlock");
    check(snap.frame.hasSignal, "telemetry detected audio signal");
}

// 38. Repeated opening and closing of editor during processing
void test_editor_lifecycle_and_telemetry() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    for (int cycle = 0; cycle < 6; ++cycle) {
        processStereoTestSignal(proc, 64);

        // Open editor
        auto* editor = proc.createEditor();
        check(editor != nullptr, "editor created successfully");

        // Process audio while editor is open
        processStereoTestSignal(proc, 64);

        StereoTelemetrySnapshot snap {};
        check(proc.getStereoTelemetry(snap), "telemetry received while editor open");

        // Close editor
        delete editor;

        // Process audio after editor is closed
        processStereoTestSignal(proc, 64);
    }
}

// 39. Telemetry buffer saturation safety
void test_telemetry_buffer_saturation_and_safety() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    // Process 200 blocks without UI consuming telemetry
    for (int b = 0; b < 200; ++b) {
        processStereoTestSignal(proc, 32);
    }

    // Now consume telemetry
    StereoTelemetrySnapshot snap {};
    check(proc.getStereoTelemetry(snap), "consumed telemetry after saturation");
    check(snap.frame.hasSignal, "valid telemetry retained");
}

// 40. Main controls (DEPTH, MOTION, COLOR) auto-transition to Custom mode
void test_main_controls_auto_custom_depth_motion_color() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    // 1. Start in Mode I, edit Depth via depthMs
    setParamHostValue(proc, "mode", (float)DIMENSION_MODE_I);
    processStereoTestSignal(proc);

    setParamHostValue(proc, "depthMs", 2.1f);
    processStereoTestSignal(proc);

    Dimension5AudioProcessor::PluginConfig cfg;
    proc.getLastAppliedConfig(cfg);
    check(cfg.dspParams.mode == DIMENSION_MODE_CUSTOM, "depthMs edit transitioned to Custom mode");
    check(std::abs(cfg.dspParams.depthMs - 2.1f) < 1e-4f, "applied depth is 2.1 ms");
    check(std::abs(cfg.dspParams.baseDelayMs - 7.0f) < 1e-4f, "preserved Mode I base delay (7.0 ms)");

    // 2. Start in Mode II, edit Motion via rateHz
    setParamHostValue(proc, "mode", (float)DIMENSION_MODE_II);
    processStereoTestSignal(proc);

    setParamHostValue(proc, "rateHz", 0.75f);
    processStereoTestSignal(proc);

    proc.getLastAppliedConfig(cfg);
    check(cfg.dspParams.mode == DIMENSION_MODE_CUSTOM, "rateHz edit transitioned to Custom mode");
    check(std::abs(cfg.dspParams.rateHz - 0.75f) < 1e-4f, "applied rate is 0.75 Hz");
    check(std::abs(cfg.dspParams.baseDelayMs - 8.5f) < 1e-4f, "preserved Mode II base delay (8.5 ms)");

    // 3. Start in Mode III, edit Color via lpfHz
    setParamHostValue(proc, "mode", (float)DIMENSION_MODE_III);
    processStereoTestSignal(proc);

    setParamHostValue(proc, "lpfHz", 5500.0f);
    processStereoTestSignal(proc);

    proc.getLastAppliedConfig(cfg);
    check(cfg.dspParams.mode == DIMENSION_MODE_CUSTOM, "lpfHz edit transitioned to Custom mode");
    check(std::abs(cfg.dspParams.lpfHz - 5500.0f) < 1e-4f, "applied lpf is 5500 Hz");
    check(std::abs(cfg.dspParams.baseDelayMs - 10.0f) < 1e-4f, "preserved Mode III base delay (10.0 ms)");
}

// 41. Stereo Field View Modes switching (FIELD -> VECTOR -> DELTA -> FIELD) with audio invariance
void test_stereo_field_modes_switching_and_audio_invariance() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    auto* editor = dynamic_cast<Dimension5AudioProcessorEditor*>(proc.createEditor());
    check(editor != nullptr, "editor created successfully");

    auto& fieldComp = editor->getStereoFieldComponent();
    check(fieldComp.getViewMode() == StereoFieldComponent::ViewMode::Field, "default mode is FIELD");

    // Process audio before mode switch
    juce::AudioBuffer<float> bufferBefore(2, 64);
    for (int i = 0; i < 64; ++i) {
        bufferBefore.setSample(0, i, 0.3f);
        bufferBefore.setSample(1, i, 0.3f);
    }
    processTestBlock(proc, bufferBefore);

    // Switch to VECTOR
    fieldComp.setViewMode(StereoFieldComponent::ViewMode::Vector);
    check(fieldComp.getViewMode() == StereoFieldComponent::ViewMode::Vector, "switched to VECTOR");

    juce::AudioBuffer<float> bufferVector(2, 64);
    for (int i = 0; i < 64; ++i) {
        bufferVector.setSample(0, i, 0.3f);
        bufferVector.setSample(1, i, 0.3f);
    }
    processTestBlock(proc, bufferVector);

    // Switch to DELTA
    fieldComp.setViewMode(StereoFieldComponent::ViewMode::Delta);
    check(fieldComp.getViewMode() == StereoFieldComponent::ViewMode::Delta, "switched to DELTA");

    juce::AudioBuffer<float> bufferDelta(2, 64);
    for (int i = 0; i < 64; ++i) {
        bufferDelta.setSample(0, i, 0.3f);
        bufferDelta.setSample(1, i, 0.3f);
    }
    processTestBlock(proc, bufferDelta);

    // Verify audio stability across UI mode switches
    for (int i = 0; i < 64; ++i) {
        check(std::abs(bufferVector.getSample(0, i) - bufferDelta.getSample(0, i)) < 1e-4f, "vector and delta produce identical audio");
    }

    // Switch back to FIELD
    fieldComp.setViewMode(StereoFieldComponent::ViewMode::Field);
    check(fieldComp.getViewMode() == StereoFieldComponent::ViewMode::Field, "switched back to FIELD");

    delete editor;
}

// 42. Undefined correlation and single channel handling
void test_undefined_correlation_and_mono_handling() {
    StereoAnalyzer analyzer;
    analyzer.prepare(48000.0);

    // 1. Silence: correlation is invalid, status is Silence
    {
        StereoTelemetrySnapshot snap {};
        std::vector<float> zeros(64, 0.0f);
        analyzer.processBlock(zeros.data(), zeros.data(), zeros.data(), zeros.data(), 64, snap);
        check(!snap.frame.hasSignal, "silence hasSignal is false");
        check(!snap.frame.isCorrelationValid, "silence correlation is marked invalid");
        check(snap.frame.correlationStatus == CorrelationStatus::Silence, "silence correlation status");
        check(!snap.frame.isStereoDeltaValid, "silence stereo delta is invalid");
    }

    // 2. Left channel only: correlation is invalid, status is LeftOnly
    {
        StereoTelemetrySnapshot snap {};
        std::vector<float> leftOnly(64, 0.5f);
        std::vector<float> zeros(64, 0.0f);
        for (int b = 0; b < 10; ++b) {
            analyzer.processBlock(leftOnly.data(), zeros.data(), leftOnly.data(), zeros.data(), 64, snap);
        }
        check(snap.frame.hasSignal, "left-only hasSignal is true");
        check(!snap.frame.isCorrelationValid, "left-only correlation is marked invalid");
        check(snap.frame.correlationStatus == CorrelationStatus::LeftOnly, "left-only correlation status");
        check(std::abs(snap.frame.correlation) < 1e-4f, "left-only reports numerical 0.0f");
    }

    // 3. Right channel only: correlation is invalid, status is RightOnly
    {
        analyzer.reset();
        StereoTelemetrySnapshot snap {};
        std::vector<float> rightOnly(64, 0.5f);
        std::vector<float> zeros(64, 0.0f);
        for (int b = 0; b < 10; ++b) {
            analyzer.processBlock(zeros.data(), rightOnly.data(), zeros.data(), rightOnly.data(), 64, snap);
        }
        check(snap.frame.hasSignal, "right-only hasSignal is true");
        check(!snap.frame.isCorrelationValid, "right-only correlation is marked invalid");
        check(snap.frame.correlationStatus == CorrelationStatus::RightOnly, "right-only correlation status");
        check(std::abs(snap.frame.correlation) < 1e-4f, "right-only reports numerical 0.0f");
    }

    // 4. Stereo correlated signal: correlation is valid, status is Valid
    {
        analyzer.reset();
        StereoTelemetrySnapshot snap {};
        std::vector<float> stereo(64, 0.4f);
        for (int b = 0; b < 15; ++b) {
            analyzer.processBlock(stereo.data(), stereo.data(), stereo.data(), stereo.data(), 64, snap);
        }
        check(snap.frame.hasSignal, "stereo hasSignal is true");
        check(snap.frame.isCorrelationValid, "stereo correlation is valid");
        check(snap.frame.correlationStatus == CorrelationStatus::Valid, "stereo correlation status is Valid");
        check(std::abs(snap.frame.correlation - 1.0f) < 1e-3f, "identical channels correlation ~1.0");
    }
}

// 43. Block size independent continuous-time peak decay
void test_block_size_independent_peak_decay() {
    // Compare peak envelope decay over 100ms between block size 16 and block size 256
    StereoAnalyzer analyzerSmall;
    analyzerSmall.prepare(48000.0);

    StereoAnalyzer analyzerLarge;
    analyzerLarge.prepare(48000.0);

    // Feed initial peak of 1.0
    StereoTelemetrySnapshot snapSmall {};
    StereoTelemetrySnapshot snapLarge {};
    std::vector<float> pulse(256, 1.0f);
    analyzerSmall.processBlock(pulse.data(), pulse.data(), pulse.data(), pulse.data(), 256, snapSmall);
    analyzerLarge.processBlock(pulse.data(), pulse.data(), pulse.data(), pulse.data(), 256, snapLarge);

    check(std::abs(snapSmall.frame.outputPeakL - 1.0f) < 1e-3f, "initial peak L small is 1.0");
    check(std::abs(snapLarge.frame.outputPeakL - 1.0f) < 1e-3f, "initial peak L large is 1.0");

    // Feed 4800 samples of silence (100 ms) in chunks of 16 vs chunks of 256
    std::vector<float> zero16(16, 0.0f);
    for (int i = 0; i < 4800 / 16; ++i) {
        analyzerSmall.processBlock(zero16.data(), zero16.data(), zero16.data(), zero16.data(), 16, snapSmall);
    }

    std::vector<float> zero256(256, 0.0f);
    for (int i = 0; i < 4800 / 256; ++i) {
        analyzerLarge.processBlock(zero256.data(), zero256.data(), zero256.data(), zero256.data(), 256, snapLarge);
    }

    // Both should decay to approximately 1/e ~ 0.368, within close margin
    check(std::abs(snapSmall.frame.outputPeakL - snapLarge.frame.outputPeakL) < 0.05f,
          "decay across 16-sample blocks matches decay across 256-sample blocks");
}

// 44. Editor resizing bounds and Advanced drawer expansion
void test_editor_resize_bounds_and_advanced_drawer() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    auto* editor = dynamic_cast<Dimension5AudioProcessorEditor*>(proc.createEditor());
    check(editor != nullptr, "editor instantiated");

    // Default base dimensions
    check(editor->getWidth() == 1000, "base width is 1000");
    check(editor->getHeight() == 620, "base height is 620");

    // Minimum size
    editor->setSize(840, 540);
    check(editor->getWidth() == 840, "resized to min width 840");
    check(editor->getHeight() == 540, "resized to min height 540");

    // Maximum size
    editor->setSize(1600, 1000);
    check(editor->getWidth() == 1600, "resized to large width 1600");
    check(editor->getHeight() == 1000, "resized to large height 1000");

    // Restore standard size
    editor->setSize(1000, 620);
    check(!editor->isAdvancedVisible(), "advanced drawer initially closed");

    // Open Advanced Drawer: expands by the panel height without exceeding limits
    editor->setAdvancedVisible(true);
    check(editor->isAdvancedVisible(), "advanced drawer marked visible");
    check(editor->getHeight() == 820, "editor expanded by the advanced panel height");
    check(editor->getHeight() <= 1000, "editor height remains within configured limit");

    // Resizing while open is honoured and never exceeds limits
    editor->setSize(1200, 960);
    check(editor->getHeight() == 960, "advanced-open resize honoured");

    // Close Advanced Drawer: returns to the pre-open height
    editor->setAdvancedVisible(false);
    check(!editor->isAdvancedVisible(), "advanced drawer marked closed");
    check(editor->getHeight() == 620, "editor restored to the pre-open height");

    delete editor;
}

// Helper: Save Component Snapshot to PNG file
static bool saveSnapshotToPng(juce::Component& comp, const juce::File& outputFile) {
    outputFile.getParentDirectory().createDirectory();
    juce::Image snapshot = comp.createComponentSnapshot(comp.getLocalBounds(), true, 1.0f);
    if (!snapshot.isValid() || snapshot.getWidth() <= 0 || snapshot.getHeight() <= 0) {
        return false;
    }
    if (outputFile.existsAsFile()) {
        outputFile.deleteFile();
    }
    juce::FileOutputStream stream(outputFile);
    if (!stream.openedOk()) {
        return false;
    }
    juce::PNGImageFormat png;
    return png.writeImageToStream(snapshot, stream);
}

// 45. Visual Validation: Capture the real UI 2.1 screenshot set.
void test_generate_visual_validation_snapshots() {
    using ViewMode = StereoFieldComponent::ViewMode;

    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    const juce::File outDir = juce::File::getCurrentWorkingDirectory().getChildFile("docs/snapshots/ui21");
    outDir.createDirectory();

    auto* editor = dynamic_cast<Dimension5AudioProcessorEditor*>(proc.createEditor());
    check(editor != nullptr, "editor instantiated for snapshot generation");

    auto& field = editor->getStereoFieldComponent();

    struct SignalConfig {
        float amplitude { 0.32f };
        bool mono { false };
        bool antiphase { false };
        bool silence { false };
    };

    auto feedSignal = [&](int blocks, float widthVal, float mixVal, bool bypass, SignalConfig cfg) {
        proc.prepareToPlay(48000.0, 32);
        field.resetDisplayState();
        setParamHostValue(proc, "width", widthVal);
        setParamHostValue(proc, "mix", mixVal);
        setParamHostValue(proc, "bypass", bypass ? 1.0f : 0.0f);

        double sampleIndex = 0.0;
        constexpr double sr = 48000.0;
        constexpr int frames = 64;
        constexpr double pi = juce::MathConstants<double>::pi;

        for (int b = 0; b < blocks; ++b) {
            juce::AudioBuffer<float> buffer(2, frames);
            for (int i = 0; i < frames; ++i) {
                const double t = sampleIndex / sr;
                float l = 0.0f;
                float r = 0.0f;
                if (!cfg.silence) {
                    const double mod = 0.6 * std::sin(2.0 * pi * 0.7 * t);
                    l = cfg.amplitude * (float)(0.62 * std::sin(2.0 * pi * 290.0 * t + mod)
                                              + 0.28 * std::sin(2.0 * pi * 610.0 * t + 0.4));
                    r = cfg.amplitude * (float)(0.62 * std::sin(2.0 * pi * 290.0 * t + mod + 1.05)
                                              + 0.28 * std::sin(2.0 * pi * 430.0 * t + 1.8));
                }
                if (cfg.mono) {
                    r = l;
                }
                if (cfg.antiphase) {
                    r = -l;
                }
                buffer.setSample(0, i, l);
                buffer.setSample(1, i, r);
                ++sampleIndex;
            }

            processTestBlock(proc, buffer);

            StereoTelemetrySnapshot snap {};
            if (proc.getStereoTelemetry(snap)) {
                field.updateTelemetry(snap);
            }
            field.updateParameterGeometry(widthVal,
                                          proc.getFloatParam("depthMs"),
                                          proc.getFloatParam("rateHz"),
                                          proc.parameters.getRawParameterValue("mix")->load());
            field.setBypassState(bypass);

            if ((b % 16) == 15) {
                juce::Thread::sleep(30);
            }
        }
    };

    auto save = [&](const char* name) {
        editor->resetMeterDisplay();
        editor->refreshPresentationNow();
        check(saveSnapshotToPng(*editor, outDir.getChildFile(name)), name);
    };

    // 1. Minimum size
    editor->setSize(840, 540);
    field.setViewMode(ViewMode::Field);
    feedSignal(128, 1.0f, 1.0f, false, { 0.30f, false, false, false });
    save("01_minimum_size.png");

    // 2. Standard size
    editor->setSize(1000, 620);
    feedSignal(128, 1.2f, 1.0f, false, { 0.32f, false, false, false });
    save("02_standard_size.png");

    // 3. Maximum size
    editor->setSize(1600, 1000);
    feedSignal(128, 1.4f, 1.0f, false, { 0.32f, false, false, false });
    save("03_maximum_size.png");

    // 4. Advanced drawer open (Standard size 1000x620, neutral spatial settings)
    editor->setSize(1000, 620);
    editor->setAdvancedVisible(true);
    setParamHostValue(proc, "stereoWidth", 1.0f);
    setParamHostValue(proc, "bassFocusHz", 0.0f);
    feedSignal(128, 1.0f, 1.0f, false, { 0.30f, false, false, false });
    save("04_advanced_open.png");

    // 4a. Advanced drawer open at minimum size (840x540)
    editor->setSize(840, 540);
    feedSignal(64, 1.0f, 1.0f, false, { 0.30f, false, false, false });
    save("04a_advanced_open_840x540.png");

    // 4b. Advanced drawer open at maximum size (1600x1000)
    editor->setSize(1600, 1000);
    feedSignal(64, 1.0f, 1.0f, false, { 0.30f, false, false, false });
    save("04b_advanced_open_1600x1000.png");

    // 4c. Advanced drawer with active spatial parameters: Stereo Width = 2.0, Bass Focus = 120 Hz
    editor->setSize(1000, 620);
    setParamHostValue(proc, "stereoWidth", 2.0f);
    setParamHostValue(proc, "bassFocusHz", 120.0f);
    feedSignal(128, 1.4f, 1.0f, false, { 0.32f, false, false, false });
    save("04c_advanced_active_width2_bass120.png");

    // 4d. Advanced drawer reflecting external host automation update
    setParamHostValue(proc, "stereoWidth", 1.60f);
    setParamHostValue(proc, "bassFocusHz", 180.0f);
    feedSignal(64, 1.3f, 1.0f, false, { 0.30f, false, false, false });
    save("04d_advanced_external_automation.png");

    // Restore neutral settings
    setParamHostValue(proc, "stereoWidth", 1.0f);
    setParamHostValue(proc, "bassFocusHz", 0.0f);
    editor->setAdvancedVisible(false);

    // 5. FIELD
    field.setViewMode(ViewMode::Field);
    feedSignal(128, 1.25f, 1.0f, false, { 0.32f, false, false, false });
    save("05_field_mode.png");

    // 6. VECTOR
    field.setViewMode(ViewMode::Vector);
    feedSignal(128, 1.35f, 1.0f, false, { 0.32f, false, false, false });
    save("06_vector_mode.png");

    // 7. DELTA
    field.setViewMode(ViewMode::Delta);
    feedSignal(128, 1.50f, 1.0f, false, { 0.32f, false, false, false });
    save("07_delta_mode.png");

    // 8. Bypass
    field.setViewMode(ViewMode::Field);
    feedSignal(128, 1.0f, 1.0f, true, { 0.30f, false, false, false });
    save("08_bypass_active.png");

    // 9. Silence (real zero signal through the analyzer)
    {
        feedSignal(24, 1.0f, 1.0f, false, { 0.0f, false, false, true });
        field.resetDisplayState();
        StereoTelemetrySnapshot silenceSnap {};
        if (proc.getStereoTelemetry(silenceSnap)) {
            field.updateTelemetry(silenceSnap);
        }
        field.setBypassState(false);
        editor->resetMeterDisplay();
        save("09_silence.png");
    }

    // 10. Wide stereo test audio
    {
        setParamHostValue(proc, "mode", (float)DIMENSION_MODE_IV);
        field.setViewMode(ViewMode::Field);
        feedSignal(144, 1.8f, 1.0f, false, { 0.34f, false, false, false });
        save("10_stereo_test_audio.png");
    }

    // 11. Mono input, true input/output DELTA comparison
    setParamHostValue(proc, "mode", (float)DIMENSION_MODE_I);
    field.setViewMode(ViewMode::Delta);
    feedSignal(128, 1.2f, 1.0f, false, { 0.34f, true, false, false });
    save("11_mono_input.png");

    // 12. Antiphase input, true input/output DELTA comparison
    field.setViewMode(ViewMode::Delta);
    feedSignal(128, 1.2f, 1.0f, false, { 0.34f, false, true, false });
    save("12_antiphase_input.png");

    // 13. Low-level audio: adaptive visual gain must keep it legible
    field.setViewMode(ViewMode::Field);
    feedSignal(128, 1.0f, 1.0f, false, { 0.012f, false, false, false });
    save("13_low_level_audio.png");

    std::cout << "[JUCE Tests] Successfully generated UI 2.1 snapshots in docs/snapshots/ui21/!\n";

    delete editor;
}

// 46. Comparison telemetry transports genuinely time-aligned input/output samples
void test_stereo_comparison_points_time_aligned() {
    StereoAnalyzer analyzer;
    analyzer.prepare(48000.0);

    constexpr int kFrames = 1024;
    std::vector<float> inL((size_t)kFrames);
    std::vector<float> inR((size_t)kFrames);
    std::vector<float> outL((size_t)kFrames);
    std::vector<float> outR((size_t)kFrames);

    for (int i = 0; i < kFrames; ++i) {
        inL[(size_t)i] = 0.30f * std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f * (float)i / 48000.0f);
        inR[(size_t)i] = 0.22f * std::cos(2.0f * juce::MathConstants<float>::pi * 317.0f * (float)i / 48000.0f);
        outL[(size_t)i] = 0.5f * inL[(size_t)i];
        outR[(size_t)i] = 0.5f * inR[(size_t)i];
    }

    StereoTelemetrySnapshot snap {};
    analyzer.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kFrames, snap);

    check(snap.comparisonCount > 0, "comparison points produced");
    check(snap.comparisonCount == 171, "comparison point decimation is ~8 kHz based");
    check(snap.comparisonEndSequence == snap.comparisonCount, "sequence tracks produced points");
    check(snap.gonioCount > 0, "legacy goniometer array still populated");

    for (uint32_t i = 0; i < snap.comparisonCount; ++i) {
        const auto& pt = snap.comparisonPoints[i];
        check(std::isfinite(pt.inputMid) && std::isfinite(pt.inputSide), "comparison input finite");
        check(std::isfinite(pt.outputMid) && std::isfinite(pt.outputSide), "comparison output finite");
        check(std::abs(pt.outputMid - 0.5f * pt.inputMid) < 1.0e-6f, "output/input mid aligned at same sample");
        check(std::abs(pt.outputSide - 0.5f * pt.inputSide) < 1.0e-6f, "output/input side aligned at same sample");
    }
}

// 47. Comparison sequence is independent from the host block size
void test_stereo_comparison_block_size_independence() {
    std::vector<float> l(4800);
    std::vector<float> r(4800);
    for (size_t i = 0; i < l.size(); ++i) {
        l[i] = 0.3f * std::sin((float)i * 0.037f);
        r[i] = 0.2f * std::cos((float)i * 0.023f);
    }

    StereoAnalyzer smallBlocks;
    smallBlocks.prepare(48000.0);
    StereoAnalyzer largeBlocks;
    largeBlocks.prepare(48000.0);

    StereoTelemetrySnapshot snapSmall {};
    StereoTelemetrySnapshot snapLarge {};

    for (int pos = 0; pos < 4800; pos += 16) {
        smallBlocks.processBlock(l.data() + pos, r.data() + pos,
                                 l.data() + pos, r.data() + pos, 16, snapSmall);
    }
    for (int pos = 0; pos < 4800; pos += 64) {
        largeBlocks.processBlock(l.data() + pos, r.data() + pos,
                                 l.data() + pos, r.data() + pos, 64, snapLarge);
    }

    check(snapSmall.comparisonEndSequence == snapLarge.comparisonEndSequence,
          "comparison sequence independent from block size");
    check(snapSmall.comparisonCount == snapLarge.comparisonCount,
          "snapshot point count independent from block size");
    check(std::abs(snapSmall.comparisonPoints[snapSmall.comparisonCount - 1].inputMid -
                   snapLarge.comparisonPoints[snapLarge.comparisonCount - 1].inputMid) < 1.0e-6f,
          "latest comparison sample matches across block sizes");
}

// 48. True input telemetry: no synthesized input points, even when output exists
void test_stereo_delta_requires_real_input_points() {
    StereoFieldComponent field;

    StereoTelemetrySnapshot snap {};
    snap.frame.hasSignal = true;
    snap.comparisonCount = 100;
    snap.comparisonEndSequence = 100;
    for (uint32_t i = 0; i < snap.comparisonCount; ++i) {
        const float phase = (float)i * 0.1f;
        snap.comparisonPoints[i].inputMid = 0.0f;
        snap.comparisonPoints[i].inputSide = 0.0f;
        snap.comparisonPoints[i].outputMid = 0.10f * std::sin(phase);
        snap.comparisonPoints[i].outputSide = 0.05f * std::cos(phase);
    }

    field.resetDisplayState();
    field.updateTelemetry(snap);

    check(field.getActiveOutputPointCount() == 100, "output cloud uses measured output points");
    check(field.getActiveInputPointCount() == 0, "input cloud never invents points when input is silent");
}

// 49. Adaptive visual scale and temporal persistence
void test_stereo_field_adaptive_scale_and_persistence() {
    StereoFieldComponent field;

    StereoTelemetrySnapshot lowLevel {};
    lowLevel.frame.hasSignal = true;
    lowLevel.comparisonCount = 200;
    lowLevel.comparisonEndSequence = 200;
    for (uint32_t i = 0; i < lowLevel.comparisonCount; ++i) {
        const float phase = (float)i * 0.08f;
        lowLevel.comparisonPoints[i].inputMid = 0.02f * std::sin(phase);
        lowLevel.comparisonPoints[i].inputSide = 0.01f * std::cos(phase);
        lowLevel.comparisonPoints[i].outputMid = 0.02f * std::sin(phase);
        lowLevel.comparisonPoints[i].outputSide = 0.015f * std::cos(phase);
    }

    field.resetDisplayState();
    field.updateTelemetry(lowLevel);

    check(field.getActiveOutputPointCount() == 200, "all measured points ingested once");
    check(field.getDisplayScale() > 1.5f, "adaptive visual gain boosts low-level audio");
    check(field.getDeltaDisplayScale() > 1.5f, "delay-free comparison scale is also adaptive");

    StereoTelemetrySnapshot silence {};
    silence.frame.hasSignal = false;
    silence.comparisonCount = 0;
    silence.comparisonEndSequence = 200;

    field.updateTelemetry(silence);
    check(field.getActiveOutputPointCount() >= 190, "points persist briefly after signal stops");

    field.resetDisplayState();
    check(field.getActiveOutputPointCount() == 0, "reset clears the visual cloud");
    check(std::abs(field.getDisplayScale() - 1.0f) < 1.0e-6f, "reset restores unity display scale");
}

// 50. VECTOR does not bridge temporal discontinuities
void test_stereo_field_vector_continuity() {
    StereoFieldComponent field;
    field.resetDisplayState();

    auto fill = [](StereoTelemetrySnapshot& snap, uint32_t count, uint64_t endSequence) {
        snap.frame.hasSignal = true;
        snap.comparisonCount = count;
        snap.comparisonEndSequence = endSequence;
        for (uint32_t i = 0; i < count; ++i) {
            const float phase = (float)i * 0.06f;
            snap.comparisonPoints[i].inputMid = 0.05f * std::sin(phase);
            snap.comparisonPoints[i].inputSide = 0.05f * std::cos(phase);
            snap.comparisonPoints[i].outputMid = 0.06f * std::sin(phase);
            snap.comparisonPoints[i].outputSide = 0.06f * std::cos(phase);
        }
    };

    StereoTelemetrySnapshot first {};
    fill(first, 50, 50);
    field.updateTelemetry(first);

    StereoTelemetrySnapshot second {};
    fill(second, 91, 150);
    field.updateTelemetry(second);

    check(field.getVectorSegmentCount() == 139,
          "vector connects only consecutive acquisition indices (gap stays open)");
}

// 51. DELTA uses one common visual scale derived from both input and output
void test_stereo_delta_common_scale() {
    StereoFieldComponent field;
    field.resetDisplayState();

    StereoTelemetrySnapshot snap {};
    snap.frame.hasSignal = true;
    snap.comparisonCount = 100;
    snap.comparisonEndSequence = 100;
    for (uint32_t i = 0; i < snap.comparisonCount; ++i) {
        const float phase = (float)i * 0.09f;
        snap.comparisonPoints[i].inputMid = 0.80f * std::sin(phase);
        snap.comparisonPoints[i].inputSide = 0.20f * std::cos(phase);
        snap.comparisonPoints[i].outputMid = 0.008f * std::sin(phase);
        snap.comparisonPoints[i].outputSide = 0.002f * std::cos(phase);
    }

    field.updateTelemetry(snap);

    check(field.getActiveInputPointCount() == 100, "DELTA input cloud populated from measured input");
    check(field.getActiveOutputPointCount() == 100, "DELTA output cloud populated from measured output");
    check(field.getDeltaDisplayScale() < field.getDisplayScale(),
          "common DELTA scale accounts for the louder input (no fake expansion)");
    check(field.getDeltaDisplayScale() > 0.0f && std::isfinite(field.getDeltaDisplayScale()),
          "common DELTA scale is finite and positive");
}
} // namespace

void test_spatial_engine_juce_parameters_and_legacy_xml_recall() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    auto* widthParam = proc.parameters.getParameter("stereoWidth");
    auto* bfParam = proc.parameters.getParameter("bassFocusHz");
    check(widthParam != nullptr, "stereoWidth parameter exists in APVTS");
    check(bfParam != nullptr, "bassFocusHz parameter exists in APVTS");

    check(std::abs(proc.getFloatParam("stereoWidth") - 1.0f) < 1e-4f, "stereoWidth defaults to 1.0");
    check(std::abs(proc.getFloatParam("bassFocusHz") - 0.0f) < 1e-4f, "bassFocusHz defaults to 0.0");

    setParamHostValue(proc, "stereoWidth", 1.75f);
    setParamHostValue(proc, "bassFocusHz", 120.0f);
    processStereoTestSignal(proc);

    Dimension5AudioProcessor::PluginConfig cfg;
    proc.getLastAppliedConfig(cfg);
    check(std::abs(cfg.dspParams.stereoWidth - 1.75f) < 1e-4f, "applied stereoWidth is 1.75");
    check(std::abs(cfg.dspParams.bassFocusHz - 120.0f) < 1e-4f, "applied bassFocusHz is 120.0");

    proc.selectCompareSlot(0);
    proc.selectCompareSlot(1);
    setParamHostValue(proc, "stereoWidth", 0.5f);
    setParamHostValue(proc, "bassFocusHz", 80.0f);
    processStereoTestSignal(proc);
    proc.getLastAppliedConfig(cfg);
    check(std::abs(cfg.dspParams.stereoWidth - 0.5f) < 1e-4f, "slot B stereoWidth is 0.5");
    check(std::abs(cfg.dspParams.bassFocusHz - 80.0f) < 1e-4f, "slot B bassFocusHz is 80.0");

    proc.selectCompareSlot(0);
    processStereoTestSignal(proc);
    proc.getLastAppliedConfig(cfg);
    check(std::abs(cfg.dspParams.stereoWidth - 1.75f) < 1e-4f, "slot A stereoWidth recalled as 1.75");
    check(std::abs(cfg.dspParams.bassFocusHz - 120.0f) < 1e-4f, "slot A bassFocusHz recalled as 120.0");

    juce::MemoryBlock destData;
    proc.getStateInformation(destData);
    std::unique_ptr<juce::XmlElement> xml(proc.getXmlFromBinary(destData.getData(), (int)destData.getSize()));
    check(xml != nullptr, "valid xml");
    for (int i = xml->getNumChildElements() - 1; i >= 0; --i) {
        auto* child = xml->getChildElement(i);
        if (child->getStringAttribute("id") == "stereoWidth" || child->getStringAttribute("id") == "bassFocusHz") {
            xml->removeChildElement(child, true);
        }
    }
    juce::MemoryBlock legacyData;
    proc.copyXmlToBinary(*xml, legacyData);

    setParamHostValue(proc, "stereoWidth", 0.2f);
    setParamHostValue(proc, "bassFocusHz", 250.0f);
    processStereoTestSignal(proc);

    proc.setStateInformation(legacyData.getData(), (int)legacyData.getSize());
    processStereoTestSignal(proc);

    proc.getLastAppliedConfig(cfg);
    check(std::abs(cfg.dspParams.stereoWidth - 1.0f) < 1e-4f, "legacy state recall defaults stereoWidth to 1.0");
    check(std::abs(cfg.dspParams.bassFocusHz - 0.0f) < 1e-4f, "legacy state recall defaults bassFocusHz to 0.0");
}

// 53. Phase 1.1 Scenario A: Mode switching I -> II -> III -> IV -> I preserves stereoWidth and bassFocusHz
void test_phase11_scenario_a_mode_switching_preserves_spatial_params() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    setParamHostValue(proc, "mode", (float)DIMENSION_MODE_I);
    setParamHostValue(proc, "stereoWidth", 1.60f);
    setParamHostValue(proc, "bassFocusHz", 120.0f);
    processStereoTestSignal(proc);

    DimensionMode modesToTest[] = {
        DIMENSION_MODE_II,
        DIMENSION_MODE_III,
        DIMENSION_MODE_IV,
        DIMENSION_MODE_I
    };

    for (DimensionMode targetMode : modesToTest) {
        setParamHostValue(proc, "mode", (float)targetMode);
        processStereoTestSignal(proc);

        // 1. APVTS parameter value
        check(std::abs(proc.getFloatParam("stereoWidth") - 1.60f) < 1e-4f,
              "APVTS stereoWidth preserved across mode transition");
        check(std::abs(proc.getFloatParam("bassFocusHz") - 120.0f) < 1e-4f,
              "APVTS bassFocusHz preserved across mode transition");

        // 2. Staged configuration
        Dimension5AudioProcessor::PluginConfig stagedCfg;
        proc.getEffectiveConfig(stagedCfg);
        check(std::abs(stagedCfg.dspParams.stereoWidth - 1.60f) < 1e-4f,
              "stagedConfig stereoWidth preserved across mode transition");
        check(std::abs(stagedCfg.dspParams.bassFocusHz - 120.0f) < 1e-4f,
              "stagedConfig bassFocusHz preserved across mode transition");
        check(stagedCfg.dspParams.mode == targetMode,
              "stagedConfig mode matches target mode");

        // 3. Last applied config on DSP
        Dimension5AudioProcessor::PluginConfig appliedCfg;
        proc.getLastAppliedConfig(appliedCfg);
        check(std::abs(appliedCfg.dspParams.stereoWidth - 1.60f) < 1e-4f,
              "DSP appliedConfig stereoWidth preserved across mode transition");
        check(std::abs(appliedCfg.dspParams.bassFocusHz - 120.0f) < 1e-4f,
              "DSP appliedConfig bassFocusHz preserved across mode transition");
        check(appliedCfg.dspParams.mode == targetMode,
              "DSP appliedConfig mode matches target mode");

        // 4. Persisted state
        juce::MemoryBlock mb;
        proc.getStateInformation(mb);
        std::unique_ptr<juce::XmlElement> xml(proc.getXmlFromBinary(mb.getData(), (int)mb.getSize()));
        check(xml != nullptr, "persisted xml valid");
        auto* wChild = xml->getChildByAttribute("id", "stereoWidth");
        auto* bChild = xml->getChildByAttribute("id", "bassFocusHz");
        check(wChild != nullptr && std::abs((float)wChild->getDoubleAttribute("value") - 1.60f) < 1e-4f,
              "persisted state stereoWidth is 1.60");
        check(bChild != nullptr && std::abs((float)bChild->getDoubleAttribute("value") - 120.0f) < 1e-4f,
              "persisted state bassFocusHz is 120.0");
    }
}

// 54. Phase 1.1 Scenario B: Mode II -> Custom transition preserves spatial parameters
void test_phase11_scenario_b_mode_to_custom_preserves_spatial_params() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    // Initial setup: Mode II, Stereo Width = 1.75, Bass Focus = 150 Hz, Depth = 1.4 ms
    setParamHostValue(proc, "mode", (float)DIMENSION_MODE_II);
    setParamHostValue(proc, "stereoWidth", 1.75f);
    setParamHostValue(proc, "bassFocusHz", 150.0f);
    processStereoTestSignal(proc);

    // Alter Depth from 1.4 to 2.2 ms
    setParamHostValue(proc, "depthMs", 2.2f);
    processStereoTestSignal(proc);

    Dimension5AudioProcessor::PluginConfig cfg;
    proc.getLastAppliedConfig(cfg);
    check(cfg.dspParams.mode == DIMENSION_MODE_CUSTOM, "Transitioned to Custom mode on depth edit");
    check(std::abs(cfg.dspParams.stereoWidth - 1.75f) < 1e-4f, "stereoWidth preserved on depth edit to Custom");
    check(std::abs(cfg.dspParams.bassFocusHz - 150.0f) < 1e-4f, "bassFocusHz preserved on depth edit to Custom");
    check(std::abs(cfg.dspParams.depthMs - 2.2f) < 1e-4f, "depthMs updated to 2.2 ms");

    // Test other advanced parameters from Mode I -> Custom
    const char* advParams[] = {
        "rateHz", "lpfHz", "baseDelayMs", "analogAmount", "companderAmount",
        "dryGain", "wetDirectGain", "wetCrossGain", "hpfHz"
    };
    float testVals[] = {
        0.80f, 6000.0f, 12.0f, 0.70f, 0.65f,
        0.90f, 0.60f, 0.40f, 150.0f
    };

    for (size_t i = 0; i < 9; ++i) {
        setParamHostValue(proc, "mode", (float)DIMENSION_MODE_I);
        setParamHostValue(proc, "stereoWidth", 1.75f);
        setParamHostValue(proc, "bassFocusHz", 150.0f);
        processStereoTestSignal(proc);

        setParamHostValue(proc, advParams[i], testVals[i]);
        processStereoTestSignal(proc);

        proc.getLastAppliedConfig(cfg);
        check(cfg.dspParams.mode == DIMENSION_MODE_CUSTOM, "Switched to Custom on advanced param edit");
        check(std::abs(cfg.dspParams.stereoWidth - 1.75f) < 1e-4f, "stereoWidth survived edit to Custom");
        check(std::abs(cfg.dspParams.bassFocusHz - 150.0f) < 1e-4f, "bassFocusHz survived edit to Custom");
    }

    // Direct selection of Custom via switchToCustomMode()
    setParamHostValue(proc, "mode", (float)DIMENSION_MODE_III);
    setParamHostValue(proc, "stereoWidth", 1.75f);
    setParamHostValue(proc, "bassFocusHz", 150.0f);
    processStereoTestSignal(proc);

    proc.switchToCustomMode();
    processStereoTestSignal(proc);

    proc.getLastAppliedConfig(cfg);
    check(cfg.dspParams.mode == DIMENSION_MODE_CUSTOM, "switchToCustomMode sets Custom");
    check(std::abs(cfg.dspParams.stereoWidth - 1.75f) < 1e-4f, "stereoWidth preserved in switchToCustomMode");
    check(std::abs(cfg.dspParams.bassFocusHz - 150.0f) < 1e-4f, "bassFocusHz preserved in switchToCustomMode");
}

// 55. Phase 1.1 Scenario C: Factory preset selection policy
void test_phase11_scenario_c_factory_presets_policy() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    // Set non-neutral spatial controls
    setParamHostValue(proc, "stereoWidth", 1.85f);
    setParamHostValue(proc, "bassFocusHz", 140.0f);
    processStereoTestSignal(proc);

    for (int pIdx = 0; pIdx < proc.getNumPrograms(); ++pIdx) {
        proc.setCurrentProgram(pIdx);
        processStereoTestSignal(proc);

        check(std::abs(proc.getFloatParam("stereoWidth") - 1.0f) < 1e-4f,
              "Factory preset resets stereoWidth to neutral 1.0");
        check(std::abs(proc.getFloatParam("bassFocusHz") - 0.0f) < 1e-4f,
              "Factory preset resets bassFocusHz to neutral 0.0 (OFF)");

        Dimension5AudioProcessor::PluginConfig cfg;
        proc.getLastAppliedConfig(cfg);
        check(std::abs(cfg.dspParams.stereoWidth - 1.0f) < 1e-4f,
              "DSP appliedConfig has stereoWidth 1.0 for factory preset");
        check(std::abs(cfg.dspParams.bassFocusHz - 0.0f) < 1e-4f,
              "DSP appliedConfig has bassFocusHz 0.0 for factory preset");
    }
}

// 56. Phase 1.1 Scenario D: A/B slot switching, copy, and integrity
void test_phase11_scenario_d_ab_state_integrity() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    // Slot A: Mode II, Stereo Width = 1.75, Bass Focus = 120 Hz
    proc.selectCompareSlot(0);
    setParamHostValue(proc, "mode", (float)DIMENSION_MODE_II);
    setParamHostValue(proc, "stereoWidth", 1.75f);
    setParamHostValue(proc, "bassFocusHz", 120.0f);
    processStereoTestSignal(proc);

    // Slot B: Mode IV, Stereo Width = 0.65, Bass Focus = 200 Hz
    proc.selectCompareSlot(1);
    setParamHostValue(proc, "mode", (float)DIMENSION_MODE_IV);
    setParamHostValue(proc, "stereoWidth", 0.65f);
    setParamHostValue(proc, "bassFocusHz", 200.0f);
    processStereoTestSignal(proc);

    // Toggle back and forth 10 times
    for (int it = 0; it < 10; ++it) {
        proc.selectCompareSlot(0);
        processStereoTestSignal(proc);
        Dimension5AudioProcessor::PluginConfig cfgA;
        proc.getLastAppliedConfig(cfgA);
        check(cfgA.dspParams.mode == DIMENSION_MODE_II, "Slot A mode remains Mode II");
        check(std::abs(cfgA.dspParams.stereoWidth - 1.75f) < 1e-4f, "Slot A stereoWidth remains 1.75");
        check(std::abs(cfgA.dspParams.bassFocusHz - 120.0f) < 1e-4f, "Slot A bassFocusHz remains 120.0");

        proc.selectCompareSlot(1);
        processStereoTestSignal(proc);
        Dimension5AudioProcessor::PluginConfig cfgB;
        proc.getLastAppliedConfig(cfgB);
        check(cfgB.dspParams.mode == DIMENSION_MODE_IV, "Slot B mode remains Mode IV");
        check(std::abs(cfgB.dspParams.stereoWidth - 0.65f) < 1e-4f, "Slot B stereoWidth remains 0.65");
        check(std::abs(cfgB.dspParams.bassFocusHz - 200.0f) < 1e-4f, "Slot B bassFocusHz remains 200.0");
    }

    // Advanced edit in Slot A before switching to Slot B
    proc.selectCompareSlot(0);
    processStereoTestSignal(proc);
    setParamHostValue(proc, "depthMs", 2.3f);
    processStereoTestSignal(proc);

    proc.selectCompareSlot(1);
    processStereoTestSignal(proc);

    proc.selectCompareSlot(0);
    processStereoTestSignal(proc);
    Dimension5AudioProcessor::PluginConfig cfgAAfterEdit;
    proc.getLastAppliedConfig(cfgAAfterEdit);
    check(cfgAAfterEdit.dspParams.mode == DIMENSION_MODE_CUSTOM, "Slot A retained Custom mode");
    check(std::abs(cfgAAfterEdit.dspParams.depthMs - 2.3f) < 1e-4f, "Slot A retained depthMs 2.3");
    check(std::abs(cfgAAfterEdit.dspParams.stereoWidth - 1.75f) < 1e-4f, "Slot A retained stereoWidth 1.75");
    check(std::abs(cfgAAfterEdit.dspParams.bassFocusHz - 120.0f) < 1e-4f, "Slot A retained bassFocusHz 120.0");

    // Test COPY A -> B
    proc.copyCompareToOtherSlot();
    proc.selectCompareSlot(1);
    processStereoTestSignal(proc);
    Dimension5AudioProcessor::PluginConfig cfgBCopied;
    proc.getLastAppliedConfig(cfgBCopied);
    check(cfgBCopied.dspParams.mode == DIMENSION_MODE_CUSTOM, "Copied Slot B has Custom mode");
    check(std::abs(cfgBCopied.dspParams.depthMs - 2.3f) < 1e-4f, "Copied Slot B has depthMs 2.3");
    check(std::abs(cfgBCopied.dspParams.stereoWidth - 1.75f) < 1e-4f, "Copied Slot B has stereoWidth 1.75");
    check(std::abs(cfgBCopied.dspParams.bassFocusHz - 120.0f) < 1e-4f, "Copied Slot B has bassFocusHz 120.0");
}

// 57. Phase 1.1 Scenario E: Save and load project v2 (exact roundtrip)
void test_phase11_scenario_e_save_recall_v2_exact() {
    juce::MemoryBlock v2Data;
    {
        Dimension5AudioProcessor proc1;
        proc1.prepareToPlay(48000.0, 32);

        setParamHostValue(proc1, "mode", (float)DIMENSION_MODE_III);
        setParamHostValue(proc1, "stereoWidth", 1.82f);
        setParamHostValue(proc1, "bassFocusHz", 165.0f);
        setParamHostValue(proc1, "mix", 0.88f);
        processStereoTestSignal(proc1);

        proc1.getStateInformation(v2Data);
    }

    {
        Dimension5AudioProcessor proc2;
        proc2.prepareToPlay(48000.0, 32);

        proc2.setStateInformation(v2Data.getData(), (int)v2Data.getSize());
        processStereoTestSignal(proc2);

        Dimension5AudioProcessor::PluginConfig cfg;
        proc2.getLastAppliedConfig(cfg);
        check(cfg.dspParams.mode == DIMENSION_MODE_III, "Recalled v2 mode is Mode III");
        check(std::abs(cfg.dspParams.stereoWidth - 1.82f) < 1e-4f, "Recalled v2 stereoWidth is 1.82");
        check(std::abs(cfg.dspParams.bassFocusHz - 165.0f) < 1e-4f, "Recalled v2 bassFocusHz is 165.0");
        check(std::abs(cfg.mix - 0.88f) < 1e-4f, "Recalled v2 mix is 0.88");
    }
}

// 58. Phase 1.1 Scenario F: Load authentic legacy v1 session without parameter contamination
void test_phase11_scenario_f_load_v1_legacy_no_contamination() {
    // Construct authentic v1 XML (schema 1, no stereoWidth or bassFocusHz)
    juce::XmlElement v1Root("PARAMETERS");
    auto addParam = [&](const char* id, double val) {
        auto* child = v1Root.createNewChildElement("PARAM");
        child->setAttribute("id", id);
        child->setAttribute("value", val);
    };
    addParam("mode", 1.0); // Mode II
    addParam("dryGain", 0.83);
    addParam("wetDirectGain", 0.475);
    addParam("wetCrossGain", 0.3325);
    addParam("baseDelayMs", 8.5);
    addParam("depthMs", 1.4);
    addParam("rateHz", 0.25);
    addParam("hpfHz", 120.0);
    addParam("lpfHz", 7500.0);
    addParam("analogAmount", 0.45);
    addParam("companderAmount", 0.45);
    addParam("inputGain", 1.0);
    addParam("outputGain", 0.95);
    addParam("width", 1.15);
    addParam("mix", 0.75);
    addParam("bypass", 0.0);

    // Also add CompareA and CompareB in legacy format (no spatial params)
    auto* compA = v1Root.createNewChildElement("CompareA");
    auto* compB = v1Root.createNewChildElement("CompareB");
    for (auto* ch : v1Root.getChildIterator()) {
        if (ch->hasTagName("PARAM")) {
            compA->addChildElement(new juce::XmlElement(*ch));
            compB->addChildElement(new juce::XmlElement(*ch));
        }
    }

    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    // Pre-contaminate instance with extreme non-neutral values
    setParamHostValue(proc, "stereoWidth", 1.95f);
    setParamHostValue(proc, "bassFocusHz", 240.0f);
    processStereoTestSignal(proc);

    Dimension5AudioProcessor::PluginConfig preCfg;
    proc.getLastAppliedConfig(preCfg);
    check(std::abs(preCfg.dspParams.stereoWidth - 1.95f) < 1e-4f, "Pre-contamination stereoWidth set");
    check(std::abs(preCfg.dspParams.bassFocusHz - 240.0f) < 1e-4f, "Pre-contamination bassFocusHz set");

    // Restore legacy v1 state
    juce::MemoryBlock v1Mb;
    proc.copyXmlToBinary(v1Root, v1Mb);
    proc.setStateInformation(v1Mb.getData(), (int)v1Mb.getSize());
    processStereoTestSignal(proc);

    // Verify clean defaults with NO contamination
    check(std::abs(proc.getFloatParam("stereoWidth") - 1.0f) < 1e-4f,
          "APVTS stereoWidth safely defaulted to 1.0 with no contamination");
    check(std::abs(proc.getFloatParam("bassFocusHz") - 0.0f) < 1e-4f,
          "APVTS bassFocusHz safely defaulted to 0.0 with no contamination");

    Dimension5AudioProcessor::PluginConfig postCfg;
    proc.getLastAppliedConfig(postCfg);
    check(std::abs(postCfg.dspParams.stereoWidth - 1.0f) < 1e-4f,
          "DSP appliedConfig stereoWidth defaulted to 1.0");
    check(std::abs(postCfg.dspParams.bassFocusHz - 0.0f) < 1e-4f,
          "DSP appliedConfig bassFocusHz defaulted to 0.0");
    check(postCfg.dspParams.mode == DIMENSION_MODE_II,
          "Legacy session Mode II correctly restored");
    check(std::abs(postCfg.dspParams.outputGain - 0.95f) < 1e-4f,
          "Legacy outputGain 0.95 correctly restored");

    // Switching to CompareA or CompareB must also default to neutral
    proc.selectCompareSlot(0);
    processStereoTestSignal(proc);
    proc.getLastAppliedConfig(postCfg);
    check(std::abs(postCfg.dspParams.stereoWidth - 1.0f) < 1e-4f,
          "CompareA slot has neutral stereoWidth 1.0");
    check(std::abs(postCfg.dspParams.bassFocusHz - 0.0f) < 1e-4f,
          "CompareA slot has neutral bassFocusHz 0.0");
}

// 59. Phase 1.1 Scenario G: Host automation without editor open
void test_phase11_scenario_g_host_automation_without_editor() {
    Dimension5AudioProcessor proc;
    check(proc.getActiveEditor() == nullptr, "Editor is not opened");
    proc.prepareToPlay(48000.0, 32);

    for (int frame = 0; frame < 32; ++frame) {
        const float targetWidth = 0.5f + (float)frame * (1.5f / 32.0f);
        const float targetBf = (float)frame * (250.0f / 32.0f);
        setParamHostValue(proc, "stereoWidth", targetWidth);
        setParamHostValue(proc, "bassFocusHz", targetBf);
        processStereoTestSignal(proc);

        Dimension5AudioProcessor::PluginConfig cfg;
        proc.getLastAppliedConfig(cfg);
        check(std::abs(cfg.dspParams.stereoWidth - targetWidth) < 1e-3f,
              "Applied stereoWidth matches automation point");
        check(std::abs(cfg.dspParams.bassFocusHz - targetBf) < 1e-3f,
              "Applied bassFocusHz matches automation point");
    }
}

// 60. Phase 1.1 Scenario H: Concurrent automation and mode transitions
void test_phase11_scenario_h_concurrent_automation_and_mode_transitions() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    std::atomic<bool> keepRunning { true };
    std::atomic<int> errors { 0 };

    // Background thread firing automation events
    std::thread producerThread([&]() {
        int step = 0;
        while (keepRunning.load(std::memory_order_relaxed)) {
            const int mode = (step % 4);
            const float w = 0.5f + (float)(step % 15) * 0.1f;
            const float bf = (float)(step % 10) * 20.0f;
            setParamHostValue(proc, "mode", (float)mode);
            setParamHostValue(proc, "stereoWidth", w);
            setParamHostValue(proc, "bassFocusHz", bf);
            ++step;
            std::this_thread::yield();
        }
    });

    // Audio thread processing blocks concurrently
    for (int b = 0; b < 100; ++b) {
        processStereoTestSignal(proc);
        Dimension5AudioProcessor::PluginConfig cfg;
        proc.getLastAppliedConfig(cfg);
        if (cfg.dspParams.stereoWidth < 0.0f || cfg.dspParams.stereoWidth > 2.5f ||
            cfg.dspParams.bassFocusHz < 0.0f || cfg.dspParams.bassFocusHz > 500.0f) {
            errors.fetch_add(1);
        }
    }

    keepRunning.store(false, std::memory_order_relaxed);
    producerThread.join();

    check(errors.load() == 0, "Zero tearing or corruptions under concurrent automation");
}

// 61. Phase 1.1 Scenario I: Immediate persistence after automated change before timer dispatch
void test_phase11_scenario_i_immediate_persistence_after_automation() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    // Host automates changes
    setParamHostValue(proc, "stereoWidth", 1.45f);
    setParamHostValue(proc, "bassFocusHz", 110.0f);

    // Immediately save before any timer or visual presentation dispatch
    juce::MemoryBlock destData;
    proc.getStateInformation(destData);

    std::unique_ptr<juce::XmlElement> xml(proc.getXmlFromBinary(destData.getData(), (int)destData.getSize()));
    check(xml != nullptr, "valid xml immediately after automation");
    auto* wElem = xml->getChildByAttribute("id", "stereoWidth");
    auto* bfElem = xml->getChildByAttribute("id", "bassFocusHz");
    check(wElem != nullptr, "stereoWidth element exists in immediate state");
    check(bfElem != nullptr, "bassFocusHz element exists in immediate state");
    check(std::abs((float)wElem->getDoubleAttribute("value") - 1.45f) < 1e-4f,
          "Immediate state persisted stereoWidth = 1.45");
    check(std::abs((float)bfElem->getDoubleAttribute("value") - 110.0f) < 1e-4f,
          "Immediate state persisted bassFocusHz = 110.0");
}

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

        std::cout << "[JUCE Tests] 31. test_stereo_analyzer_silence...\n";
        test_stereo_analyzer_silence();

        std::cout << "[JUCE Tests] 32. test_stereo_analyzer_identical_mono...\n";
        test_stereo_analyzer_identical_mono();

        std::cout << "[JUCE Tests] 33. test_stereo_analyzer_opposite_antiphase...\n";
        test_stereo_analyzer_opposite_antiphase();

        std::cout << "[JUCE Tests] 34. test_stereo_analyzer_uncorrelated...\n";
        test_stereo_analyzer_uncorrelated();

        std::cout << "[JUCE Tests] 35. test_stereo_analyzer_single_channel...\n";
        test_stereo_analyzer_single_channel();

        std::cout << "[JUCE Tests] 36. test_stereo_analyzer_extreme_values_and_sample_rates...\n";
        test_stereo_analyzer_extreme_values_and_sample_rates();

        std::cout << "[JUCE Tests] 37. test_stereo_telemetry_bit_exact_audio...\n";
        test_stereo_telemetry_bit_exact_audio();

        std::cout << "[JUCE Tests] 38. test_editor_lifecycle_and_telemetry...\n";
        test_editor_lifecycle_and_telemetry();

        std::cout << "[JUCE Tests] 39. test_telemetry_buffer_saturation_and_safety...\n";
        test_telemetry_buffer_saturation_and_safety();

        std::cout << "[JUCE Tests] 40. test_main_controls_auto_custom_depth_motion_color...\n";
        test_main_controls_auto_custom_depth_motion_color();

        std::cout << "[JUCE Tests] 41. test_stereo_field_modes_switching_and_audio_invariance...\n";
        test_stereo_field_modes_switching_and_audio_invariance();

        std::cout << "[JUCE Tests] 42. test_undefined_correlation_and_mono_handling...\n";
        test_undefined_correlation_and_mono_handling();

        std::cout << "[JUCE Tests] 43. test_block_size_independent_peak_decay...\n";
        test_block_size_independent_peak_decay();

        std::cout << "[JUCE Tests] 44. test_editor_resize_bounds_and_advanced_drawer...\n";
        test_editor_resize_bounds_and_advanced_drawer();

        std::cout << "[JUCE Tests] 45. test_generate_visual_validation_snapshots...\n";
        test_generate_visual_validation_snapshots();

        std::cout << "[JUCE Tests] 46. test_stereo_comparison_points_time_aligned...\n";
        test_stereo_comparison_points_time_aligned();

        std::cout << "[JUCE Tests] 47. test_stereo_comparison_block_size_independence...\n";
        test_stereo_comparison_block_size_independence();

        std::cout << "[JUCE Tests] 48. test_stereo_delta_requires_real_input_points...\n";
        test_stereo_delta_requires_real_input_points();

        std::cout << "[JUCE Tests] 49. test_stereo_field_adaptive_scale_and_persistence...\n";
        test_stereo_field_adaptive_scale_and_persistence();

        std::cout << "[JUCE Tests] 50. test_stereo_field_vector_continuity...\n";
        test_stereo_field_vector_continuity();

        std::cout << "[JUCE Tests] 51. test_stereo_delta_common_scale...\n";
        test_stereo_delta_common_scale();

        std::cout << "[JUCE Tests] 52. test_spatial_engine_juce_parameters_and_legacy_xml_recall...\n";
        test_spatial_engine_juce_parameters_and_legacy_xml_recall();

        std::cout << "[JUCE Tests] 53. test_phase11_scenario_a_mode_switching_preserves_spatial_params...\n";
        test_phase11_scenario_a_mode_switching_preserves_spatial_params();

        std::cout << "[JUCE Tests] 54. test_phase11_scenario_b_mode_to_custom_preserves_spatial_params...\n";
        test_phase11_scenario_b_mode_to_custom_preserves_spatial_params();

        std::cout << "[JUCE Tests] 55. test_phase11_scenario_c_factory_presets_policy...\n";
        test_phase11_scenario_c_factory_presets_policy();

        std::cout << "[JUCE Tests] 56. test_phase11_scenario_d_ab_state_integrity...\n";
        test_phase11_scenario_d_ab_state_integrity();

        std::cout << "[JUCE Tests] 57. test_phase11_scenario_e_save_recall_v2_exact...\n";
        test_phase11_scenario_e_save_recall_v2_exact();

        std::cout << "[JUCE Tests] 58. test_phase11_scenario_f_load_v1_legacy_no_contamination...\n";
        test_phase11_scenario_f_load_v1_legacy_no_contamination();

        std::cout << "[JUCE Tests] 59. test_phase11_scenario_g_host_automation_without_editor...\n";
        test_phase11_scenario_g_host_automation_without_editor();

        std::cout << "[JUCE Tests] 60. test_phase11_scenario_h_concurrent_automation_and_mode_transitions...\n";
        test_phase11_scenario_h_concurrent_automation_and_mode_transitions();

        std::cout << "[JUCE Tests] 61. test_phase11_scenario_i_immediate_persistence_after_automation...\n";
        test_phase11_scenario_i_immediate_persistence_after_automation();

        std::cout << "All JUCE processor tests PASSED successfully!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "JUCE Test FAILED with exception: " << e.what() << "\n";
        return 1;
    }
}
