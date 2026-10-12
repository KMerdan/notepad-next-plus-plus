// SPDX-License-Identifier: GPL-3.0-or-later
const test=require('node:test'), assert=require('node:assert/strict');
const fs=require('node:fs/promises'), os=require('node:os'), path=require('node:path');
const {spawn}=require('node:child_process'), {once}=require('node:events');
const {Endpoint,Framer}=require('../../packages/plugin-host/rpc.cjs');
const {createClient}=require('../../packages/plugin-host/client.cjs');
const initialize={pluginId:'test.native',sessionId:'test-session',generation:1,protocolVersions:['0.1'],apiVersion:'0.1.0',hostVersion:'0.15.0',grantedCapabilities:['commands@1','documents.read@1','documents.events@1'],limits:require('../../packages/plugin-host/protocol-profile.json')};
const session={sessionId:initialize.sessionId,generation:initialize.generation};
const document={documentId:'doc',uri:'untitled:doc',languageId:'plaintext',version:1,isDirty:true,isReadonly:false};
async function nativePackage(source) {
  const root=await fs.mkdtemp(path.join(os.tmpdir(),'nnp-host-test-'));
  await fs.mkdir(path.join(root,'dist'));
  await fs.writeFile(path.join(root,'notepadnext-plugin.json'),JSON.stringify({manifestVersion:1,id:'test.native',name:'Native test',version:'0.1.0',list:false,runtime:{kind:'node',entry:'dist/index.cjs'},capabilities:{required:initialize.grantedCapabilities,optional:[]},contributes:{commands:[{id:'test.native.read',title:'Read unsaved document'}]}}));
  await fs.writeFile(path.join(root,'dist/index.cjs'),source); return root;
}
async function runHost(root,guarded=false) {
  const env={...process.env};delete env.NNP_PARENT_WATCH_FD;delete env.NNP_PARENT_WATCH_GROUP;
  if(guarded){env.NNP_PARENT_WATCH_FD='3';env.NNP_PARENT_WATCH_GROUP='0';}
  const child=spawn(process.execPath,[path.resolve(__dirname,'../../packages/plugin-host/host.cjs'),root],{stdio:guarded?['pipe','pipe','pipe','pipe']:['pipe','pipe','pipe'],env});
  let stderr='', fatal; child.stderr.on('data',bytes=>{stderr+=bytes.toString();});
  const peer=new Endpoint(json=>child.stdin.write(Framer.frame(json))), resources=new Set(); let callbackId;
  const framer=new Framer(frame=>{ void peer.receive(frame).catch(error=>{fatal=error;}); });
  child.stdout.on('data',bytes=>{try{framer.feed(bytes);}catch(error){fatal=error;}});
  peer.register('nnp.commands.register',params=>{assert.deepEqual({sessionId:params.sessionId,generation:params.generation},session); callbackId=params.callbackId; resources.add('command');return {registrationId:'command'};});
  peer.register('nnp.documents.getSnapshot',params=>{assert.equal(params.documentId,'doc');return {...document,version:3,text:'unsaved 😀中',positionEncoding:'utf-16'};});
  peer.register('nnp.documents.watch',()=>{resources.add('watch');return {subscriptionId:'watch',eventSeq:0,document:null};});
  peer.register('nnp.resources.dispose',params=>{resources.delete(params.resourceId);return null;});
  const exited=once(child,'exit');
  return {child,peer,resources,callback:()=>callbackId,stderr:()=>stderr,fatal:()=>fatal,exited,framer};
}
test('native package lifecycle and bidirectional document read',async()=>{
  const root=await nativePackage("exports.activate=async context=>{console.log('plugin log on stderr');await context.documents.watch(()=>{});await context.commands.register('test.native.read',()=>context.documents.getSnapshot('doc'));};");
  const host=await runHost(root);
  try {
    const result=await host.peer.call('nnp.initialize',initialize); assert.equal(result.protocolVersion,'0.1');
    await host.peer.call('nnp.plugin.activate',{...session,reason:'command'}); assert.equal(host.resources.size,2);
    const snapshot=await host.peer.call('nnp.commands.invoke',{...session,callbackId:host.callback(),args:[]}); assert.equal(snapshot.text,'unsaved 😀中');
    await assert.rejects(host.peer.call('nnp.commands.invoke',{...session,generation:0,callbackId:host.callback(),args:[]}),{code:-32602});
    await host.peer.call('nnp.plugin.deactivate',{...session,reason:'disable'}); assert.equal(host.resources.size,0);
    await host.peer.call('nnp.shutdown',{...session,reason:'exit'}); const [code]=await host.exited; assert.equal(code,0); host.framer.finish();
    assert.equal(host.fatal(),undefined); assert.match(host.stderr(),/plugin log on stderr/); assert.equal(host.peer.pending.size,0);
  } finally { host.peer.close(); host.child.kill('SIGKILL'); await fs.rm(root,{recursive:true,force:true}); }
});
test('failed activation releases already registered resources',async()=>{
  const root=await nativePackage("exports.activate=async context=>{await context.commands.register('test.native.read',()=>null);throw new Error('activation fixture');};"); const host=await runHost(root);
  try {
    await host.peer.call('nnp.initialize',initialize);
    await assert.rejects(host.peer.call('nnp.plugin.activate',{...session,reason:'command'}),{code:-32603}); assert.equal(host.resources.size,0);
    await host.peer.call('nnp.shutdown',{...session,reason:'exit'}); const [code]=await host.exited; assert.equal(code,0);
  } finally { host.peer.close(); host.child.kill('SIGKILL'); await fs.rm(root,{recursive:true,force:true}); }
});
test('command exceptions retain stack diagnostics outside protocol stdout',async()=>{
  const root=await nativePackage("exports.activate=async ctx=>{await ctx.commands.register('test.native.read',()=>{throw new Error('command stack fixture');});};");const host=await runHost(root);
  try {
    await host.peer.call('nnp.initialize',initialize);await host.peer.call('nnp.plugin.activate',{...session,reason:'command'});
    await assert.rejects(host.peer.call('nnp.commands.invoke',{...session,callbackId:host.callback(),args:[]}),{code:-32603,message:'Internal error'});
    await host.peer.call('nnp.shutdown',{...session,reason:'exit'});assert.equal((await host.exited)[0],0);
    assert.match(host.stderr(),/command stack fixture/);assert.match(host.stderr(),/index\.cjs:/);assert.equal(host.fatal(),undefined);
  } finally {host.peer.close();host.child.kill('SIGKILL');await fs.rm(root,{recursive:true,force:true});}
});
test('watch buffers events that arrive before its reply',async()=>{
  const caller=new Endpoint(json=>{void host.receive(Buffer.from(json));}), host=new Endpoint(json=>{void caller.receive(Buffer.from(json));});
  const client=createClient(caller,initialize,{contributes:{commands:[]}},()=>true), events=[];
  host.register('nnp.documents.watch',()=>{
    host.notify('nnp.documents.event',{...session,subscriptionId:'watch',eventSeq:2,type:'contentInvalidated',document:{...document,version:2}});
    return {subscriptionId:'watch',eventSeq:1,document:document};
  }); host.register('nnp.resources.dispose',()=>null);
  const watch=await client.api.documents.watch(event=>events.push(event)); assert.equal(watch.document.version,1); assert.equal(events.length,1); assert.equal(events[0].document.version,2);
  await watch.dispose(); await client.dispose(); caller.close(); host.close();
});
test('delta watch buffers startup events, uses owned snapshot barriers and disposes',async()=>{
  const caller=new Endpoint(json=>{void host.receive(Buffer.from(json));}),host=new Endpoint(json=>{void caller.receive(Buffer.from(json));});
  const client=createClient(caller,initialize,{contributes:{commands:[]}},()=>true),events=[],disposed=[];
  host.register('nnp.documents.watchAll',()=>{
    host.notify('nnp.documents.delta',{...session,subscriptionId:'all',eventSeq:2,type:'changed',document:{...document,version:2},baseVersion:1,changes:[{range:{start:{line:0,character:0},end:{line:0,character:0}},rangeOffset:0,rangeLength:0,text:'x'}],reason:'edit'});
    return {subscriptionId:'all',eventSeq:1,documents:[document],activeDocumentId:'doc',maxDocuments:64,maxTotalUtf8Bytes:4194304};
  });
  host.register('nnp.documents.getMirrorSnapshot',params=>{assert.equal(params.subscriptionId,'all');assert.equal(params.documentId,'doc');return {snapshot:{...document,version:2,text:'x',positionEncoding:'utf-16'},eventSeq:2};});
  host.register('nnp.resources.dispose',params=>{disposed.push(params.resourceId);return null;});
  try {
    const watch=await client.api.documents.watchAll(event=>events.push(event));assert.equal(events.length,1);assert.equal(events[0].baseVersion,1);assert.equal((await watch.getSnapshot('doc')).eventSeq,2);
    host.notify('nnp.documents.delta',{...session,generation:2,subscriptionId:'all',eventSeq:3,type:'reset',document,reason:'bufferReset'});await new Promise(setImmediate);assert.equal(events.length,1);
    await watch.dispose();assert.throws(()=>watch.getSnapshot('doc'),{code:1004});await client.dispose();assert.deepEqual(disposed,['all']);
  }finally{await client.dispose();caller.close();host.close();}
});
test('delta startup overflow fails explicitly and releases the acquired watch',async()=>{
  const faults=[],caller=new Endpoint(json=>{void host.receive(Buffer.from(json));},{diagnostic:()=>{},fault:error=>faults.push(error.code)}),host=new Endpoint(json=>{void caller.receive(Buffer.from(json));});
  const client=createClient(caller,initialize,{contributes:{commands:[]}},()=>true),disposed=[];
  host.register('nnp.documents.watchAll',async()=>{
    for(let eventSeq=1;eventSeq<=129;++eventSeq)await caller.receive(Buffer.from(JSON.stringify({jsonrpc:'2.0',method:'nnp.documents.delta',params:{...session,subscriptionId:'overflow',eventSeq,type:'metadataChanged',document}})));
    await new Promise(setImmediate);return {subscriptionId:'overflow',eventSeq:0,documents:[document],activeDocumentId:'doc',maxDocuments:64,maxTotalUtf8Bytes:4194304};
  });host.register('nnp.resources.dispose',params=>{disposed.push(params.resourceId);return null;});
  try{await assert.rejects(client.api.documents.watchAll(()=>{}),{code:1007});assert.deepEqual(disposed,['overflow']);assert.deepEqual(faults,[1007]);}
  finally{await client.dispose();caller.close();host.close();}
});
test('incompatible version and limits reject before module import',async()=>{
  const root=await nativePackage("throw new Error('module imported');");const host=await runHost(root);
  try {
    for(const patch of [{apiVersion:'0.2.0'},{protocolVersions:['0.2']},{limits:{...initialize.limits,maxPendingRequests:65}}])
      await assert.rejects(host.peer.call('nnp.initialize',{...initialize,...patch}),{code:-32602});
    assert.doesNotMatch(host.stderr(),/module imported/);
    const result=await host.peer.call('nnp.initialize',{...initialize,protocolVersions:['0.2','0.1']});assert.equal(result.protocolVersion,'0.1');
    await host.peer.call('nnp.shutdown',{...session,reason:'exit'});assert.equal((await host.exited)[0],0);
  } finally {host.peer.close();host.child.kill('SIGKILL');await fs.rm(root,{recursive:true,force:true});}
});
test('parent-lifetime watcher permits normal shutdown', {timeout:5000,skip:process.platform==='win32'},async t=>{
  const root=await nativePackage("exports.activate=async ctx=>{await ctx.commands.register('test.native.read',()=>null);};");const host=await runHost(root,true);
  t.signal.addEventListener('abort',()=>{host.peer.close();host.child.kill('SIGKILL');},{once:true});
  try {
    await host.peer.call('nnp.initialize',initialize);await host.peer.call('nnp.plugin.activate',{...session,reason:'command'});
    await host.peer.call('nnp.shutdown',{...session,reason:'exit'});assert.deepEqual(await host.exited,[0,null]);assert.equal(host.resources.size,0);
  } finally {host.peer.close();host.child.kill('SIGKILL');await fs.rm(root,{recursive:true,force:true});}
});
test('parent pipe EOF terminates a blocked plugin main thread', {timeout:5000,skip:process.platform==='win32'},async t=>{
  const root=await nativePackage("exports.activate=async ctx=>{await ctx.commands.register('test.native.read',()=>null);setTimeout(()=>{require('node:fs').writeFileSync(require('node:path').join(__dirname,'../spinning'),'ready');while(true){}},30);};");const host=await runHost(root,true);
  t.signal.addEventListener('abort',()=>{host.peer.close();host.child.kill('SIGKILL');},{once:true});
  try {
    await host.peer.call('nnp.initialize',initialize);await host.peer.call('nnp.plugin.activate',{...session,reason:'command'});
    const start=Date.now();while(!await fs.stat(path.join(root,'spinning')).catch(()=>false)){assert.ok(Date.now()-start<2000);await new Promise(r=>setTimeout(r,10));}
    host.child.stdio[3].destroy();const [code,signal]=await host.exited;assert.equal(code,null);assert.equal(signal,'SIGKILL');assert.ok(Date.now()-start<3000);
  } finally {host.peer.close();host.child.kill('SIGKILL');await fs.rm(root,{recursive:true,force:true});}
});
test('invalid watcher descriptor rejects before plugin import', {timeout:5000},async t=>{
  const root=await nativePackage("throw new Error('plugin imported unexpectedly');");
  const child=spawn(process.execPath,[path.resolve(__dirname,'../../packages/plugin-host/host.cjs'),root],{stdio:['ignore','pipe','pipe'],env:{...process.env,NNP_PARENT_WATCH_FD:'9'}});let stdout='',stderr='';child.stdout.on('data',b=>stdout+=b);child.stderr.on('data',b=>stderr+=b);
  t.signal.addEventListener('abort',()=>child.kill('SIGKILL'),{once:true});
  try {assert.equal((await once(child,'exit'))[0],1);assert.equal(stdout,'');assert.match(stderr,/Invalid parent lifetime descriptor/);assert.doesNotMatch(stderr,/plugin imported unexpectedly/);}
  finally {child.kill('SIGKILL');await fs.rm(root,{recursive:true,force:true});}
});
