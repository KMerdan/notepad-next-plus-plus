# 插件宿主构建与验证

当前实现以 [Native API 0.3 状态](NativeSDKPhaseBStatus.zh-CN.md)和独立 [SDK 开发指南](../../packages/plugin-sdk/docs/GettingStarted.zh-CN.md)为入口。宿主只交付当前 Node/TS/JS profile；VS Code 适配实验、旧 API 协商和 Python 原型已移除。

## 构建

Qt 6.10：Core、Widgets、Network、PrintSupport、Core5Compat、WebEngineWidgets、WebChannel。Release 工作流同时准备 Qt Positioning 和校验后的 Node 24。下载 Node 不执行安装 hook：

```sh
python3 scripts/fetch-plugin-node.py --platform darwin --arch arm64 --output build/runtime
```

`build/runtime/runtime.json` 给出校验后的 binary 和 archive 路径。配置：

```sh
cmake -S . -B build/editor -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt \
  -DNOTEPADNEXT_PLUGIN_WEBVIEWS=ON \
  -DNOTEPADNEXT_PLUGIN_NODE=/absolute/path/from/runtime.json
cmake --build build/editor --parallel 3
```

macOS 打包还需 `-DNOTEPADNEXT_MACOS_NODE_ARCHIVE=/absolute/path/to/verified.tar.xz`，可运行 `cmake --build build/editor --target dmg`。本地/个人发行默认 ad-hoc 签名，无需 Developer ID 或公证凭据。Windows `zip` / `installer`、Linux `appimage` 和 [下载说明](../Downloads.md)记录发布入口。

## 验证

配置 `-DNOTEPADNEXT_BUILD_PLUGIN_TESTS=ON -DNOTEPADNEXT_TEST_NODE=/absolute/path/to/verified/node`，然后：

```sh
ctest --test-dir build/editor --output-on-failure --parallel 1 --timeout 600
```

主仓验证通用 metadata、包完整性、JSON-RPC、文档/原生方法、文件交互、面板/进程生命周期、退出及打包约束。[conformance CI](../../.github/workflows/plugin-conformance.yml)覆盖 disabled/core/webviews。SDK 验证工具/类型/六模板；Marketplace 验证四个产品插件，主仓不构建它们。实际产品验收通过显式环境变量单独选择，不影响基础 CI。

## 生命周期

注册只保存资源来源；Discover 中安装校验后的包；Installed 中启用。资源属于 session，停用、崩溃、卸载时释放。热更新先恢复并验证新 session，成功后发布已准备的窗口和高亮；失败恢复前一个版本。重载不改变包版本，卸载和删除 source 是独立操作。详见 [Marketplace](OfficialMarketplace.zh-CN.md)和 [热更新](../../packages/plugin-sdk/docs/HotUpdate.zh-CN.md)。

传输是 JSON-RPC 2.0，wire profile 0.1，native API 0.3。metadata 的 `list` 区分单插件与 Marketplace。SDK 是契约 owner，生成副本用 `scripts/sync-plugin-sdk-contracts.py` 同步。可信 Node 插件仍有当前用户的系统权限；RPC capabilities 限制宿主方法，不是 OS sandbox。
