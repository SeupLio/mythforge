# Contributing

## 构建与测试

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
ctest --test-dir build --output-on-failure
```

- 需要 C++17；Windows 推荐 MinGW-w64 或 MSVC。
- Unity 相关改动需要 Unity 2022.3.62f3c1，工程在 `unity/DemoGame`。
- 全链路（原生 + 内容 + 服务 + Agent + Unity）使用 `./scripts/e2e.ps1`。

## 代码约定

- 原生代码 2 空格缩进、`-Wall -Wextra -Werror`；提交前保证 `ctest` 全绿。
- 图节点/事件/诊断码属于对外契约：新增节点类型必须同步
  `schema/node_types.json`、`graph.cpp`、Unity 目录与测试。
- 任何“不可信输入”解析（JSON、`.mfb`、HTTP body）都要有大小上限与错误路径测试。
- C# 运行时代码放在 `unity/Packages/com.mythforge.editor/Runtime`，编辑器代码放在 `Editor`。

## 安全

- 不要在仓库中提交 Token、私钥、`Library/`、构建产物或本机路径。
- 测试里的临时文件写到系统临时目录或 `build/`，并在 finally 中清理。
