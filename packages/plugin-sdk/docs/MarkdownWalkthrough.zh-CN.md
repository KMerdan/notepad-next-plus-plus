# Markdown 插件完整开发流程

以下流程从独立 SDK 开发包开始，生成 `demo.markdown-preview`，避免与内置的 `notepadnext.markdown-preview` 冲突。示例命令以终端当前开发目录为基准；输出路径必须是新路径。

## 1. 创建独立项目

将 SDK tgz 放到开发目录，解包后执行：

```sh
tar -xzf notepadnext-plugin-sdk-0.1.0.tgz
node ./package/create.cjs ./markdown-demo --template markdown --id demo.markdown-preview --name "Markdown Preview Demo"
cd markdown-demo
npm install --ignore-scripts
npm run build
npm test
```

项目可以移动到任何目录，SDK 已在 `tools/plugin-sdk/` 中。第一次安装完成后提交 lockfile；以后使用 `npm ci --ignore-scripts`。

`src/index.ts` 在 `activate` 中注册 `demo.markdown-preview.open`。用户运行它时创建一个预览面板，并订阅活动文档变化。刷新读取 `getSnapshot()` 的缓冲区，而不是磁盘文件，所以真实编辑器中尚未保存的内容也能预览。再次执行命令会复用面板，关闭后可重新创建。

## 2. 本地调试与断点

```sh
npm run debug
```

输出 `.debug/preview.html`。用浏览器打开这个 HTML，检查标题、列表、代码块、Unicode 与原始 HTML 转义。模拟宿主将 `demo.md` 的内容当作未保存缓冲区；修改它即可产生新的测试输入。

持续开发使用两个终端，当前目录都为 `markdown-demo`：

```sh
npm run watch
```

```sh
npm run dev
```

先完成一次 build，再启动 dev。编译后的 `dist/` 代码变化会重新激活插件；`demo.md` 变化会产生文档事件，刷新 HTML。浏览器中重新加载输出文件查看结果。Ctrl+C 会调用 deactivation 并释放模拟资源。

断点使用 VS Code 的 **SDK simulated host** 配置，或执行 `npm run debug:break` 后使用 **Attach SDK debug:break**。调试器监听本机 `127.0.0.1:9229`；source maps 将断点映射到 `src/index.ts`。可在 `activate`、`open` 和 `render` 中检查调用。

这一步验证插件逻辑；Qt 的 XHTML/CSP、图片映射、JSON-RPC 和真实进程清理仍需下一步验证。模拟器不提供资源映射，图片会显示模板的不可用占位说明。

## 3. 生成本地安装包

停止 watch/dev，确认构建和测试结果，再生成源码之外的新目录：

```sh
npm run build
npm test
npm run package -- ../artifacts/markdown-preview-0.1.0
```

安装目录包含 metadata、`dist/`、生产依赖和许可证。SDK、TypeScript、模板、测试、源码目录及开发工具不会作为开发依赖进入安装包。source map 中可能包含作者源码，这是模板开启断点映射的结果；分发前可在 tsconfig 中关闭 sourceMap/inlineSources 并清理重建 dist。

## 4. 注册、安装与真实编辑器调试

1. 在 NotepadNext 打开 **Plugins → Plugin Manager… → Add Source…**。
2. 选择 **Local metadata file**，浏览到项目 `markdown-demo/notepadnext-plugin.json`，确认注册。注册后它出现在 **Discover**。
3. 选中 **Markdown Preview Demo**，点击蓝色 **Install Local Package…**，选择刚生成的 `artifacts/markdown-preview-0.1.0` 目录，确认安装。
4. 在 **Installed** 启用插件，点击 **Open Markdown Preview**，或通过 Plugins 菜单 / 工具栏打开。
5. 在编辑器打开 `.md` 文件，或将新文档语言设为 Markdown；输入标题、列表、代码块和 Unicode，不保存也应更新。
6. 关闭预览，再次打开；管理器应显示 **Running · 1 open**。禁用应关闭面板；重新启用和运行应能恢复。主窗口和未保存文档应保留。
7. 用 **Inspect** 查看运行入口和访问能力，用 **Logs** 查看 `demo.markdown-preview: activated`、运行日志与错误。临时添加 `console.info` 也会进入插件日志。

本地安装入口当前选择构建包目录。不要选择源码根目录，也不要将 SDK tgz 当作插件资源导入。已安装代码是独立副本；编辑 src/dist 不会直接更改它。

## 5. 修改、重新安装与更新

将 `package.json` 和 `notepadnext-plugin.json` 的版本都改为 `0.1.1`，再执行：

```sh
npm install --package-lock-only --ignore-scripts
npm run build
npm test
npm run package -- ../artifacts/markdown-preview-0.1.1
```

在 **Sources** 选中之前注册的本地 metadata 来源，点击 **Refresh**。回到 **Installed → Update…**，选择 `markdown-preview-0.1.1` 目录并确认。Update 保留启用状态；宿主校验后切换到新包，失败时可恢复旧包。需要手动重启当前安装代码时使用 **More → Reload**。

本流程注册源码项目的 metadata，因此提高版本后刷新同一路径就能读到新版本；若注册的是某个旧安装目录内的 metadata，需要改用新的资源来源。

## 6. 生成 ZIP 与校验文件

本地流程完成后，可以生成可分发资产；这一步只生成文件：

```sh
npm run bundle -- ../artifacts/markdown-preview-0.1.1-zip v0.1.1
```

结果为 `notepadnext-plugin.zip` 和 `notepadnext-plugin-checksums.json`。tag 必须匹配插件版本，包工具不会执行插件入口或安装 hooks。ZIP 可用于实际 GitHub Release 的插件分发；主仓库源码 ZIP 与 SDK tgz 都不是插件安装包。

| 文件 | 消费者 |
| --- | --- |
| `notepadnext-plugin-sdk-0.1.0.tgz` | 插件开发者；模板、SDK 与文档 |
| `notepadnext-plugin.json` | Plugin Manager 资源注册 |
| `artifacts/markdown-preview-0.1.1/` | 本地 Install / Update |
| `notepadnext-plugin.zip` 与 checksums | GitHub Release 安装下载 |

每一步可以在本机完成，不需要认证或远程发布。
