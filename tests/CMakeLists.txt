add_executable(VisualAudioLab.Application.Tests ApplicationTests.cpp)
target_link_libraries(VisualAudioLab.Application.Tests PRIVATE VisualAudioLab.Application)
val_configure_target(VisualAudioLab.Application.Tests)
add_test(NAME Application.Frameworks COMMAND VisualAudioLab.Application.Tests)

add_executable(VisualAudioLab.Graph.Tests GraphTests.cpp)
target_link_libraries(VisualAudioLab.Graph.Tests PRIVATE VisualAudioLab.Application)
val_configure_target(VisualAudioLab.Graph.Tests)
add_test(NAME Application.GraphEdits COMMAND VisualAudioLab.Graph.Tests)

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

add_executable(VisualAudioLab.Presentation.Tests PresentationTests.cpp NodeEditorTests.cpp ProjectPresentationTests.cpp)
target_link_libraries(VisualAudioLab.Presentation.Tests PRIVATE VisualAudioLab.Presentation VisualAudioLab.Infrastructure)
val_configure_target(VisualAudioLab.Presentation.Tests)
add_test(NAME Presentation.Shell COMMAND VisualAudioLab.Presentation.Tests)
set_tests_properties(Presentation.Shell PROPERTIES LABELS "gui" TIMEOUT 30)
add_test(NAME Presentation.NodeEditor COMMAND VisualAudioLab.Presentation.Tests --node-editor)
set_tests_properties(Presentation.NodeEditor PROPERTIES LABELS "gui" TIMEOUT 30)
add_test(NAME Presentation.Projects COMMAND VisualAudioLab.Presentation.Tests --projects)
set_tests_properties(Presentation.Projects PROPERTIES LABELS "gui" TIMEOUT 30)

# The layout value deliberately compiles and runs without JUCE or a desktop session.
add_executable(VisualAudioLab.DockLayout.Tests DockLayoutTests.cpp)
target_include_directories(VisualAudioLab.DockLayout.Tests PRIVATE
    "${PROJECT_SOURCE_DIR}/src/VisualAudioLab.Presentation/include")
val_configure_target(VisualAudioLab.DockLayout.Tests)
add_test(NAME Presentation.DockLayout COMMAND VisualAudioLab.DockLayout.Tests)
