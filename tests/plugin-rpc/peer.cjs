// SPDX-License-Identifier: GPL-3.0-or-later
const { Endpoint, Framer, RpcError } = require('../../packages/plugin-host/rpc.cjs');
const { spawn } = require('node:child_process');
const endpoint = new Endpoint(json => process.stdout.write(Framer.frame(json)), { diagnostic: message => process.stderr.write(`${message}\n`), fault: () => process.exit(2) });
endpoint.register('peer.roundtrip', params => endpoint.call('host.echo', params));
endpoint.register('peer.hang', (_, context) => new Promise((resolve, reject) => context.signal.addEventListener('abort', () => reject(new RpcError(1006, 'Cancelled')), { once: true })));
endpoint.register('peer.child', () => spawn(process.execPath, ['-e', 'setInterval(()=>{},1000)'], { stdio: 'ignore' }).pid);
const framer = new Framer(frame => endpoint.receive(frame).catch(() => process.exit(3)));
process.stdin.on('data', bytes => { try { framer.feed(bytes); } catch { process.exit(2); } });
process.stdin.on('end', () => { endpoint.close(); process.exit(0); });
