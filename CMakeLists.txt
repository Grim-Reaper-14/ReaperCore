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

FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG v1.3.4
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(minhook)

# Native hashes must remain in exactly the same order as generated native indices.
# Pin the upstream file to a known YimMenuV2 revision instead of following a moving branch.
set(REAPERCORE_YIMMENU_REVISION "1715a64c1527bb8d2708febcde8f9dd2ff0b0b40")
set(REAPERCORE_CROSSMAP_DIR "${CMAKE_BINARY_DIR}/generated/reapercore")
set(REAPERCORE_CROSSMAP_FILE "${REAPERCORE_CROSSMAP_DIR}/reapercore_crossmap_upstream.hpp")
file(MAKE_DIRECTORY "${REAPERCORE_CROSSMAP_DIR}")

if(NOT EXISTS "${REAPERCORE_CROSSMAP_FILE}")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/YimMenu/YimMenuV2/${REAPERCORE_YIMMENU_REVISION}/src/game/gta/invoker/Crossmap.hpp"
        "${REAPERCORE_CROSSMAP_FILE}"
        STATUS REAPERCORE_CROSSMAP_DOWNLOAD_STATUS
        TLS_VERIFY ON
    )
    list(GET REAPERCORE_CROSSMAP_DOWNLOAD_STATUS 0 REAPERCORE_CROSSMAP_DOWNLOAD_CODE)
    if(NOT REAPERCORE_CROSSMAP_DOWNLOAD_CODE EQUAL 0)
        list(GET REAPERCORE_CROSSMAP_DOWNLOAD_STATUS 1 REAPERCORE_CROSSMAP_DOWNLOAD_MESSAGE)
        message(FATAL_ERROR "Failed to download pinned GTA Enhanced crossmap: ${REAPERCORE_CROSSMAP_DOWNLOAD_MESSAGE}")
    endif()
endif()

add_library(ReaperCore_ImGui STATIC
    ${dear_imgui_SOURCE_DIR}/imgui.cpp
    ${dear_imgui_SOURCE_DIR}/imgui_draw.cpp
    ${dear_imgui_SOURCE_DIR}/imgui_tables.cpp
    ${dear_imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${dear_imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${dear_imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
)

target_include_directories(ReaperCore_ImGui
    PUBLIC
        ${dear_imgui_SOURCE_DIR}
)

target_compile_features(ReaperCore_ImGui PUBLIC cxx_std_20)

target_compile_definitions(ReaperCore_ImGui
    PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        UNICODE
        _UNICODE
)

target_link_libraries(ReaperCore_ImGui
    PUBLIC
        d3d12
        d3dcompiler
        dxgi
        dwmapi
)

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
    src/backend/d3d12/command_queue_manager.cpp
    src/backend/d3d12/d3d12_backend.cpp
    src/backend/d3d12/device_manager.cpp
    src/backend/d3d12/fence_manager.cpp
    src/backend/d3d12/frame_resource_manager.cpp
    src/backend/d3d12/srv_descriptor_allocator.cpp
    src/backend/gta/gta_pointers.cpp
    src/backend/gta/gta_runtime.cpp
    src/backend/gta/natives/native_manager.cpp
    src/backend/gta/pattern_scanner.cpp
    src/backend/hooking/detour_hook.cpp
    src/backend/hooking/hook_registry.cpp
    src/backend/imgui/dx12_texture_registry.cpp
    src/backend/imgui/imgui_layer.cpp
    src/backend/imgui/win32_dx12_backend.cpp
    src/backend/renderer/renderer.cpp
)

target_include_directories(ReaperCore
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${REAPERCORE_CROSSMAP_DIR}
)

target_link_libraries(ReaperCore
    PRIVATE
        ReaperCore_ImGui
        minhook
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
