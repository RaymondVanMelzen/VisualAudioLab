add_library(VisualAudioLab.Presentation STATIC
    src/MainComponent.cpp
    src/DockWorkspace.cpp
    src/ShellChrome.cpp
    src/ShellTheme.cpp)
target_include_directories(VisualAudioLab.Presentation PUBLIC include)
target_link_libraries(VisualAudioLab.Presentation
    PUBLIC VisualAudioLab.Application juce::juce_gui_basics val_juce_options
    PRIVATE VisualAudioLab.Resources)
val_configure_target(VisualAudioLab.Presentation)
