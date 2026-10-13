# Native SDK Phase B 实施状态

**2026-10-13，Phase B 开发分支。** 宿主已实现 native API **0.3.0**，保留 **0.2.0 / 0.1.0**；SDK 固定到 **0.3.0 / 69393865b9fafb2d724014174ffbbcf162e95b37**。这是开发分支的实现状态，分支 CI 与合入结果以 GitHub checks 为准；SDK 的正式 Release 与 main 合入分开进行。

目的仍是增强现有 Qt/Scintilla 编辑器。不引入 VS Code API 兼容目标，不公开 Qt pointers、任意 Scintilla 消息或 Lua 执行。

| 批次 | 实现 | 对应源码 |
| --- | --- | --- |
| B1 | 真正有界的 readRange；丰富 native 事件与选区订阅；定位/激活；有效编辑选项 | [DocumentBridgePhaseB](../../src/plugins/documents/DocumentBridgePhaseB.cpp)、[DocumentBridge](../../src/plugins/documents/DocumentBridge.cpp) |
| B2 | create/open/save/saveAs/close；固定 ID/version；类型化取消/跳过；弹窗期间取消与版本再验证；最后文档关闭保留窗口 | [FileOperations](../../src/FileOperations.cpp)、[NativeEditorServices](../../src/NativeEditorServices.cpp) |
| B3 | 字面量搜索/计数、分页、取消、搜索状态隔离；10 项新增共享命令，目录共 18 项 | [SearchService](../../src/SearchService.h)、[EditorOperations](../../src/EditorOperations.cpp)、[RPC dispatch](../../src/plugins/runtime/PluginSession.cpp) |
| B4 | 稳定语言 ID/显示名、关键词、有效缩进、EOL 转换、用户书签增删；metadata 刷新 UI | [NativeEditorServices](../../src/NativeEditorServices.cpp)、[shared buffer operations](../../src/EditorBufferOperations.h)、[MainWindow](../../src/dialogs/MainWindow.cpp) |
| B5 | owner/session/document 高亮与下划线；配额、预检、释放、耗尽处理、句柄失效事件；更新时验证后发布 | [NativeEditorServices](../../src/NativeEditorServices.cpp)、[RangeAllocator](../../src/RangeAllocator.cpp)、[session lifecycle](../../src/plugins/runtime/PluginSession.cpp) |

正式类型和用法由独立 SDK 的 [native.d.ts](../../packages/plugin-sdk/native.d.ts)、[Phase B 指南](../../packages/plugin-sdk/docs/PhaseB.zh-CN.md)维护。应用中的 wire/schema 文档副本由 SDK 同步，不能独立编辑。

## 契约边界

- 传输继续 JSON-RPC 2.0 / wire profile 0.1 / metadata manifestVersion 1。0.3 是明确 opt-in；接受 0.1 的宽泛旧 range 保留旧回调。0.2 仍只返回原有 8 项 core commands；Python prototype 仍为 0.1。
- 位置为 UTF-16、end exclusive；readRange 最多 256 KiB，位置所在单行也有 256 KiB 预算。超过 2 MiB 的文档可读小范围，全文 snapshot / edits preflight 仍为 2 MiB。native limits 单独查询，旧 initialize limits 未改变。
- native 事件包含丰富 metadata，可合并最终状态；初始快照与 eventSeq 交接、reset、dispose 和会话释放明确。选区事件按 editorId 订阅，双方都需要 document read grant；旧 watch/watchAll/delta 类型不变。
- reveal 不改选区、不抢焦点；activate 显式控制焦点。文件对话框后的 guard 检查固定目标、版本与存活会话；原生保存钩子允许改变正文，返回保存后的 version。首版插件 open 最多 2 MiB。
- 文件返回 completed/cancelled/skipped，失败为结构化错误。IO、断线、超时可有 unknown outcome，不自动重试。文件交互请求默认等待预算为 10 分钟，显式 AbortSignal / stopping 可中止等待并关闭失效对话框。
- 搜索仅字面量，使用原生规则；64 KiB 扫描窗口加边界 overlap，短批次后让出 GUI 线程，不改用户搜索 target/flags。结果最多 1000，带 version/complete/next；count 可截断。替换仍用 guarded edits。
- 语言 ID 沿用当前公开映射，例如 text/html。原生 Lua 语言名称是显示/内部名称，语言切换保留 EditorConfig 设置优先级。EOL text conversion 可一次 undo；输入 EOL mode 是独立视图状态，不随 text undo 自动恢复。
- 书签是用户资源，插件停用不清除；临时结果用 decorations。每 owner 最多 8 styles，每 style 最多 1024 ranges，位置映射也有 aggregate budget。资源不跨会话复用；清理不改用户的 current indicator/value 或内置 mark slots。
- 装饰可在 activation/restoration 准备，只有 validated publication 后才画入编辑器；版本已变化时不发布旧 ranges。文档关闭会释放资源并通知所属客户端，客户端丢弃失效句柄。取消 acquisition 时先接收创建结果并释放，避免活动会话中遗留不可访问的 style。

