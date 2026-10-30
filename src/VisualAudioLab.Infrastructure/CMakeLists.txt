add_library(VisualAudioLab.Infrastructure STATIC
    src/FileLogger.cpp
    src/LocalSettingsStore.cpp)
target_include_directories(VisualAudioLab.Infrastructure PUBLIC include)
target_link_libraries(VisualAudioLab.Infrastructure
    PUBLIC VisualAudioLab.Application
    PRIVATE juce::juce_core val_juce_options)
val_configure_target(VisualAudioLab.Infrastructure)
