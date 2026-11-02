#include "etvr_payload.hpp"

#include "osc_build.hpp"

#include "fgb/kit/module.hpp"
#include "fgb/kit/testing/check.hpp"
#include "fgb/kit/testing/host_mock.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

using etvr::Counts;
using etvr::State;
using fgb::kit::Host;
using fgb::kit::testing::HostMock;

namespace {

constexpr std::uint64_t kSecondNs = 1000ull * 1000ull * 1000ull;

FgbFrameSlot FreshSlot() {
    FgbFrameSlot slot{};
    slot.struct_size = sizeof(FgbFrameSlot);
    slot.abi_version = FGB_MODULE_ABI_V2;
    slot.shape_count = FGB_MAX_SHAPES;
    return slot;
}

// 单位向量长度必须为 1，-Z 前。
void CheckUnitDirection(const float (&dir)[3]) {
    const float length = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    CHECK_NEAR(length, 1.0, 1e-5);
    CHECK(dir[2] < 0.0f);
}

// tan 空间约定：dir = normalize(gx, gy, -1)。
void CheckGazeMatches(const float (&dir)[3], float gx, float gy) {
    const float length = std::sqrt(gx * gx + gy * gy + 1.0f);
    CHECK_NEAR(dir[0], gx / length, 1e-5);
    CHECK_NEAR(dir[1], gy / length, 1e-5);
    CHECK_NEAR(dir[2], -1.0f / length, 1e-5);
}

} // namespace

