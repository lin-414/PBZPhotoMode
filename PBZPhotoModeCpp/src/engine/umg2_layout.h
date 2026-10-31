#pragma once

// UMG 交互菜单的布局常量：ue4ss_port（摆放控件）与 mod_app（鼠标命中判定）共用，
// 两边必须保持一致。坐标都是面板内坐标；面板锚定屏幕右上角：
// - 摆放：锚点(1,0) + 对齐(0,0)，Left = x - kPanelFromRight，Top = y + kPanelTop
// - 命中：物理像素反算（客户区右缘 - kPanelFromRight×缩放），缩放取窗口 DPI 或 umg2_scale.txt
// 1.2.1 的教训：两边坐标系各说各话（摆放左上、命中右上），点击永远落空。

namespace pbzpm::umg2layout {

constexpr float kPanelFromRight = 480.0f;  // 面板左边缘距屏幕右缘
constexpr float kPanelTop = 90.0f;
constexpr float kPanelWidth = 440.0f;
constexpr float kPanelHeight = 330.0f;

// 静态/动态文本（只显示）。@开头的是动态行，由 ue4ss_port 每帧更新
struct TextItem {
    float x;
    float y;
    const char* text;
};
inline constexpr TextItem kTexts[] = {
    {12, 10, "PBZ Photo Mode"},
    {12, 34, "@status"},
    {12, 58, "-------------------"},
    {12, 84, "FOV"},
    {64, 84, "[-]"},
    {132, 84, "@fov"},
    {206, 84, "[+]"},
    {12, 116, "SPEED"},
    {64, 116, "[-]"},
    {132, 116, "@speed"},
    {206, 116, "[+]"},
    {12, 148, "SENS"},
    {64, 148, "[-]"},
    {132, 148, "@sens"},
    {206, 148, "[+]"},
    {12, 180, "-------------------"},
    {12, 206, "FREEZE"},
    {12, 234, "RESET"},
    {12, 262, "EXIT"},
    {12, 296, "click [-][+] to adjust"},
};

// 命中区（面板内坐标）：与上面文本行对齐，判定用户的鼠标点击
struct HitItem {
    const char* id;
    float x, y, w, h;
};
inline constexpr HitItem kHits[] = {
    {"fov_dec", 54, 76, 52, 26},    {"fov_inc", 198, 76, 52, 26},
    {"speed_dec", 54, 108, 52, 26}, {"speed_inc", 198, 108, 52, 26},
    {"sens_dec", 54, 140, 52, 26},  {"sens_inc", 198, 140, 52, 26},
    {"freeze", 6, 198, 116, 26},
    {"reset", 6, 226, 116, 26},
    {"exit", 6, 254, 116, 26},
};

}  // namespace pbzpm::umg2layout
