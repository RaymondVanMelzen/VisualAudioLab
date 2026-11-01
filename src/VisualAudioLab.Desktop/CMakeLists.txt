juce_add_gui_app(VisualAudioLab.Desktop
    PRODUCT_NAME "Visual Audio Lab"
    COMPANY_NAME "Visual Audio Lab"
    BUNDLE_ID "org.visualaudiolab.desktop"
    VERSION "${PROJECT_VERSION}")
target_sources(VisualAudioLab.Desktop PRIVATE Main.cpp)
target_link_libraries(VisualAudioLab.Desktop PRIVATE
    VisualAudioLab.Presentation VisualAudioLab.Infrastructure val_juce_options)
val_configure_target(VisualAudioLab.Desktop)
add_dependencies(VisualAudioLab.Desktop VisualAudioLab.AudioDiscovery.Helper)
add_custom_command(TARGET VisualAudioLab.Desktop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:VisualAudioLab.AudioDiscovery.Helper>"
        "$<TARGET_FILE_DIR:VisualAudioLab.Desktop>/$<TARGET_FILE_NAME:VisualAudioLab.AudioDiscovery.Helper>")
