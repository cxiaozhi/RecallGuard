# MCP 接入

RecallMemory 不发布独立 MCP 程序。启动 GUI 内置服务后，Agent 通过 Streamable HTTP 连接：

```text
http://127.0.0.1:47831/mcp
```

桌面界面提供“复制 MCP 地址”按钮。MCP、HTTP、SQLite 和 Code Graph 全部运行在同一个 `RecallMemory.exe` 进程中。

## 工具

| 工具 | 用途 |
| --- | --- |
| `recall_memory_create_memory_space` | 创建或取得不依赖代码目录的通用记忆空间 |
| `recall_memory_recall` | 按任务、上下文、资源、实体、流程或代码信号召回已验证经验 |
| `recall_memory_guard` | 检查计划或结果是否可能重复历史错误 |
| `recall_memory_propose_experience` | 创建尚未受信的候选经验 |
| `recall_memory_feedback` | 标记召回结果有用、过时或错误 |
| `recall_memory_index_workspace` | 可选：增量索引 C/C++ 文件 |
| `recall_memory_graph_status` | 可选：查看代码图谱覆盖情况和未解析边 |

MCP 有意不提供验证工具。Agent 不能把自己提交的记忆提升为已验证知识。

## 推荐工作流

1. 首次使用时创建一个通用记忆空间，保存返回的 `workspaceId`。
2. 开始任务前，用任务描述和已知上下文召回相关经验。
3. 读取召回原因，把已验证经验视为约束和检查线索，而不是替代当前事实来源。
4. 确定执行计划后运行防重复检查。
5. 完成任务后再次按实际涉及的资源、实体、流程或代码范围检查。
6. 通过 Agent 原有的权限系统执行必要验证。
7. 只有任务结果得到证据支持后，才提交新的候选经验。
8. 由可信用户或桌面审核流程决定是否将候选经验提升为已验证状态。

编码 Agent 可以在首次使用或代码变化后建立 Code Graph，并传入 `files`、`symbols`、`changedFiles` 和 `changedSymbols` 获得图谱邻接召回。非编码 Agent 不需要执行索引步骤。

内置服务使用无状态 Streamable HTTP：JSON-RPC 请求通过 `POST /mcp` 发送并直接返回 JSON；通知返回 HTTP 202。服务只允许本机 Origin，且不提供独立命令行入口。
