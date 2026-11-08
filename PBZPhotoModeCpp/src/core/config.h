#pragma once

#include <string>
#include <vector>

#include "core/tuning.h"

namespace pbzpm {

enum class LogLevel { Error, Warn, Info, Debug };

// 鼠标竖向行为：
//   Translate = 相机上/下平移（默认，"上键上移/下键下移"的字面语义，默认升降键为鼠标左/右键）
//   Look      = 夹角俯仰（部分人习惯的"抬头/低头"）
enum class MouseVerticalMode { Translate, Look };

enum class FreezeMode { Dilation, Pause };

struct Config {
    // ---- [keys] 快捷键名，写法与默认键名一致：F2 / F / R / ADD / SUBTRACT / OEM_PLUS ----
    std::string key_toggle{"F2"};
    std::string key_freeze{"F"};
    std::string key_frame_freeze{"F7"};  // 画面定格：挂起本进程渲染/游戏线程，再按恢复
    std::string key_reset{"R"};
    std::string key_roll_reset{"Z"};  // 滚转归零（水平校正）：只把滚转回 0，位置/朝向/FOV 不动
    std::string key_fov_up{"ADD"};
    std::string key_fov_down{"SUBTRACT"};
    std::string key_pitch_up{"UP"};    // 仰视（抬头）：方向键上
    std::string key_pitch_down{"DOWN"};  // 俯视（低头）：方向键下
    std::string key_yaw_left{"LEFT"};    // 左转视角：方向键左（1.0.46 起从左右平移改来）
    std::string key_yaw_right{"RIGHT"};  // 右转视角：方向键右
    std::string key_roll_left{"Q"};      // 滚转：逆时针（默认 Q）
    std::string key_roll_right{"E"};     // 滚转：顺时针（默认 E）
    // 升降（translate 模式=相机上下平移，look 模式=俯仰）：默认鼠标左/右键；
    // 也可写键盘键名（如 PAGE_UP/PAGE_DOWN）或其它鼠标键（MOUSE_MIDDLE/MOUSE_X1/MOUSE_X2）
    std::string key_vertical_up{"MOUSE_LEFT"};
    std::string key_vertical_down{"MOUSE_RIGHT"};
    // 移动/变速/自由视角门控（2.0 起可配；默认值与旧版写死的键一致）
    std::string key_move_forward{"W"};
    std::string key_move_back{"S"};
    std::string key_move_left{"A"};
    std::string key_move_right{"D"};
    std::string key_boost{"SHIFT"};    // 加速（原写死 Shift）
    std::string key_slow{"CONTROL"};   // 减速（原写死 Ctrl；也决定 slow_look_mult 是否生效）
    std::string key_free_look{"ALT"};  // 自由转视角门控键（原写死 Alt；free_look_requires_alt=false 时不使用）
    // 景深/对焦（仅摄影模式内生效；走门控+CineCamera 组件通道，见 core/dof_math.h）：
    // G 总开关；,/. 对焦拉近/拉远（步进语义见 [dof] dof_focus_step）；
    // [ 开大光圈（F 值减小、更虚化）、] 收小光圈（更实）
    std::string key_dof_toggle{"G"};
    std::string key_focus_near{"COMMA"};
    std::string key_focus_far{"PERIOD"};
    std::string key_aperture_open{"OEM_4"};
    std::string key_aperture_close{"OEM_6"};

    // ---- [control] ----
    double move_speed{400.0};        // 单位/秒
    double boost_mult{4.0};          // 加速倍率（key_boost，默认 Shift）
    double slow_mult{0.3};           // 减速倍率（key_slow，默认 Ctrl）
    double slow_look_mult{0.1};      // 按住减速键（默认 Ctrl；手柄 pad_slow，默认 RS）时鼠标/右摇杆视角速度的倍率，越小视角越精细
    double look_sensitivity{0.10};   // 每单位鼠标位移转多少度
    double pitch_limit{89.0};
    bool invert_y{false};
    bool free_look_requires_alt{true};
    bool block_game_input{true};
    MouseVerticalMode mouse_vertical_mode{MouseVerticalMode::Translate};
    bool wheel_fov{true};           // 鼠标滚轮调整 FOV（原写死；false = 滚轮不参与 FOV）
    bool wheel_fov_invert{false};   // 滚轮方向反转：默认向上缩小 FOV（拉近），false 保持该方向
    double vertical_speed{200.0};    // 升降键上下平移速度
    double roll_speed{60.0};         // 滚转速度（度/秒）：默认 E 顺时针，Q 逆时针（键位见 [keys] key_roll_left/right）
    double pitch_speed{30.0};        // 方向键上/下与十字键上/下俯仰速度（度/秒）：正方向 = 仰视
    double yaw_speed{90.0};          // 方向键左/右与十字键左/右转向速度（度/秒）：右 = 右转（1.0.46 起替代 arrow_pan_mult）

