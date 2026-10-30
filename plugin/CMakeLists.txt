juce_add_plugin(MX300Control
    PRODUCT_NAME "MX300 Control"
    COMPANY_NAME "MX300 Control (unofficial)"
    BUNDLE_ID io.github.jdesfossez.mx300control
    PLUGIN_MANUFACTURER_CODE Mxcl
    PLUGIN_CODE Mx3c
    FORMATS AU VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    MICROPHONE_PERMISSION_ENABLED FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Tools
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(MX300Control PRIVATE
    DeviceHub.cpp
    PluginProcessor.cpp
    PluginEditor.cpp
    Theme.cpp
    StandaloneApp.cpp)

target_compile_definitions(MX300Control PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)

target_link_libraries(MX300Control
    PRIVATE
        mx300core
        juce::juce_audio_utils
        juce::juce_audio_devices
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- UI snapshot tool: renders the editor against the demo unit to PNG files (no window needed)
juce_add_console_app(MX300Snapshot PRODUCT_NAME "MX300Snapshot")
target_sources(MX300Snapshot PRIVATE tools/Snapshot.cpp DeviceHub.cpp PluginProcessor.cpp PluginEditor.cpp Theme.cpp)
target_include_directories(MX300Snapshot PRIVATE .)
target_compile_definitions(MX300Snapshot PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
    JucePlugin_VersionString="dev" JucePlugin_Name="MX300 Control")
target_link_libraries(MX300Snapshot PRIVATE mx300core juce::juce_audio_utils juce::juce_audio_devices
    juce::juce_recommended_config_flags)
