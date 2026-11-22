# 切片 A 基准报告

环境：Windows 10、MinGW-w64 g++ 13.2.0、CMake 3.30、RelWithDebInfo。所有数字均为本机一次实测，复现命令在文末。磁盘为普通 NVMe SSD，Windows Defender 实时扫描开启（会显著影响小文件 I/O）。

## 1. 正确性

```
$ ./build/native/mythforge_tests.exe
51 test(s), 100193 check(s), 0 failed test(s)
```

覆盖：JSON/SHA-256、词法（含 UTF-8 标识符）、语法（优先级/错误恢复）、语义（未定义变量、类型、事件字段、只读字段、未使用变量）、SSA/优化、模块序列化/篡改检测/寄存器越界/跳转越界、VM（算术/分支/循环/谓词/定时器/监听器/短路求值/除零故障/宿主调用）、图校验（缺失属性/环/不可达/分支拒绝/依赖收集）、图端到端执行。

内容回归：

```
$ ./build/native/mf.exe test content
[PASS] forest_wolves_happy_path
[PASS] forest_wolves_ignores_other_targets
[PASS] lost_sheep_happy_path
test: 3 passed, 0 failed
```

## 2. 增量构建（10000 个合成任务，约 36 MB）

生成数据：

```
$ ./build/native/mf.exe gen-content --out _bench_content --files 10000 --nodes 20
gen-content: 10000 file(s) -> _bench_content
```

| 场景 | 墙钟时间 | 重编译模块数 |
|---|---|---|
| 冷编译（无缓存、无产物） | 21.0 s | 10000 |
| 增量，无改动 | 6.7 s | 0 |
| 增量，改 1 个文件 | 6.6 s | 1 |
| `mf stats`（只读 + 解析全部文件） | 4.8 s | — |

分析：

- 冷编译约 **476 文件/s**（解析 + Schema 校验 + 降级 + 语义 + SSA + 优化 + 编码 + 写 `.mfb`）。
- 增量构建的固定成本是「遍历 10000 个文件 + 读 36 MB + SHA-256」，约 5 s；编译阶段本身从 21 s 降到约 0 s。改 1 个文件与不改文件耗时相同，说明增量判定没有额外开销。
- 早期版本用 `std::filesystem::relative` 计算缓存键，每文件两次路径规范化（stat），冷编译 44.7 s；改成 `lexically_relative`（纯字符串运算）+ 一次性枚举产物目录后降到 21.0 s。这是本项目第一次「先测量、再优化」的记录。
- 尝试过用 `last_write_time + file_size` 跳过未变文件的读取，但在 Windows/libstdc++ 上每次查询元数据仍是一次 stat，反而比直接读+哈希慢（8.6 s），因此回退。生产级做法是文件监听或目录索引服务，留到 M5 完善。

## 3. VM：10 万个协程

```
$ ./build/native/mythforge_tests.exe vm_runs_hundred_thousand
[ RUN  ] vm_runs_hundred_thousand_coroutines
[  OK  ] vm_runs_hundred_thousand_coroutines
1 test(s), 100005 check(s), 0 failed test(s)
```

`Measure-Command` 墙钟 **87 ms**（包含进程启动、100k 次 `startChunk`、100k 次 `MF_CHECK` 和测试框架输出）。每个实例是一块堆上的寄存器帧 + 就绪队列节点，执行到 `HALT` 后回收到实例池，没有原生线程/栈开销。

## 4. 内容编译一致性

图与手写脚本共用流水线，`mf dsl` 可直接审阅降级结果：

```
$ ./build/native/mf.exe dsl content/quests/q_forest_wolves.json
quest q_forest_wolves {
  on start {
    emit QuestStarted("q_forest_wolves");
    {
      let n_kill_count = 0;
      while ((n_kill_count < 2)) {
        wait event(Kill, (target == "Wolf"));
        n_kill_count = (n_kill_count + 1);
      }
    }
    emit ObjectiveProgress("n_kill", 2, 2);
    wait event(Talk, (npc == "Elder"));
    call GrantReward(100, "IronSword");
    emit QuestCompleted("q_forest_wolves");
  }
}
```

## 5. 复现

```bash
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j

./build/native/mythforge_tests.exe

./build/native/mf.exe gen-content --out _bench_content --files 10000 --nodes 20
rm -rf _bench_content/.mfcache _bench_content/out
time ./build/native/mf.exe build _bench_content     # 冷编译
time ./build/native/mf.exe build _bench_content     # 无改动
printf ' ' >> _bench_content/quests/q_gen_00042.json
time ./build/native/mf.exe build _bench_content     # 单文件
```

注意：产物和合成内容都在 `.gitignore` 中；基准数字会随磁盘、杀软和编译器版本波动，结论看比例而非绝对值。