    // ---- [gamepad] 手柄 ----
    bool pad_enabled{true};           // 总开关（false=不轮询手柄）
    std::string pad_toggle{"LS+RS"};  // 进入/退出：默认左右摇杆同时按下（L3+R3）
    std::string pad_freeze{"X"};
    std::string pad_reset{"Y"};
    std::string pad_roll_reset{"LB+RB"};  // 两肩键同按 = 滚转归零（同按滚转本来净零，不占新按键）
    std::string pad_fov_up{"A"};         // A = 增大 FOV（十字键已全部让位给视角调节）
    std::string pad_fov_down{"B"};       // B = 缩小 FOV
    std::string pad_pitch_up{"UP"};      // 十字键上 = 仰视
    std::string pad_pitch_down{"DOWN"};  // 十字键下 = 俯视
    std::string pad_yaw_left{"LEFT"};    // 十字键左 = 左转视角（1.0.46 起从左右平移改来）
    std::string pad_yaw_right{"RIGHT"};  // 十字键右 = 右转视角
    // 以下原为写死键（2.0 起可配）；RT/LT 单键绑定保留模拟量行程
    std::string pad_roll_left{"LB"};          // 滚转逆时针（原写死 LB）
    std::string pad_roll_right{"RB"};         // 滚转顺时针（原写死 RB）
    std::string pad_boost{"LS"};              // 加速（原写死 LS=左摇杆按下）
    std::string pad_slow{"RS"};               // 减速（原写死 RS=右摇杆按下）
    std::string pad_vertical_up{"RT"};        // 升降上（原写死 RT，模拟量）
    std::string pad_vertical_down{"LT"};      // 升降下（原写死 LT，模拟量）
    // 景深/对焦（沿用旧版默认；必须带肩键的组合，避免单按 BACK/START 误触）：
    // LB+BACK 拉近 / LB+START 拉远；RB+BACK 开光圈 / RB+START 收光圈。
    // 注意 LB/RB 单按默认是滚转，组合取焦期间会连带滚转（旧版同款行为，可改键避开）。
    std::string pad_focus_near{"LB+BACK"};
    std::string pad_focus_far{"LB+START"};
    std::string pad_aperture_open{"RB+BACK"};
    std::string pad_aperture_close{"RB+START"};
    // 新增键默认 NONE（显式解绑，手柄用户按需配置）
    std::string pad_dof_toggle{"NONE"};

    double pad_look_speed{60.0};      // 右摇杆满行程的转速（度/秒）
    double pad_move_scale{1.0};       // 左摇杆移动占 move_speed 的比例
    double pad_deadzone{0.15};        // 摇杆死区（0~0.5）

    // ---- [camera] ----
    double fov_default{90.0};
    double fov_min{15.0};
    double fov_max{150.0};
    double fov_step{2.0};
    bool ultrawide_unlock{true};  // 超宽屏（21:9/32:9）：进入摄影模式时解除 16:9 画幅锁（两侧黑边），退出还原
    // UUU 式「接管当前相机」：默认开启；不 spawn、不切 ViewTarget，每帧（post tick）写相机 POV。
    // 进入时接管失败会在同一次 enter 内自动回退到独立相机。设 false 可强制走旧路径做对比。
    bool takeover_current_camera{true};

