# ADR-0003：图编辑器产物先降级为 MythScript AST，再走统一编译流水线

- 状态：已采纳（切片 A）
- 关联模块：M1 graph、M2 编辑器、M3 编译器

## 背景

内容有两个来源：

1. 策划在 Unity 图编辑器里连节点；
2. 程序手写 MythScript 做调试、写单测、写模板。

如果图直接生成字节码，就会出现两套语义：图里的 `count=2` 和脚本里的 `while` 计数可能行为不同；诊断、优化、调试信息也要各写一遍。

## 决策

```
Graph JSON ──lower──▶ Program(AST) ──sema──▶ IR ──SSA/opt──▶ bytecode
MythScript ──parse──▶ Program(AST) ──sema──▶ IR ──SSA/opt──▶ bytecode
```

- `lowerQuestGraph` 只负责把节点语义翻译成 AST 语句（例如 kill 节点翻译为 `let n = 0; while (n < count) { wait event(Kill, target == X); n = n + 1; }`）。
- 图节点 ID 会写进 AST 语句的 `nodeId`，再进入 IR 指令和字节码调试表；诊断因此能同时给出行列号和「哪个节点」。
- `mf dsl` 直接打印降级结果，评审时不用读字节码。

## 取舍

**直接生成 IR/字节码**：少一次 AST 构造，但会让图的语义和语言语义分叉，且无法复用 sema 的类型/事件字段检查。AST 只多一次树遍历，换来的是单一语义来源。

**图 → 文本 → 再解析**：可以复用 parser，但会引入格式化/转义的边界问题，且节点 ID 的映射要靠注释或行列号反推，诊断精度下降。直接构造 AST 更可控。

## 后果

- 图能表达的能力受语言限制：这也是 v1 明确拒绝「一个节点多条出边」的原因——`parallel/race` 还没有对应的 VM 语义，宁可报错也不生成错误行为。
- 新增节点类型时，需要同时改 `schema/node_types.json`（校验）和 lowering（语义）；这为后续「Schema 生成三端代码」留出了明确的两处落点。
- 调试信息表让 `pc → 源码行 → 图节点` 可双向定位，是 M6 实时预览「节点高亮」和 M9 Agent 修复闭环的基础。
