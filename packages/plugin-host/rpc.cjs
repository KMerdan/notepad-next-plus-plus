// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const { TextDecoder } = require('node:util');
const { randomUUID } = require('node:crypto');
const MAX_FRAME = 16 * 1024 * 1024;
const MAX_HEADER = 8192;
const MAX_PENDING = 64;
const MAX_BATCH_RESULT = MAX_FRAME - 64 * 1024;
const utf8 = new TextDecoder('utf-8', { fatal: true });

class RpcError extends Error {
  constructor(code, message, data) { super(message); this.code = code; this.data = data; }
  toJSON() { return { code: this.code, message: this.message, ...(this.data === undefined ? {} : { data: this.data }) }; }
}

class Framer {
  constructor(onFrame) { this.onFrame = onFrame; this.buffer = Buffer.alloc(0); this.length = null; this.body = null; this.offset = 0; this.broken = false; }
  feed(chunk) {
    if (this.broken) throw new Error('RPC stream is already invalid');
    try {
      for (let offset = 0; offset < chunk.length; offset += 64 * 1024) {
        this.buffer = Buffer.concat([this.buffer, chunk.subarray(offset, offset + 64 * 1024)]);
        this.parse();
      }
    } catch (error) { this.broken = true; this.buffer = Buffer.alloc(0); this.body = null; throw error; }
  }
  parse() {
    for (;;) {
      if (this.length === null) {
        const end = this.buffer.indexOf('\r\n\r\n');
        if (end < 0) { if (this.buffer.length > MAX_HEADER) throw new Error('RPC header exceeds 8 KiB'); return; }
        if (end + 4 > MAX_HEADER) throw new Error('RPC header exceeds 8 KiB');
        const header = this.buffer.subarray(0, end);
        if (/[\r\n]/.test(header.toString('ascii').replaceAll('\r\n', ''))) throw new Error('RPC headers require CRLF');
        if ([...header].some(byte => byte > 126 || (byte < 32 && byte !== 9 && byte !== 10 && byte !== 13))) throw new Error('RPC headers must be ASCII');
        let length;
        for (const line of header.toString('ascii').split('\r\n')) {
          const match = /^([A-Za-z-]+):[ \t]*(.*)$/.exec(line);
          if (!match) throw new Error('Malformed RPC header');
          const name = match[1].toLowerCase(), value = match[2].trim();
          if (name === 'content-length') {
            if (length !== undefined || !/^\d+$/.test(value)) throw new Error('Invalid or duplicate Content-Length');
            length = Number(value);
            if (!Number.isSafeInteger(length) || length > MAX_FRAME) throw new Error('RPC frame exceeds 16 MiB');
          } else if (name === 'content-type') {
            const charset = /charset\s*=\s*"?([^;"\s]+)/i.exec(value);
            if ((/charset/i.test(value) && !charset) || (charset && charset[1].toLowerCase() !== 'utf-8')) throw new Error('Only UTF-8 RPC content is supported');
          }
        }
        if (length === undefined) throw new Error('Missing Content-Length');
        this.length = length;
        this.body = Buffer.allocUnsafe(length); this.offset = 0;
        this.buffer = this.buffer.subarray(end + 4);
      }
      const available = Math.min(this.buffer.length, this.length - this.offset);
      this.buffer.copy(this.body, this.offset, 0, available); this.offset += available;
      this.buffer = this.buffer.subarray(available);
      if (this.offset < this.length) return;
      const body = this.body; this.length = null; this.body = null; this.offset = 0;
      this.onFrame(body);
    }
  }
  finish() { if (this.broken || this.length !== null || this.buffer.length) throw new Error('Truncated RPC frame'); }
  static frame(json) { const body = Buffer.from(json, 'utf8'); return Buffer.concat([Buffer.from(`Content-Length: ${body.length}\r\n\r\n`, 'ascii'), body]); }
}

