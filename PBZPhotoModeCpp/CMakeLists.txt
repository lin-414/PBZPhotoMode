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
    src/core/hook_registry.cpp
    src/core/i18n_logic.cpp
    src/core/input_logic.cpp
    src/core/mod_runtime.cpp
    src/core/photo_mode.cpp
    src/core/vk.cpp
    src/pbzfm_client_lang.cpp
    src/pbzfm_schema_map.cpp
)
target_include_directories(pbzpm_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)

option(PBZPM_BUILD_TESTS "构建核心层单元测试（不需要游戏与 UE4SS）" ON)
if (PBZPM_BUILD_TESTS)
    add_executable(pbzpm_tests
        tests/main.cpp
        tests/test_mod_runtime.cpp
        tests/test_input_pure.cpp
        tests/test_pbzfm_validation.cpp
    )
    target_link_libraries(pbzpm_tests PRIVATE pbzpm_core)
    # 不注册 ctest：本项目的测试入口统一是 tools/build_tests.bat，
    # 全仓对 ctest/add_test 的引用为 0（原 enable_testing()/add_test 是死接线）。
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

    # ---- ImGui（自带拷贝，只编 4 个核心源文件）
    # 两条消费路径：① UE4SS 控制台的 mod tab（ui_tab.cpp / mod_app.cpp）
    #               ② PBZFM 框架自绘设置页（pbzfm_client.cpp 借框架的 ImGui 上下文）
    # 本 mod 不自建覆盖层，故不编 backends/DX11/DX12/Win32（那 6 个文件已删除）。
    # 位置在 WITH_UE4SS 守卫内：纯核心测试构建不链接 imgui，不该为它白编 39k 行。
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

    add_library(PBZPhotoModeCpp SHARED
        dllmain.cpp
        # Ue4ssPort 实现按子系统拆分，共享 helper 在 ue4ss_port_internal.h
        src/engine/ue4ss_port.cpp                 # 核心/世界/日志
        src/engine/ue4ss_port_camera_view.cpp     # 相机/ViewTarget/输入接管
        src/engine/ue4ss_port_hud_ui.cpp          # HUD/血条/字幕/锁定标记
        src/engine/ue4ss_port_time_pause.cpp      # 时间缩放/暂停
        src/engine/ue4ss_port_micro_motion.cpp    # shake/风/MPC/POV 微动
        src/engine/ue4ss_port_audio.cpp           # Wwise 音频
        src/engine/ue4ss_port_diagnostics.cpp     # 冻结诊断/状态转储
        src/engine/process_freeze.cpp             # F7 画面定格（全线程冻结 + 时间 API 补偿）
        src/mod_app.cpp
        src/pbzfm_client.cpp
        src/ui_tab.cpp
        src/win_input.cpp
    )
    target_compile_definitions(PBZPhotoModeCpp PRIVATE PBZPM_WITH_UE4SS=1)
    # PBZFM 菜单框架 SDK（内置拷贝，运行时 GetProcAddress 解析，零链接依赖）
    target_include_directories(PBZPhotoModeCpp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/pbzfm)
    # 用 PUBLIC：UE4SS 的头文件目录是通过 INTERFACE 传播的，PRIVATE 会拿不到 <Mod/CppUserModBase.hpp>
    # pbzpm_imgui 放在 UE4SS 前面：两边各有一份 ImGui，链接顺序保证符号解析用我们这份
    target_link_libraries(PBZPhotoModeCpp PUBLIC pbzpm_imgui UE4SS pbzpm_core)
    set_target_properties(PBZPhotoModeCpp PROPERTIES OUTPUT_NAME "PBZPhotoModeCpp")
endif()
