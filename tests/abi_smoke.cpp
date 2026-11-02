// 走真实 ABI 的端到端冒烟：加载 DLL → query → initialize → 喂合成 OSC → update → 检查提交帧。
//
// 它会绑定 8889 端口，运行前关掉 VRCFT 和其它 ETVR OSC 接收端。
// 因此不挂进 ctest，只由 `cmake --build ... --target smoke-test` 触发。

#include "osc_build.hpp"

#include "fgb/kit/testing/check.hpp"
#include "fgb/kit/testing/host_mock.hpp"
#include "fgb_module_api.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include <thread>

namespace {

constexpr std::uint16_t kPort = 8889;

// 一整个 bundle 一次发出：PollLatest 会丢弃积压旧包，分多条发会有竞态。
std::string BuildBundle() {
    return oscbuild::Bundle({
        oscbuild::FloatMessage("/avatar/parameters/LeftEyeX", 0.4f),
        oscbuild::FloatMessage("/avatar/parameters/EyesY", 0.2f),
        oscbuild::FloatMessage("/avatar/parameters/LeftEyeLidExpandedSqueeze", 0.75f),
    });
}

bool SendToSelf(const std::string& packet) {
    WSADATA data{};
    if (::WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        return false;
    }
    const SOCKET handle = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(kPort);
    target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int sent = ::sendto(handle, packet.data(), static_cast<int>(packet.size()), 0,
                              reinterpret_cast<sockaddr*>(&target), sizeof(target));
    ::closesocket(handle);
    ::WSACleanup();
    return sent != SOCKET_ERROR;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("用法：%s <etvr.dll 路径>\n", argv[0]);
        return 2;
    }

    HMODULE library = ::LoadLibraryExA(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    CHECK(library != nullptr);
    if (library == nullptr) {
        std::printf("LoadLibrary 失败，错误码 %lu（DLL 是否已构建？依赖是否在同目录？）\n",
                    ::GetLastError());
        return CheckSummary("etvr-abi-smoke");
    }

    const auto query = reinterpret_cast<decltype(&fgb_module_query_v2)>(
        ::GetProcAddress(library, "fgb_module_query_v2"));
    CHECK(query != nullptr);

    fgb::kit::testing::HostMock host;
    FgbInitParams params = host.MakeParams();

    const FgbModule* module = query(FGB_MODULE_ABI_V2);
    CHECK(module != nullptr);
    if (module == nullptr) {
        ::FreeLibrary(library);
        return CheckSummary("etvr-abi-smoke");
    }
    CHECK(module->id == std::string{"com.fgbbridge.etvr"});
    CHECK(module->display_name == std::string{"EyeTrackVR"});
    CHECK_EQ(module->abi_version, FGB_MODULE_ABI_V2);
    // 只声明 eyes + gaze：表情/头姿/瞳孔都不填
    CHECK_EQ(module->capabilities, static_cast<std::uint32_t>(FGB_CAP_EYES | FGB_CAP_GAZE));
    CHECK(query(FGB_MODULE_ABI_V2 + 1) == nullptr); // ABI 不匹配必须拒绝

    void* self = nullptr;
    CHECK_EQ(module->initialize(&self, host.services(), &params), 1);
    if (self == nullptr) {
        std::printf("initialize 失败：%s\n", host.LastStatusDetail().c_str());
        ::FreeLibrary(library);
        return CheckSummary("etvr-abi-smoke");
    }

    // 没有数据时不应该提交帧
    module->update(self);
    CHECK(host.committed().empty());

    CHECK(SendToSelf(BuildBundle()));
    for (int attempt = 0; attempt < 200 && host.committed().empty(); ++attempt) {
        module->update(self);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(!host.committed().empty());

    if (!host.committed().empty()) {
        const FgbFrameSlot& slot = host.committed().back();
        CHECK((slot.attributes & FGB_ATTR_LEFT_OPENNESS) != 0);
        CHECK((slot.attributes & FGB_ATTR_LEFT_GAZE) != 0);
        CHECK((slot.attributes & FGB_ATTR_EXPRESSION) == 0);
        CHECK((slot.attributes & FGB_ATTR_HEAD) == 0);
        CHECK((slot.attributes & FGB_ATTR_POINT_VIEWPORT) == 0); // 没有标定到视口，绝不置位
        CHECK_NEAR(slot.left_eye.openness, 0.75, 1e-5); // 首帧直通
        // tan 空间：dir = normalize(0.4, 0.2, -1)，+X 为右
        const float dir_length = std::sqrt(0.4f * 0.4f + 0.2f * 0.2f + 1.0f);
        CHECK_NEAR(slot.left_eye.gaze_dir[0], 0.4f / dir_length, 1e-4);
        CHECK_NEAR(slot.left_eye.gaze_dir[1], 0.2f / dir_length, 1e-4);
        CHECK(slot.left_eye.gaze_dir[2] < 0.0f);
        CHECK((slot.attributes & FGB_ATTR_RIGHT_GAZE) == 0);     // 右眼缺 X，不伪造方向
        CHECK(host.HasStatus(FGB_STATUS_STARTING) || host.HasStatus(FGB_STATUS_ACTIVE));
    }

    module->teardown(self);
    ::FreeLibrary(library);
    return CheckSummary("etvr-abi-smoke");
}
