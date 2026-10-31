#pragma once

// WBP 资产菜单（1.3.0 起）的变量桥：纯状态机，不依赖引擎，可单测。
//
// 资产侧契约（在 UE 编辑器里做 WBP 时必须照此命名）：
//   BP 变量（UProperty，C++ 反射读写）：
//     FovValue  : float  视场角（度），滑条轨道 [kFovMin, kFovMax]
//     SpeedValue: float  移动速度（单位/秒），轨道 [0, kSpeedMax]
//     SensValue : float  灵敏度，轨道 [kSensMin, kSensMax]
//     Command   : int    按钮命令（0=无 1=冻结切换 2=重置视角 3=退出摄影模式）
//     bDragging : bool   任一滑条正被拖动（MouseCaptureBegin/End 维护）
//   C++ 写回（每帧直接写属性，BP 在 Event Tick 里读它们刷新显示）：
//     FovValue / SpeedValue / SensValue（外部变化或夹紧后的回写）
//     Frozen  : bool   时间是否已冻结
//     StatusId: int    0=未拍摄 1=拍摄中 2=已冻结（BP 按 id 显示状态文案）
//     Command = 0（执行完命令后清零，防重复触发）
//   BP 侧纪律：
//     - 滑条 OnValueChanged 写对应 Value 变量（SetValue 也会触发它，值相同 = 无害空转）
//     - Event Tick 里仅当 !bDragging 才用 SetValue 把变量刷回滑条显示
//     - 状态/数值文案全部在 BP 里由数值变量格式化（ToText），C++ 不写任何字符串变量

#include "core/config.h"

namespace pbzpm {

// 滑条轨道范围（WBP 编辑器里的滑条 Min/Max 必须与这里一致；C++ 按 config 实际范围夹紧）
inline constexpr float kWbpFovMin = 15.0f;
inline constexpr float kWbpFovMax = 150.0f;
inline constexpr float kWbpSpeedMax = 2000.0f;
inline constexpr float kWbpSensMin = 0.01f;
inline constexpr float kWbpSensMax = 0.50f;

// 引擎线程 → 桥：本帧从控件读出的 BP 变量快照
struct WbpVars {
    bool valid{false};  // 控件活着且关键属性都找到了；false = 本帧无输入也无写回
    float fov{};
    float speed{};
    float sens{};
    int command{};      // 见契约；0 = 无
    bool dragging{false};
};

// 桥 → 引擎线程：要对控件属性做的写回（mod_app 落到 Ue4ssPort）
struct WbpWriteBack {
    bool write_fov{false};    float fov{};
    bool write_speed{false};  float speed{};
    bool write_sens{false};   float sens{};
    bool write_frozen{false}; bool frozen{};
    bool write_status{false}; int status{};  // 0 未拍摄 / 1 拍摄中 / 2 已冻结
    bool write_command{false}; int command{}; // 恒为 0：执行完命令后清零
};

// 桥 → mod_app：要作用到 PhotoMode 的命令
struct WbpCommands {
    bool fov_changed{false};    double fov{};
    bool speed_changed{false};  double speed{};
    bool sens_changed{false};   double sens{};
    bool toggle_freeze{false};
    bool reset_view{false};
    bool exit_mode{false};
    void clear() { *this = WbpCommands{}; }
};

class WbpBridge {
public:
    // 菜单打开（或控件首次可用）时调用：以外部真值为基线，并立刻把真值推给 BP。
    void on_opened(double fov, double speed, double sens);

    // 菜单关闭/退出模式时调用：清基线，下次打开重新同步。
    void reset();

    // 每帧一次。ext_* 是 PhotoMode/Config 的当前真值；bounds 是夹紧范围。
    // 返回命令集；out 填写本帧对控件的全部写回。
    WbpCommands step(const WbpVars& vars,
                     double ext_fov, double ext_speed, double ext_sens, bool ext_frozen,
                     double fov_min, double fov_max,
                     double sens_min, double sens_max,
                     WbpWriteBack& out);

private:
    double last_fov_{};     // 最后应用的值（滑条命令基线）
    double last_speed_{};
    double last_sens_{};
    double pushed_fov_{};   // 最后写给 BP 的值（真值回显基线，只在变化时推）
    double pushed_speed_{};
    double pushed_sens_{};
    bool push_pending_{false};  // on_opened 置位：下一步强制把真值推给 BP
    int last_command_{};    // 上次执行过的命令（防同一命令双触发）
    bool opened_{false};
};

}  // namespace pbzpm
