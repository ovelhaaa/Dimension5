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

add_executable(dimension5_juce_tests
  ${CMAKE_SOURCE_DIR}/tests/test_dimension_juce.cpp)

target_include_directories(dimension5_juce_tests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/Dimension5Plugin_artefacts/JuceLibraryCode)

target_link_libraries(dimension5_juce_tests
  PRIVATE
    Dimension5Plugin
    dimension5_core
    juce::juce_audio_utils
    juce::juce_dsp
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

target_compile_definitions(dimension5_juce_tests
  PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

add_test(NAME dimension5_juce_tests COMMAND dimension5_juce_tests)

