set(VAL_JUCE_SOURCE_DIR "" CACHE PATH "Existing JUCE 8.0.12 source tree for offline builds")
option(VAL_FETCH_JUCE "Allow fetching the pinned JUCE dependency during configure" OFF)

if(VAL_JUCE_SOURCE_DIR)
    if(NOT EXISTS "${VAL_JUCE_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "VAL_JUCE_SOURCE_DIR must point to a JUCE source tree.")
    endif()
    add_subdirectory("${VAL_JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/juce" EXCLUDE_FROM_ALL)
elseif(VAL_FETCH_JUCE)
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 29396c22c93392d6738e021b83196283d6e4d850
        GIT_CONFIG core.longpaths=true
        GIT_PROGRESS FALSE)
    FetchContent_MakeAvailable(JUCE)
else()
    message(FATAL_ERROR
        "Provide VAL_JUCE_SOURCE_DIR for offline builds, or explicitly enable VAL_FETCH_JUCE.")
endif()

file(REAL_PATH "${JUCE_MODULES_DIR}/.." _val_juce_source)
get_directory_property(_val_juce_version DIRECTORY "${_val_juce_source}" DEFINITION JUCE_VERSION)
if(NOT _val_juce_version VERSION_EQUAL "8.0.12")
    message(FATAL_ERROR "This skeleton is qualified against JUCE 8.0.12; supplied version is ${_val_juce_version}.")
endif()

# Apply one consistent, UI-only module configuration to every JUCE consumer.
add_library(val_juce_options INTERFACE)
target_compile_definitions(val_juce_options INTERFACE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=0)
