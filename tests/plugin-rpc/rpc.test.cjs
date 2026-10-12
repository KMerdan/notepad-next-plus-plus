// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fixtures = require('./fixtures.json');
const { Endpoint, Framer, RpcError } = require('../../packages/plugin-host/rpc.cjs');
function normalize(value) {
  if (Array.isArray(value)) return value.map(normalize).sort((a,b) => JSON.stringify(a).localeCompare(JSON.stringify(b)));
  if (value && typeof value === 'object') return Object.fromEntries(Object.keys(value).sort().map(key => [key, key === 'error' ? { code: value.error.code } : value[key]]));
  return value;
}
for (const fixture of fixtures) test(fixture.name, async () => {
  const replies = [], endpoint = new Endpoint(json => replies.push(json));
  endpoint.register('echo', params => params);
  endpoint.register('sum', params => { if (!Array.isArray(params)) throw new RpcError(-32602, 'Invalid params'); return params.reduce((a,b) => a+b,0); });
  endpoint.register('fail', () => { throw new Error('failure'); });
  await endpoint.receive(Buffer.from(fixture.input));
  if (fixture.idRaw) { assert.equal(replies.length, 1); assert.ok(replies[0].includes(`"id":${fixture.idRaw}}`)); }
  else if (fixture.expected === null) assert.equal(replies.length, 0);
  else { assert.equal(replies.length, 1); assert.deepEqual(normalize(JSON.parse(replies[0])), normalize(fixture.expected)); }
  endpoint.close();
});
test('framing at every split and byte', () => {
  const body = '{"text":"a😀中"}', wire = Buffer.concat([Framer.frame(body), Framer.frame('{}')]);
  for (let split = 0; split <= wire.length; ++split) {
    const frames = [], framer = new Framer(bytes => frames.push(bytes.toString()));
    framer.feed(wire.subarray(0,split)); framer.feed(wire.subarray(split)); framer.finish(); assert.deepEqual(frames, [body,'{}']);
  }
  const frames = [], framer = new Framer(bytes => frames.push(bytes)); for (const byte of wire) framer.feed(Buffer.from([byte])); framer.finish(); assert.equal(frames.length,2);
});
test('invalid framing and truncation', () => {
  for (const wire of ['Content-Length: 1\r\nContent-Length: 1\r\n\r\nx','Content-Length: -1\r\n\r\n','Content-Length: 16777217\r\n\r\n','Content-Length: 999999999999999999999999999\r\n\r\n','X-Test: value\r\n\r\n','X-Test: value\nContent-Length: 0\r\n\r\n','Content-Length: 0\r\nContent-Type: application/json; charset=latin1\r\n\r\n','Content-Length: 0\r\nContent-Type: application/json; charset=\r\n\r\n','x'.repeat(8193),'Content-Length: 0\r\nX-Test: ÿ\r\n\r\n']) {
    const framer = new Framer(() => {}); assert.throws(() => framer.feed(Buffer.from(wire))); assert.throws(() => framer.feed(Framer.frame('{}')));
  }
  const framer = new Framer(() => {}); framer.feed(Buffer.from('Content-Length: 10\r\n\r\n{}')); assert.throws(() => framer.finish());
});
test('invalid UTF8 and profile budgets', async () => {
  const replies = [], faults = [], endpoint = new Endpoint(json => replies.push(json), { fault: error => faults.push(error) });
  await endpoint.receive(Buffer.from([0xff])); assert.equal(JSON.parse(replies[0]).error.code, -32700);
  await endpoint.receive(Buffer.from(JSON.stringify(Array(65).fill(1))));
  await endpoint.receive(Buffer.from(JSON.stringify({jsonrpc:'2.0',method:'echo',id:'x'.repeat(1024)})));
  await endpoint.receive(Buffer.from('['.repeat(129)+'0'+']'.repeat(129))); assert.equal(faults.length,3); assert.equal(replies.length,1);
});
test('reliable notification overload faults explicitly instead of dropping history',async()=>{
  const faults=[],replies=[],waiting=[],endpoint=new Endpoint(json=>replies.push(json),{fault:error=>faults.push(error.code)});
  endpoint.register('delta',()=>new Promise(resolve=>waiting.push(resolve)),{reliableNotification:true});
  const message=Buffer.from(JSON.stringify({jsonrpc:'2.0',method:'delta',params:{}})),inflight=[];
  for(let index=0;index<64;++index)inflight.push(endpoint.receive(message));
  await endpoint.receive(message);assert.deepEqual(faults,[1007]);assert.equal(replies.length,0);assert.equal(endpoint.incoming.size,64);
  for(const resolve of waiting)resolve();await Promise.all(inflight);assert.equal(endpoint.incoming.size,0);endpoint.close();
});
test('bidirectional requests, cancellation, timeout, invalid response and close', async () => {
  const a = new Endpoint(json => { void b.receive(Buffer.from(json)); }), b = new Endpoint(json => { void a.receive(Buffer.from(json)); });
  a.register('host.echo', params => params); b.register('roundtrip', params => b.call('host.echo', params));
  assert.deepEqual(await a.call('roundtrip', {text:'a😀中'}), {text:'a😀中'});
  let cancelled = false;
  b.register('hang', (_, context) => new Promise((resolve,reject) => context.signal.addEventListener('abort', () => { cancelled = true; reject(new RpcError(1006,'Cancelled')); }, {once:true})));
  const controller = new AbortController(), call = a.call('hang',{}, {signal:controller.signal}); controller.abort();
  await assert.rejects(call, {code:1006}); assert.equal(cancelled,true);
  await assert.rejects(a.call('hang',{}, {timeout:10}), {code:1006}); assert.equal(a.pending.size,0);
  const outgoing = [], endpoint = new Endpoint(json => outgoing.push(json));
  const pending = endpoint.call('hang',{}); const id = JSON.parse(outgoing[0]).id;
  await endpoint.receive(Buffer.from(JSON.stringify({jsonrpc:'2.0',id,result:1,error:{code:1,message:'x'}}))); assert.equal(endpoint.pending.size,1);
  endpoint.close(); await assert.rejects(pending, {code:1004}); assert.equal(endpoint.pending.size,0); a.close(); b.close();
});
