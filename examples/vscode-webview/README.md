# VS Code Webview acceptance sample

This adapted Cat Coding example exercises the real full HTML Webview compatibility profile. Microsoft source, browser JS and CSS come from the fixed MIT commit recorded in `provenance.json`. Exact original files are retained in `upstream/`. Browser JS/CSS are unchanged. TypeScript declarations are shared with the pinned HelloWorld sample.

Two adaptations are explicit: application restart activation is unsupported while the upstream panel serializer restores hot updates, and remote GIFs are replaced by an owned local PNG fixture mapped through `asWebviewUri`. This sample is not evidence of unmodified extension compatibility or full Marketplace support. The generated fixture is part of this repository under GPL-3.0-or-later; Microsoft files remain MIT.

Build and import using the controlled Node runtime:

```sh
node examples/vscode-webview/build.cjs
node packages/plugin-sdk/import-vscode.cjs examples/vscode-webview /tmp/nnp-cat-package --webviews --resource=media
```

The importer reads static files and never runs upstream npm scripts. Register the metadata first, install explicitly, then enable and choose “Start cat coding session” from Plugins. “Do some refactoring” sends a browser message that halves the counter. Browser messages show a Qt error message. Close/reopen, reload, disable and uninstall must reclaim all session resources.

This profile requires Qt WebEngine 6.6+ and both `webviews@1` and `webviews.compat@1`. Package JS/CSS/PNG are permitted within `localResourceRoots`; external network resources are blocked. The panel currently maps to logical ViewColumn.One, retains its context while hidden, and preserves `getState`/`setState` only for its own lifetime.
