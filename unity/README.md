# Unity 工程（M2）

`DemoGame` 是 Unity 2022.3 LTS 工程，引用本地包 `com.mythforge.editor`。

## 打开

1. 用 Unity Hub 添加编辑器：`E:\j\Unity\Editor\Unity.exe`（2022.3.62f3c1）。
2. 打开工程：`mythforge/unity/DemoGame`。
3. 首次打开会导入包与示例内容（`Assets/MythForgeContent`）。

许可证：Unity 2022.3 需要 Personal 许可证。打开 Hub 登录 Unity 账号即可自动激活；
离线环境可用 `Unity.exe -batchmode -createManualActivationFile` 生成 `.alf`，
在 Unity 许可证门户换取 `.ulf` 后用 `-manualLicenseFile` 激活。

## 菜单

- `MythForge/任务图编辑器`（Ctrl+Shift+Q）：任务图（`kind=quest`）。
- `MythForge/剧情编辑器`（Ctrl+Shift+D）：剧情图（`kind=dialogue`）+ Timeline 联动。
- `MythForge/技能时间轴编辑器`（Ctrl+Shift+E）：技能帧时间轴（`kind=skill`）。
- `MythForge/实时预览`（Ctrl+Shift+P）：真实 VM 驱动的时间轴预览 + 节点高亮 + JSONL 遥测。
- `MythForge/打开示例任务 / 示例剧情 / 示例技能 / 预览示例任务`：加载对应示例。
- `MythForge/同步运行时内容到 StreamingAssets`：把 4 个 `.mfb` 构建到演示游戏目录。
- `MythForge/创建演示场景`：生成 `Assets/Scenes/MythForgeDemo.unity`。

三个编辑器共用同一套图文档、撤销栈、校验与预览：

- GraphView 画布（任务/剧情）：类型化 Flow 端口、右键按分类创建节点、连线合法性检查（v1 限制单出边）。
- 检查器：内容 ID/标题、节点属性（字符串/整数/浮点/枚举/布尔）。
- 撤销/重做：快照式命令栈，覆盖移动、连线、删除、属性修改。
- 校验：本地结构检查 + 调用 `mf check <file> --json`，诊断按 `nodeId` 标在节点上，可点击定位。
- 预览：把左下角事件序列（`Kill target=Wolf`）写成 JSON，调用 `mf sim`，显示 emits / calls / 错误。
- 构建：调用 `mf build <content-root>`，输出写入 Unity Console。
- Agent 修复：`mf agent fix --write` 补必填/纠类型/排时间，设计值保持原样。
- 搜索：按中文/英文/拼音关键词淡化不匹配节点（关键词表在 `NodeCatalog.cs` / `DialogueNodeCatalog.cs`）。

## 实时预览（M6 + M10）

- 打开任意内容 JSON，窗口会在 `.mfb` 缺失/过期时自动 `mf build`；
- `MythForge.Runtime.MfPlaySession` 通过 `mythforge.dll` 加载模块，编辑器时钟推进虚拟时间；
- 事件按钮（击杀/对话/选项/Timeline 完成）注入真实事件，日志显示 emit/call/wait/fault；
- trace 里的 `node` 来自 `.mfb` 调试表，画布自动高亮刚刚执行的节点；
- 开启“记录遥测”后，每行 trace 追加到 `DemoGame/Logs/MythForge/*.jsonl`；
- 工具栏“在图编辑器中定位”会打开对应编辑器并聚焦到当前节点。

剧情编辑器额外提供 Timeline 联动：

- `dialogue.timeline` 节点的检查器可浏览/选择 `.playable` 资产，或一键在 Project 中定位；
- `Timeline 联动` 工具栏按钮会尝试用反射打开 Unity Timeline 窗口（Timeline 包未安装时只定位，不产生编译依赖）；
- 编译时该节点降级为 `call PlayTimeline(asset)`，勾选“等待播放完成”时再等待 `TimelineFinished(asset)`。

技能时间轴编辑器：

- 顶部刻度尺（100ms 小刻度 / 500ms 标签）+ 可拖动黄色播放头；
- 5 条事件泳道（damage/buff/vfx/sfx/move），方块标记可拖动改时间（默认 10ms 吸附，按住 Shift 为 1ms）；
- 左侧帧列表按时间排序，右侧检查器编辑时间/事件/目标/数值/资源；
- 任何时间修改都会重排 `cast -> frames -> end` 连线，保证 lowering 顺序；
- 保存即导出 `kind=skill` 图 JSON，校验/构建/预览仍走 `mf`。

## 内容目录

```
Assets/MythForgeContent/
  quests/     *.json  任务图
  dialogues/  *.json  剧情图
  skills/     *.json  技能图
  tests/      *.json  回归用例
```

编辑器保存时会同步 `AssetDatabase.ImportAsset`，`mf build` 一次构建全部三类内容。

