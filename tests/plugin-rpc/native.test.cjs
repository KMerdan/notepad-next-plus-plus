// SPDX-License-Identifier: GPL-3.0-or-later
const test=require('node:test'),assert=require('node:assert/strict');
const {Endpoint,RpcError}=require('../../packages/plugin-host/rpc.cjs');
const {createClient}=require('../../packages/plugin-host/client.cjs');
const identity={pluginId:'test.tools',sessionId:'native-session',generation:1,apiVersion:'0.3.0',hostVersion:'0.15.0',grantedCapabilities:['commands@1','documents.write@1'],limits:require('../../packages/plugin-host/protocol-profile.json')};
const invocation={commandId:'test.tools.upper',windowId:'window',editorId:'editor-a',documentId:'doc-a',args:['payload']};
function peers(apiVersion='0.3.0'){
 const caller=new Endpoint(json=>{void host.receive(Buffer.from(json));}),host=new Endpoint(json=>{void caller.receive(Buffer.from(json));});
 const client=createClient(caller,{...identity,apiVersion},{contributes:{commands:[{id:invocation.commandId}]}},()=>true);
 host.register('nnp.commands.register',()=>({registrationId:'registered'}));host.register('nnp.resources.dispose',()=>null);
 return {client,host,caller,async close(){await client.dispose();caller.close();host.close();}};
}
test('native callback receives one frozen invocation; positional method is absent',async()=>{
 const p=peers();let received,callbackId;
 p.host.register('nnp.commands.register',params=>{callbackId=params.callbackId;return {registrationId:'registered'};});
 try {await p.client.api.commands.register(invocation.commandId,origin=>{received=origin;return null;});
  await p.host.call('nnp.commands.invokeNative',{sessionId:identity.sessionId,generation:1,callbackId,invocation});assert.deepEqual(received,invocation);assert.equal(Object.isFrozen(received),true);
  await assert.rejects(p.host.call('nnp.commands.invoke',{sessionId:identity.sessionId,generation:1,callbackId,args:[]}),{code:-32601});
 }finally{await p.close();}
});
test('native invocation and selection guard travel over validated wire without retargeting or retries',async()=>{
 const p=peers();let callbackId,target,writes=0;
 p.host.register('nnp.commands.register',params=>{callbackId=params.callbackId;return {registrationId:'registered'};});
 p.host.register('nnp.editors.capture',params=>{target=params.invocation;throw new RpcError(1011,'SelectionConflict');});
 p.host.register('nnp.documents.applyEdits',()=>{++writes;return new Promise(()=>{});});
 try{
  await p.client.api.commands.register(invocation.commandId,async origin=>{assert.equal(Object.isFrozen(origin),true);assert.equal(Object.isFrozen(origin.args),true);await p.client.api.editors.capture(origin);});
  await assert.rejects(p.host.call('nnp.commands.invokeNative',{sessionId:identity.sessionId,generation:1,callbackId,invocation}),{code:1011});assert.deepEqual(target,invocation);
  await assert.rejects(p.client.api.documents.applyEdits('doc-a',1,[],{selectionGuard:{editorId:'editor-a',revision:4},signal:AbortSignal.timeout(10)}),error=>error.outcome==='unknown'&&error.kind==='Cancelled');assert.equal(writes,1);
  p.client.beginStopping();assert.equal(p.client.api.stopping.aborted,true);assert.equal(p.client.api.host.supports('documents.write@1'),true);assert.equal(p.client.api.host.supports('files.save@1'),false);
 }finally{await p.close();}
});
