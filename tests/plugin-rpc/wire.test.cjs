// SPDX-License-Identifier: GPL-3.0-or-later
const test=require('node:test'),assert=require('node:assert/strict');
const {validate,contract,register,requireValid}=require('../../packages/plugin-host/wire.cjs');
const {Endpoint,RpcError}=require('../../packages/plugin-host/rpc.cjs');
const {createClient}=require('../../packages/plugin-host/client.cjs');
const fixtures=require('./wire-fixtures.json');
for(const fixture of fixtures)test(fixture.name,()=>assert.equal(validate(fixture.method,fixture.part,fixture.value),fixture.valid));
test('every negotiated method has positive and negative shared fixtures',()=>{
  assert.equal(Object.keys(contract.methods).length,33);
  for(const name of Object.keys(contract.methods)){
    assert.ok(fixtures.some(f=>f.method===name&&f.part==='params'&&f.valid));
    assert.ok(fixtures.some(f=>f.method===name&&f.part==='params'&&!f.valid));
  }
  assert.equal(validate('nnp.future.method','params',{}),false);
});
test('request-only lifecycle notifications perform no mutation',async()=>{
  let activated=0;const replies=[];const endpoint=new Endpoint(json=>replies.push(JSON.parse(json)));
  register(endpoint,'nnp.plugin.activate',()=>{++activated;return null;});
  const params={sessionId:'fixture-session',generation:1,reason:'command'};
  await endpoint.receive(Buffer.from(JSON.stringify({jsonrpc:'2.0',method:'nnp.plugin.activate',params})));
  assert.equal(activated,0);assert.equal(replies.length,0);
  await endpoint.receive(Buffer.from(JSON.stringify({jsonrpc:'2.0',id:'h:1',method:'nnp.plugin.activate',params})));
  assert.equal(activated,1);assert.equal(replies[0].result,null);endpoint.close();
});
test('schema guards unsafe JSON and byte budgets before transport',()=>{
  const id={sessionId:'fixture-session',generation:1};
  assert.equal(validate('nnp.commands.invoke','result',{x:undefined}),false);
  const cyclic={};cyclic.self=cyclic;assert.equal(validate('nnp.commands.invoke','result',cyclic),false);
  assert.equal(validate('nnp.webviews.setHtml','params',{...id,panelId:'panel',html:'😀'.repeat(1600000),renderContext:{documentId:null,version:null}}),false);
  assert.throws(()=>requireValid('nnp.webviews.setHtml','params',{...id,panelId:'panel',html:'😀'.repeat(1600000),renderContext:{documentId:null,version:null}}),{code:1008});
});
test('SDK undefined deletes a key by omitting its wire value',async()=>{
  const caller=new Endpoint(json=>{void host.receive(Buffer.from(json));}),host=new Endpoint(json=>{void caller.receive(Buffer.from(json));});
  const client=createClient(caller,{pluginId:'tests.data',sessionId:'fixture-session',generation:1,grantedCapabilities:['storage@1','configuration@1']},{},()=>true);
  let removed=0;for(const namespace of ['storage','configuration'])host.register(`nnp.${namespace}.update`,params=>{assert.equal(params.key,'temporary');assert.equal(Object.hasOwn(params,'value'),false);++removed;return null;});
  await client.api.storage.update('temporary',undefined);await client.api.configuration.update('temporary',undefined);assert.equal(removed,2);
  caller.close();host.close();
});
test('SDK compare-and-exchange preserves null, deletion and conflict responses',async()=>{
  const caller=new Endpoint(json=>{void host.receive(Buffer.from(json));}),host=new Endpoint(json=>{void caller.receive(Buffer.from(json));});
  const client=createClient(caller,{pluginId:'tests.data',sessionId:'fixture-session',generation:1,grantedCapabilities:['storage@1','configuration@1']},{},()=>true);
  let calls=0;for(const namespace of ['storage','configuration'])host.register(`nnp.${namespace}.update`,params=>{++calls;assert.equal(params.key,'state');assert.equal(Object.hasOwn(params,'expectedValue'),true);if(calls===1){assert.equal(params.expectedValue,null);assert.deepEqual(params.value,{count:1});}else if(calls===2){assert.deepEqual(params.expectedValue,{count:1});assert.equal(Object.hasOwn(params,'value'),false);}else throw new RpcError(1001,'Conflict');return null;});
  try{await client.api.storage.compareExchange('state',null,{count:1});await client.api.configuration.compareExchange('state',{count:1});await assert.rejects(client.api.storage.compareExchange('state',null,1),{code:1001});await assert.rejects(client.api.storage.compareExchange('state',undefined,1),{code:-32602});assert.equal(calls,3);}finally{caller.close();host.close();}
});
