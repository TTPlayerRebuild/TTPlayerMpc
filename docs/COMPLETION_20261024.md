# ttp_mpc 后续补全与验收

日期：2026-10-24。发行版本：`2026.10.24`。

## 修改

SV7／SV8 取消后重建 demux 并恢复到实际交付位置；一次失败 Read 内的临时 PCM 不丢失。APEv2 多值逐项枚举，Get 合并；新增 Thumbnail 的 21 类封面支持，复用可靠提交。

## 验证

取消时先消耗 0／13 字节、连续两次取消均不丢 PCM；Artist 多值、PNG 前后封面、保存重开／删除和超出 MaximumBytes 拒绝在三系统通过。

Windows 11、XP SP3 x86、Win7 SP1 的组合回归各 33 项通过，其中 19 组 PCM 样本数及校验相同。这是组合测试数量，不代表本插件单独具有 33 个独立用例。Win11 按用户要求代为现代 Windows 验证，无独立 Win10 实测。

## 保留边界

同一图片类型使用一个 APEv2 键；MIME 取决于数据签名，不能独立保存任意 MIME 文字。整体标签仍有 16 MiB 上限。

测试代码和样本只保存在工作区 `rebuild/tests/plugins_complete_20261024`，不进入 Action 或发行 ZIP。运行 ZIP 仅含 DLL 与 SHA256SUMS；许可证留在仓库及既有对应源码包。本轮没有升级上游依赖版本或增加外部解码 DLL。

DLL 大小：163840 字节。SHA-256：`f104a42ddeb97f59c17b73d1548da8e1eee331a0e83d9fcc5051b31fd4ee72c7`。

总报告位于工作区 `rebuild/docs/AUDIO_PLUGIN_COMPLETION_20261024.md`；本文补充早期文档，不将旧轮次的通过范围扩大到未测分支。
