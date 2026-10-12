// Research probe, not the application's RPC implementation or a certification suite.
// Usage: node rpc-conformance.mjs <directory containing the two installed packages>
// Tested packages: json-rpc-2.0@1.8.1 and vscode-jsonrpc@9.0.3.
import { createRequire } from 'node:module';
import { PassThrough } from 'node:stream';
import { resolve } from 'node:path';
import { readFileSync } from 'node:fs';

const require = createRequire(resolve(process.argv[2], 'package.json'));
const { JSONRPCServer } = require('json-rpc-2.0');
const rpc = require('vscode-jsonrpc/node');
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
const add = (...args) => typeof args[0] === 'object'
  ? args[0].a + args[0].b : args[0] + args[1];

const request = (id, method = 'sum', params = { a: 1, b: 2 }) =>
  ({ jsonrpc: '2.0', id, method, params });
const notification = method => ({ jsonrpc: '2.0', method, params: { a: 1, b: 2 } });
const success = (value, id) => value?.jsonrpc === '2.0' && value.id === id &&
  value.result === 3 && !('error' in value);
const error = (value, code) => value?.jsonrpc === '2.0' &&
  value.error?.code === code && !('result' in value);
const one = predicate => responses => responses.length === 1 && predicate(responses[0]);

const vectors = [
  { name: 'named-parameters', input: request('named'), check: one(v => success(v, 'named')) },
  { name: 'positional-parameters', input: request('pos', 'sum', [1, 2]), check: one(v => success(v, 'pos')) },
  { name: 'numeric-zero-id', input: request(0), check: one(v => success(v, 0)) },
  { name: 'null-id-request', input: request(null), check: one(v => success(v, null)) },
  { name: 'notification-no-response', input: notification('sum'), check: r => r.length === 0, expectedCalls: 1 },
  { name: 'unknown-method', input: request('unknown', 'missing'), check: one(v => error(v, -32601) && v.id === 'unknown') },
  { name: 'unknown-notification-no-response', input: notification('missing'), check: r => r.length === 0 },
  { name: 'malformed-json', raw: '{', check: one(v => error(v, -32700) && v.id === null) },
  { name: 'invalid-scalar', input: 1, check: one(v => error(v, -32600) && v.id === null) },
  { name: 'invalid-method-type', input: { jsonrpc: '2.0', method: 7 }, check: one(v => error(v, -32600) && v.id === null) },
  { name: 'batch-two-requests', input: [request('b1'), request('b2')],
    check: one(v => Array.isArray(v) && v.length === 2 && ['b1', 'b2'].every(id => v.some(x => success(x, id)))) },
  { name: 'batch-request-and-notification', input: [request('b1'), notification('sum')], expectedCalls: 2,
    check: one(v => Array.isArray(v) && v.length === 1 && success(v[0], 'b1')) },
  { name: 'batch-only-notifications', input: [notification('sum'), notification('sum')], expectedCalls: 2,
    check: r => r.length === 0 },
  { name: 'batch-empty-array', input: [], check: one(v => error(v, -32600) && v.id === null) },
  { name: 'batch-invalid-member', input: [request('b1'), 1],
    check: one(v => Array.isArray(v) && v.length === 2 && v.some(x => success(x, 'b1')) && v.some(x => error(x, -32600) && x.id === null)) }
];

async function probeJsonRpc(vector) {
  let calls = 0;
  const diagnostics = [];
  const server = new JSONRPCServer({ errorListener: (...args) => diagnostics.push(args.map(String).join(' ')) });
  server.addMethod('sum', params => { calls++; return Array.isArray(params) ? add(...params) : add(params); });
  const result = await server.receiveJSON(vector.raw ?? JSON.stringify(vector.input));
  return { responses: result === null || result === undefined ? [] : [result], calls, diagnostics };
}

