#include "etvr_payload.hpp"

#include <cmath>
#include <iterator>

namespace etvr {

namespace {

// 注视值越界清洗：±1 已在标定 FOV 边缘，再大只可能是标定外推或噪声；
// 截到 ±8（atan → 约 82.9°）防止单位向量退化成贴边与数值病态。
constexpr float kGazeAbsMax = 8.0f;

bool IsV2Address(std::string_view address) {
    return address.find("/v2/") != std::string_view::npos;
}

// 与上游 VRCFT 模块同一口径：地址按 '/' 切分后取末段查表。
std::string_view LeafOf(std::string_view address) {
    const std::size_t slash = address.find_last_of('/');
    return slash == std::string_view::npos ? address : address.substr(slash + 1);
}

struct LeafEntry {
    std::string_view leaf;
    Field field;
};

constexpr LeafEntry kV1Entries[] = {
    {"LeftEyeX", Field::LeftX},
    {"RightEyeX", Field::RightX},
    {"EyesY", Field::BothY},
    {"LeftEyeLidExpandedSqueeze", Field::LeftLid},
    {"RightEyeLidExpandedSqueeze", Field::RightLid},
    // 瞳孔在 V1 是运行 min/max 归一的 0..1，映射不到 ABI 的 mm/基线语义，忽略。
    {"EyesDilation", Field::IgnoredKnown},
};

constexpr LeafEntry kV2Entries[] = {
    {"EyeLeftX", Field::LeftX},
    {"EyeLeftY", Field::LeftY},
    {"EyeRightX", Field::RightX},
    {"EyeRightY", Field::RightY},
    {"EyeLidLeft", Field::LeftLid},
    {"EyeLidRight", Field::RightLid},
    {"EyeX", Field::BothX},
    {"EyeY", Field::BothY},
    {"EyeLid", Field::BothLid},
    // 以下在 v2 参数族里已知，但本模块不声明表情/瞳孔通道，收到即忽略计数：
    {"PupilDilation", Field::IgnoredKnown},
    {"BrowExpression", Field::IgnoredKnown},
    {"BrowExpressionLeft", Field::IgnoredKnown},
    {"BrowExpressionRight", Field::IgnoredKnown},
    {"EyeSquint", Field::IgnoredKnown},
    {"EyeSquintLeft", Field::IgnoredKnown},
    {"EyeSquintRight", Field::IgnoredKnown},
    {"CheekSquint", Field::IgnoredKnown},
    {"CheekSquintLeft", Field::IgnoredKnown},
    {"CheekSquintRight", Field::IgnoredKnown},
};

} // namespace

Field Classify(std::string_view address, bool has_v2_marker) {
    if (address.find("/command/") != std::string_view::npos) {
        return Field::Command;
    }
    const std::string_view leaf = LeafOf(address);
    // 三目里数组会退化成指针，所以长度单独取，遍历用下标。
    const LeafEntry* table = has_v2_marker ? kV2Entries : kV1Entries;
    const std::size_t count = has_v2_marker ? std::size(kV2Entries) : std::size(kV1Entries);
    for (std::size_t i = 0; i < count; ++i) {
        if (table[i].leaf == leaf) {
            return table[i].field;
        }
    }
    return Field::Unknown;
}

OneEuroFilter::OneEuroFilter(double min_cutoff, double beta)
    : min_cutoff_(min_cutoff), beta_(beta) {}

double OneEuroFilter::Alpha(double rate, double cutoff) {
    const double tau = 1.0 / (2.0 * 3.14159265358979323846 * cutoff);
    const double te = 1.0 / rate;
    return 1.0 / (1.0 + tau / te);
}

double OneEuroFilter::Filter(double x, double rate) {
    const double dx = first_time_ ? 0.0 : (x - x_filt_.Last()) * rate;
    first_time_ = false;
    const double edx = dx_filt_.Filter(dx, Alpha(rate, dcutoff_));
    const double cutoff = min_cutoff_ + beta_ * std::fabs(edx);
    return x_filt_.Filter(x, Alpha(rate, cutoff));
}

bool State::ApplyPacket(std::string_view datagram, Counts& counts, std::string& error) {
    const auto messages = fgb::kit::ParseOsc(datagram, error);
    if (!messages.has_value()) {
        return false;
    }
    for (const auto& message : *messages) {
        ApplyMessage(message, counts);
    }
    return true;
}

void State::ApplyMessage(const fgb::kit::OscMessage& message, Counts& counts) {
    const Field field = Classify(message.address, IsV2Address(message.address));
    if (field == Field::Command) {
        ++counts.command;
        return;
    }
    if (field == Field::IgnoredKnown) {
        ++counts.ignored_known;
        return;
    }
    if (field == Field::Unknown) {
        ++counts.unknown;
        ++unknown_total_;
        last_unknown_ = message.address;
        return;
    }

    // 只接受数值参数（上游 pythonosc 一律发 'f'）；NaN/Inf 一律丢弃保留旧值，
    // 绝不把「坏值」当成 0 提交，否则会静默伪造一次注视/睁眼。
    if (message.arguments.empty()) {
        ++counts.bad_value;
        return;
    }
    const fgb::kit::OscArgument& argument = message.arguments[0];
    if (argument.type != fgb::kit::OscType::Float && argument.type != fgb::kit::OscType::Int &&
        argument.type != fgb::kit::OscType::Double) {
        ++counts.bad_value;
        return;
    }
    const double raw = argument.number;
    if (!std::isfinite(raw)) {
        ++counts.bad_value;
        return;
    }
    const float value = static_cast<float>(raw);

    switch (field) {
        case Field::LeftX:
            left_.gaze_x = value;
            left_.x_seen = true;
            break;
        case Field::RightX:
            right_.gaze_x = value;
            right_.x_seen = true;
            break;
        case Field::BothX:
            left_.gaze_x = value;
            left_.x_seen = true;
            right_.gaze_x = value;
            right_.x_seen = true;
            break;
        case Field::LeftY:
            left_.gaze_y = value;
            left_.y_seen = true;
            break;
        case Field::RightY:
            right_.gaze_y = value;
            right_.y_seen = true;
            break;
        case Field::BothY:
            left_.gaze_y = value;
            left_.y_seen = true;
            right_.gaze_y = value;
            right_.y_seen = true;
            break;
        case Field::LeftLid:
            left_.lid = value;
            left_.lid_seen = true;
            break;
        case Field::RightLid:
            right_.lid = value;
            right_.lid_seen = true;
            break;
        case Field::BothLid:
            left_.lid = value;
            left_.lid_seen = true;
            right_.lid = value;
            right_.lid_seen = true;
            break;
        default:
            return; // 上面已处理 Command/IgnoredKnown/Unknown
    }
    ++counts.applied;
    dirty_ = true;
}

namespace {

float Clamp(float value, float lo, float hi) {
    if (!(value > lo)) {
        return lo;
    }
    return value > hi ? hi : value;
}

// 注视值是 tan 空间（±1 ↔ atan 到标定 FOV 边缘），直接当方向分量再归一，
// 与 VRCFT 的 atan(gaze) 约定一致：gaze=0 → 正前方 -Z。
void GazeDirection(float gaze_x, float gaze_y, float (&out)[3]) {
    const float x = Clamp(gaze_x, -kGazeAbsMax, kGazeAbsMax);
    const float y = Clamp(gaze_y, -kGazeAbsMax, kGazeAbsMax);
    const float length = std::sqrt(x * x + y * y + 1.0f);
    out[0] = x / length;
    out[1] = y / length;
    out[2] = -1.0f / length;
}

constexpr float kGazeConfidence = 0.7f;     // 真眼动设备，但校准质量因人而异
constexpr float kOpennessConfidence = 0.8f; // 直接上报的睁眼度，含检测噪声

} // namespace

bool State::WriteFrame(FgbFrameSlot* slot) {
    if (slot == nullptr) {
        return false;
    }
    // 睁眼度每帧推进一次 1€ 滤波（与上游 VRCFT 模块一致：Filter(value, 1) 每个更新一次）。
    // 只在真的要写这一只眼的 openness 时才推进，避免「没写也空转」破坏首帧直通的可测性。
    bool wrote = false;

    if (left_.lid_seen) {
        slot->attributes |= FGB_ATTR_LEFT_OPENNESS;
        slot->left_eye.openness =
            Clamp(static_cast<float>(left_lid_filter_.Filter(left_.lid, 1.0)), 0.0f, 1.0f);
        slot->left_eye.openness_confidence = kOpennessConfidence;
        wrote = true;
    }
    if (right_.lid_seen) {
        slot->attributes |= FGB_ATTR_RIGHT_OPENNESS;
        slot->right_eye.openness =
            Clamp(static_cast<float>(right_lid_filter_.Filter(right_.lid, 1.0)), 0.0f, 1.0f);
        slot->right_eye.openness_confidence = kOpennessConfidence;
        wrote = true;
    }

    if (left_.x_seen && left_.y_seen) {
        slot->attributes |= FGB_ATTR_LEFT_GAZE;
        GazeDirection(left_.gaze_x, left_.gaze_y, slot->left_eye.gaze_dir);
        slot->left_eye.gaze_confidence = kGazeConfidence;
        wrote = true;
    }
    if (right_.x_seen && right_.y_seen) {
        slot->attributes |= FGB_ATTR_RIGHT_GAZE;
        GazeDirection(right_.gaze_x, right_.gaze_y, slot->right_eye.gaze_dir);
        slot->right_eye.gaze_confidence = kGazeConfidence;
        wrote = true;
    }
    // 合并注视只在双眼方向都齐时给：缺一只眼时不猜。
    if ((slot->attributes & FGB_ATTR_LEFT_GAZE) != 0 && (slot->attributes & FGB_ATTR_RIGHT_GAZE) != 0) {
        slot->attributes |= FGB_ATTR_COMBINED_GAZE;
        GazeDirection((left_.gaze_x + right_.gaze_x) * 0.5f,
                      (left_.gaze_y + right_.gaze_y) * 0.5f, slot->combined_gaze_dir);
        slot->combined_gaze_confidence = kGazeConfidence;
        wrote = true;
    }
    return wrote;
}

} // namespace etvr
