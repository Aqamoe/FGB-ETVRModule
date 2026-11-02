# 更新日志

遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/) 与语义化版本。

## [未发布]

## [0.1.0] - 未发布
### 新增
- EyeTrackVR OSC 眼动流（8889 端口，V1 与 V2 双参数族）→ 逐眼注视方向 + 睁眼度，
  双眼齐时给合并注视；声明 `eyes` + `gaze` 通道。
- 自实现 1€ 滤波（参数对齐上游 VRCFT 模块：minCutoff 0.1 / beta 15 / dcutoff 1）。
- 缺字段不伪造：X/Y/眼睑按「收到过才置位」；NaN/Inf/非数值参数丢弃保旧值并告警。
- `/command/*` 配置通道与未知地址分类计数、限频日志；`config.ini` 支持覆盖端口。
- 协议单测（单消息/bundle/映射/极化/清洗/断流）+ 通过真实 ABI 的端到端冒烟测试。

### 已知限制
- 未在真机（EyeTrackVR + 眼动采集设备）验证，状态为未验证；
  瞳孔/表情/头姿/2D 注视点按设计不提供（见 README「不提供」与 docs/protocol.md）。
