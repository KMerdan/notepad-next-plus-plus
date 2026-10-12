# 快速开始

## 前提

- 开发工具：Node 24 与 npm。TypeScript 5.9.3 和 Markdown-it 15.0.2 由模板安装并锁定。
- 编辑器：支持插件的 NotepadNext 0.15.x，宿主 API 0.1.x。Markdown 面板需要启用 WebEngine 的开发版。
- 无需 GitHub 仓库、Developer ID、公证或发布即可本地开发和安装。

通过解包后的 `create.cjs` 创建项目，或在工具项目中安装 SDK tgz 后使用 `nnp-create`。新项目路径必须不存在；ID 使用 `publisher.name`，两个部分由小写字母、数字和短横线组成。

```sh
node ./package/create.cjs ./my-plugin --template hello --id author.hello
```

`markdown` 模板包含完整 Markdown 渲染器、150ms 输入去抖、关闭后重开和取消过时渲染。`hello` 模板只注册一个命令，运行时显示消息。

## 生成的结构

```text
my-plugin/
  src/index.ts                 activate/deactivate 与命令逻辑
  notepadnext-plugin.json      注册 metadata、能力和菜单入口
  package.json                 依赖和开发命令
  tsconfig.json                CommonJS 构建、source maps
  test.cjs                     使用 SDK 测试宿主的行为测试
  demo.md                      模拟未保存缓冲区
  .vscode/                     源码断点和 JSON Schema 配置
  tools/plugin-sdk/            自带 SDK、模板与完整文档
  LICENSE
```

首次执行 `npm install --ignore-scripts`，提交生成的 `package-lock.json`；以后执行 `npm ci --ignore-scripts`。`npm run build` 输出 `dist/`，`npm test` 验证已构建入口。JS 作者也可以直接提供 CommonJS 的 `dist/index.js`，导出 `activate` 和可选的 `deactivate`，并移除不需要的 TS 构建配置。

## 生命周期

资源注册只保存 metadata。用户安装后插件默认禁用，启用后仍按 activationEvents 延迟激活。第一次执行声明的命令时，宿主启动独立插件进程，调用 `activate(context)`，然后执行命令回调。

`activate` 中注册命令和准备逻辑；在命令回调中创建面板、显示消息和写入持久化数据。退出、禁用、卸载或 Reload 时调用 `deactivate`；释放自己创建的订阅、面板和定时器。宿主也会清理其追踪的句柄。

`context` 方法多数为 Promise。不要导入 SDK 根入口获取运行时对象；TS 使用 `import type {PluginContext} from '@notepadnext/plugin-sdk'`，运行时对象由宿主传入。

## 每次迭代

1. 编辑源码，构建并测试；用模拟宿主快速检查行为和断点。
2. 打包到新的源码外目录，在真实编辑器验证未保存缓冲区、主题、关闭和清理。
3. 修改已经安装的插件时，同时提升 `package.json` 与 metadata 的版本，更新 lockfile，重新打包。
4. Sources → Refresh 后，在 Installed → Update… 选择新构建包；More → Reload 用于重启当前安装版本。

完整命令和安装步骤见 [Markdown 演示](MarkdownWalkthrough.zh-CN.md)。
