# Host contract fixtures

These small executable plugins belong to the application tests. They probe the
production host protocol and lifecycle without installing dependencies,
compiling TypeScript, invoking SDK packaging tools or rendering Markdown.

`contract-preview` exercises commands, document snapshots/events, panel reuse,
restoration after reload, UI registration/installation/enabling/removal and
shutdown. Service ownership, service restart, browser state and window geometry
are also exercised by the inline fixtures in `PluginWebviewTests.cpp`.

`native-phase-b` probes API 0.3 native document/selection events, bounded reads,
file lifecycle, literal search, language/EOL/bookmark operations and owned
decorations. It is executable JavaScript with no plugin business dependencies.

`legacy-hello-world` and `legacy-webview` are frozen executable probes for the
existing experimental adapter. They retain their original Microsoft MIT
licenses and import provenance. Their presence does not establish a VS Code
compatibility goal. They require neither a TypeScript compiler nor the SDK
importer during host tests.

CI ownership:

| Repository | Required checks |
| --- | --- |
| Application | Wire/API contracts, capability enforcement, host state/resources, package validation, editor/UI integration |
| SDK | TypeScript API, templates, developer commands, packer and release tools |
| Marketplace | Markdown/HTML builds, rendering, plugin tests, SDK consumption and published packages |

Product acceptance is optional in application CI. To run it locally, supply
prebuilt Marketplace artifacts with `NNP_TEST_CONSUMER_MARKDOWN_PACKAGE` or
`NNP_TEST_CONSUMER_HTML_PACKAGE`. Native enhancement acceptance uses both
`NNP_TEST_CONSUMER_INSPECTOR_PACKAGE` and `NNP_TEST_CONSUMER_TODO_PACKAGE`.
The application never builds those products.
Live GitHub acceptance additionally requires the existing explicit
`NNP_TEST_LIVE_MARKDOWN_REPOSITORY` / `NNP_TEST_LIVE_MARKETPLACE_REPOSITORY` settings.
Performance measurements require `NOTEPADNEXT_PERFORMANCE_PLUGIN_PACKAGE` at
CMake configuration time. They are not part of the required conformance matrix.

`stage-fixture.cjs` only copies executable files for installation tests. It does
not package or certify plugins; validation remains in the production host.
