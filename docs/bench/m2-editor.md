# M2 编辑器验证报告

环境：Unity 2022.3.62f3c1（Unity 中国版，Personal 许可证）、Windows 11 家庭版 10.0.26200、
Intel Core i9-12900H（14 核 / 20 线程）、15.7 GB 内存、Unity 自带 Roslyn。
除特别说明外，Unity 命令均以 `-batchmode -nographics` 运行。

## 1. 工程导入

```
Unity.exe -batchmode -nographics -quit \
  -projectPath mythforge/unity/DemoGame \
  -logFile import.log
```

- 退出码 0，`Library/ScriptAssemblies/MythForge.Editor.dll` 与
  `MythForge.Editor.Tests.dll` 均由 Unity 编译生成。
- 日志无 `error CS`；包依赖（`com.unity.test-framework@1.1.33`、`com.unity.ext.nunit@1.0.6`）
  正常解析。

## 2. EditMode 测试

```
Unity.exe -batchmode -nographics \
  -projectPath mythforge/unity/DemoGame \
  -runTests -testPlatform EditMode -testResults editmode.xml
```

结果：**67 passed / 0 failed / 0 skipped**（M2 任务图 47 个 + 剧情 9 个 + 技能 8 个 +
窗口构造 2 个 + 2000 节点性能 1 个）。

### 2.1 任务图（M2 首期）

| 测试 | 验证内容 |
|---|---|
| BuildsNodeViewsAndEdges | 3 节点 / 2 连线的 VisualElement 数量 |
| NodesExposeTypedPorts | 输入/输出端口、连接数、端口类型 `MfFlow` |
| StartAndEndHaveSingleSidedPorts | 开始无入端口、结束无出端口 |
| DiagnosticBadgeAppearsAndClears | 错误时标题出现 ⚠，清除后恢复 |
| BuildsEveryRegisteredNodeType | 10 种节点类型全部可渲染，端口符合目录声明 |
| CompatiblePortsRejectSameDirectionAndSelf | 连线候选过滤 |
| FocusNodeSelectsIt | 诊断点击定位的选中逻辑 |

其余覆盖：JSON 稳定性、图文档、连线规则、环/可达/必填校验、撤销栈、拼音搜索、`mf check` 集成。

### 2.2 剧情图与 Timeline 联动

- 目录 7 种节点：`dialogue.start/line/choice/flag/timeline/wait/end`。
- 图文档按 kind 选择目录：剧情文档里 `AddNode("quest.*")` 返回 null，避免混入错误节点。
- 校验新增：空说话人/文本、少于两个选项、Timeline 资产为空。
- JSON 往返保留 `kind=dialogue`，重新打开时目录自动切回剧情。
- `MfTimelineBridge` 通过 `AssetDatabase` 查找 `TimelineAsset`、Project 定位，并用反射调用
  `UnityEditor.Timeline.TimelineEditor.OpenTimeline`（Timeline 包未安装时不产生编译依赖）。

### 2.3 技能时间轴

- 模型 `MfSkillTimeline` 负责按 `timeMs` 排序并重建 `cast -> frames -> end` 链。
- 新增/移动/删除帧都会重建连线，保证 lowering 看到的顺序就是时间顺序。
- 图文档校验新增：负时间、非法事件名、时间倒序（`MFD024`）。
- JSON 往返保留 `kind=skill`，`skill.frame` 属性齐全。

### 2.4 窗口装配（无显示器验证）

| 测试 | 验证内容 |
|---|---|
| DialogueWindowBuildsGraphFromDemo | 反射调用 `QuestGraphWindow.CreateGUI`，剧情窗口从示例载入 **7 个节点** |
| SkillWindowLoadsDemoOnEnable | 反射调用 `SkillTimelineWindow.OnEnable`，示例技能载入 **3 个帧** |

GUI 截图不可用：自动化 shell 运行在非交互会话，Unity GUI 进程会卡在 GI Cache 初始化。
窗口装配测试直接调用 Unity 自己的 `CreateGUI` / `OnEnable` 入口，覆盖了真实的窗口构建路径。

## 3. 2000 节点性能实测

`EditorPerformanceTests` 构造 2000 节点线性任务链（1 个 start + 1998 个 log + 1 个 end），
全部节点带坐标并串成 1999 条边，然后依次测量：

| 阶段 | 冷启动（单独跑该测试） | 热路径（全量测试套件末尾） |
|---|---|---|
| 模型构建（AddNode + Connect + 坐标） | 534 ~ 560 ms | 193 ms |
| 序列化 `ToJson` | 60 ~ 61 ms | 18 ms |
| 反序列化 `FromJson` | 146 ~ 156 ms | 49 ms |
| 本地校验 `Validate` | 641 ~ 696 ms | 239 ms |
| `MfGraphView.Rebuild`（节点+端口+连线） | 2767 ~ 3210 ms | 812 ms |
| JSON 体积 | 477 KB | 477 KB |

