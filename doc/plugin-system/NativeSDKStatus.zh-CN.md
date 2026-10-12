# 原生 SDK 实施状态

**2026-10-12 的验证快照。** 此文件定义本次阶段 A 的范围；完整设计的 B/C 仍待实施。实际公开契约由 SDK `native.d.ts` / `schemas` 和应用测试验证，设计目录中的候选声明不是可安装 API。

已实现 native host API **0.2.0**，同时保留 **0.1.0** 会话。SDK **0.2.1** 的根类型提供旧 API，`./native` 提供新 API。JSON-RPC 2.0、wire profile 0.1、manifestVersion 1 保持不变。Python prototype 仍为 API 0.1；0.2 首批开发入口是 TS/JS。

| 阶段 A | 验证/边界 |
| --- | --- |
| 文档 list/getInfo | 丰富 metadata；旧 getActive/getSnapshot/watch 保留原契约 |
| 固定目标命令 | click 时捕获 window/editor/document，包括首次 activation 之前；新独立 invoke method，不改旧 args；包含 API 0.1 的宽泛旧 range 优先保留 0.1 |
| 原子 capture、选区 guard、setSelections | UTF-16、方向、多选区、mode、virtual space；setter 首版 stream、零 virtual space；全部范围预检 |
| 八项共享原生命令 | undo/redo、upper/lower、comment toggle、deduplicate、sort、fold；native menu 与 SDK 共用 EditorOperations |
| 原生 HTML 命名 | createHtml、acquireNotepadNextApi；旧名字保留迁移别名；包内资源、宿主 CSP、owned state/message |
| host/error/stopping/log facade | 协商 grant 查询；写取消/断线/超时 unknown outcome 不重试；deactivate 前 abort |
| 三仓分工 | app 固定 SDK submodule；SDK 自己维护 API/模板；Marketplace 固定 SDK tarball/lockfile |

本次验证：SDK 21 unit tests；实际安装 tarball 的 29 项独立步骤（四模板构建/测试/打包及 native 本地调试）；应用 Node/Python/wire/实验回归；C++ RPC/Document/GitHubRelease 测试；真实 macOS native case 及公开 GitHub 管理器生命周期（3 cases + init/cleanup，5 pass）。具体验证记录见 [NativeSDKValidation.json](evidence/NativeSDKValidation.json)。

真实 native case 覆盖：0.2 协商、文档列表/metadata、selection setter、首次 activation 前换 tab、Unicode/CRLF 多选区一次 undo、selection-only conflict 不写入、原生去重和 readonly 拒绝、package-local JS/CSP/new browser bridge、uninstall 清理且主窗口不变。SDK test host 明确不能替代真实 core 和 WebEngine 验证。

现有限制：单宿主窗口；2 MiB UTF-8 snapshot/edit preflight 和 aggregate selected text、12 MiB encoded capture、256 edits/selections；无 shared-buffer 克隆视图；未实现持久 operationId/跨会话写去重、readRange 或统一新事件格式。文件 save/close/search、语言、书签、插件装饰、导出、宏、providers/context menu 属于 B/C，不在正式 native types 中。可信 Node 可访问 OS filesystem/network，RPC capabilities 不是 OS sandbox。

Marketplace 的 Markdown/HTML 保留 ID、来源和 0.1 profile，使用新版 SDK 构建工具并验证通用 hot update。开发包/插件 Release 与主应用正式签名发行分开；Developer ID、公证和应用发行仍不属于本次工作。

公开 URL 完整流程再次验证：Markdown 0.1.3 → 0.1.6；HTML 0.1.0 → 0.1.2 更新保持 dock/geometry、draft、counter、scroll，并更换服务 PID；Markdown 包不被 HTML 更新替换。卸载回到 Discover，主窗口 ID 保持。SDK 与插件的 CI 资产逐字节等于本机安装/构建验证过的包。
