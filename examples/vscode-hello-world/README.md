# 官方 HelloWorld 兼容性样例

`src/extension.ts`、`package.json` 与 `LICENSE` 是 Microsoft `vscode-extension-samples` 提交 `65c1c44800eeac6fb065a54823ee5ae62c79540a` 的原文件，没有修改。`provenance.json` 记录 SHA-256。API 声明固定为 VS Code 1.100.0，保留其 MIT 许可证。

本项目的 `build.cjs` 使用已锁定的 TypeScript 5.9.3 编译源文件，不调用上游 npm scripts。先完成 Markdown 样例的锁定开发依赖安装，然后运行：

```sh
node examples/vscode-hello-world/build.cjs
node packages/plugin-sdk/import-vscode.cjs examples/vscode-hello-world /tmp/hello-world-package
```

导入目录是可检查的 NotepadNext 插件包，可以在 Plugin Manager 中从本地目录安装。安装后默认停用；启用后点击 Plugins 菜单中的 Hello World，显示由 Qt 管理的提示框。导入只读取已编译代码，不安装依赖、不运行插件或 npm hooks。

这验证命令与消息路径。编译时使用完整的上游类型声明不代表支持全部 VS Code API；实际支持范围见 `doc/plugin-system/VSCodeCompatibility.zh-CN.md`。Webview 兼容样例和跨插件调用仍需独立验收。
