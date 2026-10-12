// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const {test}=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs/promises'),os=require('node:os'),path=require('node:path');
const {spawn}=require('node:child_process'),{setTimeout:delay}=require('node:timers/promises');
const {createPlugin}=require('./create.cjs'),{createTestHost}=require('./testing.cjs'),{runDebug}=require('./debug.cjs');
async function temporary(t) {
  const root=await fs.mkdtemp(path.join(os.tmpdir(),'nnp-sdk-dev-'));
  t.after(()=>fs.rm(root,{recursive:true,force:true}));return root;
}
test('generated project is self-contained with matching command identities, schema and editor source-map setup',async t=>{
  const root=await temporary(t),directory=path.join(root,'My Markdown');
  const result=await createPlugin(directory,{template:'markdown',id:'author.markdown',name:'预览 "Markdown"'});
  const info=JSON.parse(await fs.readFile(path.join(directory,'package.json'),'utf8'));
  const manifest=JSON.parse(await fs.readFile(path.join(directory,'notepadnext-plugin.json'),'utf8'));
  assert.equal(info.devDependencies['@notepadnext/plugin-sdk'],'file:./tools/plugin-sdk');
  assert.equal(manifest.name,'预览 "Markdown"');assert.equal(result.id,manifest.id);
  assert.equal(manifest.contributes.commands[0].id,'author.markdown.open');
  assert.equal(manifest.activationEvents[0],'onCommand:author.markdown.open');
  assert.match(await fs.readFile(path.join(directory,'src/index.ts'),'utf8'),/'author.markdown.open'/);
  assert.match(await fs.readFile(path.join(directory,'.gitignore'),'utf8'),/\.debug/);
  assert.ok(JSON.parse(await fs.readFile(path.join(directory,'tsconfig.json'),'utf8')).compilerOptions.sourceMap);
  for(const file of ['index.d.ts','testing.cjs','debug.cjs','create.cjs','docs/MarkdownWalkthrough.zh-CN.md','schemas/manifest.schema.json','templates/hello/src/index.ts'])assert.ok((await fs.stat(path.join(directory,'tools/plugin-sdk',file))).isFile());
  assert.equal(await fs.readFile(path.join(directory,'tools/plugin-sdk/schemas/manifest.schema.json'),'utf8'),await fs.readFile(path.join(__dirname,'../../doc/plugin-system/manifest.schema.json'),'utf8'));
});
test('creation refuses invalid identities, recursive destinations and existing projects without changing files',async t=>{
  const root=await temporary(t),directory=path.join(root,'plugin');
  await createPlugin(directory);
  const before=await fs.readFile(path.join(directory,'package.json'));
  await assert.rejects(createPlugin(directory,{template:'markdown'}),/already exists/);
  assert.deepEqual(await fs.readFile(path.join(directory,'package.json')),before);
  for(const id of ['../escape','Author.plugin','one.two.three','a.b\n'])await assert.rejects(createPlugin(path.join(root,'bad'),{id}),/Plugin ID/);
  await assert.rejects(createPlugin(path.join(__dirname,'recursive-project')),/outside/);
  await assert.rejects(createPlugin(path.join(root,'bad'),{template:'unknown'}),/Template/);
  assert.equal((await fs.readdir(root)).filter(name=>name.startsWith('.nnp-create-')).length,0);
});
test('test host rejects early publication, delivers document events and releases resources after deactivation throws',async()=>{
  const host=createTestHost({text:'# original'});let watch,panel,events=0;
  await host.activate({async activate(context){
    await assert.rejects(context.webviews.create('preview','Preview'),/after activation/);
    await assert.rejects(context.storage.update('x',1),/after activation/);
    watch=await context.documents.watch(()=>++events);
    context.subscriptions.push(await context.commands.register('tests.open',async()=>{
      panel=await context.webviews.create('preview','Preview');
      const doc=await context.documents.getActive();
      await panel.setHtml('<h1>Preview</h1>',{documentId:doc.documentId,version:doc.version});
      await context.storage.update('x',{a:1,b:2});
      await context.storage.compareExchange('x',{b:2,a:1},3);
    }));
  },async deactivate(){throw new Error('deactivation failure');}});
  await host.invoke('tests.open');assert.equal(host.panels.size,1);assert.equal(await host.context.storage.get('x'),3);
  host.setDocument('# changed');assert.equal(events,1);
  await assert.rejects(panel.setHtml('old',{documentId:'test-document',version:1}),/Stale/);
  await assert.rejects(host.dispose(),/deactivation failure/);assert.equal(host.panels.size,0);
  await assert.rejects(host.invoke('tests.open'),/disposed/);await watch.dispose();await host.dispose();
});
function compiledPlugin(command,heading='') {
  return "let panel,api,watch;async function render(){const d=await api.documents.getActive();const s=await api.documents.getSnapshot(d.documentId);await panel.setHtml('<h1>'+s.text+"+JSON.stringify(heading)+"+'</h1>',{documentId:d.documentId,version:d.version});}exports.activate=async c=>{api=c;await c.commands.register("+JSON.stringify(command)+",async()=>{panel=await c.webviews.create('preview','Preview');watch=await c.documents.watch(()=>render());await render();});};exports.deactivate=async()=>{await watch?.dispose();await panel?.dispose();};";
}
test('debug runner loads built code from a standalone project and writes its preview',async t=>{
  const root=await temporary(t),directory=path.join(root,'project');
  await createPlugin(directory,{id:'author.debug'});await fs.mkdir(path.join(directory,'dist'));
  await fs.writeFile(path.join(directory,'dist/index.js'),compiledPlugin('author.debug.open'));
  const input=path.join(directory,'demo.md'),output=path.join(root,'preview.html');await fs.writeFile(input,'Unicode 😀中');
  await runDebug(directory,{input,output});assert.match(await fs.readFile(output,'utf8'),/<h1>Unicode 😀中<\/h1>/);
  await assert.rejects(runDebug(directory,{input,output:input}),/separate/);
});
test('watch mode reloads changed code, survives atomic input saves and stops cleanly',async t=>{
  const root=await temporary(t),directory=path.join(root,'project');
  await createPlugin(directory,{id:'author.watch'});await fs.mkdir(path.join(directory,'dist'));
  const entry=path.join(directory,'dist/index.js'),input=path.join(directory,'demo.md'),output=path.join(root,'preview.html');
  await fs.writeFile(entry,compiledPlugin('author.watch.open',' first'));
  await fs.writeFile(input,'initial');
  const child=spawn(process.execPath,[path.join(__dirname,'debug.cjs'),directory,'--input',input,'--output',output,'--watch'],{stdio:['ignore','pipe','pipe']});
  let stdout='',stderr='';child.stdout.on('data',bytes=>stdout+=bytes);child.stderr.on('data',bytes=>stderr+=bytes);
  const exit=new Promise((resolve,reject)=>{child.once('exit',code=>resolve(code));child.once('error',reject);});
  t.after(async()=>{if(child.exitCode===null){child.kill('SIGTERM');await exit;}});
  async function waitFor(check) {const end=Date.now()+5000;while(Date.now()<end){if(await check())return;await delay(30);}assert.fail('Watch timeout: '+stdout+'\n'+stderr);}
  await waitFor(()=>stdout.includes('Watching'));
  await fs.writeFile(entry,compiledPlugin('author.watch.open',' reloaded'));
  await waitFor(async()=>{try{return (await fs.readFile(output,'utf8')).includes('initial reloaded');}catch{return false;}});
  const replacement=path.join(directory,'replacement.md');await fs.writeFile(replacement,'atomic edit');await fs.rename(replacement,input);
  await waitFor(async()=>{try{return (await fs.readFile(output,'utf8')).includes('atomic edit reloaded');}catch{return false;}});
  child.kill('SIGINT');assert.equal(await exit,0,stderr);
});
