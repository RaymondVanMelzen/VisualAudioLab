add_library(VisualAudioLab.Application STATIC
    src/CommandHistory.cpp
    src/GraphCommands.cpp
    src/Project.cpp
    src/ProjectSession.cpp
    src/SettingsService.cpp
    src/WorkspaceService.cpp)
target_include_directories(VisualAudioLab.Application PUBLIC include)
target_link_libraries(VisualAudioLab.Application PUBLIC VisualAudioLab.Domain)
val_configure_target(VisualAudioLab.Application)
