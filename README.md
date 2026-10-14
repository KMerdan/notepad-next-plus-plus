# Notepad Next ++

An independently maintained fork of [NotepadNext](https://github.com/dail8859/NotepadNext), by KMerdan. A cross-platform Qt/Scintilla text editor with macOS UI improvements and a native plugin SDK for extending existing editing features.

NotepadNext's original copyright notices are retained. This fork continues under **GPL-3.0-or-later**; see [LICENSE](LICENSE). This project and its Marketplace are maintained for this fork and are not official upstream projects.

## Start here

| I want to… | Start with |
| --- | --- |
| Download / Homebrew install | [Binary downloads and installation](doc/Downloads.md), [Releases](https://github.com/KMerdan/notepad-next-plus-plus/releases) |
| Reload or convert a text encoding | [Encoding operations](doc/Encoding.md) |
| Bind plugins to editor components | [Plugin categories and lifecycle](https://github.com/KMerdan/notepadnext-plugin-sdk/blob/main/docs/PluginLifecycle.zh-CN.md) |
| Build the editor | [Build instructions](doc/Building.md), [plugin-enabled build](doc/plugin-system/Implementation.zh-CN.md) |
| Install an official plugin | [Official Marketplace](https://github.com/KMerdan/notepadnext-plugin-marketplace), [manager lifecycle](doc/plugin-system/OfficialMarketplace.zh-CN.md) |
| Develop a plugin | [Independent SDK](https://github.com/KMerdan/notepadnext-plugin-sdk), [native API 0.3](https://github.com/KMerdan/notepadnext-plugin-sdk/blob/main/docs/PhaseB.zh-CN.md), [editing API guide](https://github.com/KMerdan/notepadnext-plugin-sdk/blob/main/docs/NativeAPI.zh-CN.md) |
| Understand current capability and limits | [Phase B implementation](doc/plugin-system/NativeSDKPhaseBStatus.zh-CN.md), [current SDK guide](https://github.com/KMerdan/notepadnext-plugin-sdk/blob/main/README.md) |
| Plan the next native APIs | [SDK Phase B priorities and acceptance plan](doc/plugin-system/NativeSDKPhaseBPlan.zh-CN.md) |
| Understand architecture and future APIs | [Native SDK design](doc/plugin-system/NativeSDKDesign.zh-CN.md) |
| Find technical documentation | [Documentation index](doc/plugin-system/README.md) |

## Three repositories

| Repository | Owns |
| --- | --- |
| [notepad-next-plus-plus](https://github.com/KMerdan/notepad-next-plus-plus) | Native editor services, Qt UI, JSON-RPC host, plugin install/update/lifecycle and native tests |
| [notepadnext-plugin-sdk](https://github.com/KMerdan/notepadnext-plugin-sdk) | API types, metadata/wire contracts, templates, debugging/testing/service helpers and packaging tools |
| [Official Plugin Marketplace](https://github.com/KMerdan/notepadnext-plugin-marketplace) | Markdown/HTML product previews, separate SDK examples, catalog and independent releases |

The SDK is a Git submodule pinned to a reviewed commit. The Marketplace previews and SDK examples consume the immutable SDK 0.3.2 Release tarball with locked integrity. The catalog now uses the canonical `notepadnext-plugin-marketplace` URL. Existing installs from the previous origin need a one-time reinstall. Application, SDK and plugin versions are independent.

## Build and validate

Build the default branch, including its pinned dependencies:

```sh
git clone --recurse-submodules https://github.com/KMerdan/notepad-next-plus-plus.git
cd notepad-next-plus-plus
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt
cmake --build build --parallel
```

Core plugin support is enabled by default. Preview panels require Qt WebEngine/WebChannel and `-DNOTEPADNEXT_PLUGIN_WEBVIEWS=ON`. Bundle the pinned Node 24 binary using `-DNOTEPADNEXT_PLUGIN_NODE=/absolute/path/to/node`; [runtime preparation](doc/plugin-system/CrossPlatformCI.zh-CN.md) verifies the controlled archive. Set `-DNOTEPADNEXT_PLUGINS=OFF` for an editor-only build. Local macOS development requires no Developer ID or notarization.

For contract/integration tests, configure `-DNOTEPADNEXT_BUILD_PLUGIN_TESTS=ON` and the explicit Node test runtime. Run `ctest --test-dir build --output-on-failure`. See the [conformance workflow](.github/workflows/plugin-conformance.yml) for WebEngine, core-only and disabled-plugin configurations. The binary release workflow builds the four current platform targets with WebEngine and a controlled Node runtime.

## Extend the editor

Native API 0.3 exposes document/selection primitives, bounded range reads, native subscriptions, file lifecycle, literal search, language/EOL/bookmarks and owned highlights. The shared native command catalog contains 18 operations. Menus and plugins reuse native services. Full HTML panels use `acquireNotepadNextApi()`; panels retain generic hot-update restoration, and new-session highlights publish only after validation. See the [implementation and limits](doc/plugin-system/NativeSDKPhaseBStatus.zh-CN.md).

Both production previews declare native API 0.3. Register a repository through **Plugin Manager → Add Source…**; then explicitly install from **Discover**, enable in **Installed**, and open its command. Updates restore owned panels; uninstall is separate from removing a source.

The SDK's purpose is to enhance Notepad Next ++. Other editors' plugins may be studied and ported. No VS Code compatibility adapter or old API profile is shipped. Providers, SDK encoding mutation methods, macros and other conditional extensions remain future work; the editor Encoding menu already handles file decoding and conversion.

Trusted Node/TS/JS plugins run in separate processes. Capabilities govern host RPC; they do not provide an operating-system sandbox. Other language runtimes can implement the same JSON-RPC contract in future.

## License and upstream

[GPL-3.0-or-later](LICENSE). Based on [dail8859/NotepadNext](https://github.com/dail8859/NotepadNext), copyright Justin Dailey and contributors. Fork modifications are maintained by KMerdan. Original notices and third-party licenses remain intact. Upstream build names such as the `NotepadNext` CMake target are retained; displayed product/executable are Notepad Next ++ / NotepadNextPlusPlus.
