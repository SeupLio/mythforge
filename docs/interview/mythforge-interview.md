# MythForge 面试材料（自我介绍 + STAR + 技术原理）

> 目标岗位：米哈游「游戏客户端工具开发实习生 / 客户端开发工程师实习生」。
> 这份文档把项目讲成“为什么做、怎么做、怎么验证、哪里还能更好”，可直接用于面试准备。

## 1. 自我介绍

### 30 秒版

我是 Peishuo Li，电子信息专业研究生，主要做 C++ / C# 工具链与运行时。这个项目叫 MythForge，
把游戏里的任务、剧情、技能三类内容做成一条工业管线：策划在 Unity 里编辑图，编译器把图降级成
MythScript，再经语义分析、SSA 优化生成寄存器字节码，最后由协程 VM 执行；同一份 `.mfb`
通过 C ABI 被 Unity 编辑器、预览窗口和演示游戏复用。项目目前有 61 个原生测试、78 个 Unity
EditMode 测试和一条端到端脚本，覆盖编译、运行时、编辑器、内容服务与自动修复。

### 2 分钟版

我做的不是一个“任务系统 Demo”，而是内容生产的工具链。动机来自两个 JD 的共同点：
一是海量内容的工业化生产工具，二是客户端 SDK / 运行时。我的做法是把“图”只当成前端：
任务/剧情/技能图先经过 schema 校验和 lowering，变成和手写脚本完全相同的 MythScript AST，
再走同一套语义分析、SSA、优化和字节码生成。这样图编辑器和脚本语言不会出现两套语义。

运行时是确定性的单线程协程 VM：每个脚本实例是堆上的寄存器帧 + pc，`wait event` 只挂起，
不占原生栈；事件通过谓词程序过滤，定时器进最小堆，时间由宿主推进，所以同样的事件序列
一定得到同样的结果。字节码里带 `pc → 图节点` 的调试表，VM 在 emit/call/wait/fault 时输出
结构化 trace，Unity 预览窗口用它做节点高亮，遥测把它写成 JSONL。

工具侧我做了三套编辑器（任务图、剧情图 + Timeline、技能时间轴），一个实时预览窗口，
一个 C ABI 运行时 SDK，一个本地内容服务 `mfd`，以及一个 schema 驱动的修复 Agent。
验证上，我用 2000 节点压测 GraphView 重建，记录了冷/热路径数据；用 ASan/UBSan、
libFuzzer harness 和 CI 矩阵保证质量；最后用 `scripts/e2e.ps1` 把原生测试、内容编译、
Agent、HTTP 服务和 Unity 测试串起来，一条命令给出 PASS/FAIL。

## 2. 与岗位的契合点

| JD 要求 | 项目证据 |
|---|---|
| 大型项目海量内容的工业化工具 | 图编辑器、批量编译、增量构建、内容服务、2000 节点压测 |
| 战斗 / 关卡 / 剧情演出 / 任务模块 | 任务图、剧情图 + Timeline、技能时间轴三类 schema 与 lowering |
| 提升各环节效率 | Agent 自动修复、实时预览、节点高亮、Telemetry、一条命令 e2e |
| 维护工具稳定性/易用性 | 诊断码 + 节点定位、撤销重做、CI、ASan/UBSan、fuzz、测试矩阵 |
| C 系语言 + 面向对象 | C++17 编译器/VM/SDK；C# Unity 工具；C ABI 跨语言 |
| 编译原理 / 数据结构 / 算法 | 词法/语法/语义、SSA + 支配边界、phi、DCE/CSE、寄存器分配、堆/队列 |
| Unity / Unreal | Unity 2022.3 GraphView、UI Toolkit、IMGUI、Timeline 反射联动 |
| AIGC / AI Agent | schema 驱动的修复 Agent + 可插 LLM 的闭环设计 |

## 3. STAR 案例

### STAR 1：图 → 字节码编译器 + 协程 VM

