cmake_minimum_required(VERSION 3.15)
project(LSP_ReShade VERSION 0.3.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
)
FetchContent_MakeAvailable(imgui)

set(SOURCES
    src/main.cpp
    src/input_sim.cpp
    src/window_manager.cpp
)

add_library(LSP_ReShade SHARED ${SOURCES})

target_include_directories(LSP_ReShade PRIVATE
    ${imgui_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../LosslessProxy/sdk/include
)

target_sources(LSP_ReShade PRIVATE
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
)

target_link_libraries(LSP_ReShade user32)
