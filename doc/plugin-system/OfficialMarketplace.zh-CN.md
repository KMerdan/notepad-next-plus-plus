# 官方插件 Marketplace

Notepad Next ++ 的第一方插件由 KMerdan 在同一公开仓库维护：

**https://github.com/KMerdan/notepadnext-plugin-marketplace**

此 URL 从原 Markdown 插件仓库升级而来，保留地址以维持已安装插件的更新来源和按来源隔离的数据；它是本 fork 的官方目录，不代表上游 NotepadNext。GitHub 的仓库说明和 README 已改为官方 Marketplace。旧 Markdown Release 和 `kmerdan.markdown-preview` ID 保留。

## 注册和使用

使用本次构建的插件版开发应用，进入 Plugins → Plugin Manager → Add Source…，输入上述 URL，Git ref 留空。确认目录中两项元数据后，它们出现在 **Discover**。注册不安装、不启动插件。

- **Markdown Preview**：`kmerdan.markdown-preview`，当前版本 **0.1.6**。
- **HTML Preview**：`kmerdan.html-preview`，当前版本 **0.1.2**，承载本地 Node.js HTTP 服务、HTML、JavaScript 和同服务 fetch。

分别选择 Install，确认包与权限；安装完成后在 Installed 启用并执行对应 Open 命令。HTML 插件读取当前 HTML 语言的未保存缓冲区，也可以在预览内编辑源码并 Run preview。安装包包含服务 helper 和静态资源，不需用户另外 npm install 或手动启动服务器。

已有公开 Markdown 插件的用户只需刷新相同来源。原来本地示例的 `notepadnext.*` ID 与公开 `kmerdan.*` 插件不同，选择公开版本时可禁用旧示例。

## 独立发布和更新

统一 metadata 合约保持不变：根对象的 `list` 是完整插件元数据数组；各插件自身的 `list` 是 `false`。两个条目可以指向同一发布仓库，版本互相独立。

单插件旧版使用 `v<version>`。Marketplace 使用 **`<plugin-id>/v<version>`**，例如 `kmerdan.html-preview/v0.1.2`。每个 Release 内仍只有自己的 `notepadnext-plugin.zip` 和 `notepadnext-plugin-checksums.json`。安装器识别选中插件的 namespace，优先于同版本旧 tag；其他插件的 namespace 不进入候选列表。旧 tag 兼容保留。最终安装仍核对 manifest 的 ID、版本、来源、声明、实际文件和 SHA-256。

刷新目录后在 Installed 单独更新插件。HTML 更新不替换 Markdown 包、不重启主应用；通用热更新协议恢复支持 restorer 的已打开窗口，保留位置、滚动、预览源码和 JSON 状态，失败时恢复前一版本。Node 进程和服务 endpoint 会重新建立；任意后端内存不会迁移。

卸载某项只移除该插件，目录仍可再次安装；默认保留数据。删除来源不卸载现有插件。

## 仓库开发

仓库独立于 Qt 项目：`plugins/markdown-preview`、`plugins/html-preview`，使用独立 SDK **0.2.1** 的固定 Release URL/lockfile 和 `tools/marketplace.cjs`，不复制 SDK 源码。`npm run ci` 锁定安装、构建和测试两项，并确认根目录的完整条目等于插件 manifest。`npm run catalog` 同步目录；插件版本与 package.json 必须一致。

按插件 tag 的 GitHub Actions 只构建该插件，先生成 draft，核对实际 asset 后公开；禁止覆盖历史版本。SDK `nnp-release` 同时接受旧 tag 和该插件自己的 namespaced tag。

一次完整更新演示：Git ref 指定 `marketplace/v0.1.0` 时目录包含 Markdown 0.1.5、HTML 0.1.0；安装打开后注册默认分支，更新 HTML 到 0.1.1。常规使用不需要填写 ref。

验证记录见 [MarketplaceValidation.json](evidence/MarketplaceValidation.json)。主应用仍是本地开发构建，本次发布的是插件安装包。

本次三仓拆分及原生 API 0.3 的实现/证据见 [Native SDK 状态](NativeSDKPhaseBStatus.zh-CN.md)。当前四个官方插件均使用 native API 0.3 和正式 SDK 0.3.0 Release；原插件 ID/来源保持一致，旧 API profile 已移除。新工具 Text Inspector / TODO Navigator 也可从同一 URL 安装。
