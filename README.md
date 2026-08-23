# Recall Memory

Recall Memory 是面向各类 Agent 的本地经验记忆与重复错误防护平台。它把已完成任务中的有效经验沉淀为结构化记忆，在后续任务开始和结果验收前召回相关约束、失败模式与验证步骤，降低 Agent 重复犯错或重新引入旧问题的概率。

Recall Memory 本身不是 Agent，也不接管 Agent 的执行流程。任何 Agent 都可以通过本地 HTTP 或 MCP 使用统一记忆能力；C++ 桌面控制台负责启动和停止服务。Code Graph 是首版附带的可选编码领域适配器，不是记忆核心的前置依赖。

## 首版能力

- 通用任务经验、操作教训、用户偏好、领域事实及工程经验模型
- 候选、已验证、过时、停用四阶段信任生命周期
- 基于任务文本、Agent、上下文、资源、实体、流程和作用域的召回
- 输出历史重复风险、不变量及必要验证步骤
- SQLite 本地持久化，不要求把经验写进项目文档
- 独立创建通用记忆空间，非编码 Agent 无需绑定代码目录
- 原生 Windows C++ 服务控制台，可配置数据库、地址和端口
- 通用 MCP stdio 接口和本地 HTTP 接口
- 可选 C/C++ Code Graph：使用 Tree-sitter 建立文件、符号、包含、引用和调用关系

## 构建

前置条件：

- CMake 3.25 或更高版本
- 支持 C++23 的编译器，已验证 Visual Studio 2022
- 首次配置时可访问网络，用于下载锁定版本的依赖

```powershell
cmake -S . -B build -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

## 启动

推荐直接启动桌面服务控制台：

```powershell
.\build\Debug\recall-memory-desktop.exe
```

也可以单独启动本地 HTTP 服务：

```powershell
.\build\Debug\recall-memoryd.exe --db .\recall-memory.db --port 47831
```

启动 MCP 服务：

```powershell
$env:RECALL_MEMORY_DB = ".\recall-memory.db"
.\build\Debug\recall-memory-mcp.exe
```

集成细节见[项目架构](docs/architecture.md)、[本地 HTTP 接口](docs/http-api.md)和[MCP 接入](docs/mcp.md)。

## 当前边界

首版的通用记忆与召回能力不依赖 Code Graph。C/C++ 图谱用于给编码任务增加结构关联信号，目前是语法级分析，不等同于编译器语义；后续可通过 Clang 或 LSP 适配器增强，不需要修改记忆模型。

后续 Coding 模式将作为可选能力配置：启用代码图谱和编码任务钩子，但继续共用通用记忆、信任生命周期和召回内核，具体设计见[项目架构](docs/architecture.md)。
