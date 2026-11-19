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
// 同一版 ImGui；版本一致性在 ensure_imgui() 运行时校验，编译期只做基本 API 约束）：
//   - 「设置 · Settings」页：语言切换（内置 zh/en/ja/ko + 外部词典
//     languages/*.json，见 pbzfm_client_lang.h）+ 全部设置项（键位捕获为
//     客户端自实现，镜像框架输入层的 VkToImGuiKey 映射）；
//   - 「状态 · Status」页：运行状态/位姿/键位速查 + 常用动作按钮。
// 设置的提交路径：自绘页（框架渲染线程）改 mirror_ → 写回 settings.json +
// submit_config → 引擎 Tick 线程应用（键位表重建 + 实时开关 + PhotoMode）；
// 所有引擎交互都在引擎 Tick 执行，框架线程只提交数据。
//
// 仅真实 DLL 构建参与编译（PBZPM_WITH_UE4SS）。
#ifdef PBZPM_WITH_UE4SS

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
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

// ---- PBZFM SDK ABI 自检（API v5）----
// GetLanguage 必须是 PBZFM_Api 的最后一个成员：框架若在它之后再追加字段而
// 本地 SDK 头没同步，sizeof/offsetof 断言会当场编译失败，而不是运行期读垃圾指针。
// 签名也一并锁住，防止两边对导出契约的理解漂移。
static_assert(std::is_same_v<decltype(PBZFM_Api::GetLanguage), const char* (*)(void)>,
              "PBZFM_Api::GetLanguage 签名与 SDK 契约不一致（应为 const char* (*)(void)）");
static_assert(sizeof(PBZFM_Api) ==
                  offsetof(PBZFM_Api, GetLanguage) + sizeof(PBZFM_Api::GetLanguage),
              "PBZFM_Api::GetLanguage 必须是结构体最后一个成员（本地 pbzfm.h 未与框架同步？）");

// imgui 的 ImFont 前置声明（全局作用域，与 imgui.h 是同一类型；完整定义在实现处包含）
struct ImFont;

namespace pbzpm {

struct PbzfmField;
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
    // 状态页键位速查：移动/变速/自由视角门控（2.0 起可配，显示当前生效键）
    std::string free_look_key{"ALT"};
    std::string move_forward_key{"W"};
    std::string move_back_key{"S"};
    std::string move_left_key{"A"};
    std::string move_right_key{"D"};
    std::string boost_key{"SHIFT"};
    std::string slow_key{"CONTROL"};
    std::string roll_left_key{"Q"};
    std::string roll_right_key{"E"};
    bool wheel_fov{true};
    std::string pad_backend{"无"};
};

class PbzfmClient {
public:
    // PhotoModeMod 实现的宿主桥：客户端绝不直接碰引擎或 mod 内部状态
    class Host {
    public:
        virtual ~Host() = default;
        virtual std::string mod_dir() = 0;                  // mod 目录（settings.json / languages 路径）
        virtual Config current_config() = 0;                // 线程安全取当前配置副本
        virtual void submit_config(const Config& cfg) = 0;  // 菜单改动 → 引擎 Tick 应用
        // 滑条拖动实时预览：把拖动中的草案推给引擎（引擎 Tick 应用，下一帧上屏），但宿主侧
        // 不打「已应用」日志；写盘由菜单侧跳过（见 commit_settings）。默认空实现：测试桩
        // 与未接预览的宿主无需理会，菜单侧对其无返回值假设。同一草案随后必有完整提交兜底。
        virtual void submit_config_preview(const Config& cfg) { (void)cfg; }
        virtual void submit_command(const PbzfmCommand& cmd) = 0;
        virtual void log(LogLevel level, const std::string& msg) = 0;
        // ---- 取景角度滑条（相机组 cam_pitch/cam_yaw/cam_roll）----
        // 值不落在 Config 上：读实时姿态（鼠标/手柄转过之后滑条也跟得上），拖动直接写姿态。
        // 默认实现 = 不可用：未接这两个入口的宿主（例如测试桩）会把它们当普通配置字段处理。
        virtual bool photo_mode_active() const { return false; }
        virtual bool pose_angle(int axis, double* degrees) const {
            (void)axis;
            (void)degrees;
            return false;
        }
        virtual bool set_pose_angle(int axis, double degrees) {
            (void)axis;
            (void)degrees;
            return false;
        }
    };

    PbzfmClient() = default;
    ~PbzfmClient();
    PbzfmClient(const PbzfmClient&) = delete;
    PbzfmClient& operator=(const PbzfmClient&) = delete;

    // 绑定宿主（幂等，只记录 host_ 与写盘路径，不启动线程）。
    // on_unreal_init() 必须在 reload_config() 之前调用它：reload_config 里的
    // refresh_language() 要读 host_->mod_dir()；而 start() 要等 mode_/runtime_
    // 建好之后才能起框架线程（页面首帧要拿完整快照）。
    void attach_host(Host* host);
    // 启动发现线程（幂等）。host 生命周期必须覆盖 stop()。
    void start(Host* host);
    // 停止发现线程；之后的框架回调直接空转（mod 卸载前调用）
    void stop();

