# 通用热更新与窗口恢复

SDK 0.1.1 的 `webviews.restore@1` 是所有宿主 Webview 插件共同使用的协议。原生片段、完整 HTML、Node HTTP 服务以及适配的 VS Code Webview 使用同一条生命周期路径。宿主没有 Markdown ID 或插件菜单命令的特殊处理。

在 manifest 的 required capabilities 中加入 `webviews.restore@1`，并在 `activate` 注册每个 viewType 的恢复回调：

```ts
context.subscriptions.push(context.webviews.registerRestorer('my-view', async panel => {
  // 重建这个已打开的视图。不要调用带导出、写文件等副作用的菜单命令。
  await openView(panel.state);
}));
```

回调参数包含旧 panelId、viewType、title、kind、hasState 和 state。SDK 会为回调中创建的同 viewType 面板自动附加 restorationKey，即使同时打开多个同类型 HTML 面板，也能恢复到各自的 dock。顺序执行恢复回调，最多 8 个面板。

更新、Reload、手动回滚使用相同机制：

1. 宿主读取滚动位置、有限的输入框值和 Webview `getState()`，保留原 dock 和内容。
2. 旧进程停用。面板在准备过程中禁用交互，避免发送到旧会话。
3. 新进程激活，重新注册命令和恢复回调；恢复页面暂存在同一个 dock 中。
4. 等待所有旧面板被接管、页面加载和浏览器状态恢复成功，再确认包事务并替换内容。
5. 任一步失败，更新包回滚到旧版本，再运行旧版本的恢复回调。普通用户命令不会被重放。

宿主保留停靠区、浮动位置、尺寸、tab 关系、可见性、滚动位置及有限的表单值。更新前已关闭的面板不会再出现。未打开的插件不会因更新被打开，禁用插件仍保持禁用。主编辑器的原生窗口不重建。

完整 HTML 和服务页面可以用宿主注入的桥保存应用状态：

```js
const host = acquireVsCodeApi();
const previous = host.getState();
let count = previous?.count ?? 0;
host.setState({count, selectedItem: 'one'});
```

状态必须是可序列化 JSON，每个面板最多 64 KiB。宿主还保存最多 128 个 input/textarea/select 的值，各值最多 4096 字符，总浏览器快照 128 KiB；超限字段跳过，避免截断后覆盖应用通过 setState 恢复的完整内容。密码、文件输入不纳入表单快照；恢复不会触发 input/change 事件。应用应在修改时主动调用 setState，不能依赖更新过程中任意 JS 堆、计时器、网络连接或后台任务被复制。滚动和表单恢复仍取决于新页面提供相应元素和布局；结构改变时应在恢复回调迁移业务状态。

Python 使用 `context.webviews.register_restorer(view_type, callback)`；其回调参数遵循相同 JSON 描述。VS Code 适配层将 `window.registerWebviewPanelSerializer` 映射到恢复回调，并传入 `deserializeWebviewPanel(panel, state)`；恢复时可同步重置 webview.options 的脚本开关和本地资源目录。

旧插件若只提供普通打开命令，需要先在新版本添加恢复回调。无法重建已打开面板的目标版本会失败并保留旧面板。旧版自身也没有恢复回调时，面板仍保留但不可交互；管理器允许停用以清理并重新打开。所有自定义状态都应使用上述契约；当前不支持宿主 API 之外的自建原生窗口。
