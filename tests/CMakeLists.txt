add_executable(VisualAudioLab.Application.Tests ApplicationTests.cpp)
target_link_libraries(VisualAudioLab.Application.Tests PRIVATE VisualAudioLab.Application)
val_configure_target(VisualAudioLab.Application.Tests)
add_test(NAME Application.Frameworks COMMAND VisualAudioLab.Application.Tests)

add_executable(VisualAudioLab.Infrastructure.Tests InfrastructureTests.cpp)
target_link_libraries(VisualAudioLab.Infrastructure.Tests PRIVATE
    VisualAudioLab.Infrastructure juce::juce_core val_juce_options)
val_configure_target(VisualAudioLab.Infrastructure.Tests)
add_test(NAME Infrastructure.LocalAdapters COMMAND VisualAudioLab.Infrastructure.Tests)

add_executable(VisualAudioLab.Presentation.Tests PresentationTests.cpp)
target_link_libraries(VisualAudioLab.Presentation.Tests PRIVATE VisualAudioLab.Presentation)
val_configure_target(VisualAudioLab.Presentation.Tests)
add_test(NAME Presentation.Shell COMMAND VisualAudioLab.Presentation.Tests)
set_tests_properties(Presentation.Shell PROPERTIES LABELS "gui" TIMEOUT 30)
