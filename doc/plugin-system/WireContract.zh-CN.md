# 插件业务协议契约

契约 owner 是独立 SDK 的 `schemas/wire.schema.json` 与 `schemas/wire-contract.json`。主仓内副本由 `scripts/sync-plugin-sdk-contracts.py` 同步，Qt 和 Node 用相同 schema 验证；不得单独修改副本。

当前为 JSON-RPC 2.0、wire profile **0.1**、native API **0.3.0**，共 **68 methods**。旧位置参数 `nnp.commands.invoke` 已删除；命令只通过 `nnp.commands.invokeNative` 接收不可变 `CommandInvocation`。API 0.1/0.2 不再协商。元数据 manifestVersion 1 和 wire profile 0.1 是当前格式编号，不是历史 API 兼容层。

完整类型、方法和配额见 [native.d.ts](../../packages/plugin-sdk/native.d.ts)和 [SDK API 0.3 指南](../../packages/plugin-sdk/docs/PhaseB.zh-CN.md)。传输实现仍限制帧、队列、JSON 深度、请求并发和超时；方法额外验证参数、session/generation、状态、权限、固定目标/version 与资源所有权。文件交互可取消，未知 IO outcome 不自动重试。

共享正/负 fixtures 在 Qt/Node 两端执行；native fixtures 覆盖 Phase B 方法。应用负责原生行为和生命周期，SDK 负责协议/类型/工具，Marketplace 负责插件业务，不把产品依赖带入主仓 CI。
