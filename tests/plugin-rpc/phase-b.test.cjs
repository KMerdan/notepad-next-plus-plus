// SPDX-License-Identifier: GPL-3.0-or-later
const test=require('node:test'),assert=require('node:assert/strict');
const {Endpoint,RpcError}=require('../../packages/plugin-host/rpc.cjs');
const {register}=require('../../packages/plugin-host/wire.cjs');
const {createClient}=require('../../packages/plugin-host/client.cjs');
const document={documentId:'doc',uri:'untitled:doc',languageId:'text',version:1,isDirty:true,isReadonly:false,name:'Result',encoding:'utf-8',bom:false,lineEnding:'lf',byteLength:8,lineCount:1};
function peers(){let host,caller;caller=new Endpoint(json=>queueMicrotask(()=>host.receive(Buffer.from(json))));host=new Endpoint(json=>queueMicrotask(()=>caller.receive(Buffer.from(json))));const identity={sessionId:'phase-b',generation:1,apiVersion:'0.3.0',hostVersion:'0.15.0',grantedCapabilities:[],limits:require('../../packages/plugin-host/protocol-profile.json')};const client=createClient(caller,identity,{contributes:{commands:[]}},()=>true);host.register('nnp.resources.dispose',()=>null);return {host,caller,client,identity,async close(){await client.dispose();caller.close();host.close();}};}
test('native event handoff, ordering and dispose retain the subscription boundary',async()=>{
 const p=peers(),seen=[];
 // Send only protocol envelope fields; early notification arrives before the response.
 p.host.register('nnp.documents.watchNative',()=>{p.host.notify('nnp.documents.nativeEvent',{sessionId:'phase-b',generation:1,subscriptionId:'watch',eventSeq:2,type:'metadataChanged',document});return {subscriptionId:'watch',documents:[document],editors:[{editorId:'editor',documentId:'doc',windowId:'window'}],eventSeq:1};});
 try{const watch=await p.client.api.documents.watchNative(e=>seen.push(e.eventSeq));assert.deepEqual(seen,[2]);p.host.notify('nnp.documents.nativeEvent',{sessionId:'phase-b',generation:1,subscriptionId:'watch',eventSeq:1,type:'metadataChanged',document});await new Promise(r=>setImmediate(r));assert.deepEqual(seen,[2]);await watch.dispose();p.host.notify('nnp.documents.nativeEvent',{sessionId:'phase-b',generation:1,subscriptionId:'watch',eventSeq:3,type:'metadataChanged',document});await new Promise(r=>setImmediate(r));assert.deepEqual(seen,[2]);}finally{await p.close();}
});
test('file cancellation is a typed result; uncertain disk failure is never retried',async()=>{
 const p=peers();let writes=0;register(p.host,'nnp.files.saveAs',()=>({status:'cancelled',document}));register(p.host,'nnp.files.save',()=>{++writes;throw new RpcError(1012,'FileIOError');});
 try{assert.equal((await p.client.api.files.saveAs('doc',1)).status,'cancelled');await assert.rejects(p.client.api.files.save('doc',1),e=>e.kind==='FileIOError'&&e.outcome==='unknown');assert.equal(writes,1);}finally{await p.close();}
});
test('owned decorations cannot write after disposal, and stop releases resources',async()=>{
 const p=peers();let writes=0,disposed=0;register(p.host,'nnp.decorations.create',()=>({resourceId:'style'}));register(p.host,'nnp.decorations.set',()=>{++writes;return null;});p.host.register('nnp.resources.dispose',()=>{++disposed;return null;});
 try{const style=await p.client.api.decorations.create('doc',{style:'highlight',color:'#007aff'});await style.set(1,[]);await style.dispose();assert.throws(()=>style.set(1,[]),{code:1004});assert.equal(writes,1);assert.equal(disposed,1);}finally{await p.close();}
});
test('search cancellation propagates once across the protocol',async()=>{
 const p=peers();let searches=0;register(p.host,'nnp.search.findAll',()=>{++searches;return new Promise(()=>{});});
 try{await assert.rejects(p.client.api.search.findAll('doc',1,{text:'TODO'},{signal:AbortSignal.timeout(10)}),{code:1006});assert.equal(searches,1);}finally{await p.close();}
});
test('resource acquisition aborted after commit is acknowledged and disposed',async()=>{
 const p=peers();let disposed=0;register(p.host,'nnp.decorations.create',async()=>{await new Promise(r=>setTimeout(r,20));return {resourceId:'late-style'};});p.host.register('nnp.resources.dispose',()=>{++disposed;return null;});
 try{await assert.rejects(p.client.api.decorations.create('doc',{style:'highlight',color:'#007aff'},{signal:AbortSignal.timeout(5)}),{code:1006});assert.equal(disposed,1);}finally{await p.close();}
});
test('host disposal invalidates a decoration handle without a document subscription',async()=>{
 const p=peers();let disposed=0;register(p.host,'nnp.decorations.create',()=>({resourceId:'closed-style'}));p.host.register('nnp.resources.dispose',()=>{++disposed;return null;});
 try{const style=await p.client.api.decorations.create('doc',{style:'highlight',color:'#007aff'});p.host.notify('nnp.resources.nativeEvent',{sessionId:'phase-b',generation:1,resourceId:'closed-style',type:'disposed'});await new Promise(r=>setImmediate(r));assert.throws(()=>style.set(1,[]),{code:1004});await style.dispose();assert.equal(disposed,0);}finally{await p.close();}
});
