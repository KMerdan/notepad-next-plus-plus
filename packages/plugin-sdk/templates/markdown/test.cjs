// SPDX-License-Identifier: GPL-3.0-or-later
const test=require('node:test'),assert=require('node:assert/strict');
const {setTimeout:delay}=require('node:timers/promises');
const {createTestHost}=require('@notepadnext/plugin-sdk/testing');
const manifest=require('./notepadnext-plugin.json');
test('Markdown follows unsaved edits, reuses and reopens its panel, then cleans up',async()=>{
  const host=createTestHost({pluginId:manifest.id,text:'# unsaved 😀中\n\n- item\n\n```js\nconst a=1;\n```\n\n<script>bad()</script>'});
  try {
    await host.activate(require('./dist/index.js'));
    const command=manifest.contributes.commands[0].id;
    await host.invoke(command);assert.equal(host.panels.size,1);
    const panel=[...host.panels.values()][0];assert.match(panel.html,/<h1>unsaved 😀中<\/h1>/);assert.match(panel.html,/<li>item<\/li>/);assert.match(panel.html,/language-js/);assert.doesNotMatch(panel.html,/<script>/);
    await host.invoke(command);assert.equal([...host.panels.values()][0],panel);
    host.setDocument('# changed');await delay(250);assert.match(panel.html,/<h1>changed<\/h1>/);
    await panel.dispose();await delay(0);assert.equal(host.panels.size,0);
    await host.invoke(command);assert.equal(host.panels.size,1);assert.notEqual([...host.panels.values()][0],panel);
  }finally{await host.dispose();}
  assert.equal(host.panels.size,0);await assert.rejects(host.invoke(manifest.contributes.commands[0].id),/disposed/);
});
