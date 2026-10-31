#pragma once

// WBP 资产菜单的布局常量：编辑器 commandlet（ProjectPBZ/Source/ProjectPBZ/MakeMenuCommandlet.cpp）
// 与 mod_app（悬停静音 + 按钮命中判定）共用。改动必须两边同步。
//
// 资产结构（commandlet 搭建）：根 CanvasPanel 铺满视口 → Panel（右上角锚定）→
//   PanelBG / TitleText / TabCamera / TabKeys / T_Status / Sep1 / PageCamera / PageKeys
// PageCamera 内：三行滑条（L_*/S_*/T_*）+ Sep2 + BtnFreeze/BtnReset/BtnExit + T_Hint
// PageKeys 内：K_00..K_12 快捷键行（默认 Collapsed）
//
// 坐标约定：kHits/kPage 全部是「面板内坐标」（与 umg2_layout.h 同款）；
// mod_app 命中公式：panel_left_px = client_right - kPanelFromRight×缩放，px/py 除以缩放。

namespace pbzpm::wbplayout {

constexpr float kPanelFromRight = 484.0f;  // 面板左缘距屏幕右缘（面板宽 460 + 右边距 24）
constexpr float kPanelTop = 90.0f;
constexpr float kPanelWidth = 460.0f;
constexpr float kPanelHeight = 560.0f;

// 页签（面板内坐标，Panel 上）：TabCamera 在 (320,16)，TabKeys 在 (392,16)，各 60x24
// 按钮（PageCamera 在 Panel (16,94)，按钮在 PageCamera 内 (0,196/238/280) 各 220x30）
struct HitItem {
    const char* id;
    float x, y, w, h;
};
inline constexpr HitItem kHits[] = {
    {"tab_camera", 320, 16, 62, 26},
    {"tab_keys", 392, 16, 62, 26},
    {"freeze", 16, 290, 222, 32},
    {"reset", 16, 332, 222, 32},
    {"exit", 16, 374, 222, 32},
};

// 页签索引：0=相机页 1=快捷键页
constexpr int kPageCamera = 0;
constexpr int kPageKeys = 1;

}  // namespace pbzpm::wbplayout
