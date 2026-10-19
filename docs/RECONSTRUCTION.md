# MPC 原版恢复与验证

日期：2026-10-19。

## 原始证据

- 原 `ttp_mpc.dll` SHA-256：`c8e832ebed2424a893de2d5a4c975e7ecaedd23c4d92f968926981e8aa805293`。
- `60503d83` 导出，`605039a5` 初始化，`60503a97` 枚举一个 Reader。
- `60506d68` 同时识别 `MP+` SV7 与 `MPCK` SV8；恢复时未把 SV8 误当新增格式。
- `605023c7` Open，`60502731` Read，`60502cfe` Seek，`60501427` 最终释放保存，`6050308b/60503466` SV7 头部增益保存。
- 原输出 float32；`605021f3` 的原始位深属性固定 16，与 PCM 容器 32 位是两个属性。

## 有意修正

原 Read 忽略调用方小缓冲容量。新 Reader 按帧对齐分段返回，保留未返回的 PCM；不足一帧返回 `E_INVALIDARG`，不能写出界。Seek 保留请求毫秒，floor(duration) 时仍可读取小于 1 ms 的最后样本。

SV8 可选 RG 块不存在时不插入新块，因为插入会移动 SO/ST 表指向的音频位置。新值以 APEv2 ReplayGain 保存；存在 RG 块时同时原位更新。不会把写不进去当保存成功。

保存首先验证长度与最后写入时间，再构造完整文件。宿主只读流可关闭时优先 ReplaceFileW；宿主持有无 delete sharing 的可写流时保留该流并先复制完整恢复文件。写入、截断、刷新任一步失败均尝试恢复；恢复也失败时保留恢复文件，并在调试输出报告其路径。此回写分支不保证断电原子性。

## 可选提交 ABI

`{3AB643C1-D8A4-4D49-9CBB-EF196B04B6E7}`：IUnknown 后追加一个 `HRESULT Commit()`，使用 `STDMETHODCALLTYPE`。它是独立接口，不改变原 Reader/Metadata 虚表或导出。提交成功或失败均终结该 Reader 的读写会话；重复提交返回第一次结果，最终 Release 不重试。原版继续通过最终 Release 保存。

## 本地验证

测试只保存在工作区 `rebuild/tests/audio_plugins_fix_20261019/legacy_open/`，不进入本仓库或 Action。

- SV7/SV8：各 133019 帧、1064152 字节 PCM，新旧完全一致。
- 8 字节／8193 字节输出与 7 字节宿主短读、缓冲区 canary、停止／继续。
- Unicode 标题、ReplayGain 编辑／清空，PCM 保存前后不变。
- 无 RG 块的 SV8、重复提交、外部保留写句柄、模拟短写和刷新失败；失败后原文件 SHA-256 不变。

边界：APEv2 累计标签上限 16 MiB；输入不超过 INT_MAX；读流需可定位；不承诺所有损坏码流均与原内核有相同错误行为。XP/Win7 的运行结果与宿主界面结果以集成记录为准。

## XP 相对路径保存修正

XP VM 证明：ReplaceFileW 使用同目录相对路径返回 1176，且目标名字已消失；使用绝对路径成功，Win7 两种均成功。Reader 现于 Open 阶段将本地 Stat 名称固定为绝对路径，避开 XP 行为，也避免宿主稍后改变当前目录影响最终 Release 保存。XP／Win7 的 SV7／SV8 普通提交、保留写流、短写回滚、刷新失败、改变当前目录后提交均通过；私有记录 xp-r2-results.txt／win7-r2-results.txt。

ReplaceFile 另传唯一的绝对备份路径：若失败时系统已移走原文件，先把备份恢复到原路径；恢复失败则保留备份并在调试输出提示。私有 IAT 故障注入先移动原文件再返回 1176，SV7／SV8 均返回真实失败，重复 Commit 不再写盘，原文件 SHA-256 保持不变。

## XP／Win7 最终虚拟机回归

2026-10-19：最终 R4 探针包在 XP 与 Win7 完整运行，均 `FAILURES=0`。记录保存在私有测试目录 `xp-r4-results.txt`、`win7-r4-results.txt`。包含原 DLL 对照、播放／定位／缓冲边界、MPC 保存和模拟失败恢复、MOD 双播放 Reader 与后台扫描并发。MOD 八种样本的时长及 EOF 字节数也在两个系统分别核对原版一致。原版 DLL 使用仅限测试目录的 VC2012 私有运行库；重建 DLL 不依赖该库。

这批验证使用最终运行 ZIP 内相同 SHA-256 的 DLL。

## 最终真实播放器验证

2026-10-19：最终 MPC DLL 在原版 5.7.9 与重建播放器中分别完成 Windows 11、XP SP3、Win7 SP1 的加载、开始播放与关闭测试。六次均加载预期插件路径，主窗口存在、没有错误对话框、没有强制关闭，PlayingTime 大于零。私有记录：`rebuild/tests/audio_plugins_fix_20261019/hosts/{win11,xp,win7}-final-legacy.log`。

这确认真实宿主 ABI 与播放入口；不把短时运行当作全部曲目、音质或标签界面完整验收。Windows 11 不能替代独立 Windows 10 实测。