    // ---- [dof] 景深/对焦（2.1 轮复活：门控 + CineCamera 组件通道，见 core/dof_math.h）----
    // 距离单位 cm（沿用旧版语义）。dof_focus_step 是「最小步进」：实际步进 =
    // max(配置步进, 当前距离 × 10%)，Shift 粗调 / Ctrl 微调照常生效。
    // 启用任一项（含 [look] 的色差）会自动切到独立相机后端并打开渲染门控。
    bool dof_enabled{false};
    double dof_focal_distance{tuning::kDofDefaultFocalCm};
    double dof_fstop{tuning::kDofFstopDefault};
    double dof_sensor_width{tuning::kDofSensorDefaultMm};
    double dof_focus_step{tuning::kDofFocusStepDefaultCm};
    double dof_aperture_step{tuning::kDofApertureStepDefault};
    // NLP 过场渲染档已并入景深总开关（2026-11-08 用户决策，键登记 Deprecated）：
    // 总开关开 = 闸门 + NLP 档 + 组件通道一体（564 同款形态），关 = 全无。取证记录见 tuning.h。
    // bokeh gate cvar 二分开关（默认 false）：true 才写 r.DOF.{Gather,Recombine}.EnableBokehSettings /
    // Recombine.Quality。2026-11-07 崩溃二分用：第 5/6 次崩溃逐字节同址（call rdx→全零页），bokeh 路径
    // 是嫌疑首位（shipping 可能无实现）；false 时 DOF 仍走 ShowFlag/画质/kernel 闸门+组件通道。
    bool dof_bokeh_gate{false};

    // 画面效果（[look] 段）：全部走自建后处理体积，本作 gameplay 视图里实测只有这条通道能出画面。
    // 每项有一个独立开关；关掉只停用这一项，滑块值仍保留。每次进入摄影模式时五项会恢复默认参数。
    double vignette = tuning::kVignetteDefault;           // 暗角 0~4（0=关闭）
    double saturation = 1.0;                              // 饱和度倍率（1=原样）
    double contrast = 1.0;                                // 对比度倍率（1=原样）
    double bloom = 0.0;                                   // 泛光强度（0=不覆盖）
    double temperature = 0.0;                             // 冷暖 -1~1（0=不覆盖）
    bool vignette_enabled{true};                          // 暗角开关（false=停用但不改滑块值）
    bool saturation_enabled{true};                        // 饱和度开关
    bool contrast_enabled{true};                          // 对比度开关
    bool bloom_enabled{true};                             // 泛光开关
    bool temperature_enabled{true};                       // 冷暖开关
    // 调色·进阶三件（2026-11-08 批次）：无独立开关——值回到中性（= 默认）即「不覆盖该项」，
    // 交还游戏自己的设置；离开中性才写 bOverride_* + 绝对值。与饱和/对比同一 ColorGrading LUT
    // 每通道家族（本 build 实机验证只消费这五件）；同批试过的分区调色/胶片曲线/白平衡/泛光
    // 阈值软膝十项判死移除（取证在 tuning.h）。每次进入摄影模式随 [look] 一起恢复默认。
    double color_gain = tuning::kColorGainDefault;              // 亮度 0~4（1=原样）
    double color_gamma = tuning::kColorGammaDefault;            // 伽马 0.25~4（1=原样）
    double color_offset = tuning::kColorOffsetDefault;          // 黑位 -0.2~0.2（0=原样，-压黑/+提灰）
    // 色差（2.1 复活）：0 = 不覆盖。与五项不同，它走门控 + CineCamera 组件通道
    //（tuning.h kFilmGate*），只在独立相机后端可用；每次进入摄影模式随 [look] 一起恢复默认。
    double chromatic_aberration = 0.0;                    // 色差 0~5

