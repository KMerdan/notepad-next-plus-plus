# NotepadNext 插件项目

本项目自带 `tools/plugin-sdk/`，可以移出 NotepadNext 仓库单独开发。需要 Node 24。

```sh
npm install --ignore-scripts
npm run build
npm test
npm run debug
```

首次安装后提交 `package-lock.json`；后续使用 `npm ci --ignore-scripts`。打开 `.debug/preview.html` 查看模拟宿主输出。修改 `demo.md` 模拟未保存缓冲区；在两个终端分别运行 `npm run watch` 和 `npm run dev`，编译后的代码变化会重新激活插件，输入变化会发送文档事件。

断点调试使用 `npm run debug:break`，或 `.vscode/launch.json` 的 **SDK simulated host**。模板已经开启 source maps。模拟宿主不代表 Qt/RPC/CSP 验证；在真实编辑器中还要注册资源、安装构建包并启用。

```sh
npm run build
npm test
npm run package -- ../artifacts/plugin-0.1.0
npm run bundle -- ../artifacts/plugin-0.1.0-zip v0.1.0
```

输出目录必须是源码之外的新目录。Plugin Manager 的本地入口选择 `package` 命令生成的目录，ZIP 用于 GitHub Release 分发。

开发文档：

- [快速开始](tools/plugin-sdk/docs/GettingStarted.zh-CN.md)
- [Markdown 完整演示](tools/plugin-sdk/docs/MarkdownWalkthrough.zh-CN.md)
- [调试与更新](tools/plugin-sdk/docs/Debugging.zh-CN.md)
- [API 与 metadata](tools/plugin-sdk/docs/API.zh-CN.md)

`activate(context)` 注册命令；命令回调在用户打开插件时执行。`deactivate()` 清理面板、订阅和定时器。修改安装后的代码需要提高两个 JSON 文件里的插件版本、重新打包、刷新 Sources，再执行 Update。More → Reload 只会重启已经安装的代码。
