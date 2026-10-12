# 调试与故障定位

## 模拟宿主

`npm run debug` 执行已经构建的 CommonJS 入口，用 `demo.md` 提供模拟未保存文档，并输出 HTML。`npm run dev` 监听 dist 和输入；源码需要由另一个终端的 `npm run watch` 编译。

在 VS Code 启动 **SDK simulated host**，或运行 `npm run debug:break` 并 Attach。source maps 已开启，可以在 TypeScript 源码打断点。调试器仅监听本机；停止时调用 deactivation。这个调试器运行在作者自己的进程中，不接管已安装插件进程。

测试可以从 `@notepadnext/plugin-sdk/testing` 导入 `createTestHost()`。模板测试演示 activate、invoke、setDocument、面板关闭重开和 dispose。单文档测试宿主支持 commands、messages、普通 Webview、文档快照/事件和内存 storage/configuration。applyEdits、watchAll、兼容 Webview 和资源映射会明确报 unsupported。它不模拟 native 句柄、权限、CSP、RPC 编码、真实持久化、配额或 OS 沙箱。

## 真实编辑器

构建 → 本地打包 → 注册资源 → 显式安装 → 启用 → 运行。在 Installed 中使用 **Inspect** 和 **Logs** 查看入口、运行状态与日志。console.info/warn/error 由宿主收集；不要在日志中打印凭据或大段文档内容。

真实编辑器使用已安装的不可变包副本。More → Reload 会重启该副本；它不会读取作者目录新编译的 dist。改源码后提高版本、重新打包、刷新 Sources 并 Update；完整步骤见 [Markdown 演示](MarkdownWalkthrough.zh-CN.md)。

## 常见问题

| 现象 | 检查 |
| --- | --- |
| `tsc` / `nnp-debug` 找不到 | 在生成项目中先安装依赖，使用 Node 24 |
| debug 找不到 dist/index.js | 先执行 npm run build；dev 本身不编译 TS |
| 注册后没有运行 | 注册只产生 Discover 条目；安装、启用并执行命令 |
| 安装找不到入口 / metadata 不匹配 | 选择 nnp-package 的输出目录，metadata 和 package 版本一致 |
| 改代码后 Reload 仍是旧行为 | 已安装副本未更新；提高版本、Sources → Refresh、Update |
| 普通 HTML 在本地正常但编辑器拒绝 | 原生 setHtml 接受允许的 XHTML 片段；插件脚本和样式不属于该接口 |
| 图片变成占位 | 模拟器不映射图片；真实宿主只允许符合规则的本地资源 |
| 文档事件期间出现旧预览 | 取消旧任务，读取最新快照，setHtml 带 documentId/version |
| 创建面板 / 写数据在 activate 报错 | 在已激活的命令或事件中操作；activate 只注册命令和准备逻辑 |
| 更新按钮不可用 | 来源中未注册更高版本，先刷新正确的本地 metadata 来源 |

本地调试完成后，至少验证未保存内容、切换文档、关闭重开、禁用、Reload 和退出的清理行为。
