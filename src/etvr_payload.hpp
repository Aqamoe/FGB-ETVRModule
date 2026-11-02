#pragma once
// EyeTrackVR（ETVR）OSC 参数流的接收状态与帧写入。
//
// 上游格式（EyeTrackVR 以 pythonosc 逐条 UDP 单消息发出，本模块也兼容 #bundle）：
//   V1：/avatar/parameters/LeftEyeX|RightEyeX|EyesY|LeftEyeLidExpandedSqueeze|
//       RightEyeLidExpandedSqueeze|EyesDilation
//   V2：/avatar/parameters/v2/EyeLeftX|EyeLeftY|EyeRightX|EyeRightY|
//       EyeLidLeft|EyeLidRight|EyeX|EyeY|EyeLid|PupilDilation|…
//   配置通道：/command/set/<field>（改写 VRCFT 模块自己的设置，本模块忽略并计数）
// 语义（已在 docs/protocol.md 标注来源）：
//   - 注视 x/y 是 VRCFT 归一化向量：+x 向右、+y 向上，±1 对应标定 FOV 边缘，
//     按 tan 空间解释（等效 ±45° 起步，随标定 FOV 变化）；
//   - EyeLid* / *EyeLidExpandedSqueeze 直接就是睁眼度（1=睁、0=闭），
//     不需要 1-blink 推导；发送端 osc_invert_eye_close 为 False 时原样发送。

#include "fgb/kit/osc.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "fgb_module_api.h"

namespace etvr {

// 1€ 滤波器：与上游 VRCFT 模块同参数（minCutoff=0.1、beta=15、dcutoff=1、rate=1/更新）。
// 纯确定性实现：每帧调用一次 Filter(值, 1)，不依赖墙钟与随机数。
class OneEuroFilter {
public:
    OneEuroFilter(double min_cutoff, double beta);

    double Filter(double x, double rate);

private:
    struct Lowpass {
        bool first_time = true;
        double hat_x_prev = 0.0;
        double Filter(double x, double alpha) {
            const double hat_x = first_time ? x : alpha * x + (1.0 - alpha) * hat_x_prev;
            first_time = false;
            hat_x_prev = hat_x;
            return hat_x;
        }
        double Last() const { return hat_x_prev; }
    };

    static double Alpha(double rate, double cutoff);

    bool first_time_ = true;
    double min_cutoff_ = 0.0;
    double beta_ = 0.0;
    double dcutoff_ = 1.0;
    Lowpass x_filt_;
    Lowpass dx_filt_;
};

// 单眼的原始输入状态（收过才算「有」，缺字段绝不伪造方向）。
struct EyeInput {
    float gaze_x = 0.0f; // 归一化 tan 空间，+x 向右
    float gaze_y = 0.0f; // 归一化 tan 空间，+y 向上
    float lid = 1.0f;    // 睁眼度，1=睁、0=闭
    bool x_seen = false;
    bool y_seen = false;
    bool lid_seen = false;
};

// 一包（数据报）的处理计数，供日志与单测断言。
struct Counts {
    std::size_t applied = 0;      // 已映射进状态的注视/睁眼参数
    std::size_t ignored_known = 0; // 已知但不使用的参数（瞳孔/眉/眼周紧张等）
    std::size_t command = 0;      // /command/* 配置通道
    std::size_t unknown = 0;      // 未识别地址
    std::size_t bad_value = 0;    // 缺参数/类型不符/NaN/Inf
};

class State {
public:
    // 解析并应用一个 OSC 数据报（单消息或 #bundle）。返回 false 表示整包非法。
    bool ApplyPacket(std::string_view datagram, Counts& counts, std::string& error);

    // 应用一条已解析的消息（单测直接调用）。
    void ApplyMessage(const fgb::kit::OscMessage& message, Counts& counts);

    // 自上次 WriteFrame 提交后是否有新的有效输入。
    bool HasPending() const { return dirty_; }
    void ClearPending() { dirty_ = false; }

    // 按「收到什么才置什么位」写帧；一个可填属性都没有时返回 false（不提交）。
    // 每次调用都会推进睁眼度的 1€ 滤波（与上游一致：每个更新一次），所以滤波是有状态的，
    // 只由模块在确认要提交的 tick 上调用。
    bool WriteFrame(FgbFrameSlot* slot);

    const EyeInput& Left() const { return left_; }
    const EyeInput& Right() const { return right_; }
    std::size_t UnknownTotal() const { return unknown_total_; }
    const std::string& LastUnknown() const { return last_unknown_; }

private:
    EyeInput left_;
    EyeInput right_;
    bool dirty_ = false;
    std::size_t unknown_total_ = 0;
    std::string last_unknown_;
    OneEuroFilter left_lid_filter_{0.1, 15.0};
    OneEuroFilter right_lid_filter_{0.1, 15.0};
};

// 地址末段 + 是否 v2 → 归类。供 ApplyMessage 使用，也供单测直接查询。
enum class Field {
    LeftX, RightX, BothX, // BothX：v2 单眼共享 EyeX
    LeftY, RightY, BothY, // BothY：v1 共享 EyesY 与 v2 共享 EyeY
    LeftLid, RightLid, BothLid,
    IgnoredKnown, Command, Unknown,
};

Field Classify(std::string_view address, bool has_v2_marker);

} // namespace etvr
