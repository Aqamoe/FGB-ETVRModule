# EyeTrackVR OSC 协议要点

来源：EyeTrackVR 项目（发送端，Python/pythonosc）与 VRCFT 官方中转模块
[lorow/ETVRTrackingModule](https://github.com/lorow/ETVRTrackingModule)（C#/MIT，接收端）。
本模块按线格式独立实现，未复制上游源码；下列结论逐条标注了依据文件，
证据快照存放于仓库外（`.tmp-upstream/etvr/`），标注**已确认**/**未确认**。

## 传输

| 项 | 值 | 状态 |
|---|---|---|
| 协议 | OSC 1.0 over UDP（单地址 + `,f` float 参数） | 已确认（`OSCManager.cs`、`vrchat_osc_sender.py`） |
| 本模块监听端口 | **8889**（VRCFT 中转模块默认值，`ModuleConfig.cs Config.Default.PortNumber`） | 已确认 |
| 发送端默认目标 | `127.0.0.1:9000`（`config.py gui_osc_address/gui_osc_port`），用户需把端口对齐为 8889 | 已确认 |
| 发包形态 | pythonosc `SimpleUDPClient` **一条消息一个数据报**；本模块兼容 `#bundle` | 已确认（发送端逐条 send_message；bundle 为防御性支持） |
| 消息参数 | 数值参数一律 float（类型标记 `f`）；`/command/*` 通道另有 i/s/T/F | 已确认（`OSCManager.cs ParseOSCMessage` 只认单参数 s/i/f/T/F） |

注意与摘要不符处：**不是** `EyeOpenLeft` 这类地址；睁眼通道实际叫
`LeftEyeLidExpandedSqueeze`（V1）/ `EyeLidLeft`（V2）。

## 地址全表（发送端实际发出、接收端实际识别）

### V1 参数族（`/avatar/parameters/<末段>`，`config.py` 默认地址 + `test_osc_v1_params.py`）

| 地址 | 含义 | 单位与极化 | 状态 |
|---|---|---|---|
| `/avatar/parameters/LeftEyeX` | 左眼水平 | 归一化 tan 空间，+为向右 | 已确认 |
| `/avatar/parameters/RightEyeX` | 右眼水平 | 归一化 tan 空间，+为向右 | 已确认 |
| `/avatar/parameters/EyesY` | **双眼共享**垂直 | 归一化 tan 空间，+为向上；单眼显示直接发、双眼发 `(left+right)/2` | 已确认 |
| `/avatar/parameters/LeftEyeLidExpandedSqueeze` | 左眼睁眼度 | 直接量：**1=睁、0=闭**，可直接用，无需 1-blink 推导；范围可越过 [0,1]（>1 惊讶、<0 用力眯，VRCFT 因此做 widen/squint 模拟） | 已确认（发送端 `blink` 原值透传，`osc_invert_eye_close=False` 时不取反） |
| `/avatar/parameters/RightEyeLidExpandedSqueeze` | 右眼睁眼度 | 同上 | 已确认 |
| `/avatar/parameters/EyesDilation` | 瞳孔 | 归一化 0..1（对全时段 running min/max 归一），**不是毫米**；上游 VRCFT 把它当 `PupilDiameter_MM` 用属将就 | 已确认（`pyvrcft_expressions.py`） |

### V2 参数族（`/avatar/parameters/v2/<末段>`，`vrchat_osc_sender.py output_v2_params` + `V2Mapper.cs`）

| 地址末段 | 含义 | 单位与极化 | 本模块用法 |
|---|---|---|---|
| `EyeLeftX` / `EyeRightX` | 逐眼水平 | 同 V1（+右） | 逐眼注视 x |
| `EyeLeftY` / `EyeRightY` | 逐眼垂直 | 同 V1（+上） | 逐眼注视 y |
| `EyeLidLeft` / `EyeLidRight` | 逐眼睁眼度 | 同 V1 | 逐眼 openness |
| `EyeX` / `EyeY` / `EyeLid` | 双眼共享（单眼显示时发送） | 同上 | 同一值填双眼 |
| `PupilDilation` | 瞳孔，归一化 0..1 | 见上 | **忽略**（映射不到 ABI 的 mm/-1..1 基线语义） |
| `BrowExpression(Left/Right)` | 眉毛 -1..+1（0 中性） | 已确认（`brow_expression()`） | **忽略**（不声明表情通道） |
| `EyeSquint(Left/Right)` `CheekSquintLeft/Right` | 眼周紧张 0..1 | 已确认 | **忽略**（不声明表情通道） |

判定 V1/V2 的口径与上游一致：地址含 `/v2/` 走 V2 表，否则走 V1 表；
两族的专属末段互不串台（单测覆盖）。

### 配置通道（已知，不实现）

| 地址 | 含义 | 状态 |
|---|---|---|
| `/command/set/<gui_字段>/` | 改写**接收端**（VRCFT 模块）自己的设置：端口、widen/squint/眉毛模拟阈值等（`OSCManager.cs HandleSettingsMessage` + `ModuleConfig.cs etvr_to_config_map`） | 已确认存在；本模块只计数并忽略，不据此改配置 |

### 其它发送端通道（本模块不监听/不解析）

| 通道 | 说明 |
|---|---|
| `/tracking/eye/LeftRightVec`（VRChat 原生模式）与 `/tracking/eye/EyesClosedAmount` | 输出模式选「VRChat native」时发送；若打到 8889 按未知地址计数并告警 |
| `/tracking/eye/LeftRightPitchYaw`（UDP 9020，**度**，pitch 正为向下） | 注视点渲染专用，与本模块无关（`foveation_gaze_sender.py`） |
| pyVRCFT 原生模式 | ETVR 内置完整 VRCFT 客户端直连 VRChat（OSCQuery 发现 + bundle），不发给第三方；使用本模块时请勿选它 |
| ETVR 自身接收口 9001（`etvr_recenter`/`etvr_recalibrate`） | 方向相反（VRChat→ETVR），本模块不涉及 |

## 单位与极化（本模块的换算依据）

- 注视 x/y 是「VRCFT 归一化向量：x 右、y 上，[-1,1]」（`pyvrcft_expressions.py SingleEyeData`
  docstring，**已确认**）；语义为 tan 空间：`atan(gaze)` 得角度，默认 ±1 ↔ ±45°，
  设了标定 FOV 后 ±1 落到真实半角边缘（`native_pitch_yaw`，**已确认**）。
- 本模块换算：`dir = normalize(gx, gy, -1)`（ABI 右手系 +Y 上、-Z 前），极化一致、无翻转。
- 超过 ±8 的分量截断（约 82.9°，眼动无物理意义区间）。
- 睁眼度截断到 [0,1] 后写入；越过范围的「用力眯/惊讶」细节不保留（ABI 无处表达）。

## 本模块输出与置信度

- `FGB_ATTR_{LEFT,RIGHT}_OPENNESS`：lid 收到过才置位；每帧过一次 1€ 滤波
  （参数与上游一致：minCutoff 0.1、beta 15、dcutoff 1、每更新 rate=1），置信度 0.8。
- `FGB_ATTR_{LEFT,RIGHT}_GAZE`：同一只眼的 X **和** Y 都收到过才置位（缺字段不猜方向）。
- `FGB_ATTR_COMBINED_GAZE`：双眼方向都有时给分量平均值的方向；置信度 0.7。
- 点位属性与 `FGB_ATTR_POINT_VIEWPORT`：**不置**。归一化注视是视场角语义，
  未标定到任何显示视口，不做伪标定。
- 表情/头姿/瞳孔：**不填**，Capabilities 只声明 `eyes,gaze`。

## 断流与异常

- 10 秒没有数据报 → 模块卡片报错（超时口径对齐上游 `ConnectionTimeout=10000`ms）。
- NaN/Inf/非数值参数：丢弃并保留旧值，限频告警（发送端自己有 `_finite` 兜底，
  但旧版本与第三方发送端不可信）。
- 未知地址：忽略 + 计数 + 限频告警（附最近一个地址）。

## 未确认项（真机验收时需要人核对）

- [ ] ±1 与实际标定 FOV 边缘的对应关系（不同标定质量下可能偏移；置信度 0.7/0.8 属保守估计）。
- [ ] 双眼 Y 取平均在快速垂直眼动时是否可接受（V1 固有；V2 逐眼无此问题）。
- [ ] `*EyeLidExpandedSqueeze` 越过 [0,1] 的实际幅度分布（上游模拟阈值 V1 为
      widen≥0.6/挤压≤0.07，说明分布集中在 0..1 附近，但尾部未观测）。
- [ ] 发送端版本差异：地址表取自 EyeTrackVR main 分支与 ETVRTrackingModule HEAD
      （2026-11 核对）；老版本 ETVR 是否发过其它末段，未回查全部历史。
- [ ] 是否存在发 `#bundle` 的发送端（上游逐条发送；bundle 支持属防御性解析，未经真机流量验证）。
