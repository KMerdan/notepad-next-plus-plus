# 插件 SDK 开发入口

SDK 已拆至 [KMerdan/notepadnext-plugin-sdk](https://github.com/KMerdan/notepadnext-plugin-sdk)，主应用使用 `packages/plugin-sdk` Git submodule 固定 reviewed commit。SDK 0.2.1 同时提供 legacy API 0.1 与阶段 A 的 native API 0.2；包版本与 host API 协商独立。

- [SDK README](../../packages/plugin-sdk/README.md)：安装 tarball、创建模板、构建与打包。
- [原生 API 0.2](../../packages/plugin-sdk/docs/NativeAPI.zh-CN.md)：固定目标命令、选区事务和已有原生操作。
- [Markdown 完整流程](../../packages/plugin-sdk/docs/MarkdownWalkthrough.zh-CN.md)。
- [HTML/JS service](../../packages/plugin-sdk/docs/HtmlService.zh-CN.md) 与 [面板恢复](../../packages/plugin-sdk/docs/HotUpdate.zh-CN.md)。
- [三仓边界](../../packages/plugin-sdk/docs/RepositoryLayout.zh-CN.md) 与 [实施状态](NativeSDKStatus.zh-CN.md)。

SDK 自身没有 runtime npm 依赖或 install hooks。Node 24。四种模板为 hello、markdown、html-service、text-tools。生成项目可携带固定离线 SDK 快照；官方 Marketplace 使用固定 Release URL/lockfile，不复制 SDK 源码。

```sh
git submodule update --init --recursive
node tests/plugin-sdk/standalone.cjs build/plugin-sdk-validation-new
```

输出目录必须不存在。验证实际 npm file whitelist，安装独立 tarball，再生成、移动、安装依赖、构建/测试/打包模板。模拟宿主不验证 Qt、CSP、真实 undo 或权限；这些由应用原生测试承担。

本机开发无需 Developer ID、公证或软件发行。SDK/插件 GitHub 资产是作者开发包与插件安装包；主应用正式发行仍待以后。
