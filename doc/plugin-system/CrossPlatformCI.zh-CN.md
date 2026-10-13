# 跨平台构建与运行时

当前二进制发布使用 Qt 6.10.0 和 `packages/plugin-host/runtime-lock.json` 固定的 Node 24。目标为 macOS arm64/x86-64、Windows x86-64、Linux x86-64。平台支持与安装方式见 [Downloads](../Downloads.md)。

`python3 scripts/fetch-plugin-node.py --platform darwin|win|linux --arch arm64|x64 --output <new-directory>` 从 Node 官方 HTTPS 下载受锁文件约束的 archive，先核对 SHA-256，再只提取 binary 和 LICENSE。输出 runtime.json 中的绝对路径供 CMake 使用；不能用 PATH 自动寻找 Node。

- [二进制工作流](../../.github/workflows/build.yml)：四个当前平台构建完整预览/服务能力，打包时校验 Node、宿主、WebEngine 及 notices，生成五个独立资产和 SHA256SUMS。
- [宿主契约工作流](../../.github/workflows/plugin-conformance.yml)：四个平台的 disabled/core/webviews 配置。仅使用通用 fixtures；SDK 和 Marketplace 分别拥有自己的 CI。
- [本地构建与测试](Implementation.zh-CN.md)：可明确关闭插件或预览，以验证不同配置；Python 是构建/打包脚本工具，不作为插件 runtime 捆绑。

macOS 二进制最低版本为 14；当前个人发布仅使用 ad-hoc 签名，不自动访问钥匙串、公证凭据或上传 Apple。SDK 版本、wire profile 与应用版本分别演进。
