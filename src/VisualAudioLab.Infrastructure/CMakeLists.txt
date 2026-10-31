add_library(VisualAudioLab.Infrastructure STATIC
    src/AudioBackends.cpp
    src/FileLogger.cpp
    src/LocalProjectStore.cpp
    src/LocalSettingsStore.cpp)
target_include_directories(VisualAudioLab.Infrastructure PUBLIC include)
target_link_libraries(VisualAudioLab.Infrastructure
    PUBLIC VisualAudioLab.Application
    PRIVATE juce::juce_audio_devices juce::juce_core val_juce_options)
val_configure_target(VisualAudioLab.Infrastructure)
if(WIN32)
    target_link_libraries(VisualAudioLab.Infrastructure PRIVATE Cfgmgr32)
endif()

add_executable(VisualAudioLab.AudioDiscovery.Helper src/AudioDiscoveryHelperMain.cpp)
target_link_libraries(VisualAudioLab.AudioDiscovery.Helper PRIVATE VisualAudioLab.Infrastructure juce::juce_events val_juce_options)
val_configure_target(VisualAudioLab.AudioDiscovery.Helper)
