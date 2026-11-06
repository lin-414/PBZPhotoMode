// =============================================================================
//  Ue4ssPort 成员函数实现：时间缩放与暂停（含剧情播放器）
// =============================================================================
#ifdef PBZPM_WITH_UE4SS

#include "engine/ue4ss_port_internal.h"

namespace pbzpm {
using namespace detail;

namespace {

// A/B 实验项：UWorld.bIsCameraMoveableWhenPaused。
// UE5.7 里它是 UWorld 的 bitfield，优先走反射属性（FBoolProperty，write_bool 能处理 bitfield）；
// 属性列表里没有时退回 UEPseudo 的 UWorld 成员包装（MemberVariableLayout 已提供该字段）。
// 读不到原值就不写——恢复时不能靠猜默认值。
auto read_world_camera_moveable(UObject* scene, bool& out) -> bool {
    if (!is_ok(scene)) return false;
    if (FProperty* property = find_property(scene, "bIsCameraMoveableWhenPaused"); property != nullptr) {
        double value = 0.0;
        if (read_number(property, scene, value)) {
            out = value != 0.0;
            return true;
        }
    }
    try {
        out = static_cast<UWorld*>(scene)->GetbIsCameraMoveableWhenPaused();
        return true;
    } catch (...) {
        return false;
    }
}

auto write_world_camera_moveable(UObject* scene, bool value) -> bool {
    if (!is_ok(scene)) return false;
    if (FProperty* property = find_property(scene, "bIsCameraMoveableWhenPaused"); property != nullptr) {
        if (write_bool(property, scene, value)) return true;
    }
    try {
        static_cast<UWorld*>(scene)->GetbIsCameraMoveableWhenPaused() = value;
        return true;
    } catch (...) {
        return false;
    }
}

// 处决/过场演出会给玩家角色留下“时间暂停”残留：
//   - AActor.CustomTimeDilation 被演出曲线压到 0（只冻角色，世界照常跑）；
//   - SkeletalMeshComponent.bPauseAnims / bNoSkeletonUpdate 被演出置 true；
//   - AWorldSettings.CinematicTimeDilation（Sequencer slomo track 的附加倍率）停在 0。
// 演出被摄影模式暂停后可能再也走不到自己的收尾，这些值就会永久留着，表现为角色模型卡住。
constexpr double kStuckDilationEpsilon = 0.001;

auto read_property_number(UObject* object, const char* name, double& out) -> bool {
    if (!is_ok(object)) return false;
    FProperty* property = find_property(object, name);
    return property != nullptr && read_number(property, object, out);
}

auto write_property_number(UObject* object, const char* name, double value) -> bool {
    if (!is_ok(object)) return false;
    FProperty* property = find_property(object, name);
    return property != nullptr && write_number(property, object, value);
}

}  // namespace

double Ue4ssPort::get_time_dilation() {
    UObject* statics = gameplay_statics();
    UObject* scene = world();
    if (is_ok(statics) && is_ok(scene)) {
        UFunction* function = find_function(statics, "GetGlobalTimeDilation");
        if (function != nullptr) {
            ParamBlock params(function);
            if (params.valid()) {
                params.set_object("WorldContextObject", scene);
                statics->ProcessEvent(function, params.data());
                double value = 1.0;
                if (params.get_number("ReturnValue", value) && value > 0.0) return value;
            }
        }
    }

    // 兜底：直接读 WorldSettings.当 bAllowTimeDilation=false 时它仍可能是旧值，
    // 但至少比恒返回 1.0 更能反映真实状态。
    if (UObject* settings = world_settings(); is_ok(settings)) {
        double value = 1.0;
        if (read_number(find_property(settings, "TimeDilation"), settings, value) && value > 0.0) return value;
    }
    return 1.0;
}

std::string Ue4ssPort::time_settings_summary() {
    UObject* settings = world_settings();
    if (!is_ok(settings)) return "WorldSettings=读不到";
    double allow = 1.0;
    double dilation = 1.0;
    double min_dilation = 0.0;
    double max_dilation = 0.0;
    const bool have_allow = read_number(find_property(settings, "bAllowTimeDilation"), settings, allow);
    const bool have_dilation = read_number(find_property(settings, "TimeDilation"), settings, dilation);
    const bool have_min = read_number(find_property(settings, "MinGlobalTimeDilation"), settings, min_dilation);
    const bool have_max = read_number(find_property(settings, "MaxGlobalTimeDilation"), settings, max_dilation);
    char buf[220]{};
    std::snprintf(buf, sizeof(buf),
                  "WorldSettings bAllowTimeDilation=%s TimeDilation=%.4f Min=%.4f Max=%.4f（字段读取 %d/%d/%d/%d）",
                  have_allow ? (allow != 0.0 ? "true" : "false") : "?", have_dilation ? dilation : -1.0,
                  have_min ? min_dilation : -1.0, have_max ? max_dilation : -1.0, have_allow ? 1 : 0,
                  have_dilation ? 1 : 0, have_min ? 1 : 0, have_max ? 1 : 0);
    return buf;
}

// 时间冻结的真正入口：
//   UGameplayStatics::SetGlobalTimeDilation 最终只是 AWorldSettings::SetTimeDilation，
//   而 SetTimeDilation 会被 Min/MaxGlobalTimeDilation 夹紧；GetEffectiveTimeDilation
//   还会在 bAllowTimeDilation=false 时直接返回 1.0。所以这里直接写这三个字段，
//   再调用一次引擎函数保持引擎侧状态同步，最后回读确认是否真的生效。
bool Ue4ssPort::set_time_dilation(double dilation) {
    UObject* settings = world_settings();
    if (!is_ok(settings)) {
        // 找不到 WorldSettings 时退回原路径（至少不会比旧版更差）
        UObject* statics = gameplay_statics();
        UObject* scene = world();
        if (!is_ok(statics) || !is_ok(scene)) return false;
        return invoke(statics, "SetGlobalTimeDilation", [&](ParamBlock& params) {
            return params.set_object("WorldContextObject", scene) && params.set_number("TimeDilation", dilation);
        });
    }

    // 快照按世界令牌校验：换代即作废，重新采样（旧世界的原值不能写进新世界，H-2）
    if (!time_snapshot_.belongs_to(world_generation())) {
        time_snapshot_.reset();
        time_snapshot_.adopt(world_generation());
    }
    if (time_snapshot_.read_mask == 0) {
        TimeSettingsSnapshot& snap = time_snapshot_;
        double value = 0.0;
        // 每个字段单独判断读取是否成功：读不到就不写，也不拿默认值冒充原值
        if (read_number(find_property(settings, "bAllowTimeDilation"), settings, value)) {
            snap.allow = value != 0.0;
            snap.mark_read(TimeField::Allow);
        }
        if (read_number(find_property(settings, "TimeDilation"), settings, value)) {
            snap.dilation = value;
            snap.mark_read(TimeField::Dilation);
        }
        if (read_number(find_property(settings, "MinGlobalTimeDilation"), settings, value)) {
            snap.min_dilation = value;
            snap.mark_read(TimeField::Min);
        }
        if (read_number(find_property(settings, "MaxGlobalTimeDilation"), settings, value)) {
            snap.max_dilation = value;
            snap.mark_read(TimeField::Max);
        }
        snap.effective = get_time_dilation();
        char buf[200]{};
        std::snprintf(buf, sizeof(buf), "时间设置原始值：字段读取 %d/%d/%d/%d | %s",
                      snap.was_read(TimeField::Allow) ? 1 : 0, snap.was_read(TimeField::Dilation) ? 1 : 0,
                      snap.was_read(TimeField::Min) ? 1 : 0, snap.was_read(TimeField::Max) ? 1 : 0,
                      time_settings_summary().c_str());
        log(LogLevel::Info, buf);
    }

    // 冻结路径：只写「读到了原始值」的字段（plan_time_freeze_fields 决定集合）。
    // TimeDilation 读不到就整个放弃——没有原值，退出时无法安全回滚。
    const std::uint8_t plan = plan_time_freeze_fields(time_snapshot_, dilation);
    if (plan == 0) {
        log(LogLevel::Warn, "时间冻结放弃：WorldSettings.TimeDilation 原始值读不到（不写入，避免退出写回错误值）");
        return false;
    }

    bool ok = true;
    if ((plan & time_field_bit(TimeField::Allow)) != 0) {
        if (write_bool(find_property(settings, "bAllowTimeDilation"), settings, true)) {
            time_snapshot_.mark_written(TimeField::Allow);
        } else {
            ok = false;
        }
    }
    if ((plan & time_field_bit(TimeField::Min)) != 0 &&
        !write_number(find_property(settings, "MinGlobalTimeDilation"), settings, dilation)) {
        ok = false;
    } else if ((plan & time_field_bit(TimeField::Min)) != 0) {
        time_snapshot_.mark_written(TimeField::Min);
    }
    if ((plan & time_field_bit(TimeField::Max)) != 0 &&
        !write_number(find_property(settings, "MaxGlobalTimeDilation"), settings, dilation)) {
        ok = false;
    } else if ((plan & time_field_bit(TimeField::Max)) != 0) {
        time_snapshot_.mark_written(TimeField::Max);
    }
    const bool write_ok = write_number(find_property(settings, "TimeDilation"), settings, dilation);
    if (write_ok) {
        time_snapshot_.mark_written(TimeField::Dilation);
    } else {
        ok = false;
    }

    UObject* statics = gameplay_statics();
    UObject* scene = world();
    bool engine_call_ok = false;
    if (is_ok(statics) && is_ok(scene)) {
        engine_call_ok = invoke(statics, "SetGlobalTimeDilation", [&](ParamBlock& params) {
            return params.set_object("WorldContextObject", scene) && params.set_number("TimeDilation", dilation);
        });
    }

    const double effective = get_time_dilation();
    const bool effective_ok = std::abs(effective - dilation) <= 1e-3 * (1.0 + std::abs(dilation));
    char buf[300]{};
    std::snprintf(buf, sizeof(buf), "时间设置目标=%.4f 生效=%.4f 字段写入=%s 引擎函数=%s | %s", dilation, effective,
                  ok ? "ok" : "fail", engine_call_ok ? "ok" : "fail", time_settings_summary().c_str());
    log(effective_ok ? LogLevel::Info : LogLevel::Warn, buf);
    return ok;
}

// dilation 模式的每帧重申：游戏在 World Tick 里会把 TimeDilation 写回 1.0，
// 这里只写字段、不打日志，供 GameViewportClient Tick 之后、渲染之前调用。
bool Ue4ssPort::reassert_time_dilation(double dilation) {
    UObject* settings = world_settings();
    if (!is_ok(settings)) return false;
    if (!time_snapshot_.belongs_to(world_generation())) return false;
    if (!time_snapshot_.was_written(TimeField::Dilation)) return false;
    return write_number(find_property(settings, "TimeDilation"), settings, dilation);
}

// 时间冻结的显式恢复（H-2）：只回写 written_mask 里的字段（本 Mod 确实改过的），
// 不再靠「数值是否接近原值」的启发式去猜。
// 快照属于旧世界（generation 不一致）时直接当成功：旧世界的字段已随关卡卸载消失，
// 既无需也不能把旧值写进新世界。
bool Ue4ssPort::restore_time_dilation(double original) {
    UObject* settings = world_settings();  // 先同步世界身份（可能触发作废）
    if (!time_snapshot_.belongs_to(world_generation())) {
        time_snapshot_.reset();
        log(LogLevel::Info, "时间恢复：原始值快照属于旧世界（generation 不匹配），已作废，不回写");
        return true;
    }
    if (!time_snapshot_.any_written()) return true;
    if (!is_ok(settings)) return false;  // 世界暂时不可用：保留快照，由退出重试再处理

    bool ok = true;
    TimeSettingsSnapshot& snap = time_snapshot_;
    if (snap.was_written(TimeField::Allow)) {
        if (write_bool(find_property(settings, "bAllowTimeDilation"), settings, snap.allow)) {
            snap.written_mask &= static_cast<std::uint8_t>(~time_field_bit(TimeField::Allow));
        } else {
            ok = false;
        }
    }
    if (snap.was_written(TimeField::Min)) {
        if (write_number(find_property(settings, "MinGlobalTimeDilation"), settings, snap.min_dilation)) {
            snap.written_mask &= static_cast<std::uint8_t>(~time_field_bit(TimeField::Min));
        } else {
            ok = false;
        }
    }
    if (snap.was_written(TimeField::Max)) {
        if (write_number(find_property(settings, "MaxGlobalTimeDilation"), settings, snap.max_dilation)) {
            snap.written_mask &= static_cast<std::uint8_t>(~time_field_bit(TimeField::Max));
        } else {
            ok = false;
        }
    }
    if (snap.was_written(TimeField::Dilation)) {
        if (write_number(find_property(settings, "TimeDilation"), settings, snap.dilation)) {
            snap.written_mask &= static_cast<std::uint8_t>(~time_field_bit(TimeField::Dilation));
        } else {
            ok = false;
        }
    }

    char buf[260]{};
    std::snprintf(buf, sizeof(buf), "%s（进入前生效值=%.4f，剩余未还原字段=%u）| %s",
                  ok ? "时间设置已恢复" : "时间设置恢复不完整", original,
                  static_cast<unsigned>(snap.written_mask), time_settings_summary().c_str());
    log(ok ? LogLevel::Info : LogLevel::Warn, buf);
    if (!snap.any_written()) {
        // 全部还原成功：丢弃采样，下次冻结重新读取（游戏可能在此期间改过原值）
        snap.reset();
        snap.adopt(world_generation());
    }
    return ok;
}

bool Ue4ssPort::set_paused(bool paused) {
    UObject* pc = controller();
    if (!is_ok(pc)) return false;

    // 快照按世界令牌校验：换代即作废（旧 PC/GameMode 的原值不能写进新世界，H-2）
    if (!pause_snapshot_.belongs_to(world_generation())) {
        pause_snapshot_.reset();
        pause_snapshot_.adopt(world_generation());
    }

    if (paused) {
        PauseSnapshot& snap = pause_snapshot_;
        // 记录 PlayerController 的暂停完整 Tick 标记。默认暂停时 PC 只做最小 Tick，
        // 相机管理器不会更新，摄影相机看起来会“卡死”；打开后相机可以继续动。
        // 读不到原值就不写：否则退出时只能靠猜默认值恢复（H-2）。
        // 已经采样过就不要用当前值覆盖原值：冻结期每秒重申会再次调用本函数，
        // 此时读到的 true 是本 Mod 自己写进去的，不是玩家的原始状态。
        double full_tick = 0.0;
        if (!snap.full_tick_read && read_number(find_property(pc, "bShouldPerformFullTickWhenPaused"), pc, full_tick)) {
            snap.full_tick_read = true;
            snap.full_tick_original = full_tick != 0.0;
        }
        if (snap.full_tick_read && !snap.full_tick_original) {
            if (write_bool(find_property(pc, "bShouldPerformFullTickWhenPaused"), pc, true)) {
                snap.full_tick_written = true;
            }
        }

        // 低风险 A/B：UWorld.bIsCameraMoveableWhenPaused=true。
        // 它与 bShouldPerformFullTickWhenPaused 是 UE 暂停相机的标准成对设置：
        // 后者让 PlayerController/相机管理器继续 Tick，前者让渲染侧知道“暂停时相机可动”，
        // 避免 TAA/TSR/MotionBlur 继续按静止相机的旧视角矩阵重投影。
        // 读不到原值就不写；写失败也不阻断暂停，只在日志明确标出（便于 A/B 判读）。
        bool wrote_camera_moveable = false;
        if (UObject* scene = world(); is_ok(scene)) {
            if (!snap.camera_moveable_read) {
                bool original = false;
                if (read_world_camera_moveable(scene, original)) {
                    snap.camera_moveable_read = true;
                    snap.camera_moveable_original = original;
                } else if (!camera_moveable_probe_logged_) {
                    camera_moveable_probe_logged_ = true;
                    log(LogLevel::Warn, "UWorld.bIsCameraMoveableWhenPaused 不可读写，A/B 相机标志跳过（不影响暂停）");
                }
            }
            if (snap.camera_moveable_read) {
                bool current = false;
                if (read_world_camera_moveable(scene, current) && !current) {
                    if (write_world_camera_moveable(scene, true)) {
                        if (!snap.camera_moveable_written) wrote_camera_moveable = true;
                        snap.camera_moveable_written = true;
                    }
                }
            }
        }
        if (wrote_camera_moveable) {
            log(LogLevel::Info, "相机可移动标志已写入：UWorld.bIsCameraMoveableWhenPaused=true（A/B，解冻/退出还原）");
        }

        // 有些 GameMode 把 bPauseable 关了，SetPause 会直接返回 false。
        // 同样只在读到原值且原值为 false 时才改成 true（采样一次，重申不覆盖原值）。
        if (UObject* scene = world(); is_ok(scene)) {
            UObject* game_mode = nullptr;
            if (read_object(find_property(scene, "AuthorityGameMode"), scene, game_mode) && is_ok(game_mode)) {
                double pauseable = 1.0;
                if (!snap.game_mode_read &&
                    read_number(find_property(game_mode, "bPauseable"), game_mode, pauseable)) {
                    snap.game_mode_read = true;
                    snap.game_mode_original = pauseable != 0.0;
                }
                if (snap.game_mode_read && !snap.game_mode_original) {
                    if (write_bool(find_property(game_mode, "bPauseable"), game_mode, true)) {
                        snap.game_mode_written = true;
                    }
                }
            }
        }
    }

    bool accepted = false;
    bool invoked = invoke_bool(pc, "SetPause", [&](ParamBlock& params) { return params.set_bool("bPause", paused); }, accepted);
    if (!invoked || (paused && !accepted)) {
        // 有些游戏在 PlayerController 上禁用了 SetPause，改用 GameplayStatics 的全局暂停。
        if (UObject* statics = gameplay_statics(); is_ok(statics)) {
            if (UObject* scene = world(); is_ok(scene)) {
                invoked = invoke(statics, "SetGamePaused", [&](ParamBlock& params) {
                    return params.set_object("WorldContextObject", scene) && params.set_bool("bPaused", paused);
                });
                if (invoked) accepted = true;
            }
        }
    }

    // 回读 IsGamePaused，作为最终是否真的暂停的依据
    bool actually_paused = accepted;
    bool paused_now = false;
    if (read_is_game_paused(paused_now)) actually_paused = paused_now;

    bool ok = paused ? (invoked && accepted && actually_paused) : (invoked || !actually_paused);

    // 还原「暂停时完整 Tick / GameMode bPauseable」两项：只写回本 Mod 确实写过的项，
    // 写失败就保留标记（下一次退出重试还会再来一遍），不再无条件写 GameMode。
    auto restore_pause_flags = [&]() -> bool {
        bool restored = true;
        PauseSnapshot& snap = pause_snapshot_;
        if (snap.full_tick_written) {
            if (write_bool(find_property(pc, "bShouldPerformFullTickWhenPaused"), pc, snap.full_tick_original)) {
                snap.full_tick_written = false;
            } else {
                restored = false;
            }
        }
        if (snap.game_mode_written) {
            UObject* scene = world();
            UObject* game_mode = nullptr;
            bool done = false;
            if (is_ok(scene) && read_object(find_property(scene, "AuthorityGameMode"), scene, game_mode) &&
                is_ok(game_mode)) {
                done = write_bool(find_property(game_mode, "bPauseable"), game_mode, snap.game_mode_original);
            }
            if (done) {
                snap.game_mode_written = false;
            } else {
                restored = false;
            }
        }
        if (snap.camera_moveable_written) {
            UObject* scene = world();
            bool done = false;
            if (is_ok(scene)) {
                done = write_world_camera_moveable(scene, snap.camera_moveable_original);
            }
            if (done) {
                snap.camera_moveable_written = false;
                log(LogLevel::Info, "相机可移动标志已还原：UWorld.bIsCameraMoveableWhenPaused=" +
                                        std::string(snap.camera_moveable_original ? "true" : "false"));
            } else {
                restored = false;
            }
        }
        return restored;
    };

    // 暂停失败时把临时改的 PC/GameMode 状态还原，避免停留在“暂停时完整 Tick”的状态。
    // 取消暂停时同样要还原；这一项失败也算本次调用失败（H-1：恢复失败不能被吞）。
    if ((paused && !ok) || !paused) {
        const bool flags_restored = restore_pause_flags();
        if (!paused) ok = ok && flags_restored;
    }

    const bool camera_moveable_active = pause_snapshot_.camera_moveable_written;
    log(ok ? LogLevel::Info : LogLevel::Warn,
        std::string(ok ? (paused ? "游戏暂停成功" : "游戏暂停已恢复") : (paused ? "游戏暂停失败" : "恢复游戏运行失败")) +
            " | IsGamePaused=" + (actually_paused ? "true" : "false") +
            (paused ? std::string("（bShouldPerformFullTickWhenPaused=true，UWorld.bIsCameraMoveableWhenPaused=") +
                          (camera_moveable_active ? "true" : "未写入") + "，相机仍可移动）"
                    : "") +
            (ok ? "" : "（PC/GameMode/UWorld 原始值未能全部还原，退出时会重试）"));
    return ok;
}

// 回读 IsGamePaused：ok=false 表示接口/世界不可达。两处调用方的失败策略不同
//（set_paused 保持 accepted 的结论、world_paused 视作未暂停），所以这里只回传两值、不判断。
bool Ue4ssPort::read_is_game_paused(bool& out) {
    UObject* statics = gameplay_statics();
    UObject* scene = world();
    if (!is_ok(statics) || !is_ok(scene)) return false;
    UFunction* function = find_function(statics, "IsGamePaused");
    if (function == nullptr) return false;
    ParamBlock params(function);
    if (!params.valid()) return false;
    params.set_object("WorldContextObject", scene);
    statics->ProcessEvent(function, params.data());
    double value = 0.0;
    if (!params.get_number("ReturnValue", value)) return false;
    out = value != 0.0;
    return true;
}

bool Ue4ssPort::world_paused() {
    bool paused = false;
    return read_is_game_paused(paused) && paused;
}

void Ue4ssPort::recover_character_after_photo_mode() {
    int changed_meshes = 0;
    int changed_time_values = 0;
    double old_custom_dilation = 1.0;
    double old_cinematic_dilation = 1.0;
    double old_world_dilation = 1.0;

    auto restore_mesh_animation = [&](UObject* component) {
        if (!is_ok(component)) return;
        bool changed = false;
        FProperty* pause_prop = find_property(component, "bPauseAnims");
        double pause_value = 0.0;
        if (pause_prop != nullptr && read_number(pause_prop, component, pause_value) && pause_value != 0.0) {
            if (write_bool(pause_prop, component, false)) changed = true;
        }
        FProperty* no_update_prop = find_property(component, "bNoSkeletonUpdate");
        double no_update_value = 0.0;
        if (no_update_prop != nullptr && read_number(no_update_prop, component, no_update_value) &&
            no_update_value != 0.0) {
            if (write_bool(no_update_prop, component, false)) changed = true;
        }
        if (changed) ++changed_meshes;
    };

    UObject* pawn = player_pawn();
    if (is_ok(pawn)) {
        // AActor.CustomTimeDilation：只处理接近 0 的“演出残留”，正常的慢动作（0.2 等）不动。
        if (read_property_number(pawn, "CustomTimeDilation", old_custom_dilation) &&
            old_custom_dilation >= 0.0 && old_custom_dilation <= kStuckDilationEpsilon) {
            if (write_property_number(pawn, "CustomTimeDilation", 1.0)) ++changed_time_values;
        }
        // ACharacter.Mesh（玩家本体）+ 挂在玩家 Pawn 下的所有骨骼网格（服装/武器/替身）。
        UObject* mesh = nullptr;
        if (read_object(find_property(pawn, "Mesh"), pawn, mesh)) restore_mesh_animation(mesh);
        const std::string pawn_name = narrow(pawn->GetName());
        const std::string pawn_class_name = is_ok(pawn->GetClassPrivate()) ? narrow(pawn->GetClassPrivate()->GetName()) : "";
        std::vector<UObject*> components;
        RC::Unreal::UObjectGlobals::FindAllOf(STR("SkeletalMeshComponent"), components);
        for (UObject* component : components) {
            if (!is_ok(component)) continue;
            const std::string component_name = narrow(component->GetFullName());
            const bool owned_by_pawn =
                component->GetOuterPrivate() == pawn ||
                (!pawn_name.empty() && component_name.find(pawn_name) != std::string::npos) ||
                // 处决演出会临时生成/复制一份玩家模型（名字带玩家类名的派生 Actor），
                // 原 Pawn 身上的组件可能不是屏幕里那张卡住的模型；按类名兜住这一份。
                (!pawn_class_name.empty() && component_name.find(pawn_class_name) != std::string::npos);
            if (owned_by_pawn) restore_mesh_animation(component);
        }
    }

    // AWorldSettings.CinematicTimeDilation 是 Sequencer slomo track 的附加倍率（UE5.7）。
    // 演出被中途打断时它可能停在 0，世界/角色看起来就像定格；只清“接近 0”的异常值。
    if (UObject* settings = world_settings(); is_ok(settings)) {
        if (read_property_number(settings, "CinematicTimeDilation", old_cinematic_dilation) &&
            old_cinematic_dilation >= 0.0 && old_cinematic_dilation <= kStuckDilationEpsilon) {
            if (write_property_number(settings, "CinematicTimeDilation", 1.0)) ++changed_time_values;
        }
        if (read_property_number(settings, "TimeDilation", old_world_dilation) && old_world_dilation >= 0.0 &&
            old_world_dilation <= 0.0001) {
            if (write_property_number(settings, "TimeDilation", 1.0)) ++changed_time_values;
        }
    }

    if (changed_meshes > 0 || changed_time_values > 0) {
        char buf[240]{};
        std::snprintf(buf, sizeof(buf),
                      "玩家角色状态恢复：清理处决/过场演出残留（CustomTimeDilation %.4f→1.0，CinematicTimeDilation "
                      "%.4f→1.0，World TimeDilation %.4f→1.0，骨骼动画 %d 个）",
                      old_custom_dilation, old_cinematic_dilation, old_world_dilation, changed_meshes);
        log(LogLevel::Info, buf);
    }
}

// 剧情过场（Sequencer）播放器：世界暂停与时间缩放拦不住剧情系统的自主更新
// （画面像冻住了、动作和声音还在走），冻结时必须对 SequencePlayer 的 Pause/Play 单独下手。
// FindAllOf 沿继承链匹配，LevelSequencePlayer 与游戏自定义派生播放器都能扫到。
SequenceFreezeReport Ue4ssPort::pause_sequences(bool paused) {
    SequenceFreezeReport report{};
    report.ok = true;

    std::vector<UObject*> players;
    RC::Unreal::UObjectGlobals::FindAllOf(STR("MovieSceneSequencePlayer"), players);
    const int direct_count = static_cast<int>(players.size());

    // 兜底：过场 Actor（含游戏自定义派生类）身上的 SequencePlayer 属性
    std::vector<UObject*> actors;
    RC::Unreal::UObjectGlobals::FindAllOf(STR("LevelSequenceActor"), actors);
    for (UObject* actor : actors) {
        if (!is_ok(actor)) continue;
        UObject* player = nullptr;
        if (read_object(find_property(actor, "SequencePlayer"), actor, player) && player != nullptr) {
            players.push_back(player);
        }
    }

    std::sort(players.begin(), players.end());
    players.erase(std::unique(players.begin(), players.end()), players.end());
    report.found = static_cast<int>(players.size());

    std::string matched_class;
    for (UObject* player : players) {
        if (!is_ok(player)) continue;
        // 类默认对象（Default__...）不参与播放，显式排除避免脏命中
        if (narrow(player->GetFullName()).find("Default__") != std::string::npos) continue;
        // 暂停时找“正在播放”的；恢复时只恢复“处于暂停态”的（不去碰游戏自己停掉的）
        bool in_state = false;
        if (!invoke_bool(player, paused ? "IsPlaying" : "IsPaused", [&](ParamBlock&) { return true; }, in_state)) {
            continue;
        }
        if (!in_state) continue;
        ++report.matched;
        if (matched_class.empty()) matched_class = narrow(player->GetClassPrivate()->GetName());
        if (invoke(player, paused ? "Pause" : "Play", [&](ParamBlock&) { return true; })) {
            ++report.acted;
        }
    }

    // 剧情语音多半走 Wwise/普通 AudioComponent，暂停它们是另一条路；先记录游戏用哪种音频系统留证据
    const bool wwise = find_object("/Script/AkAudio.Default__AkGameplayStatics") != nullptr ||
                       find_object("/Script/Wwise.Default__AkGameplayStatics") != nullptr;
    char buf[280]{};
    std::snprintf(buf, sizeof(buf),
                  "剧情播放器[%s]：扫描 %d（直扫 %d + Actor 兜底 %d），命中 %d，执行 %d，类=%s，Wwise=%s，Ak组件=%d",
                  paused ? "暂停" : "恢复", report.found, direct_count, report.found - direct_count,
                  report.matched, report.acted, matched_class.empty() ? "-" : matched_class.c_str(),
                  wwise ? "有" : "无", wwise_component_count());
    log(LogLevel::Info, buf);
    if (paused && report.matched > report.acted) {
        log(LogLevel::Warn, "存在命中的播放器执行失败，请把这行日志反馈给作者");
    }
    if (paused && report.found == 0) {
        log(LogLevel::Info, "未发现 Sequencer 剧情播放器；若剧情中动作/声音仍继续，请带上 UE4SS.log 反馈");
    }
    return report;
}

}  // namespace pbzpm

#endif  // PBZPM_WITH_UE4SS
