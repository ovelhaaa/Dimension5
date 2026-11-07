#include "PluginProcessor.h"

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

void test_processor_construction_and_prepare() {
    Dimension5AudioProcessor proc;
    check(proc.getName() == "Dimension5", "processor name is Dimension5");
    check(proc.getNumPrograms() >= 6, "has factory presets");

    proc.prepareToPlay(48000.0, 32);
    juce::AudioBuffer<float> buffer(2, 32);
    buffer.clear();
    for (int i = 0; i < 32; ++i) {
        buffer.setSample(0, i, 0.2f);
        buffer.setSample(1, i, 0.2f);
    }
    juce::MidiBuffer midi;
    proc.processBlock(buffer, midi);

    check(std::isfinite(buffer.getSample(0, 0)), "output L finite");
    check(std::isfinite(buffer.getSample(1, 0)), "output R finite");
}

void test_mode_selection_and_transition_to_custom() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    /* 1. Select Mode II */
    proc.parameters.getParameter("mode")->setValueNotifyingHost(
        proc.parameters.getParameter("mode")->convertTo0to1((float)DIMENSION_MODE_II));
    proc.applyModePresetToAdvancedParameters(DIMENSION_MODE_II);
    check(juce::roundToInt(proc.getFloatParam("mode")) == (int)DIMENSION_MODE_II, "mode is II");

    /* Switch to Custom without editing any advanced parameter */
    proc.switchToCustomMode();
    check(juce::roundToInt(proc.getFloatParam("mode")) == (int)DIMENSION_MODE_CUSTOM, "mode switched to Custom");

    /* Verify all effective parameters of Mode II were preserved into Custom */
    check(std::abs(proc.getFloatParam("dryGain") - 0.83f) < 1e-4f, "dryGain is 0.83f, not 0");
    check(std::abs(proc.getFloatParam("baseDelayMs") - 8.5f) < 1e-4f, "baseDelayMs is 8.5ms");
    check(std::abs(proc.getFloatParam("depthMs") - 1.4f) < 1e-4f, "depthMs is 1.4ms");
    check(std::abs(proc.getFloatParam("rateHz") - 0.25f) < 1e-4f, "rateHz is 0.25Hz");
    check(std::abs(proc.getFloatParam("hpfHz") - 120.0f) < 1e-4f, "hpfHz is 120Hz");
    check(std::abs(proc.getFloatParam("lpfHz") - 7500.0f) < 1e-4f, "lpfHz is 7500Hz");
    check(std::abs(proc.getFloatParam("analogAmount") - 0.45f) < 1e-4f, "analogAmount is 0.45");
    check(std::abs(proc.getFloatParam("companderAmount") - 0.45f) < 1e-4f, "companderAmount is 0.45");
    check(std::abs(proc.getFloatParam("wetDirectGain") - 0.475f) < 1e-4f, "wetDirectGain is 0.475");
    check(std::abs(proc.getFloatParam("wetCrossGain") - 0.3325f) < 1e-4f, "wetCrossGain is 0.3325");

    /* 2. Select Mode IV */
    proc.parameters.getParameter("mode")->setValueNotifyingHost(
        proc.parameters.getParameter("mode")->convertTo0to1((float)DIMENSION_MODE_IV));
    proc.applyModePresetToAdvancedParameters(DIMENSION_MODE_IV);
    check(juce::roundToInt(proc.getFloatParam("mode")) == (int)DIMENSION_MODE_IV, "mode is IV");

    /* Switch to Custom without editing */
    proc.switchToCustomMode();
    check(juce::roundToInt(proc.getFloatParam("mode")) == (int)DIMENSION_MODE_CUSTOM, "mode switched to Custom from IV");
    check(std::abs(proc.getFloatParam("dryGain") - 0.83f) < 1e-4f, "dryGain is 0.83f in Mode IV Custom");
    check(std::abs(proc.getFloatParam("baseDelayMs") - 11.5f) < 1e-4f, "baseDelayMs is 11.5ms in Mode IV Custom");
    check(std::abs(proc.getFloatParam("depthMs") - 2.4f) < 1e-4f, "depthMs is 2.4ms");
    check(std::abs(proc.getFloatParam("rateHz") - 0.50f) < 1e-4f, "rateHz is 0.50Hz");
    check(std::abs(proc.getFloatParam("hpfHz") - 140.0f) < 1e-4f, "hpfHz is 140Hz");
    check(std::abs(proc.getFloatParam("lpfHz") - 6500.0f) < 1e-4f, "lpfHz is 6500Hz");
    check(std::abs(proc.getFloatParam("analogAmount") - 0.60f) < 1e-4f, "analogAmount is 0.60");
    check(std::abs(proc.getFloatParam("companderAmount") - 0.60f) < 1e-4f, "companderAmount is 0.60");
    check(std::abs(proc.getFloatParam("wetDirectGain") - 0.55f) < 1e-4f, "wetDirectGain is 0.55");
    check(std::abs(proc.getFloatParam("wetCrossGain") - 0.385f) < 1e-4f, "wetCrossGain is 0.385");
}

