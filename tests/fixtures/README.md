# Host contract fixtures

These small executable plugins belong to the application tests. They probe the
production host protocol and lifecycle without installing dependencies,
compiling TypeScript, invoking SDK packaging tools or rendering Markdown.

`contract-preview` exercises commands, document snapshots/events, panel reuse,
restoration after reload, UI registration/installation/enabling/removal and
shutdown. Service ownership, service restart, browser state and window geometry
are also exercised by the inline fixtures in `PluginWebviewTests.cpp`.

CI ownership:

| Repository | Required checks |
| --- | --- |
| Application | Wire/API contracts, capability enforcement, host state/resources, package validation, editor/UI integration |
| SDK | TypeScript API, templates, developer commands, packer and release tools |
| Marketplace | Markdown/HTML builds, rendering, plugin tests, SDK consumption and published packages |

Product acceptance is optional in application CI. To run it locally, supply
prebuilt Marketplace artifacts with `NNP_TEST_CONSUMER_MARKDOWN_PACKAGE` or
`NNP_TEST_CONSUMER_HTML_PACKAGE`. The application never builds those products.
Live GitHub acceptance additionally requires the existing explicit
`NNP_TEST_LIVE_MARKDOWN_REPOSITORY` / `NNP_TEST_LIVE_MARKETPLACE_REPOSITORY` settings.
Performance measurements require `NOTEPADNEXT_PERFORMANCE_PLUGIN_PACKAGE` at
CMake configuration time. They are not part of the required conformance matrix.

`stage-fixture.cjs` only copies executable files for installation tests. It does
not package or certify plugins; validation remains in the production host.
