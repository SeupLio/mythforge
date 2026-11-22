# ADR-0002：16 字节 tagged value 与字符串驻留

- 状态：已采纳（切片 A）
- 关联模块：M4 VM、未来的 M7 SDK 数据通道

## 背景

VM 需要在一帧内承载 int / float / bool / string 四类值；脚本里字符串比较（事件过滤、任务 ID）非常频繁。值的表示同时决定了：

- 帧内存占用（10 万协程的可行性）；
- 字符串比较的成本；
- 与 C ABI / Unity / Kotlin 之间传递值的难度。

## 决策

```cpp
enum class ValueTag : uint8_t { Nil, Int, Float, Bool, String };
struct Value {          // sizeof == 16（static_assert 保证）
  ValueTag tag;
  uint8_t padding[3];
  union { int64_t i; double f; int32_t b; uint32_t s; } as;
};
```

- 字符串在 VM 内**驻留（intern）**，Value 只存 32 位 id。`==` / `!=` 退化为整数比较；字典序比较才回表取内容。
- 常量池里的字符串在模块加载时一次性驻留；事件字段名、函数名同理。
- 不使用 NaN-boxing：它依赖浮点位模式与指针宽度，在 MSVC/MinGW/ARM64 上需要额外保证；tagged union 在每个平台语义一致，且 16 字节仍是 2 个 64 位字，缓存友好。
- 不在 Value 里放 `std::string`：那会引入非平凡构造/析构，无法直接放进 C ABI 结构体，也会让帧分配变慢。

## 取舍

**引用计数 vs 驻留表**：脚本字符串几乎全部来自常量池或事件字段（数量有限、生命周期长），驻留表天然去重，不需要逐值引用计数，也不需要 GC。代价是字符串永不释放——在「内容编译产物」这个场景里是可接受且可预测的。

**int/float 混合运算**：VM 在算术指令处做动态提升（任一操作数为 float 则按 double 计算），与 sema 的静态类型检查形成双保险。

## 后果

- 宿主（Unity/Android/iOS）拿到的字符串是 id，需要通过 `VM::stringOf` 解析；M7 的 C ABI 会在此之上提供 `mf_value` + `mf_vm_string`。
- 事件字段用 `(name, Value)` 列表传递，谓词程序按名字查字段；字段名来自编译期常量，不会产生运行时字符串拼接。
- 若未来出现大量动态字符串（玩家命名、聊天），需要另加带引用计数的堆字符串类型，届时用新的 `ValueTag` 扩展，不影响现有字节码。
