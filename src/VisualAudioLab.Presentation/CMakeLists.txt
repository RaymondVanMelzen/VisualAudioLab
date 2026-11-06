find_package(Git QUIET)
set(VAL_BUILD_REVISION "unavailable")
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        OUTPUT_VARIABLE VAL_BUILD_REVISION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE VAL_GIT_RESULT)
    if(NOT VAL_GIT_RESULT EQUAL 0 OR VAL_BUILD_REVISION STREQUAL "")
        set(VAL_BUILD_REVISION "unavailable")
    endif()
endif()
set(VAL_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${VAL_GENERATED_INCLUDE_DIR}/val/presentation")
configure_file(BuildInfo.h.in
    "${VAL_GENERATED_INCLUDE_DIR}/val/presentation/BuildInfo.h" @ONLY)

add_library(VisualAudioLab.Presentation STATIC
    src/MainComponent.cpp
    src/RuntimePanel.cpp
    src/ModulePanels.cpp
    src/NodeView.cpp
    src/ConnectionView.cpp
    src/CanvasScene.cpp
    src/CanvasView.cpp
    src/VisualizationWidgets.cpp
    src/DockWorkspace.cpp
    src/ShellChrome.cpp
    src/ShellTheme.cpp
    src/Typography.cpp)
target_include_directories(VisualAudioLab.Presentation PUBLIC include "${VAL_GENERATED_INCLUDE_DIR}")
target_link_libraries(VisualAudioLab.Presentation
    PUBLIC VisualAudioLab.Application VisualAudioLab.Audio juce::juce_gui_basics val_juce_options
    PRIVATE VisualAudioLab.Resources)
val_configure_target(VisualAudioLab.Presentation)
