# 本地 HTTP 接口

在 RecallMemory 界面点击“启动服务”后，内置服务默认监听 `127.0.0.1:47831`。请求和响应体均为 UTF-8 JSON，首版仅供本机访问。

## 健康检查

```http
GET /health
```

## 创建通用记忆空间

非编码 Agent 应先创建或取得一个记忆空间。同名调用会返回同一个 `workspaceId`，不需要索引任何目录。

```http
POST /v1/memory-spaces
Content-Type: application/json

{"name":"finance-operations"}
```

```json
{"workspaceId":"ms_1234","name":"finance-operations"}
```

## 建立 C/C++ Code Graph

此接口是可选编码适配器，不是通用记忆的前置步骤。

```http
POST /v1/workspaces/index
Content-Type: application/json

{"rootPath":"D:/work/project"}
```

响应包含 `workspaceId`、增量文件数量、图谱统计和逐文件错误。图谱状态接口如下：

```http
GET /v1/workspaces/{workspaceId}/graph/status
```

## 提交候选经验

```http
POST /v1/experiences/propose
Content-Type: application/json

{
  "workspaceId": "ws_1234",
  "kind": "operational_lesson",
  "title": "导出前确认报表周期",
  "trigger": "生成月度财务报告",
  "symptom": "错误使用了上一个月的数据",
  "rootCause": "Agent 推断了周期，没有读取已选择的参数",
  "invariant": "必须使用用户明确选择的报表周期",
  "fixSummary": "把导出参数绑定到已选择周期",
  "scopes": [
    {"kind": "workflow", "value": "monthly-report-export"},
    {"kind": "resource", "value": "finance-report"}
  ],
  "evidence": [{"type": "task-result", "uri": "local:run-1024"}],
  "verificationSteps": [{"command": "compare-report-period"}],
  "confidence": 0.9
}
```

经验类型支持 `task_outcome`、`operational_lesson`、`user_preference`、`domain_fact`、`bug_fix`、`architecture_decision`、`procedure`、`failed_approach` 和 `project_fact`。新经验固定为 `candidate` 状态。

## 验证经验

此操作只能暴露给可信桌面流程或受控调用方。

```http
POST /v1/experiences/{experienceId}/verify
Content-Type: application/json

{
  "evidence": [
    {"type": "task-result", "uri": "local:run-1024"},
    {"type": "user", "uri": "approval:local-user"}
  ]
}
```

## 召回经验

```http
POST /v1/recall
Content-Type: application/json

{
  "workspaceId": "ws_1234",
  "task": "导出已选择月份的财务报告",
  "agents": ["report-agent"],
  "contexts": ["finance"],
  "resources": ["finance-report"],
  "entities": ["monthly-report"],
  "workflows": ["monthly-report-export"],
  "scopes": [{"kind":"domain","value":"finance"}],
  "errors": [],
  "files": [],
  "symbols": [],
  "limit": 10
}
```

`files` 和 `symbols` 仅用于编码任务，可省略。其他字段均可独立用于非编码 Agent。

## 防重复检查

在 Agent 确定执行计划后和接受最终结果前各调用一次：

```http
POST /v1/guard
Content-Type: application/json

{
  "workspaceId": "ws_1234",
  "task": "交付月度财务报告",
  "diffSummary": "重新生成了报表",
  "changedAgents": ["report-agent"],
  "changedContexts": ["finance"],
  "changedResources": ["finance-report"],
  "changedEntities": ["monthly-report"],
  "changedWorkflows": ["monthly-report-export"],
  "changedScopes": [{"kind":"domain","value":"finance"}]
}
```

响应包含 `risk`、召回经验、必须保持的不变量、必要验证步骤和警告。RecallMemory 不执行验证步骤。

## 反馈与失效

```http
POST /v1/experiences/{experienceId}/feedback

{"value":"outdated","note":"业务流程已经在第三版替换"}
```

`value` 可取 `useful`、`outdated` 或 `incorrect`。后两种反馈会立即将经验标记为过时。也可以直接调用：

```http
POST /v1/experiences/{experienceId}/stale
```
