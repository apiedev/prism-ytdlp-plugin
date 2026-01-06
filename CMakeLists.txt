cmake_minimum_required(VERSION 3.16)
project(prism_ytdlp_plugin VERSION 1.0.0 LANGUAGES C)

# ============================================================================
# Options
# ============================================================================

option(BUILD_SHARED_LIBS "Build shared libraries" ON)

# ============================================================================
# Compiler Settings
# ============================================================================

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ============================================================================
# Find Dependencies
# ============================================================================

# Find Prism Core headers
if(NOT PRISM_CORE_DIR)
    # Default: look in sibling directory
    set(PRISM_CORE_DIR "${CMAKE_SOURCE_DIR}/../prism-video/Native" CACHE PATH "Path to prism-video/Native")
endif()

# Convert to absolute path to handle relative paths correctly
get_filename_component(PRISM_CORE_DIR "${PRISM_CORE_DIR}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")

if(EXISTS "${PRISM_CORE_DIR}/include/prism/prism.h")
    message(STATUS "Found Prism Core at: ${PRISM_CORE_DIR}")
    message(STATUS "Prism Core include: ${PRISM_CORE_DIR}/include")
else()
    message(FATAL_ERROR "Prism Core not found at ${PRISM_CORE_DIR}. Set PRISM_CORE_DIR to point to prism-video/Native")
endif()

# ============================================================================
# Platform Detection
# ============================================================================

if(WIN32)
    set(PRISM_PLATFORM "Windows")
    set(PRISM_PLUGIN_DIR "${CMAKE_SOURCE_DIR}/../prism-unity-plugin/Plugins/Windows/x86_64")
elseif(APPLE)
    set(PRISM_PLATFORM "macOS")
    set(PRISM_PLUGIN_DIR "${CMAKE_SOURCE_DIR}/../prism-unity-plugin/Plugins/macOS")
elseif(UNIX)
    set(PRISM_PLATFORM "Linux")
    set(PRISM_PLUGIN_DIR "${CMAKE_SOURCE_DIR}/../prism-unity-plugin/Plugins/Linux/x86_64")
endif()

message(STATUS "Building for: ${PRISM_PLATFORM}")
message(STATUS "Plugin output: ${PRISM_PLUGIN_DIR}")

# ============================================================================
# yt-dlp Plugin Library
# ============================================================================

set(PLUGIN_SOURCES
    src/ytdlp_plugin.c
    src/ytdlp_resolver.c
)

set(PLUGIN_HEADERS
    include/prism_ytdlp_plugin.h
)

add_library(prism_ytdlp ${PLUGIN_SOURCES} ${PLUGIN_HEADERS})

target_include_directories(prism_ytdlp
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${PRISM_CORE_DIR}/include
)

if(WIN32)
    target_compile_definitions(prism_ytdlp PRIVATE
        PRISM_YTDLP_EXPORTS
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
    )

    target_link_libraries(prism_ytdlp PRIVATE
        urlmon
        shell32
    )

    set_target_properties(prism_ytdlp PROPERTIES
        OUTPUT_NAME "prism_ytdlp"
        PREFIX ""
        SUFFIX ".dll"
    )

elseif(APPLE)
    target_compile_options(prism_ytdlp PRIVATE -fvisibility=hidden)

    set_target_properties(prism_ytdlp PROPERTIES
        OUTPUT_NAME "prism_ytdlp"
        PREFIX "lib"
        SUFFIX ".dylib"
        INSTALL_RPATH "@loader_path"
        BUILD_WITH_INSTALL_RPATH TRUE
    )

else()
    target_compile_options(prism_ytdlp PRIVATE -fvisibility=hidden -fPIC)

    set_target_properties(prism_ytdlp PROPERTIES
        OUTPUT_NAME "prism_ytdlp"
        PREFIX "lib"
        SUFFIX ".so"
        INSTALL_RPATH "$ORIGIN"
        BUILD_WITH_INSTALL_RPATH TRUE
    )

    target_link_libraries(prism_ytdlp PRIVATE pthread)
endif()

# ============================================================================
# Test Executable
# ============================================================================

option(BUILD_TESTS "Build test executables" ON)

if(BUILD_TESTS)
    add_executable(prism_ytdlp_tests
        test/ytdlp_resolver_tests.c
    )

    target_include_directories(prism_ytdlp_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/test
        ${PRISM_CORE_DIR}/include
    )

    if(WIN32)
        target_compile_definitions(prism_ytdlp_tests PRIVATE
            _CRT_SECURE_NO_WARNINGS
            WIN32_LEAN_AND_MEAN
        )
        target_link_libraries(prism_ytdlp_tests PRIVATE
            prism_ytdlp
            urlmon
            shell32
        )
    elseif(APPLE)
        target_link_libraries(prism_ytdlp_tests PRIVATE
            prism_ytdlp
        )
    else()
        target_link_libraries(prism_ytdlp_tests PRIVATE
            prism_ytdlp
            pthread
        )
    endif()

    set_target_properties(prism_ytdlp_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    message(STATUS "Building test executable: prism_ytdlp_tests")
endif()

# ============================================================================
# Installation
# ============================================================================

install(TARGETS prism_ytdlp
    LIBRARY DESTINATION ${PRISM_PLUGIN_DIR}
    RUNTIME DESTINATION ${PRISM_PLUGIN_DIR}
    ARCHIVE DESTINATION ${PRISM_PLUGIN_DIR}
)

# Install headers for development
install(FILES ${PLUGIN_HEADERS} DESTINATION include)

# ============================================================================
# Custom Targets
# ============================================================================

# Install to Unity Plugins folder
add_custom_target(install_plugin
    COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --prefix ${CMAKE_SOURCE_DIR}/..
    DEPENDS prism_ytdlp
    COMMENT "Installing yt-dlp plugin to Unity Plugins folder"
)

# ============================================================================
# Configuration Summary
# ============================================================================

message(STATUS "")
message(STATUS "=== Prism yt-dlp Plugin Configuration ===")
message(STATUS "Platform: ${PRISM_PLATFORM}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Prism Core: ${PRISM_CORE_DIR}")
message(STATUS "")
