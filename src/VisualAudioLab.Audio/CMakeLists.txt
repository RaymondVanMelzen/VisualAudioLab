add_library(VisualAudioLab.Audio STATIC
    src/AudioNode.cpp
    src/DspNodes.cpp
    src/AudioGraph.cpp
    src/AudioEngine.cpp
    src/AudioProcessorNode.cpp)
target_include_directories(VisualAudioLab.Audio PUBLIC include)
target_link_libraries(VisualAudioLab.Audio PUBLIC VisualAudioLab.Application)
val_configure_target(VisualAudioLab.Audio)
