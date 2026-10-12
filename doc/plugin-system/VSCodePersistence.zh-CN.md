# VS Code 状态与配置适配

兼容版本 `0.1.0` 对照 [VS Code 1.100.0 声明](https://raw.githubusercontent.com/microsoft/vscode/1.100.0/src/vscode-dts/vscode.d.ts)。导入时显式选择 `--storage` 和／或 `--configuration`，生成对应必需能力。没有能力时不会读盘，API 返回 `1003`。所有记录仍按插件 ID、规范化仓库来源和应用数据目录隔离。

| API | 当前范围 |
| --- | --- |
| `context.globalState` | 该来源、该插件的全局状态；同步 get/keys，异步 update。 |
| `context.workspaceState` | 当前应用数据目录的单个持久编辑会话，与 globalState 分开保存。产品只有一个主窗口／会话；打开、关闭或另存文档不更换此身份。 |
| `workspace.getConfiguration(section)` | 仅插件静态声明的配置；get/has/inspect、默认值、点号路径与对象内部读取。 |
| 配置 update | 显式 `ConfigurationTarget.Global` 或 true，修改声明的叶配置；undefined 删除覆盖、恢复默认值。 |
| `onDidChangeConfiguration` | 确认写入或冲突重读改变有效配置时通知；无 scope 的 affectsConfiguration，相同值不重复通知。 |

命名／多文件夹 workspace、跨插件／主程序设置、Workspace/WorkspaceFolder 写入、语言覆盖、配置直接属性访问、Settings Sync 与 `setKeysForSync` 未实现，明确返回 `1005 UnsupportedApi`。

## 保存与确认

适配器在导入扩展代码之前读取三个授权记录：storage 内 `_vscode.globalState.v1`、`_vscode.workspaceState.mainSession.v1`，configuration 内 `_vscode.configuration.v1`。外壳为 `{schemaVersion:1, values:{...}}`；不要用原生 SDK 覆盖这些保留键。更新包不删除数据；卸载默认保留，用户明确选择删除才清除此来源的整个记录。

get 返回复制后的 JSON；缺失是 undefined 或调用者默认值，null 是已存值。keys 返回独立只读数组。修改输入或读出的对象不会更改缓存。update 每命名空间串行执行，Promise 在 Qt 原子落盘应答后成功，此前同步读取仍是先前确认的值。失败返回错误并保留旧缓存，后续有效写入继续。命令完成和清理等待已排队操作；停用后尚未发送的操作拒绝 `1004`，已发送且落盘的写入不回滚。

原生 update 新增可选 expectedValue，Qt 在同一记录文件锁内比较并写入；不匹配是 `1001`，磁盘不变。适配器重新读入并只重应用本次键修改，保留其他实例更改的键；最多尝试四次。相同键按成功提交顺序决定最后值。没有跨实例变更推送，缓存只在激活或写入冲突时重读。原生 get 以 null 表示缺失，因此 expectedValue:null 同时匹配缺失或已存 null；兼容记录外壳可区分其内部 null 与缺失。

没有 expectedValue 的原生 update 保持原有行为。Node SDK 为 `compareExchange(key, expectedValue, value?)`，Python 为 `compare_exchange`；省略 value 是条件删除。

激活、模块导入和更新验证阶段允许读，禁止落盘写入。兼容层和 Qt 都检查发布屏障，写入返回 `1004`；扩展需在成功激活后的命令／事件中写入。activate 中等待 Memento.update 的扩展需要调整。当前没有暂存写入事务，不能宣称这类扩展直接兼容。

## 声明、预算与迁移

导入器允许 `contributes.configuration` 单块或至多 16 块：title/properties/order；属性支持单一 string/boolean/number/integer/object/array/null、default、enum、描述及 application scope。省略 default 使用对应类型空值。复杂 Schema、resource/language scope、重复键和父子叶冲突拒绝；不会忽略不能执行的约束。覆盖值检查基本类型与 enum，未声明项不可写。

每个兼容命名空间最多 256 键，每键最多 128 UTF-8 字节；整个外壳最多 64 KiB、32 层，最多排队 64 次写入；原生文件仍最多 1 MiB。默认值合计也检查预算。只接受有限数字、普通 JSON 对象和完整数组；循环、Date、BigInt、getter、嵌套 undefined 和稀疏数组拒绝。update 顶层 undefined 专用于删除，原型相关键不会污染普通对象。

损坏、超预算、未知 schemaVersion 或已存配置不符合新版声明类型时停止激活并保留数据。v1 外壳及旧版未知键维持；作者用版本化键、兼容读者设计迁移。没有自动迁移、数据回滚或命名 workspace 迁移。

```ts
const settings = vscode.workspace.getConfiguration('preview');
context.subscriptions.push(vscode.commands.registerCommand('preview.toggle', async () => {
  await context.globalState.update('openCount', context.globalState.get('openCount', 0) + 1);
  await settings.update('theme', 'dark', vscode.ConfigurationTarget.Global);
}));
```

preview.theme 须在 package.json 声明，导入选择 `--storage --configuration`。证据包括同步读取、null／删除、Unicode、预算、损坏保留、竞争合并、错误恢复、停用排队清理和真实 Qt 安装／重载／两种卸载路径，见 [验证记录](evidence/VSCodePersistenceValidation.json)。其余 I3/I4 范围仍由 [完整审计](CompletionAudit.json) 追踪。
