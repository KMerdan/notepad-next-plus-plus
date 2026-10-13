// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const { randomUUID,createHash } = require('node:crypto');
const {AsyncLocalStorage}=require('node:async_hooks');
const { RpcError } = require('./rpc.cjs');
const {requireValid,register}=require('./wire.cjs');
class ResourceScope {
  constructor() { this.resources = new Set(); this.closed = false; }
  own(resource) { if (this.closed) { void Promise.resolve().then(()=>resource.dispose()).catch(error=>console.warn(error)); throw new RpcError(1004,'PluginStopping'); } this.resources.add(resource); return resource; }
  async dispose() {
    if (this.closed) return; this.closed = true;
    for (const resource of [...this.resources].reverse()) { try { await resource.dispose(); } catch {} }
    this.resources.clear();
  }
}
function createClient(endpoint, identity, manifest, canCall) {
  const scope = new ResourceScope(), commands = new Map(), watches = new Map(), panels = new Map();
  const restorers=new Map(),restoration=new AsyncLocalStorage();
  const earlyEvents = []; let awaitingWatch = 0;
  const allWatches=new Map(),earlyAllEvents=[];let awaitingAll=0,earlyAllBytes=0,earlyAllFailure;
  const messages=new Map(),earlyMessages=[];let awaitingMessage=0;
  const envelope = params => ({ ...params, sessionId:identity.sessionId, generation:identity.generation });
  const call = (method, params = {}, options) => {
    if (!canCall(method)) return Promise.reject(new RpcError(1004,'PluginStopping'));
    const payload=envelope(params);
    try {requireValid(method,'params',payload);} catch(error) {return Promise.reject(error);}
    return endpoint.call(method,payload,options).then(value=>{requireValid(method,'result',value);return value;});
  };
  function resource(id, onDispose = () => {}) {
    let disposed = false;
    const handle = { async dispose() {
      if (disposed) return; disposed = true; scope.resources.delete(handle); onDispose();
      await call('nnp.resources.dispose',{resourceId:id});
    } };
    return scope.own(handle);
  }
  function deliverDocument(event) {
    const subscription = watches.get(event.subscriptionId);
    if (!subscription) {
      if (awaitingWatch) { if (earlyEvents.length >= 128) throw new RpcError(1007,'Document event buffer overflow'); earlyEvents.push(event); }
      return;
    }
    if (!Number.isSafeInteger(event.eventSeq) || event.eventSeq <= subscription.sequence) return;
    subscription.sequence = event.eventSeq; subscription.listener(event);
  }
  register(endpoint,'nnp.documents.event',params => {
    if (params?.sessionId !== identity.sessionId || params?.generation !== identity.generation) return;
    deliverDocument(params);
  });
  function deliverDelta(event) {
    const subscription=allWatches.get(event.subscriptionId);
    if(!subscription) {
      if(awaitingAll) {
        const bytes=Buffer.byteLength(JSON.stringify(event));
        if(earlyAllEvents.length>=128||earlyAllBytes+bytes>8*1024*1024){earlyAllFailure=new RpcError(1007,'Reliable document event buffer overflow');throw earlyAllFailure;}
        earlyAllBytes+=bytes;earlyAllEvents.push(event);
      }
      return;
    }
    if(event.eventSeq<=subscription.sequence)return;
    subscription.sequence=event.eventSeq;return subscription.listener(event);
  }
  register(endpoint,'nnp.documents.delta',params=>{
    if(params.sessionId!==identity.sessionId||params.generation!==identity.generation)return;
    return deliverDelta(params);
  });
  register(endpoint,'nnp.webviews.event',params => {
    if (params?.sessionId !== identity.sessionId || params?.generation !== identity.generation) return;
    const panel = panels.get(params.panelId); if (!panel) return;
    if (params.type === 'closed') { panel.closed=true;panels.delete(params.panelId); scope.resources.delete(panel.handle); }
    for (const listener of [...panel.listeners]) { try { listener(params); } catch (error) { console.error(error); } }
    if (params.type === 'closed') panel.listeners.clear();
  });
  function deliverMessage(event) {
    const message=messages.get(event.messageId);
    if(!message){if(awaitingMessage){if(earlyMessages.length>=128)throw new RpcError(1007,'Message event buffer overflow');earlyMessages.push(event);}return;}
    messages.delete(event.messageId);scope.resources.delete(message.handle);message.resolve(event.selected??undefined);
  }
  register(endpoint,'nnp.window.messageEvent',params=>{
    if(params.sessionId!==identity.sessionId||params.generation!==identity.generation)return;
    deliverMessage(params);
  });
  register(endpoint,'nnp.commands.invoke',async params => {
    if (params?.sessionId !== identity.sessionId || params?.generation !== identity.generation || !Array.isArray(params?.args)) throw new RpcError(-32602,'Invalid command invocation');
    if (!canCall('nnp.commands.invoke')) throw new RpcError(1004,'PluginStopping');
    const callback = commands.get(params.callbackId); if (!callback) throw new RpcError(-32602,'Unknown command callback');
    try {return await callback(...params.args);}
    catch(error) {console.error(error);throw error;}
  });
  const api = {
    pluginId:identity.pluginId, sessionId:identity.sessionId, generation:identity.generation,
    capabilities:Object.freeze([...identity.grantedCapabilities]), subscriptions:[],
    commands:{ async register(commandId,callback) {
      if (typeof callback !== 'function' || !manifest.contributes?.commands?.some(command => command.id === commandId)) throw new RpcError(-32602,'Command is not declared by this plugin');
      const callbackId=randomUUID(); commands.set(callbackId,callback);
      try { const {registrationId}=await call('nnp.commands.register',{commandId,callbackId}); return resource(registrationId,()=>commands.delete(callbackId)); }
      catch(error) { commands.delete(callbackId); throw error; }
    } },
    window:{async showMessage(severity,message,items=[]) {
      ++awaitingMessage;
      try {
        const result=await call('nnp.window.showMessage',{severity,message,items});
        let resolve;const selected=new Promise(done=>{resolve=done;});
        const handle=resource(result.messageId,()=>{messages.delete(result.messageId);resolve(undefined);});
        messages.set(result.messageId,{handle,resolve});
        for(let index=0;index<earlyMessages.length;)if(earlyMessages[index].messageId===result.messageId)deliverMessage(earlyMessages.splice(index,1)[0]);else ++index;
        return selected;
      }finally{if(--awaitingMessage===0)earlyMessages.length=0;}
    }},
    documents:{
      getActive: options=>call('nnp.documents.getActive',{},options),
      getSnapshot: (documentId,options)=>call('nnp.documents.getSnapshot',{documentId},options),
      applyEdits: (documentId,expectedVersion,edits)=>call('nnp.documents.applyEdits',{documentId,expectedVersion,edits}),
      async watch(listener) {
        if (typeof listener !== 'function') throw new RpcError(-32602,'Invalid document listener');
        ++awaitingWatch;
        try {
          const result=await call('nnp.documents.watch',{target:'active',mode:'invalidate'});
          watches.set(result.subscriptionId,{listener,sequence:result.eventSeq});
          const handle=resource(result.subscriptionId,()=>watches.delete(result.subscriptionId));
          for(let index=0;index<earlyEvents.length;) {
            if(earlyEvents[index].subscriptionId===result.subscriptionId) deliverDocument(earlyEvents.splice(index,1)[0]); else ++index;
          }
          return Object.assign(handle,{document:result.document,eventSeq:result.eventSeq});
        } finally { if(--awaitingWatch===0) earlyEvents.length=0; }
      },
      async watchAll(listener) {
        if(typeof listener!=='function')throw new RpcError(-32602,'Invalid document mirror listener');
        ++awaitingAll;let owned;
        try {
          const result=await call('nnp.documents.watchAll');
          const handle=resource(result.subscriptionId,()=>allWatches.delete(result.subscriptionId));owned=handle;
          if(earlyAllFailure){await handle.dispose();throw earlyAllFailure;}
          allWatches.set(result.subscriptionId,{listener,sequence:result.eventSeq});
          for(let index=0;index<earlyAllEvents.length;) {
            if(earlyAllEvents[index].subscriptionId===result.subscriptionId){const event=earlyAllEvents.splice(index,1)[0];earlyAllBytes-=Buffer.byteLength(JSON.stringify(event));await deliverDelta(event);}else ++index;
          }
          return Object.assign(handle,result,{getSnapshot(documentId,options){if(!allWatches.has(result.subscriptionId))throw new RpcError(1004,'Document mirror closed');return call('nnp.documents.getMirrorSnapshot',{subscriptionId:result.subscriptionId,documentId},options);}});
        } catch(error){try{await owned?.dispose();}catch{}throw error;} finally {if(--awaitingAll===0){earlyAllEvents.length=0;earlyAllBytes=0;earlyAllFailure=undefined;}}
      }
    },
    webviews:{
      owner:createHash('sha256').update(identity.sessionId).digest('hex').slice(0,32),
      registerRestorer(viewType,callback){
        if(typeof viewType!=='string'||typeof callback!=='function'||restorers.has(viewType))throw new RpcError(-32602,'Invalid or duplicate panel restorer');
        restorers.set(viewType,callback);return {dispose:()=>restorers.delete(viewType)};
      },
      async createCompatible(options,listener) {
        const scope=restoration.getStore(),restored=scope?.active?scope.panel:undefined;if(restored&&restored.viewType===options.viewType)options={...options,restorationKey:restored.panelId};
        const {panelId}=options;const listeners=new Set(listener?[listener]:[]),entry={listeners,handle:null,closed:false};
        if(panels.has(panelId))throw new RpcError(-32602,'Duplicate panel ID');panels.set(panelId,entry);
        try {
          const result=await call('nnp.webviews.createCompatible',options);
          if(result.panelId!==panelId)throw new RpcError(-32602,'Compatible panel ID mismatch');
          const handle=resource(panelId,()=>{panels.delete(panelId);listeners.clear();});entry.handle=handle;
          if(entry.closed)await handle.dispose();
          const alive=()=>{if(entry.closed||!panels.has(panelId))throw new RpcError(1004,'Panel closed');};
          return Object.assign(handle,{panelId,
            reveal(preserveFocus=false){alive();return call('nnp.webviews.reveal',{panelId,preserveFocus});},
            setDocument(html){alive();return call('nnp.webviews.setDocument',{panelId,html});},
            setTitle(title){alive();return call('nnp.webviews.setTitle',{panelId,title});},
            mapAsset(path){alive();return call('nnp.webviews.mapAsset',{panelId,path});},
            postMessage(message){alive();return call('nnp.webviews.postMessage',{panelId,message});},
            onEvent(listener){alive();listeners.add(listener);return {dispose:()=>listeners.delete(listener)};}
          });
        }catch(error){panels.delete(panelId);listeners.clear();throw error;}
      },
      async createService(options) {
        const scope=restoration.getStore(),restored=scope?.active?scope.panel:undefined;if(restored?.viewType===options.viewType)options={...options,restorationKey:restored.panelId};
        const {panelId}=await call('nnp.webviews.createService',options);
        const listeners=new Set(),handle=resource(panelId,()=>{panels.delete(panelId);listeners.clear();});panels.set(panelId,{listeners,handle});
        const alive=()=>{if(!panels.has(panelId))throw new RpcError(1004,'Panel closed');};
        return Object.assign(handle,{panelId,reveal(preserveFocus=false){alive();return call('nnp.webviews.reveal',{panelId,preserveFocus});},postMessage(message){alive();return call('nnp.webviews.postMessage',{panelId,message});},onEvent(listener){alive();listeners.add(listener);return {dispose:()=>listeners.delete(listener)};}});
      },
      async create(viewType,title,position='right') {
      const scope=restoration.getStore(),restored=scope?.active?scope.panel:undefined;const {panelId}=await call('nnp.webviews.create',{viewType,title,position,...(restored?.viewType===viewType?{restorationKey:restored.panelId}:{})});
      const listeners=new Set(), handle=resource(panelId,()=>{ panels.delete(panelId); listeners.clear(); });
      panels.set(panelId,{listeners,handle});
      const alive = () => { if(!panels.has(panelId)) throw new RpcError(1004,'Panel closed'); };
      return Object.assign(handle,{
        panelId,
        reveal(preserveFocus=false) { alive(); return call('nnp.webviews.reveal',{panelId,preserveFocus}); },
        setHtml(html,renderContext) { alive(); return call('nnp.webviews.setHtml',{panelId,html,renderContext}); },
        postMessage(message) { alive(); return call('nnp.webviews.postMessage',{panelId,message}); },
        mapResource(resource) { alive(); return call('nnp.webviews.mapResource',{panelId,resource}); },
        onEvent(listener) { alive(); listeners.add(listener); return {dispose:()=>listeners.delete(listener)}; }
      });
    } },
    storage:{get:key=>call('nnp.storage.get',{key}),update:(key,value)=>call('nnp.storage.update',value===undefined?{key}:{key,value}),compareExchange:(key,expectedValue,value)=>call('nnp.storage.update',value===undefined?{key,expectedValue}:{key,expectedValue,value})},
    configuration:{get:key=>call('nnp.configuration.get',{key}),update:(key,value)=>call('nnp.configuration.update',value===undefined?{key}:{key,value}),compareExchange:(key,expectedValue,value)=>call('nnp.configuration.update',value===undefined?{key,expectedValue}:{key,expectedValue,value})}
  };
  return { api,async restorePanels(descriptors){
    for(const panel of descriptors){const callback=restorers.get(panel.viewType);if(!callback)throw new RpcError(1005,'No restorer registered for '+panel.viewType);
      const descriptor=Object.freeze({...panel,state:panel.hasState?panel.state:undefined}),scope={panel:descriptor,active:true};
      try{await restoration.run(scope,()=>callback(descriptor));}finally{scope.active=false;}
    }
    return null;
  },async dispose() {
    for(const resource of api.subscriptions.splice(0).reverse()) { try { await resource.dispose(); } catch {} }
    await scope.dispose();restorers.clear(); commands.clear(); watches.clear();allWatches.clear(); panels.clear();messages.clear();earlyMessages.length=0;earlyEvents.length=0;earlyAllEvents.length=0;earlyAllBytes=0;
  } };
}
module.exports={createClient,ResourceScope};
