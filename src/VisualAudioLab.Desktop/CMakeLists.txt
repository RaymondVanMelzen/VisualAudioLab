if(WIN32)
    enable_language(RC)
    set(val_application_icon_rc "${CMAKE_CURRENT_BINARY_DIR}/ValApplicationIcon.rc")
    configure_file(ValApplicationIcon.rc.in "${val_application_icon_rc}" @ONLY)
endif()

juce_add_gui_app(VisualAudioLab.Desktop
    PRODUCT_NAME "Visual Audio Lab"
    COMPANY_NAME "Visual Audio Lab"
    BUNDLE_ID "org.visualaudiolab.desktop"
    VERSION "${PROJECT_VERSION}")
target_sources(VisualAudioLab.Desktop PRIVATE Main.cpp)
if(WIN32)
    target_sources(VisualAudioLab.Desktop PRIVATE "${val_application_icon_rc}")
endif()
target_link_libraries(VisualAudioLab.Desktop PRIVATE
    VisualAudioLab.Presentation VisualAudioLab.Infrastructure VisualAudioLab.Resources val_juce_options)
val_configure_target(VisualAudioLab.Desktop)
add_dependencies(VisualAudioLab.Desktop VisualAudioLab.AudioDiscovery.Helper)
add_custom_command(TARGET VisualAudioLab.Desktop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:VisualAudioLab.AudioDiscovery.Helper>"
        "$<TARGET_FILE_DIR:VisualAudioLab.Desktop>/$<TARGET_FILE_NAME:VisualAudioLab.AudioDiscovery.Helper>")
add_custom_command(TARGET VisualAudioLab.Desktop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:VisualAudioLab.Desktop>/licenses"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_SOURCE_DIR}/resources/fonts/Inter-OFL.txt"
        "${PROJECT_SOURCE_DIR}/resources/fonts/JetBrainsMono-OFL.txt"
        "$<TARGET_FILE_DIR:VisualAudioLab.Desktop>/licenses")
