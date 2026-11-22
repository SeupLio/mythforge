# 切片 B 验证报告（M6 / M7 / M8 / M9 / M10 / M11）

环境：Windows 11 10.0.26200、i9-12900H（14C/20T）、15.7 GB、MinGW g++ 13.2、
Unity 2022.3.62f3c1（中国版）。

## 1. 本轮新增

| 模块 | 实现 | 验证 |
|---|---|---|
| M7 运行时 SDK | `native/sdk`：C ABI 动态库 `mythforge.dll`（仅依赖 KERNEL32/msvcrt）+ Unity P/Invoke | 原生 SDK 测试 2 个；Unity `SdkIntegrationTests` 6 个 |
| M6 实时预览 | `PlayWindow`：加载内容 → 自动编译 `.mfb` → 原生 VM 驱动虚拟时钟 → 事件注入 → 节点高亮 | 窗口装配测试 + 会话测试 |
| M10 遥测 | 每条 trace 写 JSONL（`type/name/node/at/args`），窗口显示计数器 | `TelemetryWritesJsonLines` |
| M8 内容服务 | `mfd`：`/health`、`/manifest`、`/content`、`/validate`、`/compile`、`/agent/fix` | `scripts/e2e.ps1` 实测 6 个端点 |
| M9 修复 Agent | `mf agent fix`：补必填属性、类型纠偏、枚举/负数/时间排序、补入口出口；不臆造设计值 | 原生 3 个测试 + Unity 2 个测试 |
| M11 演示游戏 | `MfDemoSession` + `MythForgeDemoGame` + 菜单生成场景与 StreamingAssets 内容 | `DemoSessionTests` 2 个 |

## 2. 运行时链路

```
content/*.json
  → mf validate / build            （同一套校验 + lowering + 字节码）
  → .mfb（含 pc→图节点 debug 表）
  → mf_vm_load_module_file         （C ABI，mythforge.dll）
  → VM 执行 → TraceEvent(emit/call/wait/fault, nodeId)
  → mf_vm_poll JSON 行
  → Unity MfPlaySession / MfDemoSession
  → PlayWindow 节点高亮 + JSONL 遥测
```

- 虚拟时钟由宿主推进：`mf_vm_start` 只跑到第一个 wait，不自动跳过定时器；
  这样预览/演示与真实帧循环一致。
- `nodeId` 来自字节码里序列化的 debug 表，不需要重新解析 JSON。
- 崩溃/异常路径同样进入 trace（`fault` 行带 chunk、pc、节点），可被遥测落盘。

## 3. 实测结果

```
== native build ==              PASS
== native unit tests ==         61 test(s), 100278 check(s), 0 failed
== content pipeline ==          validate 4/4, build 4, test 5 passed
== agent repair ==              fixed 3 项，剩余 0，repair 后 check ok
== content service ==           /health /manifest /content /validate /compile 全 PASS
== Unity EditMode ==            total=78 passed=78 failed=0
E2E PASSED
```

复现：

```powershell
./scripts/e2e.ps1              # 含 Unity
./scripts/e2e.ps1 -SkipUnity   # 只跑原生 + 内容 + 服务 + Agent
```

## 4. 内容服务示例

```bash
./build/native/mfd.exe content --port 7788
```

```bash
curl http://127.0.0.1:7788/health
curl http://127.0.0.1:7788/manifest
curl http://127.0.0.1:7788/content
curl -X POST --data-binary @content/quests/q_forest_wolves.json \
     -H "Content-Type: application/json" http://127.0.0.1:7788/validate
curl -X POST --data-binary @content/quests/q_forest_wolves.json \
     -H "Content-Type: application/json" http://127.0.0.1:7788/compile
```

`/compile` 返回 `id / kind / chunks / sha256 / artifactBase64`，可直接交给 SDK 加载，
不需要落盘。

## 5. Agent 修复示例

```
$ mf agent fix build/broken.json --write --json
{
  "changed": true,
  "fixed": [
    "node k: filled required prop 'target'",
    "node k: count clamped to 1",
    "node e: unknown result coerced to success"
  ],
  "ok": true,
  "remaining": [],
  "written": true
}
```

设计边界：Agent 只做“可由 schema 推导”的修复；Timeline 资产、flag 名、剧情文本等
设计意图保持为空并保留为 remaining error，避免静默篡改内容。

## 6. 演示游戏

- 菜单 `MythForge/同步运行时内容到 StreamingAssets`：强制构建 4 个模块到
  `Assets/StreamingAssets/MythForge`。
- 菜单 `MythForge/创建演示场景`：生成 `Assets/Scenes/MythForgeDemo.unity`。
- 进入 Play：左侧 IMGUI 面板可击杀狼、与长老对话、推进剧情、施放技能；
  任务日志、当前台词、技能帧事件实时刷新。
- `MfDemoSession` 同时持有任务/剧情/技能三个 VM，验证一个宿主可并行驱动多类内容。

## 7. 已知限制 / 诚实说明

- Kotlin（JNA）与 Swift 绑定是**参考实现**，本机没有 Android NDK / Xcode，未编译验证；
  它们调用的是与 Unity 完全相同的 C ABI。
- “热更”当前等价于 `load_module_file` 重载模块（PlayWindow 的“重新编译”会重载）；
  未做资源级热更与版本回滚。
- “崩溃”当前是 VM fault 的结构化 trace + JSONL 落盘，不是进程级崩溃上传 SDK。
- `mfd` 是单线程、无 TLS、仅监听 127.0.0.1 的本地内容服务，定位是工具链后端，不是生产服务。
- Unity 2022.3 在本机 `-runTests` 写完结果 XML 后可能不退出（已确认结果已落盘）；
  `e2e.ps1` 采用“轮询 XML → 解析 → 结束进程”，避免 CI 卡死。
