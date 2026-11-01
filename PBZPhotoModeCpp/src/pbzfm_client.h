#pragma once

// PBZFM（PBZ-Framework-Menu，github.com/lin-414/PBZ-Framework-Menu）客户端：
// 把摄影模式的设置接进游戏内菜单框架（F8 呼出的 DirectComposition 覆盖菜单）。
//
// 接入方式与框架 SDK 的约定一致：运行时 GetModuleHandleW(L"PBZFM.dll") +
// GetProcAddress 解析 C API（见 third_party/pbzfm/pbzfm.h），零链接依赖；
// 后台线程轮询等待框架 DLL 出现（UE4SS 的 mod 加载顺序不定），注册在任何
// 线程都可以做，唯独不能在 DllMain 里做——本客户端的注册线程在 on_unreal_init
// 之后才启动，天然满足。
//
// 页面全部由客户端自绘（SDK 的 PBZFM_GetImGuiContext 通道，要求与框架链接
// 同一版 ImGui，1.92.1，见 static_assert）：
//   - 「设置 · Settings」页：语言切换（内置 zh/en/ja/ko + 外部词典
//     languages/*.json，见 pbzfm_client_lang.h）+ 全部设置项（键位捕获为
//     客户端自实现，镜像框架输入层的 VkToImGuiKey 映射）；
//   - 「状态 · Status」页：运行状态/位姿/键位速查 + 常用动作按钮。
// 设置的提交路径：自绘页（框架渲染线程）改 mirror_ → 写回 config.ini +
// submit_config → 引擎 Tick 线程应用（键位表重建 + 实时开关 + PhotoMode）；
// 所有引擎交互都在引擎 Tick 执行，框架线程只提交数据。
//
// 仅真实 DLL 构建参与编译（PBZPM_WITH_UE4SS）。
#ifdef PBZPM_WITH_UE4SS

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/config.h"

// pbzfm.h 会拉进 <windows.h>，其 min/max 宏会打坏本文件之后才解析的 UE4SS 头
// （如 std::numeric_limits<int32_t>::max()）。这里先要 NOMINMAX；若 windows.h
// 已被别处先包含（宏已存在），就地 undef 兜底。
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include "pbzfm.h"  // third_party/pbzfm（CMake 提供包含目录）
#include "pbzfm_client_lang.h"

// imgui 的 ImFont 前置声明（全局作用域，与 imgui.h 是同一类型；完整定义在实现处包含）
struct ImFont;

namespace pbzpm {

class PbzfmField;
struct KeyMap;

// 诊断页动作：框架渲染线程提交，引擎 Tick 线程执行（引擎调用全部留在游戏线程）
enum class PbzfmAction { ToggleMode, ToggleFreeze, ResetView, SetFov, ReloadConfig };

struct PbzfmCommand {
    PbzfmAction action{};
    double value{0.0};  // SetFov 用
};

// 引擎 Tick 每帧发布的只读快照（诊断页渲染用；全是标量与短字符串）
struct PbzfmSnapshot {
    bool active{false};
    bool frozen{false};
    double fov{0.0};
    double fov_min{15.0};
    double fov_max{150.0};
    double pos[3]{};
    double pitch{0.0};
    double yaw{0.0};
    double roll{0.0};
    std::string toggle_key{"F2"};
    std::string freeze_key{"F"};
    std::string reset_key{"R"};
    std::string roll_reset_key{"Z"};
    std::string pad_backend{"无"};
};

class PbzfmClient {
public:
    // PhotoModeMod 实现的宿主桥：客户端绝不直接碰引擎或 mod 内部状态
    class Host {
    public:
        virtual ~Host() = default;
        virtual std::string mod_dir() = 0;                  // mod 目录（config.ini / languages 路径）
        virtual Config current_config() = 0;                // 线程安全取当前配置副本
        virtual void submit_config(const Config& cfg) = 0;  // 菜单改动 → 引擎 Tick 应用
        virtual void submit_command(const PbzfmCommand& cmd) = 0;
        virtual void log(LogLevel level, const std::string& msg) = 0;
    };

