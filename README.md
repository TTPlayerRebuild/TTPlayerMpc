# TTPlayer Musepack 插件重建

最新补全与验收：[2026-10-24](docs/COMPLETION_20261024.md)。

面向原版千千静听 5.7.9 与重建版的 x86 `ttp_mpc.dll`。唯一导出为 ordinal 1 的 `ttpGetSoundAddIn`，恢复一个 `MPC Reader` 工厂，扩展名 `mpc;mp+`，支持原版已经具备的 SV7 和 SV8。

## 已实现

- 宿主 IStream、扩展等待读取、回调、停止和重启；float32 PCM、声道与采样率、样本长度、定位。
- 小缓冲、非整帧缓冲、短读、取消和错误返回。修复原版读取函数整块复制可能越界的问题。
- APEv2 文本读取、修改、删除、UTF-8 保存；保留未知项与二进制项。旧 ID3v1 字段修改或删除后不会重新覆盖新标签。
- SV7/SV8 ReplayGain 读取和保存；SV7 更新头部增益字段；SV8 原有 RG 块原位更新，没有 RG 时以标准 APEv2 字段保存，保持 SO/ST 定位偏移不变。
- 最后一次 Release 延迟保存，以及独立 QI 的可选终结式 Commit 接口。临时文件替换优先；宿主保留写句柄时使用完整备份和失败回滚。重复 Commit 不重试。
- Huffman 初始化改为一次初始化，CRC 使用不可变表，避免后台扫描与播放同时打开时的数据竞争。

## 构建与分发

```powershell
./build.ps1 -Package
```

使用现代 MSVC、Win32、VC-LTL 与 YY-Thunks，目标 XP SP3／Win7／Windows 10/11（SSE2）。构建时下载固定版本并核验 SHA-256，源码不纳入仓库。独立 Action 使用北京时间 `yyyy.MM.dd[pN]` 版本；发行 ZIP 仅含 `AddIn/ttp_mpc.dll` 和 `SHA256SUMS.txt`。Action 不运行或上传私有测试。

内核为官方 `musepack_src_r475`，SHA-256 `a4b1742f997f83e1056142d556a8c20845ba764b70365ff9ccf2e3f81c427b2b`。固定来源及小补丁见 `cmake/codec.cmake`；许可证保存在 `licenses/`。

## 实测与边界

2026-10-19 本机：SV7、SV8 的完整解码 PCM 与原 DLL 逐字节一致；8 字节与 8193 字节输出、7 字节短读、定位、Unicode 标签、增益、清空、宿主写句柄、短写／刷新失败回滚通过。详见 `docs/RECONSTRUCTION.md`。

当前内核 I/O 偏移是有符号 32 位，因此显式拒绝大于 2 GiB 的输入；要求可定位流。保存仅接受本地命名文件，当前事务路径使用 MAX_PATH 内路径。未恢复不存在于原工厂中的 MPC 编码器，也不增加封面接口；已有二进制封面项保存时保留。实际旧系统及原宿主界面验证由集成测试记录单独说明，静态导入检查不等于运行验证。

## 可选格式识别接口

2026-10-20：Reader 工厂增加 Probe，Reader 增加 SourceInfo；旧导出和接口槽位保持不变。声明与辅助实现位于 `include/ttpcomm/audio_format_probe.h` 和 `audio_format_probe_impl.h`，不增加运行时 DLL 依赖。协议原件在相邻 `ttpcomm/sdk/AUDIO_FORMAT_PROBE.md`；新建插件指南在相邻 `rebuild/docs/AUDIO_PLUGIN_DEVELOPMENT_GUIDE.md`。原版播放器继续使用旧接口；错误后缀候选选择和更名提示由支持新协议的重建播放器执行。
