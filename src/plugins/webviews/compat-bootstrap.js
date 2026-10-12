// SPDX-License-Identifier: GPL-3.0-or-later
// Host-injected main-world bridge. No general plugin RPC endpoint is exposed.
(() => {
    'use strict';
    const revision = __NNP_REVISION__, initialState = __NNP_STATE__;
    // Provisional/old navigations can receive the current script collection.
    // Only initialize the document whose URL owns this revision.
    if (location.protocol !== 'nnp-plugin:' || !location.pathname.endsWith('/page-' + revision + '.html')) return;
    const key = Symbol.for('NotepadNext.compatWebviewBootstrap');
    if (window[key]) { window[key].update(revision, initialState); return; }
    let record = {revision, state: initialState, acquired: false}, bridge;
    const pending = []; let pendingBytes = 0;
    const clone = value => {
        const text = JSON.stringify(value);
        if (text === undefined) throw new TypeError('Expected JSON data');
        const bytes = new TextEncoder().encode(text).length;
        if (bytes > 65536) throw new Error('Webview data exceeds 64 KiB');
        return {value: JSON.parse(text), bytes};
    };
    const send = (revision, kind, value, hasState = true) => {
        const entry = clone(value);
        if (bridge) {
            if (kind === 'message') bridge.postMessage(revision, entry.value);
            else bridge.commitState(revision, hasState, entry.value);
        } else {
            if (pending.length >= 128 || pendingBytes + entry.bytes > 8388608) throw new Error('Webview bridge queue overflow');
            pending.push({revision, kind, ...entry, hasState}); pendingBytes += entry.bytes;
        }
    };
    Object.defineProperty(window, 'acquireVsCodeApi', {value: () => {
        const current = record;
        if (current.acquired) throw new Error('An instance of the VS Code API has already been acquired');
        current.acquired = true;
        return Object.freeze({
            postMessage: message => send(current.revision, 'message', message),
            getState: () => current.state === undefined ? undefined : clone(current.state).value,
            setState: value => {
                const next = value === undefined ? undefined : clone(value).value;
                send(current.revision, 'state', next === undefined ? null : next, next !== undefined);
                current.state = next;
                return value;
            }
        });
    }});
    Object.defineProperty(window, key, {value: Object.freeze({update: (nextRevision, nextState) => {
        if (record.revision === nextRevision) return;
        record = {revision: nextRevision, state: nextState, acquired: false};
        pending.length = 0; pendingBytes = 0;
    }})});
    let attempts = 0;
    const connect = () => {
        if (typeof qt === 'undefined' || !qt.webChannelTransport) {
            if (++attempts > 128) { console.error('Webview bridge transport unavailable'); return; }
            setTimeout(connect, 10); return;
        }
        new QWebChannel(qt.webChannelTransport, channel => {
            bridge = channel.objects.panelBridge;
            for (const entry of pending.splice(0)) send(entry.revision, entry.kind, entry.value, entry.hasState);
            pendingBytes = 0;
        });
    };
    connect();
})();
