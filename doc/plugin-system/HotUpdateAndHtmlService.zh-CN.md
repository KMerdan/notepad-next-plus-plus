# 通用插件热更新与 HTML 服务

本次把恢复能力放在 PluginEngine / PluginSession / WebviewService 和语言适配层中。没有 Markdown 插件 ID 或普通菜单命令的特殊分支。

更新、Reload、手动回滚先冻结并保留原 QDockWidget，读取 browser state；随后停用旧进程，新进程通过 nnp.webviews.restore 的专用回调接管各个旧面板。新页面暂存到同一 dock 中，加载并恢复成功才确认包事务和替换内容。失败会恢复旧包；普通命令不会被重放。关闭的面板不再出现，初次激活中的未发布面板不进入恢复缓存。

恢复保留停靠、浮动位置和尺寸、tab 关系、可见性、滚动及有界的表单/应用状态。Node SDK 使用有期限的 AsyncLocalStorage 上下文传递 restorationKey；Python 使用 ContextVar，VS Code serializer 适配到同一契约。回调结束后，后台 HTTP 请求及后续任务不再携带旧面板 key。

JSON 状态最多 64 KiB/面板；浏览器快照最多 128 KiB。密码和文件输入不复制，超限表单字段跳过以避免截断覆盖应用 state。任意 JavaScript 堆、计时器和服务器内存不自动迁移。旧插件必须接入恢复契约；旧版自身也不能恢复时，原面板保留但禁用，管理器可停用清理。

HTML Preview 使用插件所属 Node 进程的本地 HTTP 服务。宿主验证 IPv4 loopback URL 的随机路径、当前 sessionId 和 nonce，禁止重定向，探测限 2 秒/4 KiB。WebEngine 限制页面只访问自己的服务前缀；共享服务按面板引用计数，关闭一个面板不会影响其他面板。停用/更新结束旧服务，新进程的端口可变化。SDK 模板支持 HTML/JS 预览、读取未保存的 HTML buffer、服务 API 和序列化客户端状态。

公开 Markdown 插件 [v0.1.4](https://github.com/KMerdan/notepadnext-markdown-preview/releases/tag/v0.1.4) 已从 CI 包发布，并在真实管理器中由 v0.1.3 更新验证。HTML 示例及 SDK 开发包为本地开发交付，没有发布主应用或进行 Developer ID/公证。

- [SDK 热更新契约](../../packages/plugin-sdk/docs/HotUpdate.zh-CN.md)
- [HTML 服务开发流程](../../packages/plugin-sdk/docs/HtmlService.zh-CN.md)
- [HTML 示例](../../examples/html-preview/README.md)
- [验证记录](evidence/HotUpdateValidation.json)

Figma 的更新确认文字已同步为自动恢复；真实 HTML 服务截图在 build/plugin-hot-update-review。验证包含真实 GitHub 更新、浮动位置/滚动、长输入、同类型多个面板、共享服务、失败回滚、不重放命令、关闭后 Reload、初始激活期间更新及退出。完整 WebEngine 回归包括 100 次生命周期循环；后续修改用针对性测试再次验证。Linux/Windows 的原生验收仍由跨平台工作流执行，不能把本机结果当作这些平台的完成证明。
