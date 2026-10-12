// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const fs=require('node:fs'),path=require('node:path'),Module=require('node:module');
const {RpcError}=require('../rpc.cjs');
const {DocumentMirror}=require('./documents.cjs');
const {Disposable,EventEmitter,Position,Range,Uri}=require('./types.cjs');
const {createWebviews}=require('./webviews.cjs');
const {createPersistence}=require('./persistence.cjs');
const VERSION='0.1.0',BASELINE='1.100.0';
class UnsupportedApi extends RpcError {constructor(api){super(1005,'UnsupportedApi: '+api,{api,compatVersion:VERSION});this.name='UnsupportedApi';}}
function object(name,values,diagnostic){Object.defineProperty(values,'then',{value:undefined});return new Proxy(Object.freeze(values),{get(target,key,receiver){if(typeof key==='symbol')return Reflect.get(target,key,receiver);if(!Object.hasOwn(target,key)){const error=new UnsupportedApi(name+'.'+key);diagnostic?.(error);throw error;}return Reflect.get(target,key,receiver);}});}
function verify(manifest){
  const c=manifest.compatibility;if(!c||c.kind!=='vscode'||c.version!==VERSION||c.api!==BASELINE||c.extensionRoot!=='dist'||manifest.runtime?.kind!=='node'||!Array.isArray(c.commands))throw new RpcError(1005,'Unsupported VS Code compatibility profile');
  const aliases=new Map(),targets=new Set(),declared=new Set(manifest.contributes?.commands?.map(c=>c.id)??[]);
  for(const entry of c.commands){if(!entry||typeof entry.id!=='string'||entry.id.length>192||!/^[A-Za-z0-9_.-]+$/.test(entry.id)||aliases.has(entry.id)||targets.has(entry.target)||!declared.has(entry.target))throw new RpcError(-32602,'Invalid VS Code command aliases');aliases.set(entry.id,entry.target);targets.add(entry.target);}
  if(targets.size!==declared.size)throw new RpcError(-32602,'Every contributed command needs one VS Code alias');return aliases;
}
async function createAdapter(native,{manifest,root,fault=()=>{},diagnostic=console.error,hostVersion='0.15.0'}={}) {
  const aliases=verify(manifest),registrations=new Map(),registrationTails=new Map(),pending=new Set(),owned=new Set(),documents=new WeakMap(),editors=new WeakMap();let closed=false,active=false,failure;
  const facade=(name,values)=>object(name,values,diagnostic);
  const unsupported=api=>{const error=new UnsupportedApi(api);diagnostic(error);return error;};
  const extensionRoot=fs.realpathSync(path.join(root,manifest.compatibility.extensionRoot));if(extensionRoot!==root&&!extensionRoot.startsWith(root+path.sep))throw new RpcError(1003,'Extension root escapes package');
  const assertOpen=()=>{if(closed)throw new RpcError(1004,'VS Code adapter closed');};
  const track=promise=>{const operation=Promise.resolve(promise);pending.add(operation);operation.then(()=>pending.delete(operation),error=>{pending.delete(operation);failure??=error;diagnostic(error);if(active&&!closed)fault(error);});return operation;};
  async function flush(){for(;;){const current=[...pending];if(!current.length)break;await Promise.allSettled(current);}await persistence.flush();if(failure)throw failure;assertOpen();}
  const persistence=await createPersistence(native,{extensionRoot,assertOpen,facade,unsupported,owned,diagnostic});
  function emitter(){const value=new EventEmitter(diagnostic);owned.add(value);return value;}
  const opened=emitter(),changed=emitter(),closedDocuments=emitter(),activeEditor=emitter();
  function document(record){if(!record)return undefined;if(documents.has(record))return documents.get(record);const uri=Uri.parse(record.uri,true);
    const result=facade('TextDocument',{
      get uri(){return uri;},get fileName(){return uri.fsPath;},get languageId(){return record.languageId;},get version(){return record.version;},get isDirty(){return record.isDirty;},get isClosed(){return record.isClosed;},get isUntitled(){return record.isUntitled;},get lineCount(){return record.lineCount;},
      getText:range=>record.getText(range),offsetAt:position=>record.offsetAt(position),positionAt:offset=>{const p=record.positionAt(offset);return new Position(p.line,p.character);},
      validatePosition:value=>{const p=record.validatePosition(value);return value instanceof Position&&value.line===p.line&&value.character===p.character?value:new Position(p.line,p.character);},
      validateRange:value=>{const r=record.validateRange(value);return value instanceof Range&&value.start.isEqual(r.start)&&value.end.isEqual(r.end)?value:new Range(r.start,r.end);},
      lineAt:value=>{if(typeof value!=='number'&&!(value instanceof Position))throw new TypeError('Expected a line number or Position');const line=record.lineAt(value instanceof Position?value.line:value);return Object.freeze({...line,range:new Range(line.range.start,line.range.end),rangeIncludingLineBreak:new Range(line.rangeIncludingLineBreak.start,line.rangeIncludingLineBreak.end)});}
    });documents.set(record,result);return result;
  }
  function editor(record){if(!record)return undefined;if(editors.has(record))return editors.get(record);
    const result=facade('TextEditor',{document:document(record),viewColumn:1,async edit(callback,options={}) {
      assertOpen();if(typeof callback!=='function')throw new TypeError('Expected an edit callback');if(options.undoStopBefore===false||options.undoStopAfter===false)throw unsupported('TextEditor.edit.undoStop=false');
      if(!native.capabilities.includes('documents.write@1'))throw new RpcError(1003,'CapabilityDenied: documents.write@1');await mirror.flush();if(record.isClosed)return false;
      const version=record.version,edits=[];let valid=true;
      const add=(range,text)=>{if(!valid)throw new Error('Edit builder expired');if(typeof text!=='string')throw new TypeError('Expected edit text');if(edits.length>=256)throw new RpcError(1008,'Too many text edits');edits.push({range:new Range(range.start,range.end).toJSON(),text});};
      const builder=facade('TextEditorEdit',{insert:(position,text)=>add(new Range(position,position),text),replace:(range,text)=>add(range,text),delete:range=>add(range,'')});
      try{const returned=callback(builder);if(returned&&typeof returned.then==='function')throw unsupported('TextEditor.edit.asyncCallback');}finally{valid=false;}
      try{await native.documents.applyEdits(record.documentId,version,edits);await mirror.flush();return true;}catch(error){if([1001,1002,1003,-32602].includes(error.code))return false;throw error;}
    }});editors.set(record,result);return result;
  }
  const mirror=await new DocumentMirror(native.documents,{diagnostic,fault:error=>{failure??=error;if(!closed)fault(error);},onEvent:event=>{
    if(event.type==='opened')opened.fire(document(event.document));
    else if(event.type==='closed')closedDocuments.fire(document(event.document));
    else if(event.type==='changed')changed.fire(Object.freeze({document:document(event.document),contentChanges:Object.freeze(event.contentChanges.map(change=>Object.freeze({...change,range:new Range(change.range.start,change.range.end)}))),reason:event.reason==='undo'?1:event.reason==='redo'?2:undefined}));
    else if(event.type==='metadataChanged')changed.fire(Object.freeze({document:document(event.document),contentChanges:Object.freeze([]),reason:undefined}));
    else if(event.type==='activeChanged')activeEditor.fire(editor(event.document));
    else if(event.type==='unsupported')diagnostic(new RpcError(event.error.code,'Document unavailable to compatibility mirror: '+event.error.message,{documentId:event.documentId}));
    else if(event.type==='resynchronized'||event.type==='resynchronizedAll')diagnostic(new RpcError(1001,'Document mirror resynchronized: '+event.reason));
  }}).start();
  const subscriptions=[];
  const context=facade('ExtensionContext',{subscriptions,extensionUri:Uri.file(extensionRoot),extensionPath:extensionRoot,extensionMode:1,globalState:persistence.globalState,workspaceState:persistence.workspaceState,asAbsolutePath:relative=>{if(typeof relative!=='string'||path.isAbsolute(relative))throw new TypeError('Expected a package-relative path');const resolved=path.resolve(extensionRoot,relative);if(resolved!==extensionRoot&&!resolved.startsWith(extensionRoot+path.sep))throw new RpcError(1003,'Extension path escapes package');return resolved;}});
  function registerCommand(id,callback,thisArg){assertOpen();if(typeof callback!=='function')throw new TypeError('Expected a command callback');const target=aliases.get(id);if(!target)throw unsupported('commands.registerCommand undeclared '+id);if(registrations.has(id))throw new RpcError(-32602,'Duplicate command: '+id);
    const entry={callback,thisArg,disposed:false,remote:null};registrations.set(id,entry);
    const invoke=async(...args)=>{assertOpen();if(entry.disposed)throw new RpcError(1004,'Command disposed');await mirror.flush();await flush();const value=await callback.apply(thisArg,args);await flush();return value;};
    entry.ready=track(Promise.resolve(registrationTails.get(id)).then(()=>native.commands.register(target,invoke)).then(async remote=>{entry.remote=remote;if(entry.disposed||closed)await remote.dispose();return remote;}));
    const disposable=new Disposable(()=>{entry.disposed=true;if(registrations.get(id)===entry)registrations.delete(id);owned.delete(disposable);const cleanup=track(entry.ready.then(remote=>remote.dispose()));registrationTails.set(id,cleanup);});owned.add(disposable);return disposable;
  }
  const commands=facade('commands',{registerCommand,async executeCommand(id,...args){assertOpen();const entry=registrations.get(id);if(!entry)throw unsupported('commands.executeCommand '+id);await entry.ready;await mirror.flush();assertOpen();if(entry.disposed)throw new RpcError(1004,'Command disposed');return entry.callback.apply(entry.thisArg,args);}});
  function message(severity,text,...items){assertOpen();if(typeof text!=='string'||!text||items.some(item=>typeof item!=='string'))throw unsupported('window.showMessage object/options overload');return native.window.showMessage(severity,text,items);}
  const webviews=createWebviews(native,{root,extensionRoot,assertOpen,facade,unsupported,track,owned,diagnostic});
  const window=facade('window',{get activeTextEditor(){assertOpen();return editor(mirror.activeDocument);},onDidChangeActiveTextEditor:activeEditor.event,showInformationMessage:(...args)=>message('info',...args),showWarningMessage:(...args)=>message('warning',...args),showErrorMessage:(...args)=>message('error',...args),createWebviewPanel:webviews.create,registerWebviewPanelSerializer:webviews.registerSerializer});
  const workspace=facade('workspace',{get textDocuments(){assertOpen();return mirror.documents.map(document);},onDidOpenTextDocument:opened.event,onDidCloseTextDocument:closedDocuments.event,onDidChangeTextDocument:changed.event,getConfiguration:persistence.getConfiguration,onDidChangeConfiguration:persistence.onDidChangeConfiguration});
  class OwnedEmitter extends EventEmitter {constructor(){super(diagnostic);owned.add(this);}}
  const values={Disposable,EventEmitter:OwnedEmitter,Position,Range,Uri,commands,window,workspace,ConfigurationTarget:Object.freeze({Global:1,Workspace:2,WorkspaceFolder:3}),ViewColumn:Object.freeze({Active:-1,Beside:-2,One:1,Two:2,Three:3}),ExtensionMode:Object.freeze({Production:1,Development:2,Test:3}),TextDocumentChangeReason:Object.freeze({Undo:1,Redo:2}),version:hostVersion};
  Object.defineProperty(values,'__esModule',{value:false});const api=facade('vscode',values);
  let restore=()=>{};
  return {api,context,mirror,async ready(){await flush();await mirror.flush();await persistence.flush();active=true;persistence.activate();},install(){
    const original=Module._load;const load=function(request,parent,isMain){if(request==='vscode'&&parent?.filename&&(parent.filename===root||parent.filename.startsWith(root+path.sep)))return api;return original.apply(this,arguments);};Module._load=load;restore=()=>{if(Module._load===load)Module._load=original;};
  },async dispose(){if(closed)return;closed=true;active=false;restore();for(const value of subscriptions.splice(0).reverse())try{value.dispose();}catch(error){diagnostic(error);}for(const value of [...owned].reverse())try{value.dispose();}catch(error){diagnostic(error);}owned.clear();await persistence.dispose();await Promise.allSettled([...pending]);await mirror.dispose();registrations.clear();}};
}
module.exports={createAdapter,verify,UnsupportedApi,VERSION,BASELINE};