function whitespace(raw, index) { while (/\s/.test(raw[index] ?? '') && index < raw.length) ++index; return index; }
function valueEnd(raw, start) {
  let index = whitespace(raw, start);
  if (raw[index] === '"') {
    for (++index; index < raw.length; ++index) {
      if (raw[index] === '\\') ++index;
      else if (raw[index] === '"') return index + 1;
    }
  } else if (raw[index] === '{' || raw[index] === '[') {
    const closing = raw[index] === '{' ? '}' : ']';
    for (++index; index < raw.length;) {
      index = whitespace(raw, index);
      if (raw[index] === closing) return index + 1;
      if (raw[index] === ':' || raw[index] === ',') ++index;
      else index = valueEnd(raw, index);
    }
  } else {
    while (index < raw.length && !/[,\]}\s]/.test(raw[index])) ++index;
    return index;
  }
  return raw.length;
}
function arrayValues(raw) {
  let index = whitespace(raw, 0) + 1;
  const values = [];
  while (index < raw.length) {
    index = whitespace(raw, index);
    if (raw[index] === ']') break;
    if (raw[index] === ',') { ++index; continue; }
    const end = valueEnd(raw, index); values.push(raw.slice(index, end)); index = end;
  }
  return values;
}
function rawId(raw) {
  let index = whitespace(raw, 0) + 1, id = 'null';
  while (index < raw.length) {
    index = whitespace(raw, index);
    if (raw[index] === '}') break;
    if (raw[index] === ',') { ++index; continue; }
    const keyEnd = valueEnd(raw, index), key = JSON.parse(raw.slice(index, keyEnd));
    index = whitespace(raw, keyEnd) + 1; index = whitespace(raw, index);
    const end = valueEnd(raw, index);
    if (key === 'id') id = raw.slice(index, end);
    index = end;
  }
  return id;
}
const object = value => value !== null && typeof value === 'object' && !Array.isArray(value);
const has = (value, key) => Object.prototype.hasOwnProperty.call(value, key);
const validId = id => id === null || typeof id === 'string' || typeof id === 'number';
const response = (id, result, error) => `{"jsonrpc":"2.0",${error ? `"error":${JSON.stringify(error)}` : `"result":${JSON.stringify(result === undefined ? null : result)}`},"id":${id}}`;

function checkDepth(raw) {
  let quoted = false, depth = 0;
  for (let index = 0; index < raw.length; ++index) {
    const char = raw[index];
    if (quoted) { if (char === '\\') ++index; else if (char === '"') quoted = false; }
    else if (char === '"') quoted = true;
    else if (char === '{' || char === '[') { if (++depth > 128) return false; }
    else if (char === '}' || char === ']') --depth;
  }
  return true;
}