void test_first_advanced_edit_after_mode_selection() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    /* Select Mode II */
    proc.parameters.getParameter("mode")->setValueNotifyingHost(
        proc.parameters.getParameter("mode")->convertTo0to1((float)DIMENSION_MODE_II));
    proc.applyModePresetToAdvancedParameters(DIMENSION_MODE_II);

    /* User edits baseDelayMs from 8.5 to 9.2 ms */
    proc.switchToCustomMode();
    auto* param = proc.parameters.getParameter("baseDelayMs");
    param->beginChangeGesture();
    param->setValueNotifyingHost(param->convertTo0to1(9.2f));
    param->endChangeGesture();

    check(juce::roundToInt(proc.getFloatParam("mode")) == (int)DIMENSION_MODE_CUSTOM, "mode is Custom after edit");
    check(std::abs(proc.getFloatParam("baseDelayMs") - 9.2f) < 1e-4f, "first edit is applied fully to 9.2ms");
    /* The other 9 advanced parameters retain Mode II's values */
    check(std::abs(proc.getFloatParam("dryGain") - 0.83f) < 1e-4f, "dryGain retained 0.83f");
    check(std::abs(proc.getFloatParam("depthMs") - 1.4f) < 1e-4f, "depthMs retained 1.4ms");
    check(std::abs(proc.getFloatParam("rateHz") - 0.25f) < 1e-4f, "rateHz retained 0.25Hz");
    check(std::abs(proc.getFloatParam("analogAmount") - 0.45f) < 1e-4f, "analogAmount retained 0.45");
}

void test_old_state_initialization_and_restore() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    /* Construct an XML state from an older session: Mode is II, but advanced params had old defaults */
    juce::XmlElement xml("PARAMETERS");
    xml.setAttribute("program", 0);
    xml.setAttribute("activeCompareSlot", 0);

    auto addParam = [&](const char* id, float val) {
        auto* child = xml.createNewChildElement("PARAM");
        child->setAttribute("id", id);
        child->setAttribute("value", (double)val);
    };

    addParam("mode", 1.0f); /* Mode II */
    addParam("baseDelayMs", 7.0f); /* Stale Mode I value */
    addParam("depthMs", 0.9f); /* Stale Mode I value */
    addParam("dryGain", 0.83f);
    addParam("inputGain", 1.25f);
    addParam("width", 1.10f);

    juce::MemoryBlock mem;
    Dimension5AudioProcessor::copyXmlToBinary(xml, mem);
    proc.setStateInformation(mem.getData(), (int)mem.getSize());

    check(juce::roundToInt(proc.getFloatParam("mode")) == (int)DIMENSION_MODE_II, "restored mode is Mode II");
    check(std::abs(proc.getFloatParam("inputGain") - 1.25f) < 1e-4f, "inputGain restored correctly");
    check(std::abs(proc.getFloatParam("width") - 1.10f) < 1e-4f, "width restored correctly");

    /* When switching to Custom, Mode II baseline must be used (8.5 ms, not the stale 7.0 ms from old XML) */
    proc.switchToCustomMode();
    check(std::abs(proc.getFloatParam("baseDelayMs") - 8.5f) < 1e-4f, "baseDelayMs aligned to Mode II baseline");
    check(std::abs(proc.getFloatParam("depthMs") - 1.4f) < 1e-4f, "depthMs aligned to Mode II baseline");
    check(std::abs(proc.getFloatParam("dryGain") - 0.83f) < 1e-4f, "dryGain is 0.83f");
}

