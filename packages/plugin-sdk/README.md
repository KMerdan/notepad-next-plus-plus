# @notepadnext/plugin-sdk

NotepadNext 插件开发包 **0.1.1**。包含 TypeScript API、插件模板、内存测试宿主、本地调试、metadata Schema 和打包工具。开发者使用 **Node 24**，无需在插件项目里编译 Qt/C++。宿主通过 `activate(context)` 注入运行时 API；SDK 根入口用于 `import type`，不需要插件自己实现 JSON-RPC。

SDK 可打成独立 npm 格式的 `.tgz`，在主仓库之外使用。当前采用本地开发包分发。

## 从独立包创建项目

将 SDK tgz 放到一个开发目录，解包并创建 Markdown 项目：

```sh
tar -xzf notepadnext-plugin-sdk-0.1.1.tgz
node ./package/create.cjs ./markdown-demo --template markdown --id demo.markdown-preview --name "Markdown Preview Demo"
cd markdown-demo
npm install --ignore-scripts
npm run build
npm test
npm run debug
```

项目携带 `tools/plugin-sdk/`，devDependency 指向自己的 SDK 副本。安装时不依赖 NotepadNext 仓库，也不需要尚未发布的 npm SDK。第一次安装生成 lockfile，后续使用 `npm ci --ignore-scripts`。

打开 `.debug/preview.html` 查看本地输出。模板还有 `hello`，用于最小命令与消息窗口插件。

## 工具

| 命令 | 用途 |
| --- | --- |
| `nnp-create <新目录> --template hello\|markdown\|html-service --id publisher.name` | 创建独立项目；不自动安装或运行代码 |
| `nnp-debug <项目> --input demo.md --output preview.html [--watch]` | 用模拟宿主执行编译后的 CommonJS 插件 |
| `nnp-package <项目> <新输出目录>` | 从构建结果生成本地可安装目录 |
| `nnp-archive <安装目录> <新 ZIP 文件>` | 生成安装器使用的 ZIP 格式 |
| `nnp-release <项目> <新输出目录> [v0.1.0]` | 生成插件 ZIP 和 SHA-256 校验文件 |
| `nnp-import-vscode` | 迁移已有适配范围内的 VS Code 插件；不表示完整兼容 |

在生成项目里通过 npm scripts 使用这些工具，或用 `node tools/plugin-sdk/<工具>.cjs`。SDK `.tgz` 用于开发依赖；插件 `.zip` 用于分发；本地 Plugin Manager 当前选择 `nnp-package` 生成的目录。

## 文档

- [快速开始与项目结构](docs/GettingStarted.zh-CN.md)
- [Markdown：开发 → 本地调试 → 打包 → 安装 → 更新](docs/MarkdownWalkthrough.zh-CN.md)
- [调试、断点、日志与故障定位](docs/Debugging.zh-CN.md)
- [通用热更新与窗口恢复](docs/HotUpdate.zh-CN.md)
- [本地 Node.js HTML 服务插件](docs/HtmlService.zh-CN.md)
- [API、生命周期与 metadata](docs/API.zh-CN.md)
- [API 类型](index.d.ts) / [测试宿主类型](testing.d.ts)

模拟宿主支持模板使用的单文档、命令、消息、普通 Webview 和内存数据。它不验证 Qt 渲染、RPC、CSP、权限、配额或进程清理；这些必须在真实编辑器中验证。Python 插件已有独立宿主适配器，本文的模板与本地调试工具面向 Node/TS/JS。

## 在主仓库生成 SDK 开发包

从仓库根目录执行，输出目录需先创建：

```sh
mkdir -p build/plugin-sdk
cd packages/plugin-sdk
npm test
npm pack --ignore-scripts --pack-destination ../../build/plugin-sdk
```

生成 `build/plugin-sdk/notepadnext-plugin-sdk-0.1.1.tgz`。`files` 白名单携带模板、Schema 和文档，排除仓库、测试、构建物及 node_modules。SDK 本身没有运行时 npm 依赖和安装 hooks。

### Visual identity

Set `"displayIcon": "markdown"` for the host-owned Markdown mark, or `"displayIcon": "plugin"` for the generic plugin mark. This optional metadata field works in Discover before installation and activation. Existing SDK Markdown packages are recognized by their `markdown-preview.png` command marker for compatibility. Other package-relative PNG command icons are still validated before use.
