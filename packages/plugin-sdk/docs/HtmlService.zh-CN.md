# 本地 Node.js HTML 服务插件

首版 HTML Preview 使用插件自身的 Node 24 进程启动 HTTP 服务。页面能执行 HTML/JS、加载 CSS、fetch 自己服务的 API，也能通过 acquireVsCodeApi 的 state/message 桥与插件交互。模板包含 HTML 编辑区、运行按钮、计数器和服务 PID；可以从当前 HTML 编辑器读取包含未保存修改的内容。

```sh
node package/create.cjs ./html-demo --template html-service --id author.html-preview
cd html-demo
npm install --ignore-scripts
npm run build
npm test
node tools/plugin-sdk/pack.cjs . ../html-local-package
```

在 Notepad Next++ 的 Plugin Manager 注册项目的 notepadnext-plugin.json，再在 Discover 中选 Local Package，选择 html-local-package。安装完成后在 Installed 中启用并运行 Open HTML Preview。打开一个 HTML 文档，点击 Load active HTML buffer，再 Run preview。计数器和 Node PID 用来确认客户端脚本和真实后台服务都在工作。

build.cjs 把 SDK 的 local-service.cjs 复制到 dist。发布包不依赖开发机器上的 SDK 路径，也没有 npm 或 Qt 编译步骤。模板是 CommonJS；TypeScript 开发者可使用 local-service.d.ts 和 @types/node。

```js
const {startLocalService} = require('./local-service.cjs');
const service = await startLocalService(context, (request, response, path) => {
  response.setHeader('Content-Type', 'text/html');
  response.end('<h1>My service</h1>');
});
const panel = await context.webviews.createService({
  viewType: 'my-service', title: 'My Service',
  endpoint: service.endpoint, nonce: service.nonce
});
await panel.reveal();
```

声明 commands@1、webviews@1、webviews.service@1 和 webviews.restore@1。读取编辑器需要 documents.read@1；监听修改需要 documents.events@1。startLocalService 自动加入 subscriptions；面板、监听和恢复回调也应加入 subscriptions。

服务绑定 127.0.0.1 的随机端口和随机路径。宿主探测 __nnp_probe，核对当前会话和 nonce；2 秒、4 KiB 上限，禁止重定向。页面只可请求同一服务路径下的资源，其他服务端口、其他插件服务、远程站点、file URL、子窗口、下载、权限请求均不放行。不得把 nonce 放到日志、页面或用户配置中。插件本身运行受信任的 Node 代码，这套服务限制不等于 OS 沙箱。

更新时服务在新进程中重新启动，端口可以改变，原 dock 保留。面板 state 迁移后恢复输入、计数器和滚动；旧服务随停用和进程退出结束。后台内存不自动迁移；需要持久化的数据写入 SDK storage，或由恢复回调读取 state。

HTML Preview 的宿主调试入口是实际应用；nnp-debug 的内存宿主不模拟浏览器 HTTP 服务。页面脚本可使用 Qt WebEngine 的开发工具，本地服务可用 Node Inspector 调试。保存源码后 build、打包，再在 Installed 中 Local Update；无需签名、公证或重启编辑器。
