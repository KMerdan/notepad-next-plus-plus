// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs/promises'),os=require('node:os'),path=require('node:path');
const {spawn}=require('node:child_process'),{once}=require('node:events');
const {Endpoint,Framer}=require('../../packages/plugin-host/rpc.cjs');
const profile=require('../../packages/plugin-host/protocol-profile.json');
const initialize={pluginId:'tests.python',sessionId:'python-session',generation:1,protocolVersions:['0.1'],apiVersion:'0.1.0',hostVersion:'0.15.0',grantedCapabilities:['commands@1','documents.read@1','documents.events@1'],limits:profile};
const identity={sessionId:initialize.sessionId,generation:1};
async function fixture(t,code,guarded=false,caps=initialize.grantedCapabilities) {
  const root=await fs.mkdtemp(path.join(os.tmpdir(),'nnp-python-host-'));
  await fs.writeFile(path.join(root,'notepadnext-plugin.json'),JSON.stringify({manifestVersion:1,id:'tests.python',version:'0.1.0',list:false,runtime:{kind:'python',entry:'plugin.py'},capabilities:{required:caps,optional:[]},contributes:{commands:[{id:'tests.python.read',title:'Read'}]}}));
  await fs.writeFile(path.join(root,'plugin.py'),code);
  guarded=guarded&&process.platform!=='win32'; // Windows lifetime supervision uses a native job, not POSIX FD 3.
  const env={...process.env};delete env.NNP_PARENT_WATCH_FD;delete env.NNP_PARENT_WATCH_GROUP;
  if(guarded){env.NNP_PARENT_WATCH_FD='3';env.NNP_PARENT_WATCH_GROUP='0';}
  const child=spawn(process.execPath,[path.resolve(__dirname,'../../packages/plugin-host/python-host.cjs'),root,process.env.NNP_TEST_PYTHON],{env,stdio:guarded?['pipe','pipe','pipe','pipe']:['pipe','pipe','pipe']});
  let stderr='',callback,fault;const resources=new Set();child.stderr.on('data',bytes=>{stderr+=bytes.toString();});
  const peer=new Endpoint(raw=>child.stdin.write(Framer.frame(raw))),framer=new Framer(bytes=>{void peer.receive(bytes).catch(error=>{fault=error;});});child.stdout.on('data',bytes=>{try{framer.feed(bytes);}catch(error){fault=error;}});
  peer.register('nnp.commands.register',params=>{callback=params.callbackId;resources.add('command');return {registrationId:'command'};});
  peer.register('nnp.documents.getSnapshot',()=>({documentId:'doc',uri:'untitled:doc',languageId:'plaintext',version:3,isDirty:true,isReadonly:false,text:'unsaved 😀中',positionEncoding:'utf-16'}));
  peer.register('nnp.documents.watch',()=>{peer.notify('nnp.documents.event',{...identity,subscriptionId:'watch',eventSeq:2,type:'contentInvalidated',document:{documentId:'doc',uri:'untitled:doc',languageId:'plaintext',version:2,isDirty:true,isReadonly:false}});resources.add('watch');return {subscriptionId:'watch',eventSeq:1,document:null};});
  peer.register('nnp.resources.dispose',params=>{resources.delete(params.resourceId);return null;});
  const exited=once(child,'exit');
  t.after(async()=>{peer.close();if(child.exitCode===null){child.stdin.end();await Promise.race([exited,new Promise(resolve=>setTimeout(resolve,1000))]);if(child.exitCode===null)child.kill('SIGKILL');}await fs.rm(root,{recursive:true,force:true});});
  return {root,child,peer,resources,callback:()=>callback,fault:()=>fault,stderr:()=>stderr,exited,framer};
}
test('Python lifecycle, early watch events, Unicode snapshot and stdout isolation',{timeout:10000},async t=>{
  const host=await fixture(t,"import os\nasync def activate(ctx):\n    print('python print on stderr')\n    os.write(1,b'python direct fd output on stderr\\n')\n    events=[]\n    await ctx.documents.watch(lambda event: events.append(event))\n    async def read():\n        snapshot=await ctx.documents.get_snapshot('doc')\n        return {'text':snapshot['text'],'events':len(events)}\n    await ctx.commands.register('tests.python.read',read)\n",true);
  assert.equal((await host.peer.call('nnp.initialize',initialize)).protocolVersion,'0.1');await host.peer.call('nnp.plugin.activate',{...identity,reason:'command'});
  const result=await host.peer.call('nnp.commands.invoke',{...identity,callbackId:host.callback(),args:[]});assert.equal(result.text,'unsaved 😀中');assert.equal(result.events,1);assert.equal(host.resources.size,2);
  await host.peer.call('nnp.plugin.deactivate',{...identity,reason:'disable'});assert.equal(host.resources.size,0);await host.peer.call('nnp.shutdown',{...identity,reason:'exit'});assert.equal((await host.exited)[0],0);host.framer.finish();assert.equal(host.fault(),undefined);assert.match(host.stderr(),/python print on stderr/);assert.match(host.stderr(),/python direct fd output on stderr/);
});
test('Python initialize rejects incompatible contracts without importing the module',{timeout:10000},async t=>{
  const host=await fixture(t,"raise RuntimeError('module must not be imported')\n");
  for(const patch of [{apiVersion:'0.2.0'},{protocolVersions:['0.2']},{limits:{...profile,maxPendingRequests:65}}])await assert.rejects(host.peer.call('nnp.initialize',{...initialize,...patch}),{code:-32602});
  assert.doesNotMatch(host.stderr(),/module must not be imported/);await host.peer.call('nnp.initialize',initialize);await host.peer.call('nnp.shutdown',{...identity,reason:'exit'});assert.equal((await host.exited)[0],0);
});
test('Python partial activation preserves diagnostics and releases resources',{timeout:10000},async t=>{
  const host=await fixture(t,"async def activate(ctx):\n    await ctx.commands.register('tests.python.read',lambda:None)\n    raise RuntimeError('python activation stack fixture')\n");
  await host.peer.call('nnp.initialize',initialize);await assert.rejects(host.peer.call('nnp.plugin.activate',{...identity,reason:'command'}),{code:-32603});assert.equal(host.resources.size,0);await host.peer.call('nnp.shutdown',{...identity,reason:'exit'});assert.equal((await host.exited)[0],0);assert.match(host.stderr(),/python activation stack fixture/);assert.match(host.stderr(),/plugin\.py/);assert.equal(host.fault(),undefined);
});
test('Python command exceptions retain stacks outside stdout and keep the session usable',{timeout:10000},async t=>{
  const host=await fixture(t,"async def activate(ctx):\n    def fail():\n        raise RuntimeError('python command stack fixture')\n    await ctx.commands.register('tests.python.read',fail)\n");
  await host.peer.call('nnp.initialize',initialize);await host.peer.call('nnp.plugin.activate',{...identity,reason:'command'});await assert.rejects(host.peer.call('nnp.commands.invoke',{...identity,callbackId:host.callback(),args:[]}),{code:-32603,message:'Internal error'});
  await host.peer.call('nnp.health.ping',identity);await host.peer.call('nnp.shutdown',{...identity,reason:'exit'});assert.equal((await host.exited)[0],0);assert.match(host.stderr(),/python command stack fixture/);assert.match(host.stderr(),/plugin\.py/);assert.equal(host.fault(),undefined);
});
test('Python restore callback receives state and adds the original restoration key',async t=>{
  const caps=[...initialize.grantedCapabilities,'webviews@1','webviews.restore@1'];
  const host=await fixture(t,"async def activate(ctx):\n    async def restore(panel):\n        assert panel['state']['count'] == 37\n        await ctx.webviews.create('python-view', panel['title'])\n    ctx.webviews.register_restorer('python-view', restore)\n    await ctx.commands.register('tests.python.read', lambda:None)\n",false,caps);
  const key='a'.repeat(32);let creates=0;host.peer.register('nnp.webviews.create',params=>{assert.equal(params.restorationKey,key);++creates;return {panelId:key};});
  await assert.rejects(host.peer.call('nnp.webviews.restore',{...identity,panels:[]}),{code:1004});
  await host.peer.call('nnp.initialize',{...initialize,grantedCapabilities:caps});await host.peer.call('nnp.plugin.activate',{...identity,reason:'command'});
  await host.peer.call('nnp.webviews.restore',{...identity,panels:[{panelId:key,viewType:'python-view',title:'Python',kind:'fragment',hasState:true,state:{count:37}}]});assert.equal(creates,1);
  await host.peer.call('nnp.shutdown',{...identity,reason:'exit'});assert.equal((await host.exited)[0],0);
});
