cmake_minimum_required(VERSION 3.25)
project(VisualAudioLab VERSION 0.0.1 LANGUAGES C CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows" AND NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    message(FATAL_ERROR "Visual Audio Lab currently targets Windows and macOS only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)
include(cmake/ProjectOptions.cmake)
include(cmake/JuceDependency.cmake)

add_subdirectory(modules)
add_subdirectory(resources)
add_subdirectory(src)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
