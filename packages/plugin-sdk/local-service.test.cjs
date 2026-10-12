// SPDX-License-Identifier: GPL-3.0-or-later
const {test}=require('node:test'),assert=require('node:assert/strict');
const {startLocalService}=require('./local-service.cjs');
test('local service proves activation ownership, isolates paths and closes all connections',async t=>{
  const context={sessionId:'test-session',capabilities:['webviews.service@1'],subscriptions:[]};
  const service=await startLocalService(context,(request,response,file)=>{response.end('page:'+file);});t.after(()=>service.dispose());
  assert.match(service.endpoint,/^http:\/\/127\.0\.0\.1:[0-9]+\/nnp\/[a-f0-9]{32}\/index\.html$/);
  assert.equal(await(await fetch(service.endpoint)).text(),'page:index.html');
  assert.deepEqual(await(await fetch(service.baseURL+'__nnp_probe')).json(),{sessionId:'test-session',nonce:service.nonce});
  assert.equal((await fetch(new URL('/unowned/index.html',service.endpoint))).status,404);
  assert.equal((await fetch(service.baseURL+'%2e%2e/secrets')).status,404);
  assert.equal(context.subscriptions[0],service);await service.dispose();await service.dispose();await assert.rejects(fetch(service.endpoint));
});
test('service errors stay bounded and capability is explicit',async t=>{
  await assert.rejects(startLocalService({sessionId:'x',capabilities:[]},()=>{}),/Declare/);
  const service=await startLocalService({sessionId:'x',capabilities:['webviews.service@1'],subscriptions:[]},()=>{throw new Error('private diagnostics');});t.after(()=>service.dispose());
  const response=await fetch(service.endpoint);assert.equal(response.status,500);assert.equal(await response.text(),'Service request failed');
});
