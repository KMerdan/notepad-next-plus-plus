# 同仓库多插件发布

统一 metadata 的根对象 `list` 为完整插件 metadata 数组时，它是 Marketplace；每个安装包的 `list` 仍为 `false`。注册目录只发现条目，安装和启用由用户分别选择。

多个插件可共享 GitHub 仓库 URL。它们必须保留不同 `publisher.name` ID，分别使用 **`<plugin-id>/v<version>`** tag：

```sh
nnp-release ./plugins/html-preview ../html-assets kmerdan.html-preview/v0.1.1
```

工具检查 tag 的 ID 和版本与所选插件一致，再从已构建文件生成 ZIP 和校验 JSON。不能把整个 Marketplace 根目录作为插件安装包。单插件旧 `v<version>` 仍支持。

每个插件的 Release 含 `notepadnext-plugin.zip` 与 `notepadnext-plugin-checksums.json`。相同资产名位于不同 Release 内，互不覆盖。原生主应用需支持 namespaced Release：按选中插件 ID/目录版本筛选，再验证实际包内容和完整性。不同插件的版本可相同，也可独立递增。

保持根 list 中的版本、权限和 commands 与对应包 manifest 完全相同，尽量用生成与 CI 检查防止漂移；发布新版本时更新目录并生成对应独立 Release。每个更新都保留自身 ID 和 repository 来源，支持窗口恢复的插件遵循 [HotUpdate](HotUpdate.zh-CN.md)。

实际示例：[Notepad Next ++ Official Plugins](https://github.com/KMerdan/notepadnext-markdown-preview)。仓库包含 Markdown、HTML 服务插件，独立 lockfile、本地开发文档和 tag 驱动 CI。此地址沿用原 Markdown 仓库，因此旧安装可以刷新同一来源升级为目录。
