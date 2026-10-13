// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const { randomUUID,createHash,randomBytes } = require('node:crypto');
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
  const stopping=new AbortController();
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
    const mutating=method==='nnp.documents.applyEdits'||method==='nnp.core.execute'||method==='nnp.editors.setSelections'||method.startsWith('nnp.files.')||method.startsWith('nnp.decorations.')||method==='nnp.documents.convertLineEndings'||method==='nnp.languages.set'||method==='nnp.bookmarks.set';
    const nativeOptions=identity.apiVersion==='0.3.0'&&method!=='nnp.resources.dispose'?{...(method.startsWith('nnp.files.')?{timeout:600000}:{}),...options,signal:options?.signal?AbortSignal.any([options.signal,stopping.signal]):stopping.signal}:options;
    return endpoint.call(method,payload,nativeOptions).then(value=>{requireValid(method,'result',value);return value;}).catch(error=>{
      error.kind=error.message==='ReadOnlyDocument'?'ReadOnlyDocument':({1001:'VersionConflict',1011:'SelectionConflict',1002:'DocumentClosed',1003:'CapabilityDenied',1004:'PluginStopping',1005:'UnsupportedCapability',1006:error.message.includes('timed out')?'Timeout':'Cancelled',1007:'Overloaded',1008:'ContentTooLarge',1009:'UnsupportedEncoding',1012:'FileIOError'}[error.code]??'InternalError');
      if(mutating)error.outcome=[1004,1006,1012,-32603].includes(error.code)?'unknown':'notCommitted';
      throw error;
    });
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
  register(endpoint,'nnp.commands.invokeNative',async params=>{
    if(!['0.2.0','0.3.0'].includes(identity.apiVersion)||params.sessionId!==identity.sessionId||params.generation!==identity.generation||!canCall('nnp.commands.invokeNative'))throw new RpcError(1004,'PluginStopping');
    const callback=commands.get(params.callbackId);if(!callback)throw new RpcError(-32602,'Unknown command callback');
    return callback(Object.freeze({...params.invocation,args:Object.freeze([...params.invocation.args])}));
  });
  const nativeWatches=new Map(),earlyNative=[];let awaitingNative=0,earlyNativeBytes=0;
  function deliverNative(event){const entry=nativeWatches.get(event.subscriptionId);if(!entry){if(awaitingNative){const bytes=Buffer.byteLength(JSON.stringify(event));if(earlyNative.length>=128||earlyNativeBytes+bytes>2*1024*1024)throw new RpcError(1007,'Native subscription handoff overflow');earlyNativeBytes+=bytes;earlyNative.push(event);}return;}if(event.eventSeq<=entry.sequence)return;entry.sequence=event.eventSeq;return entry.listener(event);}
  register(endpoint,'nnp.documents.nativeEvent',params=>{if(params.sessionId!==identity.sessionId||params.generation!==identity.generation)return;return deliverNative(params);});
  async function watchNative(method,params,listener){
    if(typeof listener!=='function')throw new RpcError(-32602,'Invalid native listener');++awaitingNative;let handle;
    try{const result=await call(method,params);handle=resource(result.subscriptionId,()=>nativeWatches.delete(result.subscriptionId));nativeWatches.set(result.subscriptionId,{listener,sequence:result.eventSeq});
      for(let n=0;n<earlyNative.length;)if(earlyNative[n].subscriptionId===result.subscriptionId){const event=earlyNative.splice(n,1)[0];earlyNativeBytes-=Buffer.byteLength(JSON.stringify(event));await deliverNative(event);}else ++n;
      return Object.assign(handle,result);
    }catch(error){try{await handle?.dispose();}catch{}throw error;}finally{if(--awaitingNative===0){earlyNative.length=0;earlyNativeBytes=0;}}
  }
  const nativeDecorations=new Map(),earlyDisposed=new Set();let awaitingDecoration=0;
  register(endpoint,'nnp.resources.nativeEvent',event=>{
    if(event.sessionId!==identity.sessionId||event.generation!==identity.generation)return;
    const entry=nativeDecorations.get(event.resourceId);if(entry){entry.invalidate();scope.resources.delete(entry.handle);nativeDecorations.delete(event.resourceId);}else if(awaitingDecoration){if(earlyDisposed.size>=64)throw new RpcError(1007,'Resource event handoff overflow');earlyDisposed.add(event.resourceId);}
  });
  const api = {
    pluginId:identity.pluginId, sessionId:identity.sessionId, generation:identity.generation,
    capabilities:Object.freeze([...identity.grantedCapabilities]), subscriptions:[],stopping:stopping.signal,
    host:Object.freeze({product:'Notepad Next ++',applicationVersion:identity.hostVersion,apiVersion:identity.apiVersion,
      platform:process.platform==='darwin'?'macos':process.platform==='win32'?'windows':'linux',capabilities:Object.freeze([...identity.grantedCapabilities]),
      limits:Object.freeze({...identity.limits,maxEditsPerTransaction:256,maxPanelStateBytes:65536}),getNativeLimits:options=>call('nnp.host.getNativeLimits',{},options),supports:capability=>identity.grantedCapabilities.includes(capability)}),
    log:Object.freeze(Object.fromEntries(['debug','info','warn','error'].map(level=>[level,(message,details)=>console[level](message,...(details===undefined?[]:[details]))]))),
    editors:{
      watchSelections:(editorId,listener)=>watchNative('nnp.editors.watchSelections',{editorId},listener),
      activate:(editorId,expectedDocumentVersion,options={})=>call('nnp.editors.activate',{editorId,expectedDocumentVersion,preserveFocus:options.preserveFocus??false},options),
      revealRange:(editorId,expectedDocumentVersion,range,options)=>call('nnp.editors.revealRange',{editorId,expectedDocumentVersion,range},options),
      getOptions:(editorId,options)=>call('nnp.editors.getOptions',{editorId},options),
      getActive:options=>call('nnp.editors.getActive',{},options),capture:(invocation,options)=>call('nnp.editors.capture',{invocation},options),getSelections:(editorId,options)=>call('nnp.editors.getSelections',{editorId},options),
      setSelections:(editorId,expectedDocumentVersion,expectedRevision,selections,mainIndex)=>call('nnp.editors.setSelections',{editorId,expectedDocumentVersion,expectedRevision,selections,mainIndex})},
    core:{list:()=>call('nnp.core.list'),execute:(commandId,request,options)=>call('nnp.core.execute',{commandId,request},options)},
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
    files:{create:(options={},callOptions)=>call('nnp.files.create',options,callOptions),open:(uri,options={},callOptions)=>call('nnp.files.open',{uri,...options},callOptions),save:(documentId,expectedVersion,options)=>call('nnp.files.save',{documentId,expectedVersion},options),saveAs:(documentId,expectedVersion,uri,options)=>call('nnp.files.saveAs',{documentId,expectedVersion,...(uri===undefined?{}:{uri})},options),close:(documentId,expectedVersion,policy='prompt',options)=>call('nnp.files.close',{documentId,expectedVersion,policy},options)},
    search:{findAll:(documentId,expectedVersion,query,options)=>call('nnp.search.findAll',{documentId,expectedVersion,...query},options),count:(documentId,expectedVersion,query,options)=>call('nnp.search.count',{documentId,expectedVersion,...query},options)},
    languages:{list:options=>call('nnp.languages.list',{},options),getKeywords:(languageId,options)=>call('nnp.languages.getKeywords',{languageId},options),set:(documentId,expectedVersion,languageId,options)=>call('nnp.languages.set',{documentId,expectedVersion,languageId},options)},
    bookmarks:{list:(documentId,options)=>call('nnp.bookmarks.list',{documentId},options),set:(documentId,expectedVersion,lines,marked,options)=>call('nnp.bookmarks.set',{documentId,expectedVersion,lines,marked},options)},
    decorations:{async create(documentId,style,options){if(options?.signal?.aborted)throw new RpcError(1006,'Cancelled');++awaitingDecoration;try{const {resourceId}=await call('nnp.decorations.create',{documentId,...style},options?{timeout:options.timeout}:undefined);let disposed=false;const handle=resource(resourceId,()=>{disposed=true;nativeDecorations.delete(resourceId);});const dispose=handle.dispose.bind(handle);nativeDecorations.set(resourceId,{handle,invalidate:()=>{disposed=true;}});handle.dispose=async()=>{if(disposed)return;await dispose();};if(earlyDisposed.delete(resourceId)){disposed=true;scope.resources.delete(handle);nativeDecorations.delete(resourceId);}if(options?.signal?.aborted){await handle.dispose();throw new RpcError(1006,'Cancelled');}return Object.assign(handle,{resourceId,set(expectedVersion,ranges,options){if(disposed)throw new RpcError(1004,'Decoration disposed');return call('nnp.decorations.set',{resourceId,expectedVersion,ranges},options);}});}finally{if(--awaitingDecoration===0)earlyDisposed.clear();}}},
    documents:{
      readRange:(documentId,expectedVersion,range,options={})=>call('nnp.documents.readRange',{documentId,expectedVersion,range,...(options.maxBytes===undefined?{}:{maxBytes:options.maxBytes})},options),
      watchNative:listener=>watchNative('nnp.documents.watchNative',{},listener),
      convertLineEndings:(documentId,expectedVersion,lineEnding,options)=>call('nnp.documents.convertLineEndings',{documentId,expectedVersion,lineEnding},options),
      getActive: options=>call('nnp.documents.getActive',{},options),
      list:options=>call('nnp.documents.list',{},options),getInfo:(documentId,options)=>call('nnp.documents.getInfo',{documentId},options),
      getSnapshot: (documentId,options)=>call('nnp.documents.getSnapshot',{documentId},options),
      applyEdits: (documentId,expectedVersion,edits,options={})=>call('nnp.documents.applyEdits',{documentId,expectedVersion,edits,...(options.selectionGuard?{selectionGuard:options.selectionGuard}:{})},options),
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
      async createHtml(options,listener){return this.createCompatible({panelId:randomBytes(16).toString('hex'),viewType:options.viewType,title:options.title,enableScripts:options.enableScripts,localResourceRoots:options.resourceRoots??[]},listener,'nnp.webviews.createHtml');},
      async createCompatible(options,listener,method='nnp.webviews.createCompatible') {
        const scope=restoration.getStore(),restored=scope?.active?scope.panel:undefined;if(restored&&restored.viewType===options.viewType)options={...options,restorationKey:restored.panelId};
        const {panelId}=options;const listeners=new Set(listener?[listener]:[]),entry={listeners,handle:null,closed:false};
        if(panels.has(panelId))throw new RpcError(-32602,'Duplicate panel ID');panels.set(panelId,entry);
        try {
          const result=await call(method,options);
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
  },beginStopping(){stopping.abort();},async dispose() {
    stopping.abort();
    for(const resource of api.subscriptions.splice(0).reverse()) { try { await resource.dispose(); } catch {} }
    await scope.dispose();nativeDecorations.clear();earlyDisposed.clear();nativeWatches.clear();earlyNative.length=0;restorers.clear(); commands.clear(); watches.clear();allWatches.clear(); panels.clear();messages.clear();earlyMessages.length=0;earlyEvents.length=0;earlyAllEvents.length=0;earlyAllBytes=0;
  } };
}
module.exports={createClient,ResourceScope};
