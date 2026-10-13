**Host integration fixture.** The independently maintained first-party plugin source and Releases live in the [official Marketplace](https://github.com/KMerdan/notepadnext-markdown-preview). This application example uses its own ID for repeatable host tests. Plugin authors should start from the [SDK templates](../../packages/plugin-sdk/README.md).

# HTML Preview

基于 SDK 0.1.1 的 Node.js HTTP 服务示例。预览当前 HTML buffer（包括未保存修改），运行 HTML/JS，并调用本地服务 API。更新、Reload 和回滚自动恢复已打开窗口与 state。

```sh
npm install --ignore-scripts --install-links
npm run build
npm test
node ../../packages/plugin-sdk/pack.cjs . ../../build/plugin-dev/html-preview
```

Plugin Manager 注册本项目 notepadnext-plugin.json，Discover → Local Package 选择上述打包目录，Installed 中启用并运行 Open HTML Preview。

独立项目使用 SDK 的 html-service 模板，完整说明见 [HTML 服务开发](../../packages/plugin-sdk/docs/HtmlService.zh-CN.md) 和 [热更新](../../packages/plugin-sdk/docs/HotUpdate.zh-CN.md)。原生宿主更新测试同时覆盖两个同类服务窗口、浮动位置、滚动、输入、序列化状态、服务 API、失败回滚及关闭后 Reload。