    // ---- [effects] ----
    bool hide_player{false};
    bool hide_game_hud{true};    // 摄影模式默认隐藏游戏 HUD（退出时恢复）
    bool hide_interact_prompt{true};  // 摄影模式默认隐藏交互提示（可互物体旁的 E 键图标，退出恢复）
    bool hide_enemy_health_bars{true};  // 摄影模式默认隐藏敌人（怪物）血条：切到 OnlyLockedTarget 显示模式，退出还原
    bool hide_lockon_marker{true};  // 摄影模式默认隐藏锁定标记（锁定敌人时其身上的白点），退出还原
    bool hide_subtitles{true};  // 摄影模式默认隐藏对话字幕：字幕控件 Collapsed、FlowDialog 用 Hidden 保 Tick；对话中不改字幕设置，退出还原
    bool repair_ui_settings_once{true};  // 1.0.69 一次性修复：首次运行把字幕/场景交互提示开关强制打开（旧版可能写坏，成功后写标记不再执行）
    bool auto_pause_time{true};  // 进入摄影模式时自动暂停时间（默认开启）
    FreezeMode freeze_mode{FreezeMode::Pause};
    double freeze_dilation{0.0001};
    bool freeze_sequences{true};  // 冻结时同时暂停剧情/过场（Sequencer 播放器），解冻恢复
    bool freeze_dialogue_flow{true};  // 冻结时把对话 Flow（WBP_FlowDialog_C）改为 Collapsed 停 Tick，避免对话 Flow 在冻结期推进/提前结束语音；解冻恢复 Hidden。仅在 hide_subtitles=true 时生效
    bool stop_wwise_on_freeze{true};  // 冻结时处理 Wwise 音频（有逐 PlayingID 时逐语音真暂停并跳过整引擎；无 ID 才整引擎兜底）
    bool wwise_engine_suspend{true};  // 仅无逐语音 PlayingID 时用 AK::SoundEngine::Suspend(true,false) 作整引擎静音兜底（有 ID 不调用，避免 render-anyway 推进语音时间轴）
    bool stop_camera_shakes_on_freeze{true};  // 冻结时停用相机修饰器并立即结束 shake（真实时间驱动的残余微动，冻结画面边缘抖动/脸部阴影爬动的主变化源），解冻按原状态还原
    bool freeze_wind{true};  // 冻结时把风源（WindDirectionalSource）风强/风速归零：材质顶点摆动（布料/植被）的幅度源，暂停拦不住；解冻还原
    bool freeze_mpc{true};   // 冻结时钉住材质参数集合（MPC）当前值：冻结期每帧写回快照，压制实时驱动参数继续推布料/植被摆动；解冻释放
    bool freeze_cloth{true}; // 冻结时挂起角色/敌人的布料模拟（SkeletalMeshComponent clothing + ChaosClothComponent），保留最后姿态；解冻恢复
    bool freeze_custom_cloth{false}; // 冻结时额外处理 PBZ 自定义布料/摆动 + 毛发/粒子：Sway 组件 tick（全对象扫描按类名找）、r.Cloth.Enable、Groom 模拟、Niagara 暂停（1.0.76 起不再写 pbz.Sway.SkipSkeletalKinematicBoneUpdate）
    bool freeze_wpo{false}; // 冻结时关闭 WPO 评估（SkeletalMesh + StaticMesh/植被/地形图元），排查材质顶点动画造成的布料/植被闪烁
    bool freeze_tonemapper_dither_off{false}; // 冻结时把 r.Tonemapper.Quality 从 5 降到 4（去掉 GrainJitter 逐帧抖动）并关 Backbuffer 量化抖动
    bool freeze_skeletal_animation{true}; // 冻结时暂停全部 SkeletalMesh 的 AnimBP/骨骼 pose（bPauseAnims/bNoSkeletonUpdate），防止骨骼驱动的 cape 继续流动；解冻恢复
    bool freeze_motion_blur_off{true};    // 冻结时 r.MotionBlurQuality=0，消除残留速度缓冲造成的流动拖影；解冻还原
    bool freeze_aa_off{false};           // 冻结时关 AA：cvar r.AntiAliasingMethod=0，同时直接改 PPV/相机 POV 的 FPostProcessSettings（AA/动态模糊/Lumen/反射），解冻还原
    bool freeze_wpo_velocity_off{false}; // 冻结时临时关闭 WPO/Nanite WPO/植被/毛发的速度输出（r.Velocity.EnableVertexDeformation 等），解冻/退出还原；用于判断假运动向量
    bool freeze_framegen_off{true};      // 冻结时抑制帧生成（r.PBZ.SuppressFrameGen=1 + 显式关 DLSSG/FSR-FI），解冻/退出还原；静止拍照不需要插帧，且 FG 可能放大/制造闪烁
    bool freeze_dlss_off{false};         // 冻结时临时关闭 DLSS 超分（r.NGX.DLSS.Enable=0），解冻/退出还原；用于判断闪烁是否来自 DLSS SR（默认 false，保留画质）
    // ---- 光照 A/B（Lumen / 屏幕空间光照）：冻结时临时改光照 CVar，解冻/退出还原 ----
    // 每个子项独立，默认全关；freeze_lighting_ab_cycle=true 时按阶段自动轮换（见 README hotfix11）。
    bool freeze_lumen_screen_traces_off{false}; // 关 Lumen 屏幕空间追踪：GI/反射的 ScreenTraces=0（保留 Lumen 距离场追踪）
    bool freeze_lumen_short_range_ao_off{false};// 关 Lumen 短距屏幕空间 AO（ShortRangeAO / DiffuseIndirect.SSAO / r.AmbientOcclusionLevels）
    bool freeze_lumen_radiance_cache_off{false};// 关 Lumen 屏幕探针辐射缓存（r.Lumen.ScreenProbeGather.RadianceCache=0）
    bool freeze_lumen_fixed_jitter{false};      // 固定 Lumen 抖动/状态帧（FixedJitterIndex/FixedStateFrameIndex=0），判断逐帧随机采样
    bool freeze_lumen_off{false};               // 关 Lumen GI/反射总开关（DiffuseIndirect.Allow / Reflections.Allow / 方法 CVar）
    bool freeze_lighting_ab_cycle{false};       // 冻结期自动轮换光照阶段（原始→屏幕追踪→短距AO→辐射缓存→固定抖动→Lumen全关）
    double freeze_lighting_ab_phase_seconds{8.0}; // 自动轮换每阶段持续秒数（1..120）
    // ---- 阴影/后处理 A/B（Lumen 之外的第二层）：自动轮换 SMRT/VSM/接触阴影/体积雾/后处理 ----
    bool freeze_shadow_ab_cycle{false};         // 冻结期自动轮换阴影/后处理阶段（见 README hotfix12）
    double freeze_shadow_ab_phase_seconds{8.0}; // 阴影/后处理轮换每阶段持续秒数（1..120）
    bool freeze_diag{false}; // 冻结诊断默认关闭（诊断本身要做反射扫描）；开启后只在状态翻转或每 10 秒输出一条
    // ---- F7 画面定格（全线程冻结，独立于上面的引擎暂停冻结）----
    bool frame_freeze_time_compensation{true};  // F7 定格期间冻结时间 API，恢复时扣掉定格时长；只在“连渲染线程一起停”时成立
    bool frame_freeze_audio_suspend{true};      // F7 定格前若摄影模式没挂 Wwise，则由 F7 整引擎 Suspend(true,false)，恢复时唤醒
    double frame_freeze_timeout_seconds{60.0};  // F7 定格安全阀：超时自动恢复，避免永久卡死（5~300 秒）；60 秒够一次截图框选

