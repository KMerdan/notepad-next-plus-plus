# 运输配置 0.1 与当前验证边界

2026-10-11：Qt、Node 与 Python 原型端点已实现 JSON-RPC 2.0 消息语义及 Content-Length 分帧。这里冻结运输配置；当前 33 个业务方法的 Schema、状态／能力／取消契约与共享 fixtures 见 [业务协议契约](WireContract.zh-CN.md)。兼容仅限已验证的 0.1 profile，其他版本仍需独立验收；[Python](PythonAdapter.zh-CN.md) 使用开发者配置的解释器，尚未正式打包。

## 消息语义

请求、应答与通知使用 `jsonrpc: "2.0"`。params 只能省略、为对象或数组；应答必须只有 result 或 error 之一。合法通知不会收到应答，包括未知方法和处理失败。非空批量请求聚合成一个数组应答；只有通知的批次静默；空批次返回单个 Invalid Request。无效、重复、未知和迟到应答只做诊断，避免错误应答循环。

应用生成 `h:<UUID>` 或 `p:<UUID>` 字符串 ID。标准端点另外接受字符串、null 和数字 ID，并从原始 JSON token 回显，覆盖 `9007199254740993`、`1e400`、小数及转义的 id 属性名。业务数据按各自 Schema 约束数值范围；Qt 数值树以有限 IEEE-754 值与安全整数为互操作范围，不承诺任意精度业务数值。取消扩展 `nnp.request.cancel` 的 requestId 限定为应用生成的字符串 ID，不对转换后的数字模糊匹配。

## 预算

握手必须确认 [共享配置](../../packages/plugin-host/protocol-profile.json)。Qt 的 `profileLimits()` 与该 JSON 有一致性测试。

| 预算 | 数值 | 超限处理 |
| --- | --- | --- |
| JSON 帧 | 16 MiB | 关闭损坏的运输连接 |
| ASCII 头 | 8 KiB | 关闭连接；只接受 CRLF、唯一十进制 Content-Length 和 UTF-8 charset |
| 单批条目 | 64 | 在派发任何条目前触发协议故障 |
| 每方向 pending | 64 | 请求返回 Overloaded |
| JSON 结构嵌套 | 128 | 在派发前触发协议故障 |
| 原始 ID token | 1024 字节 | 在派发前触发协议故障 |
| 文档 UTF-8 快照 | 2 MiB | ContentTooLarge |
| 原生 XHTML fragment | 6 MiB | 拒绝发布；宿主生成的页面另有 8 MiB 上限 |
| Webview 应用消息 | 64 KiB | ContentTooLarge |
| 单插件持久化记录 / 单值 | 1 MiB / 64 KiB | ContentTooLarge |
| 每个数据命名空间 / 值嵌套 | 256 keys / 32 层 | Overloaded / ContentTooLarge |
| 发送队列 | 8 MiB 高水位 / 32 MiB 硬限 | 拒绝新增普通请求 / 关闭连接 |

64 条是运输限制，区别于 64 个 pending 的接纳预算。这修订了研究稿中任意大小批次逐项返回 Overloaded 的描述：大量短小无效条目的标准错误应答可能超过帧预算。预算内保持标准批次封装；聚合结果超限的只读结果替换为 ContentTooLarge，写确认保持小型结构。

Node 分帧器按已验证长度分配一次 body，直接复制分片，避免反复连接大 Buffer。Qt 分帧和 JSON 解析位于工作线程。取消或超时释放调用者的 pending、计时器与监听；超时不撤销已提交的副作用。连接关闭使所有 pending 失败。

命令的 5 秒执行计时在会话拥有消息提示框时暂停，最后一个关闭后恢复剩余预算；健康与退出计时不暂停。消息创建与选择分开确认，等待用户选择的 Promise 不占用业务创建请求，见 [VS Code 核心兼容配置](VSCodeCompatibility.zh-CN.md)。

## 原生原型

Node 宿主接受初始化、激活、命令调用、停用、健康请求和关闭。Qt 会话层校验 sessionId、generation、状态、能力、声明命令与资源所有权。标准允许通知；原生宿主的登记、读取和面板操作契约要求请求及确认，通知形式不执行这些业务操作。

原生页面使用宿主页面及 CSP，仅接受允许标签/属性构成的 XHTML fragment。Markdown 原始 HTML 被转义。图片通过 `nnp.webviews.mapResource` 登记不透明句柄，种类为 `package` 或 `documentImage`；后者限定当前文档目录内的 PNG/JPEG/WebP，拒绝目录穿越、根目录外 symlink 和网络图片。当前原型不开放插件自己的页面脚本。

`webviews.compat@1` 是另行授权的完整 HTML 配置，须同时授予 `webviews@1`，并使用 Qt WebEngine 6.6+ 的响应头 CSP。它允许有界本地 JS/CSS 及当前页的 state/message 桥，仍禁止任意网络与跨面板资源；详细语义与兼容限制见 [兼容配置](VSCodeCompatibility.zh-CN.md)。普通 Node 仍具有用户权限。OS 沙箱、签名发行、应用重启面板恢复与完整 VS Code 兼容尚未交付。

配置与数据的会话状态、持久化、预算、错误及卸载语义见 [插件数据契约](PluginData.zh-CN.md)。

## 验证入口

- `tests/plugin-rpc/fixtures.json`：Qt、Node 共用的 28 个消息输入。
- `PluginRpcTests.cpp`：分片、UTF-8、预算、异步批次、应答、超时/取消、真实 Node 反向调用、Unix 子进程回收及 1/5/10 进程测量。
- `rpc.test.cjs`：同一消息 fixtures、分片及 pending 管理。
- `host.test.cjs`：真实宿主激活/停用、激活失败回收、watch 应答前事件缓存。
- `tests/plugin-documents/PluginDocumentTests.cpp`：真实 Scintilla、UTF-16 边界、无效及截断 UTF-8、关闭失效、元数据/重置与原子编辑撤销。
- `tests/plugin-webviews/PluginWebviewTests.cpp`：真实页面、scheme、内容/图片边界、消息桥、大页面及 Qt↔Node Markdown 会话。