**Situation**：任务、剧情、技能三类内容如果各写一套解释器，语义会漂移，调试也割裂。
**Task**：让三类图共用一套编译与执行语义，并且能在无头环境回归。
**Action**：
- 定义 `schema/node_types.json`：节点类型、属性约束、端口度数；
- 图校验：单入口、无环、可达、能到出口、技能帧时间有序；
- lowering：图 → MythScript AST（任务/剧情/技能按 kind 分派）；
- 语义分析：作用域、类型、事件字段、未使用变量、未知事件建议；
- SSA：Cytron 支配边界插 phi、支配树重命名；常量折叠、平凡 phi 消除、CSE、DCE、跳转线程化；
- 字节码：32 位定长指令（8 位 opcode + 3 个 8 位操作数），255 寄存器/256 常量上限；
- VM：堆帧协程、ready queue、timer heap、event waiters、postfix 谓词求值；
- 调试表：`pc → 源码行/列/图节点`，为预览高亮与诊断定位服务。
**Result**：61 个原生测试 / 100278 个断言全过；图与手写脚本共用 `mf dsl` 输出；
同一份 `.mfb` 被 CLI、Unity、演示游戏复用。

**追问：为什么不用 Lua/IL？**
我需要的是可验证、可裁剪、确定性强的内容 DSL：节点类型有限、事件字段可静态检查、
字节码格式可控（SHA-256 trailer、加载时结构校验）。Lua 太通用，难以做 schema 级诊断；
IL 生态重，且不利于嵌入式/移动端裁剪。

### STAR 2：2000 节点 GraphView 性能

**Situation**：策划反馈大任务图打开慢，节点视图创建与连线是主要成本。
**Task**：量化 2000 节点图的构建、序列化、校验、渲染时间，并避免明显算法退化。
**Action**：
- 写 `EditorPerformanceTests`：构造 2000 节点 / 1999 边链，分别计时；
- 发现节点 ID 生成每次从头扫描，最坏 O(n²)，改为按类型维护自增计数器；
- 区分“冷启动（JIT 未热）”和“热路径（功能测试之后）”两种口径；
- 报告 GraphView.Rebuild 只含元素/端口/连线创建，不含帧循环与渲染。
**Result**：热路径模型 193 ms、校验 239 ms、Rebuild 812 ms；冷启动 Rebuild 2.8–3.2 s；
测试断言设 15 s 上限捕获数量级回归。面试时能明确说明“这个数字不是什么”（不是帧率）。

### STAR 3：跨语言运行时 SDK + 全链路测试

**Situation**：编辑器预览、演示游戏、未来 Android/iOS 都需要执行同一份字节码。
**Task**：只维护一份 VM，同时让 Unity 能稳定调用，并能观测执行过程。
**Action**：
- 设计 C ABI：`mf_vm_create/load/start/emit/advance/poll`，不暴露 STL；
- 宿主推进时钟：start 只跑到第一个 wait，避免预览时定时器被瞬间跳过；
- trace 采用拉取式 JSON 队列：`{type,name,node,at,args}`，避免跨语言回调生命周期问题；
- `node` 来自 `.mfb` 调试表，预览窗口零额外解析即可高亮；
- MinGW 静态链接 libstdc++/libgcc/winpthread，DLL 只依赖系统库；
- Unity 侧 `MfPlaySession` / `MfDemoSession`，EditMode 直接跑真实 VM；
- `scripts/e2e.ps1` 串起原生、内容、Agent、mfd、Unity，并处理 Unity 测试进程不退出的问题。
**Result**：Unity 78 个 EditMode 测试、原生 61 个测试全过；`E2E PASSED`；
Kotlin（JNA）/ Swift 绑定复用同一 ABI（参考实现，未在本机编译）。

### STAR 4：Agent 修复 + 内容服务

**Situation**：策划提交的图经常缺必填属性、负数、技能帧时间倒序，人工修效率低。
**Task**：在编译前自动修复“可推导”的问题，同时不篡改设计意图。
**Action**：
- `mf agent fix` 读取 schema 与诊断，补必填、纠类型、clamp 负数、修枚举、重排技能帧、补入口出口；
- 设计边界：Timeline 资产、flag 名、剧情文本等设计值保持为空，作为 remaining error；
- `mfd` 暴露 `/validate`、`/compile`、`/modules/{id}`、`/agent/fix`，让工具链可以不解包 CLI；
- 对 HTTP body、manifest、module id 做大小/字符校验。
**Result**：坏图修复后 `mf check` 通过；不可推导的问题仍然显式暴露；
`mfd` 端点全部被 e2e 覆盖。

## 4. 核心技术原理（面试可展开）

### 4.1 图 lowering 与“单一语义”

