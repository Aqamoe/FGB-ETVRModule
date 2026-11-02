// EyeTrackVR → FaceGaze Bridge 原生模块。
//
// 监听 EyeTrackVR 以 OSC 协议发到本机 8889 端口的 UDP 单消息（兼容 #bundle），
// 把逐眼注视（归一化 tan 空间）与睁眼度写进 eyes / gaze 通道。
// 不需要 VRCFT，不注入任何进程，也不需要管理员权限。

#include "etvr_payload.hpp"

#include "fgb/kit/config.hpp"
#include "fgb/kit/module.hpp"

#include <cstdint>
#include <string>

namespace {

constexpr std::uint16_t kDefaultPort = 8889;
constexpr std::uint64_t kNoDataTimeoutNs = 10ull * 1000ull * 1000ull * 1000ull;
constexpr std::uint64_t kWarnIntervalNs = 5ull * 1000ull * 1000ull * 1000ull;

class ETVRSource : public fgb::kit::IModuleSource {
public:
    bool Start(fgb::kit::Host& host, const FgbInitParams& params, std::string& error) override {
        fgb::kit::ConfigIni config;
        std::string config_error;
        const std::string directory = params.module_dir != nullptr ? params.module_dir : "";
        config.AllowKeys({"port"});
        if (config.Load(directory, "config.ini", config_error)) {
            port_ = static_cast<std::uint16_t>(config.GetInt("port", port_));
            if (!config_error.empty()) {
                host.Warn("config.ini：" + config_error);
            }
            if (!config.UnknownKeys().empty()) {
                host.Warn("config.ini 有未识别的键：" + config.UnknownKeys()[0]);
            }
        }

        if (!endpoint_.Bind(port_, error)) {
            return false;
        }
        host.Info("EyeTrackVR 监听 UDP " + std::to_string(port_) +
                  "；请在 EyeTrackVR 里把 OSC 输出端口从 9000 改成该端口");
        host.StatusStarting("等待 EyeTrackVR 数据");
        return true;
    }

    bool Read(fgb::kit::Host& host, FgbFrameSlot& slot, std::uint64_t now_ns) override {
        fgb::kit::UdpPacket datagram;
        // 上游按追踪帧率逐条发送，只处理最新一包；积压的旧包丢掉比逐帧回放更准。
        if (endpoint_.PollLatest(datagram)) {
            const std::string_view text{reinterpret_cast<const char*>(datagram.data.data()),
                                        datagram.data.size()};
            etvr::Counts counts;
            std::string error;
            if (state_.ApplyPacket(text, counts, error)) {
                if (counts.command > 0 && !command_seen_) {
                    command_seen_ = true;
                    host.Debug("收到 /command/* 配置通道消息：那是改写 VRCFT 模块设置的，"
                               "本模块不据此改配置，已忽略");
                }
                if (counts.bad_value > 0) {
                    host.WarnThrottled(bad_value_ns_, now_ns, kWarnIntervalNs,
                                       "EyeTrackVR 发来了 NaN/Inf/非数值参数，已丢弃并保留旧值");
                }
                if (counts.unknown > 0) {
                    host.WarnThrottled(unknown_ns_, now_ns, kWarnIntervalNs,
                                       "忽略未知 OSC 地址（最近一个：" + state_.LastUnknown() +
                                           "），累计 " + std::to_string(state_.UnknownTotal()) +
                                           " 条；若 EyeTrackVR 改版请把日志提到 issue");
                }
            } else {
                host.WarnThrottled(warn_ns_, now_ns, kWarnIntervalNs, "丢弃一包：" + error);
            }
        }

        if (!staleness_.Check(host, endpoint_.LastPacketNs(), now_ns)) {
            return false;
        }
        if (!state_.HasPending()) {
            return false;
        }
        // 「缺字段不伪造」：一个可填属性都没有（比如只有单眼 X）就不提交。
        if (!state_.WriteFrame(&slot)) {
            return false;
        }
        state_.ClearPending();
        return true;
    }

    void Stop(fgb::kit::Host& host) override {
        endpoint_.Close();
        host.StatusStopped("EyeTrackVR 已停止");
    }

private:
    fgb::kit::UdpEndpoint endpoint_;
    etvr::State state_;
    fgb::kit::Staleness staleness_{kNoDataTimeoutNs, "EyeTrackVR"};
    std::uint16_t port_ = kDefaultPort;
    std::uint64_t warn_ns_ = 0;
    std::uint64_t unknown_ns_ = 0;
    std::uint64_t bad_value_ns_ = 0;
    bool command_seen_ = false;
};

} // namespace

FGB_KIT_EXPORT(ETVRSource, "com.fgbbridge.etvr", "EyeTrackVR", FGB_CAP_EYES | FGB_CAP_GAZE)
