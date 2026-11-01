add_executable(VisualAudioLab.Application.Tests ApplicationTests.cpp)
target_link_libraries(VisualAudioLab.Application.Tests PRIVATE VisualAudioLab.Application)
val_configure_target(VisualAudioLab.Application.Tests)
add_test(NAME Application.Frameworks COMMAND VisualAudioLab.Application.Tests)

add_executable(VisualAudioLab.Graph.Tests GraphTests.cpp)
target_link_libraries(VisualAudioLab.Graph.Tests PRIVATE VisualAudioLab.Application)
val_configure_target(VisualAudioLab.Graph.Tests)
add_test(NAME Application.GraphEdits COMMAND VisualAudioLab.Graph.Tests)

add_executable(VisualAudioLab.AudioEngine.Tests AudioEngineTests.cpp AudioAllocationProbe.cpp)
target_link_libraries(VisualAudioLab.AudioEngine.Tests PRIVATE VisualAudioLab.Audio)
val_configure_target(VisualAudioLab.AudioEngine.Tests)
add_test(NAME Audio.Engine COMMAND VisualAudioLab.AudioEngine.Tests)
set_tests_properties(Audio.Engine PROPERTIES TIMEOUT 60)

add_executable(VisualAudioLab.Dsp.Tests DspTests.cpp AudioAllocationProbe.cpp)
target_link_libraries(VisualAudioLab.Dsp.Tests PRIVATE VisualAudioLab.Audio)
val_configure_target(VisualAudioLab.Dsp.Tests)
add_test(NAME Audio.Dsp COMMAND VisualAudioLab.Dsp.Tests)
set_tests_properties(Audio.Dsp PROPERTIES TIMEOUT 60)

add_executable(VisualAudioLab.Visualization.Tests VisualizationTests.cpp AudioAllocationProbe.cpp)
target_link_libraries(VisualAudioLab.Visualization.Tests PRIVATE VisualAudioLab.Audio)
val_configure_target(VisualAudioLab.Visualization.Tests)
add_test(NAME Audio.Visualization COMMAND VisualAudioLab.Visualization.Tests)
set_tests_properties(Audio.Visualization PROPERTIES TIMEOUT 90)

add_executable(VisualAudioLab.Script.Tests ScriptTests.cpp AudioAllocationProbe.cpp)
target_link_libraries(VisualAudioLab.Script.Tests PRIVATE VisualAudioLab.Script VisualAudioLab.Audio)
val_configure_target(VisualAudioLab.Script.Tests)
add_test(NAME Script.CompilerRuntime COMMAND VisualAudioLab.Script.Tests)
add_test(NAME Script.Examples COMMAND VisualAudioLab.Script.Tests --examples "${PROJECT_SOURCE_DIR}/examples/scripts")
add_test(NAME Script.Stress COMMAND VisualAudioLab.Script.Tests --stress)
set_tests_properties(Script.CompilerRuntime Script.Examples Script.Stress PROPERTIES TIMEOUT 90)

add_executable(VisualAudioLab.Project.Tests ProjectTests.cpp)
target_link_libraries(VisualAudioLab.Project.Tests PRIVATE
    VisualAudioLab.Infrastructure juce::juce_core val_juce_options)
val_configure_target(VisualAudioLab.Project.Tests)
add_test(NAME Persistence.Projects COMMAND VisualAudioLab.Project.Tests)

add_executable(VisualAudioLab.Infrastructure.Tests InfrastructureTests.cpp)
target_link_libraries(VisualAudioLab.Infrastructure.Tests PRIVATE
    VisualAudioLab.Infrastructure juce::juce_core val_juce_options)
val_configure_target(VisualAudioLab.Infrastructure.Tests)
add_test(NAME Infrastructure.LocalAdapters COMMAND VisualAudioLab.Infrastructure.Tests)

add_executable(VisualAudioLab.AudioBackend.Tests AudioBackendTests.cpp)
target_link_libraries(VisualAudioLab.AudioBackend.Tests PRIVATE
    VisualAudioLab.Infrastructure VisualAudioLab.Audio juce::juce_events val_juce_options)
add_dependencies(VisualAudioLab.AudioBackend.Tests VisualAudioLab.AudioDiscovery.Helper)
add_custom_command(TARGET VisualAudioLab.AudioBackend.Tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:VisualAudioLab.AudioDiscovery.Helper>"
        "$<TARGET_FILE_DIR:VisualAudioLab.AudioBackend.Tests>/$<TARGET_FILE_NAME:VisualAudioLab.AudioDiscovery.Helper>")
val_configure_target(VisualAudioLab.AudioBackend.Tests)
add_test(NAME Audio.BackendContracts COMMAND VisualAudioLab.AudioBackend.Tests)
set_tests_properties(Audio.BackendContracts PROPERTIES TIMEOUT 45)
add_test(NAME Audio.StreamLifecycle COMMAND VisualAudioLab.AudioBackend.Tests --streams)
set_tests_properties(Audio.StreamLifecycle PROPERTIES TIMEOUT 45)
add_test(NAME Audio.NativeDiscovery COMMAND VisualAudioLab.AudioBackend.Tests --native)
set_tests_properties(Audio.NativeDiscovery PROPERTIES LABELS "native-audio" TIMEOUT 60)

add_executable(VisualAudioLab.Presentation.Tests PresentationTests.cpp NodeEditorTests.cpp ProjectPresentationTests.cpp VisualizationPresentationTests.cpp RuntimeIntegrationTests.cpp ModulePanelIntegrationTests.cpp)
target_link_libraries(VisualAudioLab.Presentation.Tests PRIVATE VisualAudioLab.Presentation VisualAudioLab.Infrastructure)
val_configure_target(VisualAudioLab.Presentation.Tests)
add_test(NAME Presentation.Shell COMMAND VisualAudioLab.Presentation.Tests)
set_tests_properties(Presentation.Shell PROPERTIES LABELS "gui" TIMEOUT 30)
add_test(NAME Presentation.NodeEditor COMMAND VisualAudioLab.Presentation.Tests --node-editor)
set_tests_properties(Presentation.NodeEditor PROPERTIES LABELS "gui" TIMEOUT 30)
add_test(NAME Presentation.Projects COMMAND VisualAudioLab.Presentation.Tests --projects)
set_tests_properties(Presentation.Projects PROPERTIES LABELS "gui" TIMEOUT 30)
add_test(NAME Presentation.Visualization COMMAND VisualAudioLab.Presentation.Tests --visualization)
set_tests_properties(Presentation.Visualization PROPERTIES LABELS "gui" TIMEOUT 60)
add_test(NAME Presentation.Runtime COMMAND VisualAudioLab.Presentation.Tests --runtime)
add_test(NAME Presentation.ModulePanels COMMAND VisualAudioLab.Presentation.Tests --module-panels)
set_tests_properties(Presentation.Runtime Presentation.ModulePanels PROPERTIES LABELS "gui" TIMEOUT 60)

# The layout value deliberately compiles and runs without JUCE or a desktop session.
add_executable(VisualAudioLab.DockLayout.Tests DockLayoutTests.cpp)
target_include_directories(VisualAudioLab.DockLayout.Tests PRIVATE
    "${PROJECT_SOURCE_DIR}/src/VisualAudioLab.Presentation/include")
val_configure_target(VisualAudioLab.DockLayout.Tests)
add_test(NAME Presentation.DockLayout COMMAND VisualAudioLab.DockLayout.Tests)
