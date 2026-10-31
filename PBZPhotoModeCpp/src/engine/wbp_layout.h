#pragma once

// WBP 资产菜单的布局常量：编辑器里制作的资产与 mod_app（悬停静音判定）共用。
// 资产侧（Phase 3 制作 WBP 时照此摆）：根 CanvasPanel → 面板 CanvasPanel，
// 面板的 CanvasPanelSlot：锚点(1,0) + 对齐(0,0) + Position(-kPanelFromRight, kPanelTop)
// + Size(kPanelWidth, kPanelHeight)。
// mod_app 命中公式（Slate 单位 × 缩放 → 物理像素，与 umg2_layout.h 同款）：
//   panel_left_px = client_right - kPanelFromRight×缩放；panel_top_px = kPanelTop×缩放
namespace pbzpm::wbplayout {

constexpr float kPanelFromRight = 484.0f;  // 面板左缘距屏幕右缘（面板宽 460 + 右边距 24）
constexpr float kPanelTop = 90.0f;
constexpr float kPanelWidth = 460.0f;
constexpr float kPanelHeight = 560.0f;

}  // namespace pbzpm::wbplayout