    // settings.json 被外部改动（重新载入）后，把自绘页显示值同步成新配置
    void sync_from_config(const Config& cfg);

    // 引擎 Tick 每帧发布快照（诊断页读取）
    void publish_snapshot(const PbzfmSnapshot& snap);

    // 词典实例：UE4SS 控制台诊断面板与自绘页共用同一份（外部 languages/*.json）
    PbzmI18n& lang() { return lang_; }

    // 生效语言重算（settings.json 显式 id 优先，否则跟随 PBZFM）。
    // 跟随来源优先取框架 v5 的 PBZFM_GetLanguage（每帧实时值）；框架较旧
    // （该指针为 NULL）时降级读 Documents\PBZ\PBZFM\PBZFM.ini，按 mtime 判变化。
    // 框架渲染线程（页面回调）与引擎 Tick 线程都会调用，内部互斥。
    void refresh_language(bool rename_tree = true);
    // 引擎 Tick 每帧调用：让语言跟随不依赖"本模组页面正在渲染"。
    // 这也保证 PBZFM 里切完语言、人还停在框架自己的页面上时，侧栏标签与
    // 控制台帮助文案当场跟着变，不需要退出再进菜单。
    void tick_language();

    // 框架菜单当前是否可见（PBZFM_IsMenuOpen；框架未接入/旧版返回 false）。
    bool menu_open() const;
    // 请求框架关闭菜单（PBZFM_CloseMenu，v3 可选导出；旧框架返回 false）。延迟到帧边界生效。
    bool request_close_menu();
    // 引擎 Tick 线程每帧调用：摄影模式在菜单打开期间暂停输入采集，
    // 否则菜单里的鼠标左键会被 GetAsyncKeyState 当成 key_vertical_up（上升）。

private:
    static void RenderSettingsThunk(void* userdata);   // 0.4.x 回退：整页（语言 + 全部分组）
    static void RenderLanguageThunk(void* userdata);   // 0.5.0+ 分类树：设置分类页（语言切换）
    static void RenderGroupThunk(void* userdata);      // 0.5.0+ 分类树：分组子节点（userdata = 组名）
    static void RenderDiagThunk(void* userdata);
    static void OnMenuEventThunk(int open, void* userdata);  // v2：菜单开关（旧框架为 NULL）
    void worker_main();
    void register_all();
    // 后台写盘线程：渲染线程只标记脏数据，落盘（临时文件 + 原子替换）在这里做；
    // stop() 会通知它退出并在退出前 flush 最后一次改动。
    void saver_main();
    void request_save(const Config& draft);

    // ---- 自绘页（框架渲染线程） ----
    bool ensure_imgui();                 // SetCurrentContext + 后端指纹校验（一次）
    bool ensure_page_font();             // 自绘页专用合并字体（雅黑 + Malgun，韩文用）
    void refresh_api_language();         // 取 PBZFM_GetLanguage（v5；旧框架指针为 NULL）
    void refresh_ini_language();         // 旧框架降级：mtime 变了才重读 Documents 下的实例 ini
    void render_settings_page();                       // 0.4.x 回退整页
    void render_language_page();                       // 0.5.0+：设置分类页（语言切换 + 词典）
    void render_group_page(const char* group);
    // 「画面效果」组页的风格预设区（0.4.x 整页与 0.5.0+ 分组页共用）：保存预设按钮 + 预设
    // 下拉框（选中即套用 <mod>/look_preset_<编号>.ini）+ 删除按钮（两击确认删当前选中项）。
    // 不走 schema，直接改 draft 走 commit_settings；文件文本真源在 core/look_preset.h。
    void render_look_presets(Config& draft);
    // 0.5.0+：单个分组的字段
    // 单个字段的控件 + 提交语义。keys 只用于 pbzfm_read_field 的 Key 回退（键名解析失败 → 默认键）
    void render_field_widget(const PbzfmField& f, Config& draft, const KeyMap& keys);
    void render_language_controls(Config& draft);                  // 语言下拉 + 词典重扫（两处共用）
    void rename_tree_for_language();  // 生效语言变化时用 RenameSection 热改名（0.5.0+ 树）
    void render_diag_page();
    void render_key_field(const PbzfmField& f, Config& draft, const KeyMap& keys);
    void commit_settings(const Config& draft, const std::string& what);
    // 页面每帧从 mirror_ 取一份草案。Float 滑条拖动中改草案 + 推预览到引擎、松手才落盘
    // 提交，所以草案必须写回 mirror_ 才能跨帧保持——否则每帧重新拷贝会让滑条弹回原位、
    // 松手提交旧值。
    // 写回前校验代次：期间发生过配置重载/其它提交就放弃，避免旧草案覆盖新配置。
    Config take_draft(std::uint64_t& generation_out);
    void store_draft(const Config& draft, std::uint64_t generation);

