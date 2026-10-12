// SPDX-License-Identifier: GPL-3.0-or-later
const test=require('node:test'),assert=require('node:assert/strict');
const {createTestHost}=require('@notepadnext/plugin-sdk/testing');
const manifest=require('./notepadnext-plugin.json');
test('registered command shows a message and disposal ends the session',async()=>{
  const host=createTestHost({pluginId:manifest.id});
  await host.activate(require('./dist/index.js'));
  await host.invoke(manifest.contributes.commands[0].id);
  assert.equal(host.messages[0].message,'Hello from NotepadNext!');
  await host.dispose();await assert.rejects(host.invoke(manifest.contributes.commands[0].id),/disposed/);
});