    // ---- [ui] ----
    bool imgui_tab{true};        // UE4SS 控制台里的只读诊断面板（游戏内无菜单，v1.8.0 起纯快捷键）；
                                 // false 时不注册 UE4SS tab，可写设置的唯一入口是 PBZFM 菜单页
    std::string menu_language{"auto"};  // PBZFM 菜单界面语言：auto=跟随 PBZFM 菜单；或词典 id
                                        //（内置 zh-CN/en-US/ja-JP/ko-KR，或 languages/*.json 的文件名）

    // ---- [misc] ----
    LogLevel log_level{LogLevel::Info};
};

struct ConfigLoadResult {
    Config cfg{};
    std::vector<std::string> warnings{};
    std::string source{"内置默认值"};
    // ok=false 表示文件层失败；用下面的细分标志区分「文件不存在」与「存在但读不了」，
    // 解析层面的非法项只进 warnings，仍返回可用的 cfg。
    bool ok{true};
    bool file_missing{false};
    bool read_error{false};
    // 命中已废弃的键（[dof] 段、色差/胶片颗粒）时按原样记下来：解析照常成功，调用方据此
    // **一次性重写 config.ini**，把这些键从文件里物理删掉（只告警不清理会一直留着，见 hotfix35）。
    std::vector<std::string> legacy_keys{};
};

// 把 [look] 五项画面效果恢复为默认参数（数值 + 五个开关）。每次进入摄影模式时调用；
// 返回是否真的发生了变化（调用方据此决定是否写回 config.ini / 刷新菜单镜像）。
bool reset_look_defaults(Config& cfg);

// 解析 INI 文本。未知键 / 非法值只累积 warning，保留对应默认值。
ConfigLoadResult parse_config(const std::string& ini_text, const char* source = "config.ini");

// 从磁盘读取（不存在时返回默认值，source 标注为默认）
ConfigLoadResult load_config_file(const std::string& path);

// 带中文注释的默认配置文件内容（缺配置文件时可自动写出）
std::string default_config_text();

// 把任意一份配置序列化成与 default_config_text 完全相同格式的 INI 文本
// （PBZFM 菜单写回 config.ini 用；所有字段都会输出，未改动的字段保持默认值）
std::string config_ini_text(const Config& cfg);

// 原子写出配置文件（UTF-8）：先写 <path>.tmp，成功后再 rename 覆盖目标，
// 避免引擎线程读盘时读到半写的文件。失败时 error 非空则填原因；返回是否成功。
bool save_config_file(const std::string& path, const Config& cfg, std::string* error = nullptr);

const char* log_level_name(LogLevel level);
const char* mouse_vertical_mode_name(MouseVerticalMode mode);

}  // namespace pbzpm