void test_compare_ab_and_persistence() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    /* Slot A (slot 0) */
    check(proc.getActiveCompareSlot() == 0, "starts in slot A");
    proc.parameters.getParameter("inputGain")->setValueNotifyingHost(
        proc.parameters.getParameter("inputGain")->convertTo0to1(1.4f));

    /* Switch to Slot B (slot 1) */
    proc.selectCompareSlot(1);
    check(proc.getActiveCompareSlot() == 1, "active slot is B");
    proc.parameters.getParameter("inputGain")->setValueNotifyingHost(
        proc.parameters.getParameter("inputGain")->convertTo0to1(2.8f));
    proc.parameters.getParameter("mode")->setValueNotifyingHost(
        proc.parameters.getParameter("mode")->convertTo0to1((float)DIMENSION_MODE_IV));

    /* Switch back to Slot A */
    proc.selectCompareSlot(0);
    check(proc.getActiveCompareSlot() == 0, "active slot is A");
    check(std::abs(proc.getFloatParam("inputGain") - 1.4f) < 1e-4f, "slot A has inputGain 1.4");

    /* Switch back to Slot B */
    proc.selectCompareSlot(1);
    check(std::abs(proc.getFloatParam("inputGain") - 2.8f) < 1e-4f, "slot B has inputGain 2.8");
    check(juce::roundToInt(proc.getFloatParam("mode")) == (int)DIMENSION_MODE_IV, "slot B has mode IV");

    /* Copy B to A */
    proc.copyCompareToOtherSlot();
    proc.selectCompareSlot(0);
    check(std::abs(proc.getFloatParam("inputGain") - 2.8f) < 1e-4f, "slot A copied from B has inputGain 2.8");

    /* Persistence of compare state */
    juce::MemoryBlock mem;
    proc.getStateInformation(mem);

    Dimension5AudioProcessor proc2;
    proc2.setStateInformation(mem.getData(), (int)mem.getSize());
    check(proc2.getActiveCompareSlot() == 0, "restored active compare slot");
    check(std::abs(proc2.getFloatParam("inputGain") - 2.8f) < 1e-4f, "restored slot state");
}

void test_repeated_prepare_preserves_mode_and_smoother() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(44100.0, 32);

    /* Select Mode IV */
    proc.parameters.getParameter("mode")->setValueNotifyingHost(
        proc.parameters.getParameter("mode")->convertTo0to1((float)DIMENSION_MODE_IV));
    proc.applyModePresetToAdvancedParameters(DIMENSION_MODE_IV);

    /* Host changes sample rate to 96 kHz and calls prepareToPlay again */
    proc.prepareToPlay(96000.0, 64);
    check(juce::roundToInt(proc.getFloatParam("mode")) == (int)DIMENSION_MODE_IV, "mode IV preserved across prepareToPlay");

    Dimension5AudioProcessor::PluginConfig cfg;
    proc.getEffectiveConfig(cfg);
    check(cfg.dspParams.mode == DIMENSION_MODE_IV, "effective DSP mode is IV");
    check(std::abs(cfg.dspParams.sampleRate - 96000.0f) < 1.0f, "sample rate updated to 96k");

    juce::AudioBuffer<float> buffer(2, 64);
    for (int i = 0; i < 64; ++i) {
        buffer.setSample(0, i, 0.2f);
        buffer.setSample(1, i, 0.2f);
    }
    juce::MidiBuffer midi;
    proc.processBlock(buffer, midi);
    check(std::isfinite(buffer.getSample(0, 0)), "processed block finite at 96k");
}