int main() {
    // ---- 地址表分类（对照上游 ETVRTrackingModule 的 V1/V2 参数族）----
    CHECK(etvr::Classify("/avatar/parameters/LeftEyeX", false) == etvr::Field::LeftX);
    CHECK(etvr::Classify("/avatar/parameters/RightEyeX", false) == etvr::Field::RightX);
    CHECK(etvr::Classify("/avatar/parameters/EyesY", false) == etvr::Field::BothY);
    CHECK(etvr::Classify("/avatar/parameters/LeftEyeLidExpandedSqueeze", false) ==
          etvr::Field::LeftLid);
    CHECK(etvr::Classify("/avatar/parameters/RightEyeLidExpandedSqueeze", false) ==
          etvr::Field::RightLid);
    CHECK(etvr::Classify("/avatar/parameters/EyesDilation", false) == etvr::Field::IgnoredKnown);
    CHECK(etvr::Classify("/avatar/parameters/v2/EyeLeftX", true) == etvr::Field::LeftX);
    CHECK(etvr::Classify("/avatar/parameters/v2/EyeLeftY", true) == etvr::Field::LeftY);
    CHECK(etvr::Classify("/avatar/parameters/v2/EyeLidRight", true) == etvr::Field::RightLid);
    CHECK(etvr::Classify("/avatar/parameters/v2/EyeLid", true) == etvr::Field::BothLid);
    CHECK(etvr::Classify("/avatar/parameters/v2/BrowExpressionLeft", true) ==
          etvr::Field::IgnoredKnown);
    CHECK(etvr::Classify("/avatar/parameters/v2/EyeSquint", true) == etvr::Field::IgnoredKnown);
    // V2 专属末段出现在 V1 语境必须判 Unknown，防止模式串台
    CHECK(etvr::Classify("/avatar/parameters/v2/EyeLidLeft", false) == etvr::Field::Unknown);
    CHECK(etvr::Classify("/command/set/gui_OutputMultiplier/", false) == etvr::Field::Command);
    CHECK(etvr::Classify("/avatar/parameters/Whatever", false) == etvr::Field::Unknown);

    // ---- OSC 单消息解析 ----
    State v1{};
    Counts counts{};
    std::string error;
    CHECK(v1.ApplyPacket(oscbuild::FloatMessage("/avatar/parameters/LeftEyeX", 0.5f), counts,
                         error));
    CHECK_EQ(counts.applied, static_cast<std::size_t>(1));
    CHECK(v1.Left().x_seen);
    CHECK_NEAR(v1.Left().gaze_x, 0.5, 1e-6);

    // ---- OSC #bundle：一包多条消息 ----
    const std::string bundle = oscbuild::Bundle({
        oscbuild::FloatMessage("/avatar/parameters/EyesY", 0.25f),
        oscbuild::FloatMessage("/avatar/parameters/RightEyeX", -0.5f),
        oscbuild::FloatMessage("/avatar/parameters/LeftEyeLidExpandedSqueeze", 0.7f),
        oscbuild::FloatMessage("/avatar/parameters/RightEyeLidExpandedSqueeze", 0.3f),
    });
    counts = Counts{};
    CHECK(v1.ApplyPacket(bundle, counts, error));
    CHECK_EQ(counts.applied, static_cast<std::size_t>(4));
    CHECK(v1.Left().y_seen);
    CHECK(v1.Right().x_seen);
    CHECK(v1.Left().lid_seen);
    CHECK(v1.Right().lid_seen);

    // 结构非法的输入必须整包丢弃
    State junk{};
    counts = Counts{};
    CHECK(!junk.ApplyPacket("not osc at all!!", counts, error));
    std::string truncated = oscbuild::FloatMessage("/avatar/parameters/EyesY", 0.1f);
    truncated.resize(truncated.size() - 2);
    CHECK(!junk.ApplyPacket(truncated, counts, error));

    // ---- V1 已知地址 → eyes/gaze 帧写入（含左右极化）----
    FgbFrameSlot slot = FreshSlot();
    CHECK(v1.WriteFrame(&slot));
    // 属性位精确匹配：没测到/没声明的一概不许出现
    CHECK_EQ(slot.attributes,
             static_cast<std::uint64_t>(FGB_ATTR_LEFT_OPENNESS | FGB_ATTR_RIGHT_OPENNESS |
                                        FGB_ATTR_LEFT_GAZE | FGB_ATTR_RIGHT_GAZE |
                                        FGB_ATTR_COMBINED_GAZE));
    for (std::uint32_t i = 0; i < slot.shape_count; ++i) {
        CHECK(slot.shape_valid[i] == 0); // 不声明表情通道，一个形状都不写
    }
    // EyesY 同时喂双眼；LeftEyeX=+0.5 → +X（向右），RightEyeX=-0.5 → 向左
    CheckUnitDirection(slot.left_eye.gaze_dir);
    CheckUnitDirection(slot.right_eye.gaze_dir);
    CheckGazeMatches(slot.left_eye.gaze_dir, 0.5f, 0.25f);
    CheckGazeMatches(slot.right_eye.gaze_dir, -0.5f, 0.25f);
    CHECK(slot.left_eye.gaze_dir[0] > 0.0f);
    CHECK(slot.right_eye.gaze_dir[0] < 0.0f);
    CHECK(slot.left_eye.gaze_dir[1] > 0.0f); // EyesY 为正 → 向上 → +Y
    // 合并注视 = 双眼分量平均：x 相互抵消
    CheckUnitDirection(slot.combined_gaze_dir);
    CheckGazeMatches(slot.combined_gaze_dir, 0.0f, 0.25f);
    // 睁眼度首帧直通（1€ 首次返回原值），1=睁 0=闭，不需要 1-blink 推导
    CHECK_NEAR(slot.left_eye.openness, 0.7, 1e-5);
    CHECK_NEAR(slot.right_eye.openness, 0.3, 1e-5);
    CHECK_NEAR(slot.left_eye.openness_confidence, 0.8, 1e-6);
    CHECK_NEAR(slot.left_eye.gaze_confidence, 0.7, 1e-6);
    CHECK_NEAR(slot.combined_gaze_confidence, 0.7, 1e-6);

    // ---- V2 逐眼 ----
    State v2{};
    counts = Counts{};
    CHECK(v2.ApplyPacket(
        oscbuild::Bundle({
            oscbuild::FloatMessage("/avatar/parameters/v2/EyeLeftX", 0.2f),
            oscbuild::FloatMessage("/avatar/parameters/v2/EyeLeftY", 0.1f),
            oscbuild::FloatMessage("/avatar/parameters/v2/EyeRightX", -0.4f),
            oscbuild::FloatMessage("/avatar/parameters/v2/EyeRightY", 0.1f),
            oscbuild::FloatMessage("/avatar/parameters/v2/EyeLidLeft", 0.9f),
            oscbuild::FloatMessage("/avatar/parameters/v2/EyeLidRight", 0.1f),
            oscbuild::FloatMessage("/avatar/parameters/v2/PupilDilation", 0.5f),
            oscbuild::FloatMessage("/avatar/parameters/v2/EyeSquintLeft", 0.2f),
        }),
        counts, error));
    CHECK_EQ(counts.applied, static_cast<std::size_t>(6));
    CHECK_EQ(counts.ignored_known, static_cast<std::size_t>(2)); // 瞳孔 + 眼周紧张
    CHECK_EQ(counts.unknown, static_cast<std::size_t>(0));
    FgbFrameSlot slot_v2 = FreshSlot();
    CHECK(v2.WriteFrame(&slot_v2));
    CHECK_EQ(slot_v2.attributes,
             static_cast<std::uint64_t>(FGB_ATTR_LEFT_OPENNESS | FGB_ATTR_RIGHT_OPENNESS |
                                        FGB_ATTR_LEFT_GAZE | FGB_ATTR_RIGHT_GAZE |
                                        FGB_ATTR_COMBINED_GAZE));
    // 逐眼 Y：左右独立；这里双眼同为 +0.1
    CheckGazeMatches(slot_v2.left_eye.gaze_dir, 0.2f, 0.1f);
    CheckGazeMatches(slot_v2.right_eye.gaze_dir, -0.4f, 0.1f);
    CheckGazeMatches(slot_v2.combined_gaze_dir, -0.1f, 0.1f);
    CHECK_NEAR(slot_v2.left_eye.openness, 0.9, 1e-5);
    CHECK_NEAR(slot_v2.right_eye.openness, 0.1, 1e-5);

    // ---- V2 双眼共享（单眼显示模式）：一份数据喂满两只眼 ----
    State v2s{};
    counts = Counts{};
    CHECK(v2s.ApplyPacket(
        oscbuild::Bundle({
            oscbuild::FloatMessage("/avatar/parameters/v2/EyeX", 0.3f),
            oscbuild::FloatMessage("/avatar/parameters/v2/EyeY", -0.2f),
            oscbuild::FloatMessage("/avatar/parameters/v2/EyeLid", 0.6f),
        }),
        counts, error));
    CHECK_EQ(counts.applied, static_cast<std::size_t>(3));
    FgbFrameSlot slot_shared = FreshSlot();
    CHECK(v2s.WriteFrame(&slot_shared));
    CHECK_EQ(slot_shared.attributes,
             static_cast<std::uint64_t>(FGB_ATTR_LEFT_OPENNESS | FGB_ATTR_RIGHT_OPENNESS |
                                        FGB_ATTR_LEFT_GAZE | FGB_ATTR_RIGHT_GAZE |
                                        FGB_ATTR_COMBINED_GAZE));
    CheckGazeMatches(slot_shared.left_eye.gaze_dir, 0.3f, -0.2f);
    CheckGazeMatches(slot_shared.right_eye.gaze_dir, 0.3f, -0.2f);
    CHECK_NEAR(slot_shared.left_eye.openness, 0.6, 1e-5);
    CHECK_NEAR(slot_shared.right_eye.openness, 0.6, 1e-5);

    // ---- 未知地址忽略并计数；/command/* 配置通道单独计数 ----
    State unknown{};
    counts = Counts{};
    CHECK(unknown.ApplyPacket(oscbuild::FloatMessage("/avatar/parameters/SomeUnknown", 1.0f),
                              counts, error));
    CHECK_EQ(counts.unknown, static_cast<std::size_t>(1));
    CHECK_EQ(counts.applied, static_cast<std::size_t>(0));
    CHECK(!unknown.HasPending()); // 未知地址不算新数据
    CHECK_EQ(unknown.UnknownTotal(), static_cast<std::size_t>(1));
    CHECK(unknown.LastUnknown() == "/avatar/parameters/SomeUnknown");
    counts = Counts{};
    CHECK(unknown.ApplyPacket(oscbuild::FloatMessage("/tracking/eye/LeftRightVec", 0.5f), counts,
                              error)); // 本模块未实现 VRChat 原生通道 → 按未知计
    CHECK_EQ(counts.unknown, static_cast<std::size_t>(1));
    counts = Counts{};
    CHECK(unknown.ApplyPacket(
        oscbuild::FloatMessage("/command/set/gui_OutputMultiplier/", 1.0f), counts, error));
    CHECK_EQ(counts.command, static_cast<std::size_t>(1));
    CHECK_EQ(counts.unknown, static_cast<std::size_t>(0));
    CHECK_EQ(unknown.UnknownTotal(), static_cast<std::size_t>(2));

    // ---- 缺字段不伪造方向：只提交真收到了「水平+垂直/睁眼」的属性 ----
    State partial{};
    counts = Counts{};
    CHECK(partial.ApplyPacket(oscbuild::FloatMessage("/avatar/parameters/LeftEyeX", 0.5f), counts,
                              error));
    FgbFrameSlot none_slot = FreshSlot();
    // 只有 X 没有 Y：不能猜俯仰，一个属性位都不置
    CHECK(!partial.WriteFrame(&none_slot));
    CHECK_EQ(none_slot.attributes, static_cast<std::uint64_t>(0));

    counts = Counts{};
    CHECK(partial.ApplyPacket(oscbuild::FloatMessage("/avatar/parameters/EyesY", 0.2f), counts,
                              error));
    FgbFrameSlot left_only = FreshSlot();
    CHECK(partial.WriteFrame(&left_only));
    // EyesY 让双眼 Y 都「收到」了，但右眼还没有 X → 只置左眼方向
    CHECK_EQ(left_only.attributes, static_cast<std::uint64_t>(FGB_ATTR_LEFT_GAZE));
    CheckGazeMatches(left_only.left_eye.gaze_dir, 0.5f, 0.2f);

    counts = Counts{};
    CHECK(partial.ApplyPacket(oscbuild::FloatMessage("/avatar/parameters/LeftEyeLidExpandedSqueeze",
                                                     0.4f),
                              counts, error));
    FgbFrameSlot left_open = FreshSlot();
    CHECK(partial.WriteFrame(&left_open));
    CHECK_EQ(left_open.attributes,
             static_cast<std::uint64_t>(FGB_ATTR_LEFT_GAZE | FGB_ATTR_LEFT_OPENNESS));
    CHECK_NEAR(left_open.left_eye.openness, 0.4, 1e-5); // 这只眼首次写 → 直通

    counts = Counts{};
    CHECK(partial.ApplyPacket(oscbuild::FloatMessage("/avatar/parameters/RightEyeX", -0.3f), counts,
                              error));
    FgbFrameSlot both_gaze = FreshSlot();
    CHECK(partial.WriteFrame(&both_gaze));
    // 右眼 Y 早在 EyesY 时收到 → 现在右眼方向齐了；右眼睑仍没有 → 不置右眼 openness
    CHECK_EQ(both_gaze.attributes,
             static_cast<std::uint64_t>(FGB_ATTR_LEFT_GAZE | FGB_ATTR_RIGHT_GAZE |
                                        FGB_ATTR_COMBINED_GAZE | FGB_ATTR_LEFT_OPENNESS));
    CHECK(both_gaze.right_eye.openness == 0.0f); // 未置位，宿主不会读到
    CHECK((both_gaze.attributes & FGB_ATTR_RIGHT_OPENNESS) == 0);

    // ---- NaN/Inf/类型不符清洗：丢坏值保旧值，绝不静默当 0 ----
    State dirty{};
    counts = Counts{};
    const float nan_value = std::numeric_limits<float>::quiet_NaN();
    const float inf_value = std::numeric_limits<float>::infinity();
    CHECK(dirty.ApplyPacket(oscbuild::FloatMessage("/avatar/parameters/LeftEyeX", nan_value),
                            counts, error));
    CHECK(dirty.ApplyPacket(oscbuild::FloatMessage("/avatar/parameters/LeftEyeX", inf_value), counts,
                            error));
    CHECK(dirty.ApplyPacket(oscbuild::StringMessage("/avatar/parameters/LeftEyeX", "hello"), counts,
                            error));
    CHECK(dirty.ApplyPacket(oscbuild::NoArgumentMessage("/avatar/parameters/LeftEyeX"), counts,
                            error));
    CHECK_EQ(counts.applied, static_cast<std::size_t>(0));
    CHECK_EQ(counts.bad_value, static_cast<std::size_t>(4));
    CHECK(!dirty.Left().x_seen);
    CHECK(!dirty.HasPending());

    // 合法值照常进入，证明坏值没有污染状态机
    counts = Counts{};
    CHECK(dirty.ApplyPacket(oscbuild::FloatMessage("/avatar/parameters/LeftEyeX", -0.25f), counts,
                            error));
    CHECK_EQ(counts.applied, static_cast<std::size_t>(1));
    CHECK_NEAR(dirty.Left().gaze_x, -0.25, 1e-6);

    // 越界清洗：睁眼度截到 [0,1]，注视分量截到 ±8
    State clamp{};
    counts = Counts{};
    CHECK(clamp.ApplyPacket(
        oscbuild::Bundle({
            oscbuild::FloatMessage("/avatar/parameters/LeftEyeLidExpandedSqueeze", 1.5f),
            oscbuild::FloatMessage("/avatar/parameters/RightEyeLidExpandedSqueeze", -0.2f),
            oscbuild::FloatMessage("/avatar/parameters/LeftEyeX", 100.0f),
            oscbuild::FloatMessage("/avatar/parameters/EyesY", 0.0f),
        }),
        counts, error));
    FgbFrameSlot clamp_slot = FreshSlot();
    CHECK(clamp.WriteFrame(&clamp_slot));
    // 只有左眼 X 收到（右眼仍缺 X），属性位依然是精确集合
    CHECK_EQ(clamp_slot.attributes,
             static_cast<std::uint64_t>(FGB_ATTR_LEFT_OPENNESS | FGB_ATTR_RIGHT_OPENNESS |
                                        FGB_ATTR_LEFT_GAZE));
    CHECK_NEAR(clamp_slot.left_eye.openness, 1.0, 1e-5);
    CHECK_NEAR(clamp_slot.right_eye.openness, 0.0, 1e-5);
    const float clamped_length = std::sqrt(8.0f * 8.0f + 1.0f);
    CHECK_NEAR(clamp_slot.left_eye.gaze_dir[0], 8.0f / clamped_length, 1e-4);
    CHECK_NEAR(clamp_slot.left_eye.gaze_dir[2], -1.0f / clamped_length, 1e-4);

    // ---- 1€ 滤波：纯确定性（rate=1/帧，无时间无随机），与上游参数一致 ----
    etvr::OneEuroFilter filter{0.1, 15.0};
    CHECK_NEAR(filter.Filter(1.0, 1.0), 1.0, 1e-12); // 首次直通
    const double step_one = filter.Filter(0.0, 1.0); // 大跳变 → beta 抬截止频率 → 快速跟随
    CHECK(step_one > 0.0);
    CHECK(step_one < 0.05);
    double previous = step_one;
    for (int i = 0; i < 30; ++i) {
        const double next = filter.Filter(0.0, 1.0);
        CHECK(next <= previous + 1e-12); // 单调收敛，不振荡
        CHECK(next >= 0.0);
        previous = next;
    }
    CHECK(previous < 0.01);
    const double jump_back = filter.Filter(1.0, 1.0); // 反向大跳变同样快速跟随
    CHECK(jump_back > 0.5);
    CHECK(jump_back <= 1.0);

    // WriteFrame 集成：第一帧直通 1.0，眨眼那一帧被 beta 快速压低但不为 0（平滑证据）
    State smooth{};
    counts = Counts{};
    CHECK(smooth.ApplyPacket(oscbuild::FloatMessage("/avatar/parameters/v2/EyeLidLeft", 1.0f),
                             counts, error));
    CHECK(smooth.ApplyPacket(oscbuild::FloatMessage("/avatar/parameters/v2/EyeX", 0.0f), counts,
                             error));
    CHECK(smooth.ApplyPacket(oscbuild::FloatMessage("/avatar/parameters/v2/EyeY", 0.0f), counts,
                             error));
    FgbFrameSlot frame_a = FreshSlot();
    CHECK(smooth.WriteFrame(&frame_a));
    CHECK_NEAR(frame_a.left_eye.openness, 1.0, 1e-5);
    counts = Counts{};
    CHECK(smooth.ApplyPacket(oscbuild::FloatMessage("/avatar/parameters/v2/EyeLidLeft", 0.0f),
                             counts, error));
    FgbFrameSlot frame_b = FreshSlot();
    CHECK(smooth.WriteFrame(&frame_b));
    CHECK(frame_b.left_eye.openness < 0.05f); // 眨眼不拖尾（beta 生效）
    CHECK(frame_b.left_eye.openness > 0.0f);  // 也不是硬阶跃

    // ---- 断流判定（kit::Staleness，10 秒超时）----
    HostMock host;
    Host logger{host.services()};
    fgb::kit::Staleness staleness{10ull * kSecondNs, "EyeTrackVR"};
    CHECK(!staleness.Check(logger, 0, kSecondNs));
    CHECK_EQ(host.CountStatuses(FGB_STATUS_ERROR), static_cast<std::size_t>(1));
    CHECK(host.LastStatusDetail().find("还没收到任何数据包") != std::string::npos);
    // 一直静默：只报一次，不刷屏
    CHECK(!staleness.Check(logger, 0, 3ull * kSecondNs));
    CHECK_EQ(host.CountStatuses(FGB_STATUS_ERROR), static_cast<std::size_t>(1));
    // 数据到达 → 活跃
    CHECK(staleness.Check(logger, 4ull * kSecondNs, 4ull * kSecondNs + kSecondNs / 2));
    CHECK_EQ(host.CountStatuses(FGB_STATUS_ACTIVE), static_cast<std::size_t>(1));
    CHECK(host.LastStatusDetail().find("数据已恢复") != std::string::npos);
    // 超过 10 秒没包 → 再报一次断流，重复静默不再报
    CHECK(!staleness.Check(logger, 4ull * kSecondNs, 16ull * kSecondNs));
    CHECK_EQ(host.CountStatuses(FGB_STATUS_ERROR), static_cast<std::size_t>(2));
    CHECK(host.LastStatusDetail().find("12 秒") != std::string::npos);
    CHECK(!staleness.Check(logger, 4ull * kSecondNs, 30ull * kSecondNs));
    CHECK_EQ(host.CountStatuses(FGB_STATUS_ERROR), static_cast<std::size_t>(2));
    // 恢复
    CHECK(staleness.Check(logger, 31ull * kSecondNs, 31ull * kSecondNs));
    CHECK_EQ(host.CountStatuses(FGB_STATUS_ACTIVE), static_cast<std::size_t>(2));

    return CheckSummary("etvr-payload");
}
