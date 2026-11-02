#pragma once
// 单测与冒烟共用的最小 OSC 构造器：只发本模块会收到的形态
// （单消息 float/string/无参，以及 #bundle 打包），不做发送端库。

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace oscbuild {

// OSC 字符串：内容 + 结尾 NUL + 补零到 4 字节边界。
inline void AppendPaddedString(std::string& out, std::string_view text) {
    out.append(text);
    out.push_back('\0');
    while (out.size() % 4 != 0) {
        out.push_back('\0');
    }
}

inline void AppendInt32BE(std::string& out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<char>((value >> shift) & 0xFFu));
    }
}

inline std::string FloatMessage(std::string_view address, float value) {
    std::string out;
    AppendPaddedString(out, address);
    AppendPaddedString(out, ",f");
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    AppendInt32BE(out, bits);
    return out;
}

inline std::string StringMessage(std::string_view address, std::string_view text) {
    std::string out;
    AppendPaddedString(out, address);
    AppendPaddedString(out, ",s");
    AppendPaddedString(out, text);
    return out;
}

// 只有 "," 类型标记、没有参数的消息（上游异常形态之一）。
inline std::string NoArgumentMessage(std::string_view address) {
    std::string out;
    AppendPaddedString(out, address);
    AppendPaddedString(out, ",");
    return out;
}

inline std::string Bundle(const std::vector<std::string>& elements) {
    std::string out;
    AppendPaddedString(out, "#bundle"); // "#bundle\0" 恰好 8 字节
    AppendInt32BE(out, 0);              // timetag 高 32 位（immediate）
    AppendInt32BE(out, 0);              // timetag 低 32 位
    for (const auto& element : elements) {
        AppendInt32BE(out, static_cast<std::uint32_t>(element.size()));
        out.append(element);
    }
    return out;
}

} // namespace oscbuild
