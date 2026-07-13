juce_add_plugin(Dimension5Plugin
  COMPANY_NAME "Dimension5"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  PLUGIN_MANUFACTURER_CODE Dm5a
  PLUGIN_CODE Dm5c
  FORMATS VST3
  PRODUCT_NAME "Dimension5")

juce_generate_juce_header(Dimension5Plugin)

target_sources(Dimension5Plugin
  PRIVATE
    PluginEditor.cpp
    PluginProcessor.cpp)

target_include_directories(Dimension5Plugin
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(Dimension5Plugin
  PRIVATE
    dimension5_core
    juce::juce_audio_utils
    juce::juce_dsp
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

target_compile_definitions(Dimension5Plugin
  PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)
