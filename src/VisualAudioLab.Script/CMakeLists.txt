add_library(VisualAudioLab.Script STATIC src/Compiler.cpp src/Program.cpp)
target_include_directories(VisualAudioLab.Script PUBLIC include)
val_configure_target(VisualAudioLab.Script)
