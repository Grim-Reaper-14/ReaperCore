cmake_minimum_required(VERSION 3.24)

project(ReaperCore VERSION 0.3.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "ReaperCore currently supports Windows only.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "ReaperCore must be built for x64.")
endif()

include(FetchContent)

FetchContent_Declare(
    dear_imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.8
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(dear_imgui)

add_library(ReaperCore_ImGui STATIC
    ${dear_imgui_SOURCE_DIR}/imgui.cpp
    ${dear_imgui_SOURCE_DIR}/imgui_draw.cpp
    ${dear_imgui_SOURCE_DIR}/imgui_tables.cpp
    ${dear_imgui_SOURCE_DIR}/imgui_widgets.cpp
)

target_include_directories(ReaperCore_ImGui
    PUBLIC
        ${dear_imgui_SOURCE_DIR}
)

target_compile_features(ReaperCore_ImGui PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(ReaperCore_ImGui PRIVATE /W3 /permissive- /EHsc /utf-8)
endif()

add_library(ReaperCore SHARED
    src/bootstrap/dllmain.cpp
    src/core/application/application.cpp
    src/core/core/core.cpp
    src/core/events/event_manager.cpp
    src/core/file_system/file_system_manager.cpp
    src/core/folder_system/folder_system_manager.cpp
    src/core/logging/logging_manager.cpp
    src/core/lua/lua_core_manager.cpp
    src/core/lua/reapercore_lua_system.cpp
    src/core/services/service_manager.cpp
    src/core/settings_system/settings_system_manager.cpp
    src/core/tasks/task_manager.cpp
    src/backend/backend.cpp
    src/backend/d3d12/d3d12_backend.cpp
    src/backend/hooking/hook_registry.cpp
    src/backend/imgui/imgui_layer.cpp
    src/backend/renderer/renderer.cpp
)

target_include_directories(ReaperCore
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ReaperCore
    PRIVATE
        ReaperCore_ImGui
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