图只是前端。`graph.cpp` 把节点降级为 `wait event / emit / call / wait time` 等语句，
后面与手写脚本完全同路。这样：
- 图编辑器、CLI、CI 共享同一份校验与诊断；
- 新增节点类型只影响 lowering，不影响 VM；
- `mf dsl` 可以打印中间结果，便于 review 和教学。

### 4.2 SSA 与 phi

- 先建 CFG，计算支配树与支配边界；
- 在支配边界插 phi，再沿支配树重命名；
- 优化：常量折叠、平凡 phi 消除、块内值编号 CSE、DCE、跳转线程化；
- codegen 处理 phi 并行复制与临界边拆分，避免赋值覆盖。

### 4.3 寄存器字节码

每条指令 32 位：8 位 opcode、8 位 A、8 位 B、8 位 C；跳转用 8 位条件寄存器 + 16 位偏移。
255 寄存器/256 常量是刻意限制：一条指令一个字，dispatcher 缓存友好；超出时给出明确诊断，
把“拆 chunk”变成可执行的修复建议，而不是隐性崩溃。

### 4.4 协程 VM

- 实例 = 寄存器数组 + pc + 状态（Ready/WaitingTime/WaitingEvent/Done/Faulted）；
- `wait time` 入最小堆，`wait event` 入 waiters 表；
- 事件到达时用谓词程序过滤，避免唤醒无关协程；
- 时间是宿主推进的虚拟时钟，保证确定性、可重放；
- trace 在 emit/call/wait/fault 时输出，带 chunk/pc/节点。

### 4.5 增量构建

内容文件 SHA-256 作为缓存键；`quest.depends` 形成依赖边，变更向下游传播；
未变文件跳过解析/校验/编译，只更新 manifest。缓存版本号在编译器/格式变化时 bump，
避免旧产物被误复用。

### 4.6 GraphView 性能

GraphView 的成本主要在元素创建、端口连接与 UI Toolkit 样式计算，不在数据模型。
因此优化先消除 O(n²) 的 ID 生成，再如实区分“数据层”和“视图层”的耗时；
2000 节点是压力上界，常规内容只有个位数节点。

### 4.7 C ABI 与 P/Invoke

- 只暴露 C 函数与不透明指针，避免 C++ ABI/STL 差异；
- 字符串统一 UTF-8 + 长度/缓冲区，错误写入调用方缓冲区；
- trace 拉取式队列，宿主批量 poll，避免回调重入；
- 单 VM 单线程，宿主负责切主线程；
- Windows 静态链接 MinGW 运行库，减少部署摩擦。

## 5. 高频追问与回答

**Q：为什么 v1 只支持线性链？**
A：先把语义、诊断、字节码、VM 打通，分支需要 VM 的并行/竞争语义与图编辑器的多出边 UI，
是明确的下一阶段，而不是遗漏；当前编译器会明确拒绝多出边，避免“看起来支持、实际语义不对”。

**Q：如何保证图和脚本语义一致？**
A：图先 lowering 成同一 AST，后面完全同路；测试里同时覆盖图路径和脚本路径。

**Q：2000 节点 812 ms 算快吗？**
A：这是 UI Toolkit 创建 2000 个节点视图 + 1999 条边的成本，不是帧率；常规图只有几个节点。
真正的优化方向是视口裁剪/增量添加，而不是重写文档模型；报告里明确写了口径。

**Q：Agent 为什么不用 LLM？**
A：当前修复是“可由 schema 推导”的确定性问题，规则更可控、可测试；LLM 适合处理设计意图类
问题（例如该用哪个 Timeline 资产）。接口上把 remaining diagnostics 暴露出来，后续可接 LLM
做二次决策。

**Q：这个项目最大的不足？**
A：跨端只验证了 Unity；Kotlin/Swift 是参考绑定；热更/崩溃还是基础能力；分支与实时协作未做。
这些都在 README 的“已知限制”里，不回避。

## 6. 可以继续做的方向

- VM 支持 `parallel/race`，图编辑器开放多出边与剧情分支；
- 视口裁剪 + 增量 GraphView，把 2000 节点重建压到百毫秒级；
- LLM Agent：把 remaining diagnostics 与内容上下文交给模型，生成修复建议并回填；
- 真机 SDK：Android JNI + iOS Swift 包，补热更与崩溃上报；
- 多人协作：图的三方合并与冲突诊断（JSON 已按 key 排序，利于 diff）。