    Host* host_{nullptr};
    PBZFM_Api api_{};
    std::thread worker_;
    std::atomic<bool> quit_{false};
    std::atomic<bool> alive_{false};   // 注册完成；回调入口用它挡掉 stop 之后的回调
    // ---- 后台写盘（commit_settings 只排队，不阻塞渲染线程）----
    std::thread saver_;
    std::atomic<bool> saver_running_{false};
    // 保护 save_dirty_ / save_quit_ / save_cfg_ / save_path_：渲染线程（commit_settings）
    // 在锁内写并 notify，后台 saver 线程在锁内取走快照后落盘。
    std::mutex save_mutex_;
    std::condition_variable save_cv_;
    bool save_dirty_{false};
    bool save_quit_{false};
    Config save_cfg_{};
    std::string save_path_{};
    // 保护 mirror_：引擎 Tick（sync_from_config）与 PBZFM 渲染线程（页面读写/提交）共用。
    std::mutex mirror_mutex_;
    Config mirror_{};                  // 自绘页绑定的当前配置（改动先落这里再提交）
    // mirror_ 代次：外部重载/提交时自增。页面写回草案前比对，防止「拖动中的旧草案」
    // 覆盖掉同帧发生的配置重载。
    std::uint64_t mirror_generation_{0};
    PbzmI18n lang_;                    // 客户端词典（外部 languages/*.json；控制台面板共用）
    bool imgui_checked_{false};        // ensure_imgui 只校验一次
    bool imgui_ok_{false};
    ImFont* page_font_{nullptr};       // 自绘页合并字体（懒建一次）
    bool page_font_failed_{false};     // 字体创建失败只告警一次
    // 保护 snapshot_：引擎 Tick（publish_snapshot）写，PBZFM 渲染线程（诊断页）读。
    std::mutex snap_mutex_;
    PbzfmSnapshot snapshot_{};

    // v2 菜单事件：关闭菜单的当帧取消改键捕获
    std::atomic<bool> menu_closed_tick_{false};
    int menu_event_id_{0};

    // ---- 语言跟随状态（H-6）----
    // 线程归属：引擎 Tick、框架渲染线程、配置重载线程都会进 refresh_language，
    // 统一由 lang_mutex_ 串行化；快照本身已由 PbzmI18n 内部保证读写安全。
    std::mutex lang_mutex_;
    // 跟随来源缓存：v5 为 PBZFM_GetLanguage 的实时值；旧框架为实例 ini 的 language
    // （已解析成具体语言 id；文件里键缺失 = 跟随系统时也落成系统语言）。空=无来源。
    std::string pbzfm_lang_;
    // 诊断：当前跟随来源的可读描述（api(v5) / ini降级(path,key) / 空=无），
    // 供启动后只打一次的 "[lang] 跟随来源 = ..." 日志使用。受 lang_mutex_ 保护。
    std::string lang_source_;
    bool lang_diag_logged_ = false;
    std::int64_t pbzfm_ini_mtime_{-1};   // 上次成功读取时的 last_write_time ticks（-1=还没读过）
    std::uint64_t pbzfm_ini_size_{0};    // 上次读取时的文件大小（mtime 精度不足时兜底）
    bool pbzfm_ini_loaded_{false};       // 是否已有一次读取结果（含"文件不存在"）
    double lang_changed_at_{0.0};        // 最近一次生效语言变化的时间（诊断用）
    double rename_retry_at_{0.0};        // 树标签改名失败后的下次重试时间（steady 秒）
    bool key_capturing_{false};          // 改键捕获进行中
    const PbzfmField* key_capture_field_{nullptr};  // 捕获中的字段（指向静态字段表）
    double key_capture_started_{0.0};    // 捕获开始时间（steady 秒，用于 5 秒超时）
    std::string preset_msg_;             // 预设保存/套用/删除的最近一次结果提示（渲染线程独占；空=不显示）
    int preset_selected_{-1};            // 下拉框当前选中的预设下标（渲染线程独占；-1=未选）
    std::string preset_delete_target_;   // 删除两击确认的待删文件路径（渲染线程独占；空=无待确认）
    double preset_delete_deadline_{0.0}; // 确认态截止时刻（steady 秒）；超时/完成后清空回到普通删除钮

    // 分类树节点名（注册时按启动语言；语言变化后经 RenameSection 热改名）
    //   "language"/"status" = 固定顶层节点；"#group_*" = 六个分组顶层节点的当前显示名
    std::map<std::string, std::string> node_names_;
    std::vector<std::string> group_keys_;  // 分组键（注册顺序）
    std::string renamed_lang_id_;          // 上次注册/改名用的语言 id
};

}  // namespace pbzpm

#endif  // PBZPM_WITH_UE4SS
