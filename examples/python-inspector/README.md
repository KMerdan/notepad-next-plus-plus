# Python Buffer Inspector

This first-party GPL-3.0-or-later example opens the active editor buffer in a native dock panel, including unsaved Unicode text. **Inspect Buffer with Python** takes a new snapshot each time it runs. It does not automatically refresh while you type.

Configure the application with an explicit Python 3.12 interpreter:

```sh
cmake -S ../.. -B /tmp/notepadnext-build \
  -DNOTEPADNEXT_PLUGIN_WEBVIEWS=ON \
  -DNOTEPADNEXT_PLUGIN_NODE=/absolute/path/to/verified/node \
  -DNOTEPADNEXT_PLUGIN_PYTHON=/absolute/path/to/python3
cmake --build /tmp/notepadnext-build
```

Use Node 24 to create a release directory or both GitHub release assets. Output paths must be new:

```sh
node ../../packages/plugin-sdk/pack.cjs . /tmp/python-inspector-release
node ../../packages/plugin-sdk/release.cjs . /tmp/python-inspector-assets v0.1.0
```

The packer reads the existing Python files without importing them or running hooks. `package.json` supplies release name/version/license information; this example has no npm or Python package dependencies. Vendor any reviewed Python modules under `dist/`, keep bytecode out of the package, and include their licenses. Runtime dependency installation with pip is not supported.

Register matching metadata first, select the packed directory with **Uninstalled → Local Package…**, then enable the plugin in **Installed**. Use **Run Command** or its **Plugins** menu entry. Disable closes its panel and process; Reload starts a fresh session. The source repository URLs in the integration tests are illustrative; no public repository or Release has been published for this example.

The [Python adapter contract](../../doc/plugin-system/PythonAdapter.zh-CN.md) describes the API and execution model. The interpreter is a developer configuration, not bundled in the application. A build with no configured Python interpreter can register and install the resource, but activation reports that its runtime is unavailable.

源码在 src/index.py。打包前在本目录执行 npm run build，把源码复制到 dist。示例使用 register_restorer 恢复已有面板，恢复回调不重放记录快照的普通命令。