// A research-only adapter demonstrating the two observed fixes. The production
// endpoint still needs response validation, framing limits, cancellation and
// lifecycle tests; this function handles incoming requests only.
async function probeAdaptedJsonRpc(vector) {
  let calls = 0;
  const server = new JSONRPCServer({ errorListener: () => {} });
  server.addMethod('sum', params => { calls++; return Array.isArray(params) ? add(...params) : add(params); });
  const invalid = () => ({ jsonrpc: '2.0', id: null, error: { code: -32600, message: 'Invalid Request' } });
  const valid = value => value !== null && typeof value === 'object' && !Array.isArray(value) &&
    value.jsonrpc === '2.0' && typeof value.method === 'string' &&
    (value.params === undefined || value.params !== null && typeof value.params === 'object') &&
    (value.id === undefined || value.id === null || typeof value.id === 'string' ||
      typeof value.id === 'number' && Number.isFinite(value.id));
  const dispatch = value => valid(value) ? server.receive(value) : Promise.resolve(invalid());
  let payload;
  try { payload = JSON.parse(vector.raw ?? JSON.stringify(vector.input)); }
  catch {
    return { responses: [{ jsonrpc: '2.0', id: null, error: { code: -32700, message: 'Parse error' } }], calls, diagnostics: [] };
  }
  let result;
  if (Array.isArray(payload)) {
    if (payload.length === 0) result = invalid();
    else {
      const entries = (await Promise.all(payload.map(dispatch))).filter(value => value !== null && value !== undefined);
      result = entries.length === 0 ? null : entries;
    }
  } else result = await dispatch(payload);
  return { responses: result === null || result === undefined ? [] : [result], calls, diagnostics: [] };
}

async function probeVscode(vector) {
  const incoming = new PassThrough();
  const outgoing = new PassThrough();
  const responses = [];
  const diagnostics = [];
  let calls = 0;
  let buffer = Buffer.alloc(0);
  outgoing.on('data', chunk => {
    buffer = Buffer.concat([buffer, chunk]);
    while (true) {
      const boundary = buffer.indexOf('\r\n\r\n');
      if (boundary < 0) return;
      const length = Number(/Content-Length:\s*(\d+)/i.exec(buffer.subarray(0, boundary).toString('ascii'))?.[1]);
      if (!Number.isSafeInteger(length)) throw new Error('Unexpected probe output header');
      if (buffer.length < boundary + 4 + length) return;
      responses.push(JSON.parse(buffer.subarray(boundary + 4, boundary + 4 + length).toString('utf8')));
      buffer = buffer.subarray(boundary + 4 + length);
    }
  });
  const logger = Object.fromEntries(['error', 'warn', 'info', 'log'].map(key => [key, value => diagnostics.push(String(value))]));
  const connection = rpc.createMessageConnection(new rpc.StreamMessageReader(incoming), new rpc.StreamMessageWriter(outgoing), logger);
  const handler = (...args) => { calls++; return add(...args); };
  connection.onRequest('sum', handler);
  connection.onNotification('sum', handler);
  connection.onError(event => diagnostics.push(String(event[0])));
  connection.listen();
  const payload = Buffer.from(vector.raw ?? JSON.stringify(vector.input), 'utf8');
  incoming.write(Buffer.concat([Buffer.from(`Content-Length: ${payload.length}\r\n\r\n`, 'ascii'), payload]));
  await pause(100);
  connection.dispose();
  incoming.destroy();
  outgoing.destroy();
  return { responses, calls, diagnostics };
}

const results = [];
for (const [packageName, adapter, probe] of [
  ['json-rpc-2.0', 'unmodified', probeJsonRpc],
  ['vscode-jsonrpc', 'unmodified', probeVscode],
  ['json-rpc-2.0', 'research-request-adapter', probeAdaptedJsonRpc]
]) {
  const packageVersion = JSON.parse(readFileSync(resolve(process.argv[2], 'node_modules', packageName, 'package.json'), 'utf8')).version;
  const cases = [];
  for (const vector of vectors) {
    try {
      const observation = await probe(vector);
      cases.push({ name: vector.name, pass: vector.check(observation.responses) &&
        (vector.expectedCalls === undefined || observation.calls === vector.expectedCalls), ...observation });
    } catch (e) {
      cases.push({ name: vector.name, pass: false, exception: String(e) });
    }
  }
  results.push({ packageName, packageVersion, adapter, passed: cases.filter(c => c.pass).length, total: cases.length, cases });
}
process.stdout.write(JSON.stringify({ date: new Date().toISOString().slice(0, 10), node: process.version,
  scope: '15 envelope, dispatch and batch vectors; vscode-jsonrpc observed for 100 ms per case; no Qt integration, throughput or security certification', results }, null, 2) + '\n');