## 分仓与验证

主仓测试只使用通用 API/生命周期 fixtures，不构建 Markdown、HTML 或新示例插件。SDK 自己验证类型、工具与六模板；Marketplace 自己验证业务、metadata list 和独立包。

本地验证已完成：SDK **26 unit tests**；实际 tarball **40 独立步骤**（六模板安装/构建/测试/打包，支持的模拟调试）；C++ RPC **311 passed**；文档桥 **25 passed**；真实应用 Phase B **3 cases + init/cleanup，5 passed / 0 failed / 0 skipped**。真实应用用例覆盖新协议、文件成功/取消/版本变化/停用、嵌入 NUL 文本、书签、两个 owner 高亮隔离、100 次释放循环、配额与最后标签关闭。后续源码修改需复核，不把此快照替代分支 CI。

宿主 **22 suites 最终均通过**：Windows 搜索位置宽度修复后的完整运行通过 21/22；唯一失败是共享 provider 的管道总数断言，调整其验证范围后完整 Webview suite 复跑 **45 passed / 0 failed / 4 optional skips**。覆盖旧 0.1/0.2、通用 WebEngine 热更新、应用退出与 macOS bundle。禁用插件的编辑器构建通过。全量测试未配置 Python、外部产品包和 live GitHub，因此 Webview/App 分别有 4/5 个明确 opt-in skips；两个新 Marketplace 插件另行真实安装验收 **3 passed / 0 failed / 0 skipped**。

100 次启用/重载/停用确认 **200 个 owned Node identities 已退出**，每次停用后 session/panel/Dock/resources/focus connections 清零。原始句柄 **38 → 44**，变化全为 Qt provider 的事件管道；非管道类型均回到 warm baseline。未修改 main 基线也出现延迟 pipe 增长，pipe/close 栈诊断确认 RPC worker 初始化的管道全部关闭。最终测试不约束 Qt 全局线程池、不要求共享 provider 的管道总数等于 cycle 3；原始 pipe/threads/RSS 仍完整记录，不能将这些全进程指标冒充单个插件的所有权证明。验证输入、限制与原始样本见 [验证记录](evidence/NativeSDKPhaseBValidation.json)和 [生命周期原始记录](evidence/NativeSDKPhaseBLifecycle.macos.json)。

共享搜索服务采用完整宽度的 Scintilla range，避免 Windows LLP64 对旧 32 位 range 的隐式窄化。旧 Finder carrier 保留明确转换；原生 API 本轮搜索仍有 2 MiB 预算。

规划基线和后续项见 [Phase B 计划](NativeSDKPhaseBPlan.zh-CN.md)。分屏、导出/剪贴板、宏、编码转换、列编辑、大文件写入和 providers 属于计划中的后续/条件项，本轮 B1–B5 未开放。SDK 包的具体版本和原生 API profile 独立演进。

有效缩进 getter 将 Scintilla 的 `indent=0` 哨兵解析成 tab width；以实际 Tab 操作和显式 indent 验证。修正后文档桥 **25 passed**，旧 0.2 与 Phase B 应用复核 **6 passed / 0 failed / 0 skipped**。