void test_channel_layouts_and_bypass() {
    Dimension5AudioProcessor proc;

    /* Mono in -> Mono out */
    {
        juce::AudioProcessor::BusesLayout monoLayout;
        monoLayout.inputBuses.add(juce::AudioChannelSet::mono());
        monoLayout.outputBuses.add(juce::AudioChannelSet::mono());
        check(proc.setBusesLayout(monoLayout), "mono->mono layout supported");
        proc.prepareToPlay(48000.0, 32);

        juce::AudioBuffer<float> buffer(1, 32);
        for (int i = 0; i < 32; ++i) buffer.setSample(0, i, 0.35f);
        juce::MidiBuffer midi;
        proc.processBlock(buffer, midi);
        check(std::isfinite(buffer.getSample(0, 0)), "mono->mono output finite");
    }

    /* Mono in -> Stereo out */
    {
        juce::AudioProcessor::BusesLayout upmix;
        upmix.inputBuses.add(juce::AudioChannelSet::mono());
        upmix.outputBuses.add(juce::AudioChannelSet::stereo());
        check(proc.setBusesLayout(upmix), "mono->stereo layout supported");
        proc.prepareToPlay(48000.0, 32);

        juce::AudioBuffer<float> buffer(2, 64);
        for (int i = 0; i < 64; ++i) buffer.setSample(0, i, 0.40f);
        juce::MidiBuffer midi;
        proc.processBlock(buffer, midi);
        check(std::isfinite(buffer.getSample(0, 0)), "mono->stereo L finite");
        check(std::isfinite(buffer.getSample(1, 0)), "mono->stereo R finite");
    }

    /* Stereo in -> Stereo out */
    {
        juce::AudioProcessor::BusesLayout stereoLayout;
        stereoLayout.inputBuses.add(juce::AudioChannelSet::stereo());
        stereoLayout.outputBuses.add(juce::AudioChannelSet::stereo());
        check(proc.setBusesLayout(stereoLayout), "stereo->stereo layout supported");
        proc.prepareToPlay(48000.0, 32);

        juce::AudioBuffer<float> buffer(2, 64);
        for (int i = 0; i < 64; ++i) {
            buffer.setSample(0, i, 0.25f);
            buffer.setSample(1, i, -0.25f);
        }
        juce::MidiBuffer midi;
        proc.processBlock(buffer, midi);
        check(std::isfinite(buffer.getSample(0, 0)), "stereo L finite");
        check(std::isfinite(buffer.getSample(1, 0)), "stereo R finite");
    }

    /* Bypass in Mono in -> Stereo out */
    {
        juce::AudioProcessor::BusesLayout upmix;
        upmix.inputBuses.add(juce::AudioChannelSet::mono());
        upmix.outputBuses.add(juce::AudioChannelSet::stereo());
        proc.setBusesLayout(upmix);
        proc.prepareToPlay(48000.0, 32);

        /* Set bypass active */
        auto* bypassParam = proc.parameters.getParameter("bypass");
        bypassParam->setValueNotifyingHost(1.0f);

        /* Process enough blocks for bypass smoother to ramp (tau ~20ms, run ~100ms) */
        juce::AudioBuffer<float> buffer(2, 32);
        for (int b = 0; b < 150; ++b) {
            for (int i = 0; i < 32; ++i) {
                buffer.setSample(0, i, 0.50f);
                buffer.setSample(1, i, 0.0f);
            }
            juce::MidiBuffer midi;
            proc.processBlock(buffer, midi);
        }

        /* After bypass settles, mono input (0.50f) should be routed to both L and R outputs */
        check(std::abs(buffer.getSample(0, 31) - 0.50f) < 0.01f, "bypassed L is dry mono input");
        check(std::abs(buffer.getSample(1, 31) - 0.50f) < 0.01f, "bypassed R is dry mono input");
    }
}

