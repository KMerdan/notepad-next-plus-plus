# 官方插件 Marketplace

Notepad Next ++ 的第一方目录由 KMerdan 维护，注册 URL：

**https://github.com/KMerdan/notepadnext-plugin-marketplace**

它是本 fork 的官方目录，不代表上游 NotepadNext。应用 0.15.2、SDK 0.3.2、插件版本分别维护。

## 注册、安装与使用

在 Plugins → Plugin Manager → Add Source… 输入 URL，Git ref 留空。确认后资源进入 **Discover**；注册只读取 metadata，不安装或启动代码。默认产品目录包含：

- **Markdown Preview 0.1.8**：`kmerdan.markdown-preview`，渲染未保存的 Markdown 与受控的相对图片。
- **HTML Preview 0.1.4**：`kmerdan.html-preview`，Node 服务直接渲染未保存的 HTML/JS 与受控相对资源，不插入源码编辑框、计数器或进程信息。

分别选择 Install，审阅包和权限；安装后在 **Installed** 启用，再于匹配标签执行 Open。无需系统 Node/npm。Text Inspector 和 TODO Navigator 位于 Marketplace 的 `examples/`，标明为 SDK 开发示例；它们展示选区、原生搜索、定位、高亮等 API，不在默认产品目录内。

## 组件生命周期

预览由用户手动打开，每插件/窗口一个共享 dock。它跟随匹配的活动标签，分别保存各标签的浏览器状态与滚动；切到非匹配标签时隐藏。关闭某个标签后遵循编辑器选择的下一个活动标签。用户关闭 dock 或最后一个匹配标签关闭时释放面板和进程，插件仍保持 Installed/Enabled；以后切换或打开文件不会自动重开。

更新、Reload 和回滚只恢复仍存活的 dock、位置、源映射与有界的逐标签状态，不重放 Open。进程和服务 endpoint 重新建立；任意 JS heap 或 Node 内存不迁移。详细分类、限额和所有权见 [SDK 生命周期契约](https://github.com/KMerdan/notepadnext-plugin-sdk/blob/main/docs/PluginLifecycle.zh-CN.md)。

## 来源改名与更新

仓库从 `notepadnext-markdown-preview` 改名为 `notepadnext-plugin-marketplace`。宿主按来源隔离包与数据；旧来源的安装需要一次卸载并从新 URL 安装。卸载时可以保留原数据，但不会自动搬到新来源。之后统一从新 URL 注册、安装和更新。

根 metadata 的 `list` 是完整插件 metadata 数组，单插件的 `list` 是 `false`。插件各自发布 `<plugin-id>/v<version>`，Release 包含 ZIP 与 SHA-256 sidecar。安装/更新验证 ID、版本、来源、声明和文件。刷新目录后在 Installed 单独更新；失败恢复前一包。卸载不会删除目录资源，删除来源也不卸载已装插件。

## 开发与验证职责

Marketplace 锁定正式 SDK Release URL 与 npm integrity，不复制 SDK 源码。`npm run ci` 构建测试两个产品和两个开发示例，并验证默认目录只列产品。按插件 tag 的 CI 构建该包并生成 draft，验收实际 asset 后公开，历史资产不可覆盖。开发说明见 [Marketplace README](https://github.com/KMerdan/notepadnext-plugin-marketplace)。

业务渲染测试属于 Marketplace，类型/模板属于 SDK，方法和组件所有权契约属于主仓 CI。真实编辑器验收是可选外部消费者验证，不作为主仓 CI 的业务依赖。旧证据文件记录对应历史版本，当前行为以源码与上述合约为准。
