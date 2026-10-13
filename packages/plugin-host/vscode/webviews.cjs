// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const path=require('node:path'),fs=require('node:fs'),{randomBytes}=require('node:crypto');
const {RpcError}=require('../rpc.cjs');
const {Uri,EventEmitter}=require('./types.cjs');
function createWebviews(native,{root,extensionRoot,assertOpen,facade,unsupported,track,owned,diagnostic}) {
  const panels=new Set();
  function column(value){if(value===undefined||value===-1||value===1)return 1;throw unsupported('WebviewPanel editor column '+value);}
  function create(viewType,initialTitle,show=1,options={},restoring=false) {
    assertOpen();if(!native.capabilities.includes('webviews@1')||!native.capabilities.includes('webviews.compat@1'))throw new RpcError(1003,'CapabilityDenied: webviews.compat@1');
    if(typeof viewType!=='string'||! /^[A-Za-z0-9_][A-Za-z0-9_.-]{0,127}$/.test(viewType)||typeof initialTitle!=='string'||!initialTitle||[...initialTitle].length>128)throw new TypeError('Invalid Webview panel declaration');
    const preserveFocus=typeof show==='object'?Boolean(show.preserveFocus):false;column(typeof show==='object'?show.viewColumn:show);
    for(const key of Object.keys(options))if(!['enableScripts','localResourceRoots','retainContextWhenHidden'].includes(key))throw unsupported('Webview option '+key);
    if(options.enableScripts!==undefined&&typeof options.enableScripts!=='boolean')throw new TypeError('Expected boolean enableScripts');
    if(options.retainContextWhenHidden!==undefined&&options.retainContextWhenHidden!==true)throw unsupported('Webview discard-context-on-hide');
    let enableScripts=options.enableScripts??false;
    const declaredRoots=options.localResourceRoots??[Uri.file(extensionRoot)];if(!Array.isArray(declaredRoots)||declaredRoots.length>16)throw new TypeError('Expected at most 16 localResourceRoots');
    let roots=Object.freeze([...declaredRoots]);
    let relativeRoots=roots.map(uri=>{if(!(uri instanceof Uri)||uri.scheme!=='file'||uri.query||uri.fragment)throw unsupported('Webview localResourceRoots URI');const canonical=fs.realpathSync(uri.fsPath);if(!canonical.startsWith(root+path.sep)||!fs.statSync(canonical).isDirectory())throw new RpcError(1003,'Resource root escapes package');return path.relative(root,canonical).split(path.sep).join('/');});
    if(panels.size>=8)throw new RpcError(1007,'Compatible panel limit reached');
    const panelId=randomBytes(16).toString('hex'),cspSource='nnp-plugin://'+native.webviews.owner;
    let creationStarted=false,disposed=false,title=initialTitle,html='',visible=Boolean(restoring?.visible),active=Boolean(restoring?.active),chain=Promise.resolve(),queued=0,queueBytes=0,panel;
    const mapped=new Map(),onDispose=new EventEmitter(diagnostic),onViewState=new EventEmitter(diagnostic),onMessage=new EventEmitter(diagnostic);
    const alive=()=>{assertOpen();if(disposed)throw new RpcError(1004,'Webview panel disposed');};
    const ready=track(Promise.resolve().then(()=>{creationStarted=true;return native.webviews.createCompatible({panelId,viewType,title:initialTitle,enableScripts,localResourceRoots:relativeRoots},event=>{
      if(disposed)return;
      if(event.type==='closed')close(false);
      else if(event.type==='message')onMessage.fire(event.payload);
      else if(event.type==='viewState'){if(visible===event.payload.visible&&active===event.payload.active)return;visible=event.payload.visible;active=event.payload.active;onViewState.fire(Object.freeze({webviewPanel:panel}));}
      else if(event.type==='loadFailed')diagnostic(new RpcError(1005,'Webview load failed: '+event.payload));
    });}));
    function enqueue(operation,{bytes=0,closing=false}={}) {
      if(!closing){alive();if(queued>=128||queueBytes+bytes>8*1024*1024)throw new RpcError(1007,'Webview proxy queue overflow');}
      ++queued;queueBytes+=bytes;
      const task=track(chain.then(async()=>{const remote=await ready;if(disposed&&!closing)return false;try{return await operation(remote);}catch(error){
        // A native user-close can overtake an already-sent title/HTML update.
        // Ownership loss ends this panel without poisoning the whole adapter.
        if(!closing&&error?.code===1004){close();return false;}throw error;
      }}));
      chain=task.then(()=>{},()=>{});task.then(()=>{--queued;queueBytes-=bytes;},()=>{--queued;queueBytes-=bytes;});return task;
    }
    function close(remote=true){if(disposed)return;disposed=true;visible=false;active=false;panels.delete(panel);owned.delete(panel);onDispose.fire(undefined);onDispose.dispose();onViewState.dispose();onMessage.dispose();mapped.clear();html='';if(remote)enqueue(handle=>handle.dispose(),{closing:true});}
    const webview=facade('Webview',{
      get html(){alive();return html;},set html(value){alive();if(typeof value!=='string')throw new TypeError('Expected HTML string');const bytes=Buffer.byteLength(value);if(bytes>6*1024*1024)throw new RpcError(1008,'HTML exceeds 6 MiB');enqueue(handle=>handle.setDocument(value),{bytes});html=value;},
      get options(){alive();return Object.freeze({enableScripts,localResourceRoots:roots});},set options(value){
        alive();if(!restoring||creationStarted)throw unsupported('Webview.options.update');
        if(!value||Object.keys(value).some(key=>!['enableScripts','localResourceRoots'].includes(key))||value.enableScripts!==undefined&&typeof value.enableScripts!=='boolean')throw new TypeError('Invalid restored Webview options');
        const next=value.localResourceRoots??[Uri.file(extensionRoot)];if(!Array.isArray(next)||next.length>16)throw new TypeError('Invalid resource roots');
        relativeRoots=next.map(uri=>{if(!(uri instanceof Uri)||uri.scheme!=='file'||uri.query||uri.fragment)throw unsupported('Webview localResourceRoots URI');const canonical=fs.realpathSync(uri.fsPath);if(!canonical.startsWith(root+path.sep)||!fs.statSync(canonical).isDirectory())throw new RpcError(1003,'Resource root escapes package');return path.relative(root,canonical).split(path.sep).join('/');});
        roots=Object.freeze([...next]);enableScripts=value.enableScripts??false;
      },
      get cspSource(){alive();return cspSource;},onDidReceiveMessage:onMessage.event,
      asWebviewUri(uri){alive();if(!(uri instanceof Uri)||uri.scheme!=='file'||uri.query||uri.fragment)throw unsupported('Webview.asWebviewUri URI');const file=fs.realpathSync(uri.fsPath);if(!relativeRoots.some(relative=>file.startsWith(path.join(root,relative)+path.sep))||!fs.statSync(file).isFile())throw new RpcError(1003,'Asset escapes localResourceRoots');
        const relative=path.relative(root,file).split(path.sep).join('/');if(relative.length>512)throw new RpcError(1008,'Asset path too long');if(!mapped.has(relative)){if(mapped.size>=64)throw new RpcError(1007,'Webview asset limit reached');const expected=Uri.from({scheme:'nnp-plugin',authority:native.webviews.owner,path:'/'+panelId+'/asset/'+relative});enqueue(async handle=>{const result=await handle.mapAsset(relative);if(result!==expected.toString())throw new RpcError(-32602,'Webview asset URI mismatch');});mapped.set(relative,expected);}return mapped.get(relative);
      },
      postMessage(message){alive();return enqueue(handle=>handle.postMessage(message));}
    });
    panel=facade('WebviewPanel',{viewType,webview,get title(){alive();return title;},set title(value){alive();if(typeof value!=='string'||!value||[...value].length>128)throw new TypeError('Invalid panel title');enqueue(handle=>handle.setTitle(value));title=value;},get viewColumn(){alive();return 1;},get visible(){alive();return visible;},get active(){alive();return active;},onDidDispose:onDispose.event,onDidChangeViewState:onViewState.event,
      reveal(viewColumn,preserveFocus=false){alive();column(viewColumn);enqueue(handle=>handle.reveal(Boolean(preserveFocus)));},dispose:()=>close(true)});
    panels.add(panel);owned.add(panel);enqueue(handle=>handle.reveal(preserveFocus));return panel;
  }
  function registerSerializer(viewType,serializer) {
    assertOpen();if(!serializer||typeof serializer.deserializeWebviewPanel!=='function')throw new TypeError('Expected WebviewPanelSerializer');
    const disposable=native.webviews.registerRestorer(viewType,async descriptor=>{
      const options=descriptor.options??{enableScripts:true,localResourceRoots:['dist']};
      const panel=create(viewType,descriptor.title,{viewColumn:1,preserveFocus:true},{enableScripts:options.enableScripts,localResourceRoots:options.localResourceRoots.map(relative=>Uri.file(path.join(root,relative)))},descriptor);
      await serializer.deserializeWebviewPanel(panel,descriptor.state);
    });owned.add(disposable);return disposable;
  }
  return {create,registerSerializer};
}
module.exports={createWebviews};
