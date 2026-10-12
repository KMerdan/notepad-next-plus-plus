# 独立插件 SDK 开发包

当前 SDK 0.1.0 位于 [packages/plugin-sdk](../../packages/plugin-sdk/README.md)，可通过 `npm pack --ignore-scripts` 生成独立 `.tgz`。包内携带 API 类型、hello/Markdown 模板、测试宿主、本地调试工具、Schema 与中文文档。SDK 本身没有运行时依赖或安装 hooks。

`nnp-create` 创建的项目自带 SDK，不再引用 NotepadNext 主仓库路径。模板包含 source maps、VS Code launch/attach 配置、行为测试和打包脚本。作者使用 Node 24；模板锁定 TypeScript 与 Markdown-it。第一次安装提交 lockfile，之后使用 locked install。

- [快速开始](../../packages/plugin-sdk/docs/GettingStarted.zh-CN.md)
- [Markdown：开发、本地调试、打包、安装与更新](../../packages/plugin-sdk/docs/MarkdownWalkthrough.zh-CN.md)
- [断点、日志与故障定位](../../packages/plugin-sdk/docs/Debugging.zh-CN.md)
- [API、能力和生命周期](../../packages/plugin-sdk/docs/API.zh-CN.md)

本地模拟宿主执行作者已构建的 CommonJS 插件。`nnp-debug --watch` 轮询编译结果与输入文件的变化，避免依赖系统 watcher 配额，并支持原子保存。它没有 Qt、RPC、CSP 和真实持久化校验；这些由原生应用集成检查负责。已安装插件是不可变包副本，Reload 重启安装版本；作者改源码需要重新打包、刷新来源并 Update。

主仓库可复现的独立包验证入口：

```sh
node tests/plugin-sdk/standalone.cjs build/plugin-sdk-validation-new
```

输出目录必须不存在。入口使用实际 SDK npm tarball 创建项目，将项目移动后安装依赖，执行 locked install、API 类型检查、两种模板的构建/测试/调试，以及安装目录、ZIP 和校验文件生成。可通过 `NNP_SDK_NPM_CACHE` 配置独立 npm 缓存；缓存齐备时 `NNP_SDK_OFFLINE=1` 可执行离线验证。

原生 Application 测试通过测试专用 `NNP_TEST_SDK_MARKDOWN_PROJECT` 指向生成项目，复用真实注册确认、本地目录选择、安装、启用、未保存 Markdown 预览与最后窗口退出路径。它使用临时配置，不写入用户插件记录。验证摘要见 [SDKDevelopmentValidation.json](evidence/SDKDevelopmentValidation.json)。

SDK 本身是本地开发包，不执行 npm 发布、Developer ID 配置或公证。2026-10-12，用户另行授权发布了使用它开发的独立 Markdown 插件；[公开仓库与真实安装更新验证](MacUiGitHub.zh-CN.md)记录完整流程。打包器现在检查命令图标是否符合原生 PNG profile，拒绝不兼容的 SVG 图标声明。
