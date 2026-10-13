# Plugin system documentation

Current implementation: native API 0.3, one SDK profile, Node/TS/JS plugins. Application, SDK and Marketplace have separate responsibilities and CI.

| Task | Owner |
| --- | --- |
| Download binaries / Homebrew | [Installation](../Downloads.md) |
| Register, install, update and remove plugins | [Official Marketplace](OfficialMarketplace.zh-CN.md) |
| Current native editor API and limits | [Phase B implementation](NativeSDKPhaseBStatus.zh-CN.md), [SDK guide](../../packages/plugin-sdk/docs/PhaseB.zh-CN.md) |
| Create, debug and package a plugin | [SDK getting started](../../packages/plugin-sdk/docs/GettingStarted.zh-CN.md) |
| Restore open plugin panels | [SDK hot update guide](../../packages/plugin-sdk/docs/HotUpdate.zh-CN.md) |
| Build and verify the host | [Implementation](Implementation.zh-CN.md), [conformance CI](../../.github/workflows/plugin-conformance.yml) |
| Package a plugin Release | [Marketplace releases](https://github.com/KMerdan/notepadnext-markdown-preview/blob/main/docs/Releases.md) |
| Inspect original scope and decisions | [Native SDK design](NativeSDKDesign.zh-CN.md), [Phase B plan](NativeSDKPhaseBPlan.zh-CN.md) |

Source and current tests are authoritative. The SDK owns wire/schema contracts; use `scripts/sync-plugin-sdk-contracts.py` after changing its reviewed submodule pin. Marketplace plugin business acceptance belongs to Marketplace CI. Design/planning and dated files under `evidence` describe their recorded versions, not the current shipped profile. Removed VS Code experiments, old 0.1/0.2 API negotiation and Python prototype are no longer part of the application.
