cmake_minimum_required(VERSION 3.24)

project(ReaperCore VERSION 0.3.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "ReaperCore currently supports Windows only.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "ReaperCore must be built for x64.")
endif()

add_library(ReaperCore SHARED
    src/bootstrap/dllmain.cpp
    src/core/application/application.cpp
    src/core/core/core.cpp
    src/core/file_system/file_system_manager.cpp
    src/core/folder_system/folder_system_manager.cpp
    src/core/logging/logging_manager.cpp
    src/core/lua/lua_core_manager.cpp
    src/core/lua/reapercore_lua_system.cpp
    src/core/settings_system/settings_system_manager.cpp
    src/backend/backend.cpp
)

target_include_directories(ReaperCore
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(ReaperCore PRIVATE cxx_std_20)

target_compile_definitions(ReaperCore
    PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        UNICODE
        _UNICODE
)

if(MSVC)
    target_compile_options(ReaperCore PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

set_target_properties(ReaperCore PROPERTIES
    OUTPUT_NAME "ReaperCore"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