测量口径：

- `Stopwatch` 包住单个调用，单位毫秒；不含 Unity 启动、程序集加载和测试框架开销。
- `Rebuild` 只测量 `DeleteElements + 创建 MfNodeView + AddElement + 端口连线`，
  **不包含**帧循环、面板布局与渲染；batchmode 下没有可见面板，不能把它当成编辑器帧率。
- “冷启动”是只跑性能测试（GraphView/UI Toolkit 类型尚未 JIT）；“热路径”是 64 个
  功能测试之后运行，更接近编辑器启动后实际点击时的状态。
- 2000 节点是压力上界；示例内容只有 5 ~ 7 个节点。测试断言 `Rebuild < 15 s`、
  `Validate < 3 s`，用于捕获数量级回归。

结论：数据层（构建/序列化/校验）在 2000 节点下均低于 0.7 s；主要成本在 UI Toolkit
创建 2000 个节点视图与 1999 条边。若后续内容规模继续增长，可优先做视口裁剪与增量
`AddNodeView`，而不是重写文档模型。

## 4. 内容流水线扩展

- `schema/node_types.json` 新增 `dialogue`（7 种）与 `skill`（3 种）节点。
- `graph.cpp` 按 `kind` 分派校验与 lowering：任务 → `/start`，剧情 → `/start`，
  技能 → `/cast`；VM 的 `startQuest` 同时识别 `/start` 与 `/cast`。
- 剧情 lowering：`DialogueLine` → 等 `DialogueAdvance`；选项 → 等
  `DialogueChoiceSelected`；Timeline → `PlayTimeline(asset)` +（可选）等
  `TimelineFinished(asset)`。
- 技能 lowering：帧时间转相对 `wait time`，再调用宿主
  `SkillEvent(event, target, amount, asset)`。
- `mf validate/build/test` 会同时收集 `content/quests`、`content/dialogues`、
  `content/skills`；manifest 记录 `kind`；测试用例可用 `content` 或 `quest` 指定模块。

实测：

```
validate: 4 file(s), 4 ok, 0 error(s), 0 warning(s)
build:    4 file(s), 4 compiled, 0 error(s)   # 缓存版本升级后的首次构建
test:     5 passed, 0 failed
```

## 5. 集成中发现并修复的问题

| 现象 | 根因 | 修复 |
|---|---|---|
| 包解析失败 `package.json cannot be found` | `file:` 相对路径以工程根为基准，少了一级 | 改为 `file:../../Packages/com.mythforge.editor` |
| 测试程序集不编译 | 该编辑器构建未定义 `UNITY_INCLUDE_TESTS` | 测试 asmdef 去掉该约束（仍是 Editor-only） |
| `unknown command 'check'` | `mf.exe` 还是旧构建 | 重新编译 C++ CLI |
| 路径断言失败 | `Application.dataPath` 正斜杠与 `FullName` 反斜杠混用 | 测试两侧统一 `Path.GetFullPath` |
| GraphView 没有 `selectionChanged` | 2022.3 API 不提供 | 检查器每帧轮询选中节点（字符串比较） |
| 技能帧数断言多 1 | `n` 个节点只有 `n-1` 条边 | 测试按 `frames + 1` 计算边数 |
| GUI 模式卡在 GI Cache | 非交互会话没有 `AppData/LocalLow/Unity/Caches` | 改用 headless 窗口装配测试，不依赖显示器 |

## 6. 复现命令

```bash
# 原生
cmake --build build -j
./build/native/mythforge_tests.exe
./build/native/mf.exe validate content
./build/native/mf.exe build content
./build/native/mf.exe test content

# Unity
Unity.exe -batchmode -nographics -quit -projectPath unity/DemoGame -logFile import.log
Unity.exe -batchmode -nographics -projectPath unity/DemoGame \
  -runTests -testPlatform EditMode -testResults editmode.xml

# 只跑 2000 节点性能
Unity.exe -batchmode -nographics -projectPath unity/DemoGame \
  -runTests -testPlatform EditMode -testFilter EditorPerformanceTests \
  -testResults perf.xml
```

人工查看窗口：用 Unity Hub 打开 `unity/DemoGame`，菜单
`MythForge/任务图编辑器`（Ctrl+Shift+Q）、`MythForge/剧情编辑器`（Ctrl+Shift+D）、
`MythForge/技能时间轴编辑器`（Ctrl+Shift+E）。
