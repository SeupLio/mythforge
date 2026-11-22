# ADR-0004：运行时 SDK 用 C ABI + 拉取式 JSON trace

## 状态

已采纳（M7 / M6 / M10）。

## 背景

编辑器、演示游戏和未来的 Android/iOS 宿主都需要执行同一份 `.mfb` 字节码。可选项：

1. 每个平台重写一份 VM（语义漂移、无法复用测试）；
2. 进程内嵌 C++（Unity 原生插件 / JNI / Swift C 互操作）；
3. 独立进程 + RPC（多一次序列化与进程管理）。

## 决策

- 对外只暴露 **C ABI**（`mf_sdk.h`），不暴露 STL 类型；Unity 用 P/Invoke，
  Kotlin 用 JNA，Swift 通过 module map 直接调用。
- 一个 `mf_vm*` 只允许单线程使用；VM 本身仍是确定性单线程协程调度。
- **宿主推进时钟**：`mf_vm_start` 只跑到第一个 wait，`mf_vm_advance_time(micros)`
  由编辑器帧/游戏帧驱动。这样预览、演示、真机行为一致，也便于暂停/单步。
- 事件与调用通过 **拉取式 trace 队列**（`mf_vm_poll`）返回 JSON 行：
  `{type, name, node, at, args}`。不做回调，避免跨语言回调的生命周期与线程问题。
- `node` 来自 `.mfb` 中已序列化的 `pc → 图节点` 调试表；VM 在 emit/call/wait/fault
  时按当前 pc 查表，不需要重新解析 JSON 图。
- Windows/MinGW 构建加 `-static -static-libgcc -static-libstdc++`，让 DLL 只依赖
  `KERNEL32.dll` / `msvcrt.dll`，Unity Editor 不需要额外运行时 DLL。

## 后果

- 优点：一份 VM 服务所有宿主；trace 天然可落盘为 JSONL 遥测；编辑器节点高亮零额外解析。
- 代价：trace 是字符串 JSON，极高频率事件下需要宿主批量 poll（当前上限 2000 行/会话）；
  单线程模型要求宿主自己切主线程。
- 未覆盖：Kotlin/Swift 绑定未在本机编译（缺少 Android NDK / Xcode）；热更目前是重载模块，
  不是资源级热更；崩溃是 VM fault trace，不是进程级崩溃 SDK。
