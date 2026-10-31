cmake_minimum_required(VERSION 3.22)
project(PBZPhotoModeCpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /EHsc)
endif()

# ---------------------------------------------------------------- 纯核心层
# 不含任何 UE4SS/UE 头文件：配置解析、输入状态机、数学、摄影模式状态机。
add_library(pbzpm_core STATIC
    src/core/config.cpp
    src/core/gamepad.cpp
    src/core/input_logic.cpp
    src/core/photo_mode.cpp
    src/core/vk.cpp
)
target_include_directories(pbzpm_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)

option(PBZPM_BUILD_TESTS "构建核心层单元测试（不需要游戏与 UE4SS）" ON)
if (PBZPM_BUILD_TESTS)
    add_executable(pbzpm_tests tests/main.cpp)
    target_link_libraries(pbzpm_tests PRIVATE pbzpm_core)
    enable_testing()
    add_test(NAME pbzpm_core COMMAND pbzpm_tests)
endif()

# ---------------------------------------------------------------- ImGui（自带拷贝：仅 UE4SS 控制台诊断面板用，纯核心源文件）
# 游戏内图形菜单已移除（v1.8.0 判死、v1.8.1 代码清除），不再需要 DX11/DX12/Win32 后端。
set(PBZPM_IMGUI_SOURCES
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
)
add_library(pbzpm_imgui STATIC ${PBZPM_IMGUI_SOURCES})
target_include_directories(pbzpm_imgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
if (MSVC)
    set_source_files_properties(${PBZPM_IMGUI_SOURCES} PROPERTIES COMPILE_OPTIONS "/W3")
endif()

# ---------------------------------------------------------------- UE4SS DLL
# 需要先准备好 UE4SS 源码 + 依赖（见 docs/BUILD.md）：
#   cmake -S . -B build -DPBZPM_WITH_UE4SS=ON -DPBZPM_UE4SS_DIR=D:/path/to/RE-UE4SS
option(PBZPM_WITH_UE4SS "构建挂到 UE4SS 的 DLL（需要 UE4SS 源码与 UEPseudo）" OFF)
if (PBZPM_WITH_UE4SS)
    if (NOT PBZPM_UE4SS_DIR)
        message(FATAL_ERROR "启用 PBZPM_WITH_UE4SS 时必须指定 -DPBZPM_UE4SS_DIR=<UE4SS 源码根目录>")
    endif()
    add_subdirectory(${PBZPM_UE4SS_DIR} ${CMAKE_BINARY_DIR}/ue4ss)

    add_library(PBZPhotoModeCpp SHARED
        dllmain.cpp
        src/engine/ue4ss_port.cpp
        src/mod_app.cpp
        src/ui_tab.cpp
        src/win_input.cpp
    )
    target_compile_definitions(PBZPhotoModeCpp PRIVATE PBZPM_WITH_UE4SS=1)
    # 用 PUBLIC：UE4SS 的头文件目录是通过 INTERFACE 传播的，PRIVATE 会拿不到 <Mod/CppUserModBase.hpp>
    # pbzpm_imgui 放在 UE4SS 前面：两边各有一份 ImGui，链接顺序保证符号解析用我们这份
    target_link_libraries(PBZPhotoModeCpp PUBLIC pbzpm_imgui UE4SS pbzpm_core)
    set_target_properties(PBZPhotoModeCpp PROPERTIES OUTPUT_NAME "PBZPhotoModeCpp")
endif()
