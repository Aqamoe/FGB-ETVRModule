# FGB ETVRModule

[![build](https://github.com/Aqamoe/FGB-ETVRModule/actions/workflows/ci.yml/badge.svg)](https://github.com/Aqamoe/FGB-ETVRModule/actions/workflows/ci.yml)

**状态：0.1.0，未验证（untested）**。协议解析、映射与 ABI 生命周期有单元测试和合成 OSC
端到端冒烟；真机（EyeTrackVR + 带眼动采集的头显）验收待补，见 [docs/verification.md](docs/verification.md)。

[FaceGaze Bridge](https://github.com/Aqamoe/FaceGaze-Bridge) 的 EyeTrackVR 原生模块。
直接接收 **EyeTrackVR** 按 OSC 协议发到本机 8889 端口的 UDP 数据报，输出逐眼注视方向
与睁眼度（`eyes` + `gaze` 通道）。真眼动设备输入，不含表情系数。不需要 VRCFT，
不注入任何进程，也不需要管理员权限。

EyeTrackVR (EyeTrackVR project) native input module for FaceGaze Bridge. Reads the OSC
gaze stream your tracker already sends and outputs per-eye gaze directions and eye
openness. No VRCFT, no process injection, no admin rights.

## 功能 / Features

- **注视（gaze）**：`LeftEyeX`/`RightEyeX`/`EyesY`（V1）与 `EyeLeftX/Y`、`EyeRightX/Y`、
  `EyeX/EyeY`（V2）→ HMD 局部单位向量（+X 右、+Y 上、-Z 前）。上游单位已核对为
  **VRCFT 归一化 tan 空间**（±1 ↔ 标定 FOV 边缘），不是角度。
- **眼睛（eyes）**：`EyeLid*`/`*EyeLidExpandedSqueeze` 就是直接睁眼度（1=睁、0=闭），
  无需 1-blink 推导；每帧过 1€ 滤波（参数与上游一致），快速眨眼不拖尾。
- **缺字段不伪造**：某只眼的水平或垂直没收到就不置该眼方向位；眼睑没收到就不置
  该眼 openness。未实现 2D 注视点与视口标定，`FGB_ATTR_POINT_VIEWPORT` 永不置位。
- **断连可恢复**：10 秒没有数据时在模块卡片提示原因，数据恢复后自动回到活跃。
- **可诊断**：端口冲突、畸形 OSC、未知地址、NaN/Inf 参数都有明确日志，不静默丢数据。

## 不提供 / Not provided

| 数据 | 原因 |
|---|---|
| 表情系数 | 这是真眼动设备；V2 里顺带的 `BrowExpression`/`EyeSquint`/`CheekSquint` 是算法模拟值，本模块不声明表情通道、收到即忽略 |
| 头姿 | 上游没有头姿通道 |
| 瞳孔 | `EyesDilation`/`PupilDilation` 是 running min/max 归一的 0..1 相对量，映射不到 ABI 的毫米/-1..1 基线语义，宁缺毋滥 |
| 2D 注视点 | 归一化注视是视场角语义，未标定到具体显示视口；按 ADR-0033 不置点位/视口位 |
| pyVRCFT / VRChat native 输出模式 | 那是 ETVR 直连 VRChat 的通道，不经本模块；请选择 VRCFT 参数集（V1 或 V2）输出 |

## 环境要求

| 项 | 要求 |
|---|---|
| 系统 | Windows 10 / 11 x64 |
| 宿主 | FaceGaze Bridge（原生模块 ABI v2） |
| 眼动源 | EyeTrackVR 运行并完成注视标定，OSC 输出端口对齐 8889 |
| 其它 | 关闭 VRCFaceTracking：它的 ETVR 模块和本模块抢同一个 UDP 端口 |

## 安装

1. 从 Releases 下载 `etvr-0.1.0.zip`。
2. FaceGaze Bridge →「模块库 → 原生模块 → 安装原生模块 (.zip)」，或「模块」页「从 .zip 安装」。
3. 「模块」页把 **EyeTrackVR** 设为启动，角色通常给 `gaze`（需要驱动头像眼睛时给 `eyes`）。
4. 重启引擎；EyeTrackVR 开始发送后模块卡片应显示活跃。

## 配置

模块目录下可选 `config.ini`：

```ini
port = 8889
```

改完重启引擎，并把 EyeTrackVR 的输出端口改成同一个值。未知键会被记录一条警告，
不会静默失效。

## 从源码构建

需要 Visual Studio 2022（x64）、CMake ≥ 3.24 与 Node 20（脚本用）。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

SDK 与 Kit 由 `sdk.lock.json` / `kit.lock.json` 在配置阶段拉取并校验 SHA256，仓库里不含 SDK 头文件副本。
本地想指向已解压的 Kit：

```powershell
cmake -S . -B build -A x64 -DFGB_KIT_DIR="../FGB-Module-Kit"
```

打包与测试：

```powershell
cmake --build build --config Release --target package-modules   # build/Release/etvr-<版本>.zip
ctest --test-dir build -C Release --output-on-failure           # 协议与映射单测
cmake --build build --config Release --target smoke-test        # 真实 ABI + 合成 OSC 冒烟（需 8889 空闲）
```

## 目录结构

```
src/main.cpp                ABI 生命周期、UDP 接收、状态上报
src/etvr_payload.*          OSC 解析、地址映射、1€ 滤波与帧写入
module/                     fgb-native.json 与图标
tests/                      协议单测、真实 ABI 冒烟
docs/protocol.md            线格式与映射依据（已确认/未确认逐条标注）
docs/verification.md        真机验收清单
cmake/                      依赖锁解析（SDK / Kit 都是拉取，不复制）
```

## 排错

| 现象 | 处理 |
|---|---|
| 卡片显示「无法绑定 UDP 8889（端口已被占用）」 | 关闭 VRCFT 或其它接收端；或用 `config.ini` 换端口并同步改 EyeTrackVR 设置 |
| 一直提示「还没收到任何数据包」 | 确认 EyeTrackVR 输出端口=8889、地址=本机 IP、防火墙放行该端口 |
| 只有睁眼没有方向（或反之） | 检查输出模式：V1 与 V2 的地址族不同，本模块都支持，但中途换模式需重启 EyeTrackVR 使参数族一致 |
| 日志出现「忽略未知 OSC 地址」 | 你可能选了 VRChat native / pyVRCFT 输出模式，或上游改了字段名；把版本号和日志一并提到 issue |
| 注视方向左右/上下颠倒 | 标定质量或镜像设置问题；按 docs/verification.md 逐项核对极化并反馈 |

## 致谢 / Attribution

- OSC 地址与语义核对参考 VRCFT 模块
  [lorow/ETVRTrackingModule](https://github.com/lorow/ETVRTrackingModule)（MIT）与
  发送端 [EyeTrackVR](https://github.com/EyeTrackVR/EyeTrackVR)；本仓库是该线格式的
  独立 C++ 实现，未包含其源码。
- ABI 头与宿主：[FaceGaze Bridge](https://github.com/Aqamoe/FaceGaze-Bridge)；共享工具链：[FGB-Module-Kit](https://github.com/Aqamoe/FGB-Module-Kit)。

## 许可

Apache License 2.0，见 [LICENSE](LICENSE) 与 [NOTICE](NOTICE)。