## 测试

Unity 菜单 `Window > General > Test Runner > EditMode > Run All`，或命令行：

```
Unity.exe -batchmode -nographics -projectPath unity/DemoGame \
  -runTests -testPlatform EditMode -testResults editmode.xml
```

当前 **78 个 EditMode 测试全部通过**（本机实测），覆盖：

- 任务图 47 个：节点视图、端口、连线过滤、诊断徽标、全类型渲染、JSON 稳定性、图文档、
  Cycle/可达/必填校验、撤销栈、搜索、`mf check` 集成；
- 剧情图 9 个：节点目录、kind 隔离、Timeline 资产校验、选项校验、JSON 往返、GraphView 渲染；
- 技能时间轴 8 个：帧排序、链路重建、移动/删除、默认属性、JSON 往返、时间倒序校验；
- SDK 6 个：P/Invoke 加载 `.mfb`、任务/剧情/技能执行、trace 解析、JSONL 遥测落盘；
- Agent 2 个：坏图修复后 `mf check` 通过、不可推导的设计值保持 remaining；
- 窗口装配 3 个：剧情窗口 7 节点、技能窗口 3 帧、实时预览窗口启动真实 VM；
- 演示会话 2 个：一个会话同时驱动任务/剧情/技能，缺失模块错误路径；
- 性能 1 个：2000 节点构建/序列化/校验/GraphView 重建。

## 运行时 SDK 与演示游戏

- `Runtime/MythForge.Runtime.asmdef`：`MfNativeLibrary`（DLL 定位）、`MfNativeVm`（P/Invoke）、
  `MfPlaySession` / `MfTelemetryWriter`、`MfDemoSession`。
- DLL 定位顺序：环境变量 `MYTHFORGE_SDK` → `<仓库根>/build/native/mythforge.dll`。
- 演示场景：菜单 `MythForge/同步运行时内容到 StreamingAssets` 后
  `MythForge/创建演示场景`，打开 `Assets/Scenes/MythForgeDemo.unity` 进入 Play。
- 演示面板可击杀狼、与长老对话、推进剧情、施放技能；任务日志/台词/技能帧实时刷新。

## 验证记录（2026-11-20，Unity 2022.3.62f3c1）

- 工程导入成功，四个程序集（Editor/Runtime/Tests）均由 Unity 编译。
- EditMode：78 passed / 0 failed。
- 无显示器窗口验证：剧情窗口 7 节点、技能窗口 3 帧、实时预览窗口加载真实 VM。
- 2000 节点：模型 193 ms、校验 239 ms、`GraphView.Rebuild` 812 ms（热路径）；
  冷启动 `Rebuild` 约 2.8 ~ 3.2 s。测量口径见 `docs/bench/m2-editor.md`。
- 全链路：`scripts/e2e.ps1` 输出 `E2E PASSED`（详见 `docs/bench/slice-b.md`）。
- 集成中修掉的问题：本地包路径应为 `file:../../Packages/...`；该编辑器构建未定义
  `UNITY_INCLUDE_TESTS`（测试 asmdef 去掉该约束）；`mf.exe` 未包含新的 `check` 命令；
  测试中 Windows 路径分隔符需 `Path.GetFullPath` 归一化；Unity 2022.3 在写完测试 XML 后
  可能不退出，`e2e.ps1` 轮询 XML 后结束进程。

## 与原生管线的边界

编辑器不重复实现编译器：

- 结构校验在 C# 里先跑一遍，给即时反馈；
- 权威校验始终是 `mf check`；
- 预览始终是 `mf sim`；
- 这样编辑器、CLI、CI 共用同一份语义与诊断。

`MfCli` 查找 `mf` 的顺序：环境变量 `MYTHFORGE_MF` → `<仓库根>/build/native/mf.exe` → PATH。
仓库根通过从 `Application.dataPath` 向上查找 `schema/node_types.json` 得到。

## 已知限制

- 2022.3 的 GraphView 没有公开的 `selectionChanged` 事件，检查器用每帧轮询选中节点（O(1) 字符串比较）。
- v1 只支持线性内容链（任务/剧情/技能都一样），多出边在文档层和编译层都会被拒绝；
  剧情选项暂时是“展示 + 等待选择”，多分支走向留待 v2。
- Timeline 窗口的脚本打开依赖 `UnityEditor.Timeline.TimelineEditor.OpenTimeline` 反射；
  不同 Unity 版本签名可能变化，失败时退化为在 Project 中定位资产。
- 拼音搜索依赖节点目录里的关键词表，不是通用拼音库。
- 节点位置保存在 JSON 的 `editor: {x, y}` 字段，C++ 编译器会忽略它。
- 技能时间轴编辑器与图编辑器使用独立的 EditorPrefs 键（`MythForge.Last.<kind>`），
  三个窗口可以同时打开。
