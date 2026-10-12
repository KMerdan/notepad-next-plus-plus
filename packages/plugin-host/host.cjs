// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const {pathToFileURL} = require('node:url');
const util = require('node:util');
const {Endpoint,Framer,RpcError} = require('./rpc.cjs');
const {createClient} = require('./client.cjs');
const {register} = require('./wire.cjs');
const limits=require('./protocol-profile.json');
const parentWatch=require('./parent-watch.cjs').start();
// Reserve stdout for framed protocol traffic, before importing plugin code.
for(const method of ['log','info','warn','error','debug']) console[method]=(...args)=>process.stderr.write(util.format(...args).slice(0,16384)+'\n');
if(Number(process.versions.node.split('.')[0])!==24) throw new Error('The plugin host requires the controlled Node 24 runtime');
const directory=fs.realpathSync(process.argv[2] ?? '');
const manifest=JSON.parse(fs.readFileSync(path.join(directory,'notepadnext-plugin.json'),'utf8'));
if(manifest.list!==false || manifest.runtime?.kind!=='node') throw new Error('Expected a validated native Node package');
const relativeEntry=manifest.runtime.entry;
if(typeof relativeEntry!=='string' || path.isAbsolute(relativeEntry) || relativeEntry.split(/[\\/]/).includes('..')) throw new Error('Invalid plugin entry');
const entry=fs.realpathSync(path.join(directory,relativeEntry));
if(!entry.startsWith(directory+path.sep) || !fs.statSync(entry).isFile()) throw new Error('Plugin entry escapes the package');
let state='new', identity, client, plugin, stopping, compatibility;
function fatal(error) { console.error(error?.message ?? error); endpoint.close(); process.exitCode=2; process.stdin.destroy(); process.stdout.end(()=>process.exit(2)); }
const endpoint=new Endpoint(json=>{
  if(process.stdout.writableLength+Buffer.byteLength(json)>32*1024*1024) { fatal(new Error('Plugin output queue exceeds its hard limit')); throw new RpcError(1007,'Overloaded'); }
  process.stdout.write(Framer.frame(json));
},{diagnostic:message=>console.warn(message),fault:fatal});
function requireObject(value) { if(!value || typeof value!=='object' || Array.isArray(value)) throw new RpcError(-32602,'Expected object params'); }
function validIdentity(params) { requireObject(params); if(!identity || params.sessionId!==identity.sessionId || params.generation!==identity.generation) throw new RpcError(1004,'Stale plugin session'); }
register(endpoint,'nnp.initialize',async params=>{
  await parentWatch.ready;
  requireObject(params);
  if(state!=='new') throw new RpcError(-32602,'Already initialized');
  if(params.pluginId!==manifest.id || typeof params.sessionId!=='string' || !params.sessionId || !Number.isSafeInteger(params.generation) || params.generation<1
    || !Array.isArray(params.protocolVersions) || !params.protocolVersions.includes('0.1') || !['0.1.0','0.2.0'].includes(params.apiVersion)
    || !Array.isArray(params.grantedCapabilities) || new Set(params.grantedCapabilities).size!==params.grantedCapabilities.length
    || !params.limits || Object.keys(limits).some(key=>params.limits[key]!==limits[key])) throw new RpcError(-32602,'Incompatible initialize contract');
  const declared=new Set([...(manifest.capabilities?.required ?? []),...(manifest.capabilities?.optional ?? [])]);
  if(params.grantedCapabilities.some(capability=>!declared.has(capability)) || manifest.capabilities?.required?.some(capability=>!params.grantedCapabilities.includes(capability))) throw new RpcError(1005,'UnsupportedCapability');
  identity=Object.freeze({...params});
  client=createClient(endpoint,identity,manifest,method=>{
    if(method==='nnp.resources.dispose') return state!=='new' && state!=='closed';
    if(method==='nnp.commands.invoke'||method==='nnp.commands.invokeNative')return state==='active';
    return state==='activating' || state==='active';
  });
  state='initialized';
  return {protocolVersion:'0.1',apiVersion:identity.apiVersion,capabilities:params.grantedCapabilities,limits:params.limits};
});
register(endpoint,'nnp.plugin.activate',async params=>{
  validIdentity(params); if(state!=='initialized') throw new RpcError(1004,'Plugin cannot activate in this state'); state='activating';
  try {
    if(manifest.compatibility){
      compatibility=await require('./vscode/adapter.cjs').createAdapter(client.api,{manifest,root:directory,hostVersion:identity.hostVersion,fault:error=>{if(state!=='stopping'&&state!=='closed')fatal(error);}});
      compatibility.install();client.api.subscriptions.push(compatibility);
    }
    const loaded=await import(pathToFileURL(entry).href); plugin=loaded.default ?? loaded;
    if(typeof plugin.activate!=='function') throw new Error('Plugin module must export activate(context)');
    await plugin.activate(compatibility?.context??client.api);
    await compatibility?.ready();
    if(state!=='activating') throw new RpcError(1004,'PluginStopping');
    state='active'; return null;
  } catch(error) { state='failed'; await client.dispose(); console.error(error); throw new RpcError(-32603,'Plugin activation failed'); }
});
async function deactivate() {
  if(stopping) return stopping;
  state='stopping';client?.beginStopping();
  stopping=(async()=>{ try { if(typeof plugin?.deactivate==='function') await plugin.deactivate(); } finally { await client?.dispose(); } })();
  return stopping;
}
register(endpoint,'nnp.webviews.restore',async params=>{
  validIdentity(params);if(state!=='active')throw new RpcError(1004,'Restoration requires an active plugin');
  if(!identity.grantedCapabilities.includes('webviews.restore@1'))throw new RpcError(1003,'Panel restoration was not granted');
  await client.restorePanels(params.panels);await compatibility?.ready();return null;
});
register(endpoint,'nnp.plugin.deactivate',async params=>{ validIdentity(params); await deactivate(); return null; });
register(endpoint,'nnp.health.ping',params=>{ validIdentity(params); return null; });
register(endpoint,'nnp.shutdown',async params=>{
  validIdentity(params); await deactivate();
  setImmediate(()=>{ state='closed'; endpoint.close(); process.stdin.destroy(); process.stdout.end(()=>process.exit(0)); });
  return null;
});
const framer=new Framer(frame=>{ void endpoint.receive(frame).catch(fatal); });
process.stdin.on('data',bytes=>{ try { framer.feed(bytes); } catch(error) { fatal(error); } });
process.stdin.on('end',()=>{ try { framer.finish(); } catch(error) { console.warn(error.message); } void deactivate().finally(()=>process.exit(0)); });
process.stdin.on('error',fatal); process.stdout.on('error',()=>process.exit(2));
process.on('uncaughtException',fatal); process.on('unhandledRejection',fatal);
