juce_add_gui_app(VisualAudioLab.Desktop
    PRODUCT_NAME "Visual Audio Lab"
    COMPANY_NAME "Visual Audio Lab"
    BUNDLE_ID "org.visualaudiolab.desktop"
    VERSION "${PROJECT_VERSION}")
target_sources(VisualAudioLab.Desktop PRIVATE Main.cpp)
target_link_libraries(VisualAudioLab.Desktop PRIVATE
    VisualAudioLab.Presentation VisualAudioLab.Infrastructure val_juce_options)
val_configure_target(VisualAudioLab.Desktop)
