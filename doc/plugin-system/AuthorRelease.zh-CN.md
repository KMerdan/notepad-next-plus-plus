# 插件作者发行流程

用户注册仓库或 Marketplace 时只读取 metadata。可安装发行包必须与注册条目的 metadata 一致，使用匹配 stable 版本的 Release tag；刷新来源后用户再显式选择安装或更新。本文描述当前原生 Node／TS profile。

## 准备独立仓库

使用 [独立 SDK 开发包](../../packages/plugin-sdk/README.md) 的 `nnp-create --template markdown` 创建项目，工具自动生成完整源码、metadata、测试、调试配置和 `tools/plugin-sdk/`。开发包尚未发布到 npm；SDK 副本通过 `file:./tools/plugin-sdk` 引用，不需要主仓库的 `../../packages/` 路径。修改插件 ID、名称、命令和实际仓库信息，使用固定 Node 24 安装并提交 `package-lock.json`。从创建、本地断点调试到打包安装的完整步骤见 [Markdown 开发演示](../../packages/plugin-sdk/docs/MarkdownWalkthrough.zh-CN.md)。

依赖安装使用 `npm ci --ignore-scripts`。作者显式执行 `npm run build` 和 `npm test`，然后从已经构建的文件准备发行资产：

```sh
node tools/plugin-sdk/release.cjs . /tmp/my-plugin-release v0.1.0
```

输出目录必须不存在，且位于源码之外。工具检查 ID、stable SemVer、tag 与 metadata/package 版本，打包 `dist/`、资源、实际生产依赖及许可证，生成 ZIP 和校验文件，再发布完成的本地目录。它不导入插件入口、不安装依赖、不运行 hooks；作者的 build/test 步骤会执行作者代码。

将[发行工作流模板](templates/github-release.yml)复制到独立仓库的 `.github/workflows/plugin-release.yml`。push 一个与版本匹配的 `v*` tag 后，工作流使用固定 Node 24.21.0，构建／测试，再上传两份发行资产并创建 **草稿**。作者检查资产和 Release 后自行发布；草稿不会出现在管理器安装列表。工作流不会重写或覆盖现有 Release。模板中的 checkout/setup-node 固定到已核查的提交：[checkout v6.0.2](https://github.com/actions/checkout/releases/tag/v6.0.2)、[setup-node v6.2.0](https://github.com/actions/setup-node/releases/tag/v6.2.0)。远程 Actions 执行仍需在实际仓库验收。

## 下载校验契约

支持 `notepadnext-plugin.zip`，或当前平台的 `notepadnext-plugin-<macos|windows|linux>-<x64|arm64>.zip`；同 Release 优先当前平台资产。GitHub asset 的非空 `digest` 必须为小写 `sha256:<64 hex>`；已提供的 digest 无效或不匹配时拒绝下载，不降级使用其他校验。[GitHub Release asset API](https://docs.github.com/en/rest/releases/assets?apiVersion=2022-11-28)

GitHub 未提供 digest 时，必须同时上传 `notepadnext-plugin-checksums.json`，结构如下：

```json
{
  "schemaVersion": 1,
  "pluginId": "author.sample",
  "version": "0.1.0",
  "assets": [
    {
      "name": "notepadnext-plugin.zip",
      "size": 12345,
      "sha256": "0000000000000000000000000000000000000000000000000000000000000000"
    }
  ]
}
```

示例中的大小与 hash 是占位，必须由实际 ZIP 生成。[结构 Schema](release-checksums.schema.json)记录静态约束。根对象只有上述四个字段；条目只有 name、size、sha256。最多 16 条、不重复的支持文件名，正整数大小不超过 128 MiB，SHA-256 为小写 64 hex。插件 ID／版本必须与用户已注册的条目一致。校验文件最多 64 KiB，并且是同一经过检查的 Release 中唯一命名为 `notepadnext-plugin-checksums.json` 的 uploaded asset。

管理器先按该校验 asset ID 请求内容；如果 GitHub 提供了校验文件自己的 digest，也核对它。验证内容结构与 ZIP 的名称／大小后，再按用户选择的 ZIP asset ID 下载并核对 SHA-256。两步共用 120 秒期限、各最多五次 HTTPS 白名单重定向；取消、错误、超限或不匹配移除未完成下载，不改变安装索引。不读取 metadata 中的任意下载 URL。

无 GitHub digest 且无作者校验的包不进入安装列表。安装 provenance 保存 Release/tag/asset ID、实际 archive hash、上游 hash、校验来源和作者校验 asset ID。SHA-256 证明下载与来源声明一致，来源仍由用户选择的 GitHub 仓库确定；它不提供发布者数字签名或 OS 隔离。

## 已执行的验证

Qt 模拟网络测试覆盖优先 GitHub digest、作者校验重定向、插件／版本／文件名／大小／hash 不符、重复和额外字段、超限、HTTP／外域跳转、404／限流、取消、伪造选择和实际安装。SDK 测试从入口会抛异常的包生成实际 ZIP，独立核对字节大小与 hash，验证工具不执行入口、错误 tag 不发布目录、失败清理和禁止覆盖。

实际公开仓库注册／下载、远程 CI 和干净系统部署仍在完整审计的未完成项中。不能用模拟请求或本地 ZIP 通过替代这些证明。

已在独立临时目录复制上述仓库布局，并通过实际的 lockfile 生成、`npm ci --ignore-scripts`、TS build/test 和 release 工具；metadata 与生成的校验文件另经 Draft 2020-12 validator 检查。首次尝试 offline 安装时缓存只有包信息、缺少 tarball，随后按 lockfile integrity 下载完成。记录与锁文件见 `evidence/AuthorReleaseValidation.json`。这证明当前本机目录／依赖／工具路径可复现，远程 Linux runner 执行仍待验收。