class Endpoint {
  constructor(send, options = {}) {
    this.send = send; this.methods = new Map(); this.pending = new Map(); this.incoming = new Map();
    this.diagnostic = options.diagnostic ?? (() => {});
    this.fault = options.fault ?? (error => { throw error; }); this.closed = false;
    this.reliableNotifications = new Set();
  }
  register(method, handler, {reliableNotification=false}={}) { if (method.startsWith('rpc.')) throw new Error('Reserved RPC method namespace'); this.methods.set(method, handler); if(reliableNotification)this.reliableNotifications.add(method);else this.reliableNotifications.delete(method); }
  async receive(bytes) {
    if (this.closed) return;
    let raw, value;
    try { raw = utf8.decode(bytes); value = JSON.parse(raw); }
    catch { this.send(response('null', undefined, new RpcError(-32700, 'Parse error'))); return; }
    if (!checkDepth(raw)) { this.fault(new Error('RPC JSON exceeds 128 nesting levels')); return; }
    if (!Array.isArray(value)) {
      if (object(value) && has(value, 'id') && Buffer.byteLength(rawId(raw)) > 1024) { this.fault(new Error('RPC ID token exceeds 1024 bytes')); return; }
      const reply = await this.handle(raw, value); if (reply) this.send(reply); return;
    }
    if (value.length === 0) { this.send(response('null', undefined, new RpcError(-32600, 'Invalid Request'))); return; }
    if (value.length > 64) { this.fault(new Error('RPC batch exceeds 64 entries')); return; }
    const rawValues = arrayValues(raw);
    if (value.some((item, index) => object(item) && has(item, 'id') && Buffer.byteLength(rawId(rawValues[index])) > 1024)) {
      this.fault(new Error('RPC ID token exceeds 1024 bytes')); return;
    }
    const replies = await Promise.all(value.map((item, index) => this.handle(rawValues[index], item)));
    let bytesUsed = 0;
    const bounded = replies.filter(Boolean).map(reply => {
      if (bytesUsed + Buffer.byteLength(reply) > MAX_BATCH_RESULT) {
        reply = response(rawId(reply), undefined, new RpcError(1008, 'ContentTooLarge'));
        // Retain the original ID token rather than a rounded JS number.
      }
      bytesUsed += Buffer.byteLength(reply); return reply;
    });
    if (bounded.length) this.send(`[${bounded.join(',')}]`);
  }
  async handle(raw, value) {
    const isObject = object(value), id = isObject && has(value, 'id') && validId(value.id) ? rawId(raw) : 'null';
    if (isObject && !has(value, 'method') && (has(value, 'result') || has(value, 'error'))) {
      const failure = value.error;
      const valid = value.jsonrpc === '2.0' && has(value, 'id') && validId(value.id)
        && (has(value, 'result') !== has(value, 'error'))
        && (!has(value, 'error') || (object(failure) && Number.isInteger(failure.code) && typeof failure.message === 'string'));
      const pending = this.pending.get(value.id);
      if (!valid || !pending) { this.diagnostic('Ignored invalid, unknown or late RPC response'); return; }
      this.pending.delete(value.id); pending.cleanup();
      if (failure) pending.reject(new RpcError(failure.code, failure.message, failure.data)); else pending.resolve(value.result);
      return;
    }
    if (!isObject || value.jsonrpc !== '2.0' || typeof value.method !== 'string'
      || (has(value, 'id') && !validId(value.id))
      || (has(value, 'params') && !object(value.params) && !Array.isArray(value.params)))
      return response(id, undefined, new RpcError(-32600, 'Invalid Request'));
    const notification = !has(value, 'id');
    if (value.method === 'nnp.request.cancel' && notification) {
      for (const call of this.incoming.values()) if (typeof value.params?.requestId === 'string' && call.id === value.params.requestId) call.controller.abort();
      return;
    }
    const handler = this.methods.get(value.method);
    if (!handler) return notification ? undefined : response(id, undefined, new RpcError(-32601, 'Method not found'));
    if (this.incoming.size >= MAX_PENDING) {
      if (notification) { if(this.reliableNotifications.has(value.method))this.fault(new RpcError(1007,'Reliable RPC notification queue overflow'));else this.diagnostic('RPC notification rejected: overloaded'); return; }
      return response(id, undefined, new RpcError(1007, 'Overloaded'));
    }
    const token = randomUUID(), controller = new AbortController();
    this.incoming.set(token, { id: value.id, controller });
    try {
      const result = await handler(value.params, { signal: controller.signal, notification });
      if (notification || this.closed) return;
      let reply = response(id, result);
      if (Buffer.byteLength(reply) > MAX_BATCH_RESULT) reply = response(id, undefined, new RpcError(1008, 'ContentTooLarge'));
      return reply;
    } catch (error) {
      if (notification || this.closed) { if (notification) {if(this.reliableNotifications.has(value.method)&&!this.closed)this.fault(error);else this.diagnostic('RPC notification handler failed');} return; }
      const failure = error instanceof RpcError ? error : new RpcError(-32603, 'Internal error');
      return response(id, undefined, failure);
    } finally { this.incoming.delete(token); }
  }
  call(method, params, { signal, timeout = 5000 } = {}) {
    if (this.closed || this.pending.size >= MAX_PENDING) return Promise.reject(new RpcError(this.closed ? 1004 : 1007, 'RPC connection closed or overloaded'));
    if (signal?.aborted) return Promise.reject(new RpcError(1006, 'Cancelled'));
    const id = `p:${randomUUID()}`;
    return new Promise((resolve, reject) => {
      const cancel = () => {
        if (!this.pending.has(id)) return;
        this.pending.delete(id); cleanup();
        try { this.notify('nnp.request.cancel', { requestId: id }); } catch { this.diagnostic('RPC cancellation could not be sent'); }
        reject(new RpcError(1006, 'Cancelled'));
      };
      const timer = setTimeout(() => {
        if (!this.pending.has(id)) return;
        this.pending.delete(id); cleanup();
        try { this.notify('nnp.request.cancel', { requestId: id }); } catch { this.diagnostic('RPC timeout cancellation could not be sent'); }
        reject(new RpcError(1006, 'RPC request timed out'));
      }, timeout);
      const cleanup = () => { clearTimeout(timer); signal?.removeEventListener('abort', cancel); };
      this.pending.set(id, { resolve, reject, cleanup }); signal?.addEventListener('abort', cancel, { once: true });
      try { this.send(JSON.stringify({ jsonrpc: '2.0', id, method, ...(params === undefined ? {} : { params }) })); }
      catch (error) { this.pending.delete(id); cleanup(); reject(error); }
    });
  }
  notify(method, params) {
    if (!this.closed) this.send(JSON.stringify({ jsonrpc: '2.0', method, ...(params === undefined ? {} : { params }) }));
  }
  close() {
    if (this.closed) return; this.closed = true;
    for (const call of this.pending.values()) { call.cleanup(); call.reject(new RpcError(1004, 'Plugin connection closed')); }
    this.pending.clear(); for (const call of this.incoming.values()) call.controller.abort(); this.incoming.clear();
  }
}

module.exports = { Framer, Endpoint, RpcError, MAX_FRAME, MAX_HEADER };