    PbzfmClient() = default;
    ~PbzfmClient();
    PbzfmClient(const PbzfmClient&) = delete;
    PbzfmClient& operator=(const PbzfmClient&) = delete;

    // 启动发现线程（幂等）。host 生命周期必须覆盖 stop()。
    void start(Host* host);
    // 停止发现线程；之后的框架回调直接空转（mod 卸载前调用）
    void stop();

    // config.ini 被外部改动（重新载入）后，把自绘页显示值同步成新配置
    void sync_from_config(const Config& cfg);

    // 引擎 Tick 每帧发布快照（诊断页读取）
    void publish_snapshot(const PbzfmSnapshot& snap);

    // 词典实例：UE4SS 控制台诊断面板与自绘页共用同一份（外部 languages/*.json）
    PbzmI18n& lang() { return lang_; }

private:
    static void RenderSettingsThunk(void* userdata);   // 0.4.x 回退：整页（语言 + 全部分组）
    static void RenderLanguageThunk(void* userdata);   // 0.5.0+ 分类树：设置分类页（语言切换）
    static void RenderGroupThunk(void* userdata);      // 0.5.0+ 分类树：分组子节点（userdata = 组名）
    static void RenderDiagThunk(void* userdata);
    static void OnMenuEventThunk(int open, void* userdata);  // v2：菜单开关（旧框架为 NULL）
    void worker_main();
    void register_all();

    // ---- 自绘页（框架渲染线程） ----
    bool ensure_imgui();                 // SetCurrentContext + 后端指纹校验（一次）
    bool ensure_page_font();             // 自绘页专用合并字体（雅黑 + Malgun，韩文用）
    void refresh_language();             // menu_language（auto=跟随 PBZFM.ini）→ lang_ 生效
    void render_settings_page();                       // 0.4.x 回退整页
    void render_language_page();                       // 0.5.0+：设置分类页（语言切换 + 词典）
    void render_group_page(const char* group);         // 0.5.0+：单个分组的字段
    void render_field_widget(const PbzfmField& f, Config& draft);  // 单个字段的控件 + 提交语义
    void render_language_controls(Config& draft);                  // 语言下拉 + 词典重扫（两处共用）
    void rename_tree_for_language();  // 生效语言变化时用 RenameSection 热改名（0.5.0+ 树）
    void render_diag_page();
    void render_key_field(const PbzfmField& f, Config& draft);
    void commit_settings(const Config& draft, const std::string& what);

    Host* host_{nullptr};
    PBZFM_Api api_{};
    std::thread worker_;
    std::atomic<bool> quit_{false};
    std::atomic<bool> alive_{false};   // 注册完成；回调入口用它挡掉 stop 之后的回调
    std::mutex mirror_mutex_;
    Config mirror_{};                  // 自绘页绑定的当前配置（改动先落这里再提交）
    PbzmI18n lang_;                    // 客户端词典（外部 languages/*.json；控制台面板共用）
    bool imgui_checked_{false};        // ensure_imgui 只校验一次
    bool imgui_ok_{false};
    ImFont* page_font_{nullptr};       // 自绘页合并字体（懒建一次）
    bool page_font_failed_{false};     // 字体创建失败只告警一次
    std::mutex snap_mutex_;
    PbzfmSnapshot snapshot_{};

    // v2 菜单事件：关闭菜单的当帧取消改键捕获（键位捕获的 static 状态在渲染线程）
    std::atomic<bool> menu_closed_tick_{false};
    int menu_event_id_{0};

    // 分类树节点名（注册时按启动语言；语言变化后经 RenameSection 热改名）
    //   "language"/"status" = 固定顶层节点；"#group_*" = 六个分组顶层节点的当前显示名
    std::map<std::string, std::string> node_names_;
    std::vector<std::string> group_keys_;  // 分组键（注册顺序）
    std::string renamed_lang_id_;          // 上次注册/改名用的语言 id
};

}  // namespace pbzpm

#endif  // PBZPM_WITH_UE4SS