void test_partial_blocks_and_large_buffers() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    const int blockSizes[] = { 1, 7, 13, 16, 31, 32, 63, 64, 127, 128, 256, 512, 1024, 2048, 4096 };
    for (int size : blockSizes) {
        juce::AudioBuffer<float> buffer(2, size);
        for (int i = 0; i < size; ++i) {
            buffer.setSample(0, i, 0.1f * (float)std::sin(2.0 * 3.14159265 * 440.0 * (double)i / 48000.0));
            buffer.setSample(1, i, 0.1f * (float)std::sin(2.0 * 3.14159265 * 440.0 * (double)i / 48000.0));
        }
        juce::MidiBuffer midi;
        proc.processBlock(buffer, midi);
        for (int i = 0; i < size; ++i) {
            check(std::isfinite(buffer.getSample(0, i)), "partial block output finite L");
            check(std::isfinite(buffer.getSample(1, i)), "partial block output finite R");
        }
    }
}

void test_controlled_concurrency_and_configuration_consistency() {
    Dimension5AudioProcessor proc;
    proc.prepareToPlay(48000.0, 32);

    std::atomic<bool> stopFlag { false };
    std::atomic<int> blocksProcessed { 0 };
    std::atomic<int> batchUpdatesPerformed { 0 };

    /* Thread 1: Audio thread processing continuous audio */
    std::thread audioThread([&] {
        juce::AudioBuffer<float> buffer(2, 64);
        juce::MidiBuffer midi;
        while (!stopFlag.load(std::memory_order_relaxed)) {
            for (int i = 0; i < 64; ++i) {
                buffer.setSample(0, i, 0.2f);
                buffer.setSample(1, i, 0.2f);
            }
            proc.processBlock(buffer, midi);
            for (int i = 0; i < 64; ++i) {
                const float l = buffer.getSample(0, i);
                const float r = buffer.getSample(1, i);
                if (!std::isfinite(l) || !std::isfinite(r) || std::abs(l) > 3.0f || std::abs(r) > 3.0f) {
                    check(false, "audio thread produced invalid output during concurrent update");
                }
            }
            blocksProcessed.fetch_add(1, std::memory_order_relaxed);
        }
    });

    /* Thread 2: Control thread executing batch operations */
    std::thread controlThread([&] {
        for (int iter = 0; iter < 100; ++iter) {
            /* Preset switch */
            proc.setCurrentProgram(iter % proc.getNumPrograms());

            /* Compare slot toggles */
            proc.selectCompareSlot(iter % 2);

            /* Switch to Custom mode */
            proc.switchToCustomMode();

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
} // namespace

int main() {
    juce::ScopedJuceInitialiser_GUI initialiser;

    try {
        std::cout << "[JUCE Tests] Running test_processor_construction_and_prepare...\n";
        test_processor_construction_and_prepare();

        std::cout << "[JUCE Tests] Running test_mode_selection_and_transition_to_custom...\n";
        test_mode_selection_and_transition_to_custom();

        std::cout << "[JUCE Tests] Running test_first_advanced_edit_after_mode_selection...\n";
        test_first_advanced_edit_after_mode_selection();

        std::cout << "[JUCE Tests] Running test_old_state_initialization_and_restore...\n";
        test_old_state_initialization_and_restore();

        std::cout << "[JUCE Tests] Running test_compare_ab_and_persistence...\n";
        test_compare_ab_and_persistence();

        std::cout << "[JUCE Tests] Running test_repeated_prepare_preserves_mode_and_smoother...\n";
        test_repeated_prepare_preserves_mode_and_smoother();

        std::cout << "[JUCE Tests] Running test_channel_layouts_and_bypass...\n";
        test_channel_layouts_and_bypass();

        std::cout << "[JUCE Tests] Running test_partial_blocks_and_large_buffers...\n";
        test_partial_blocks_and_large_buffers();

        std::cout << "[JUCE Tests] Running test_controlled_concurrency_and_configuration_consistency...\n";
        test_controlled_concurrency_and_configuration_consistency();

        std::cout << "All JUCE processor tests PASSED successfully!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "JUCE Test FAILED with exception: " << e.what() << "\n";
        return 1;
    }
}
