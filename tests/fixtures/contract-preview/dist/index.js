// SPDX-License-Identifier: GPL-3.0-or-later
// Host-owned protocol probe: no Markdown parser, product UI, SDK or build step.
let api, panel, watch, revision = 0, stopped = false;
const escape = text => text.replace(/[&<>"']/g, char => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&apos;'}[char]));
async function render() {
  const ticket = ++revision, target = panel;
  if (stopped || !target) return;
  const document = await api.documents.getActive();
  const snapshot = document && await api.documents.getSnapshot(document.documentId);
  if (stopped || ticket !== revision || target !== panel) return;
  const text = snapshot?.text ?? '';
  await target.setHtml(`<h1>${escape(text.split('\n')[0])}</h1><pre><code>${escape(text)}</code></pre>`,
    {documentId: document?.documentId ?? null, version: snapshot?.version ?? null});
}
async function open() {
  if (panel) return panel.reveal();
  const created = await api.webviews.create('contract-preview', 'Contract Preview');
  panel = created;
  created.onEvent(event => {
    if (event.type === 'closed' && panel === created) {
      panel = undefined; ++revision;
      const old = watch; watch = undefined;
      void Promise.resolve(old?.dispose()).catch(console.error);
    }
  });
  watch = await api.documents.watch(() => void render().catch(console.error));
  await created.reveal(); await render();
}
exports.activate = async context => {
  api = context; stopped = false;
  api.subscriptions.push(api.webviews.registerRestorer('contract-preview', open));
  await api.commands.register('tests.contract-preview.open', open);
};
exports.deactivate = async () => {
  stopped = true; ++revision;
  await watch?.dispose(); watch = undefined;
  await panel?.dispose(); panel = undefined;
};
