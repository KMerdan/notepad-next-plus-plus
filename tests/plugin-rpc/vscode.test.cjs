// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs/promises'),path=require('node:path'),os=require('node:os');
const {createRequire}=require('node:module');
const {isDeepStrictEqual}=require('node:util');
const {KEYS,configurationDefinitions}=require('../../packages/plugin-host/vscode/persistence.cjs');
const {createAdapter,verify}=require('../../packages/plugin-host/vscode/adapter.cjs');
const {Disposable,EventEmitter,Position,Range,Uri}=require('../../packages/plugin-host/vscode/types.cjs');
const {Endpoint,RpcError}=require('../../packages/plugin-host/rpc.cjs');
const {createClient}=require('../../packages/plugin-host/client.cjs');
const info=(version=1)=>({documentId:'a',uri:'untitled:a',languageId:'text',version,isDirty:true,isReadonly:false});
const manifest=()=>({id:'tests.compat',runtime:{kind:'node',entry:'dist/out/extension.js'},compatibility:{kind:'vscode',version:'0.1.0',api:'1.100.0',extensionRoot:'dist',commands:[{id:'extension.read',target:'tests.compat.read'}]},contributes:{commands:[{id:'tests.compat.read',title:'Read'}]}});
const tick=()=>new Promise(setImmediate);
class Native {
  constructor({data=false,backend=new Map()}={}){this.capabilities=['commands@1','documents.read@1','documents.delta@1','documents.write@1','messages@1'];this.text='a😀中\r\né\n';this.version=1;this.sequence=0;this.listeners=new Set();this.registrations=new Map();this.disposals=0;this.registrationHook=null;this.editFailure=null;this.messageHook=null;
    this.backend=backend;this.dataHook=null;this.dataCalls=[];
    if(data){this.capabilities.push('storage@1','configuration@1');for(const space of ['storage','configuration'])this[space]={get:async key=>{await this.dataHook?.('get',space,key);return structuredClone(backend.get(space+':'+key)??null);},compareExchange:async(key,expected,value)=>{this.dataCalls.push([space,key]);await this.dataHook?.('write',space,key);if(!isDeepStrictEqual(backend.get(space+':'+key)??null,expected))throw new RpcError(1001,'Data changed');backend.set(space+':'+key,structuredClone(value));}};}
    this.documents={watchAll:async listener=>{this.listeners.add(listener);return {subscriptionId:'all',eventSeq:this.sequence,maxDocuments:64,maxTotalUtf8Bytes:4194304,documents:[info(this.version)],activeDocumentId:'a',getSnapshot:async()=>({snapshot:{...info(this.version),text:this.text,positionEncoding:'utf-16'},eventSeq:this.sequence}),dispose:async()=>{this.listeners.delete(listener);++this.disposals;}};},applyEdits:async(id,version,edits)=>{assert.equal(id,'a');assert.equal(version,this.version);if(this.editFailure)throw new RpcError(this.editFailure,'Edit denied');this.edits=edits;const before=this.text,baseVersion=this.version;const changes=edits.map(e=>{const starts=[0];for(let n=0;n<before.length;++n)if(before[n]==='\n')starts.push(n+1);const begin=starts[e.range.start.line]+e.range.start.character,end=starts[e.range.end.line]+e.range.end.character;return {...e,rangeOffset:begin,rangeLength:end-begin};});for(const change of [...changes].sort((a,b)=>b.rangeOffset-a.rangeOffset))this.text=this.text.slice(0,change.rangeOffset)+change.text+this.text.slice(change.rangeOffset+change.rangeLength);++this.version;this.emit({type:'changed',document:info(this.version),baseVersion,changes,reason:'edit'});return {document:info(this.version)};}};
    this.commands={register:async(id,callback)=>{await this.registrationHook?.();if(this.registrations.has(id))throw new Error('Remote duplicate');this.registrations.set(id,callback);let disposed=false;return {dispose:async()=>{if(disposed)return;disposed=true;this.registrations.delete(id);++this.disposals;}};}};
    this.window={showMessage:(...args)=>this.messageHook?.(...args)??Promise.resolve(undefined)};
  }
  emit(event){for(const listener of this.listeners)listener({subscriptionId:'all',eventSeq:++this.sequence,...event});}
}
async function fixture(options={}){const root=await fs.realpath(await fs.mkdtemp(path.join(os.tmpdir(),'nnp-vscode-')));await fs.mkdir(path.join(root,'dist/out'),{recursive:true});if(options.configuration)await fs.writeFile(path.join(root,'dist/vscode-package.json'),JSON.stringify({contributes:{configuration:options.configuration}}));const native=new Native(options),errors=[];const adapter=await createAdapter(native,{manifest:manifest(),root,diagnostic:error=>errors.push(error),fault:error=>errors.push(error)});return {root,native,adapter,errors,async dispose(){await adapter.dispose();await fs.rm(root,{recursive:true,force:true});}};}

test('core values preserve Unicode URI paths and immutable position/range semantics',()=>{
  const p=new Position(2,4);assert.equal(p.compareTo(new Position(20,0)),-1);assert.equal(p.with({character:4}),p);assert.deepEqual(p.translate({lineDelta:-2}).toJSON(),{line:0,character:4});assert.throws(()=>p.translate(-3),TypeError);
  const r=new Range(new Position(2,4),new Position(0,1));assert.equal(r.start.line,0);assert.equal(r.with({}),r);assert.equal(r.with({end:new Position(3,0)}).end.line,3);assert.equal(r.contains(new Position(1,0)),true);assert.equal(r.intersection(new Range(3,0,4,0)),undefined);assert.equal(r.union(new Range(3,0,4,0)).end.line,4);
  const local=path.resolve(os.tmpdir(),'中 %?#.md'),u=Uri.file(local);assert.equal(Uri.parse(u.toString(),true).fsPath,local);assert.equal(Uri.joinPath(u,'../next.md').fsPath,path.join(path.dirname(local),'next.md'));assert.equal(Uri.parse('untitled:doc',true).toString(),'untitled:doc');assert.throws(()=>Uri.parse('relative',true),TypeError);assert.throws(()=>Uri.file('relative'),TypeError);assert.throws(()=>Uri.parse('https://name:pass@example.com/a'),TypeError);
});
test('Disposable and EventEmitter handle reentrancy, thisArg, exceptions and cleanup once',()=>{
  let disposed=0;const a=new Disposable(()=>{++disposed;a.dispose();});a.dispose();a.dispose();assert.equal(disposed,1);
  const errors=[],events=[],emitter=new EventEmitter(e=>errors.push(e)),owner={label:'owner'},subscriptions=[];
  const first=emitter.event(function(v){events.push(this.label+v);second.dispose();throw new Error('listener');},owner,subscriptions),second=emitter.event(v=>events.push('second'+v));emitter.fire(1);assert.deepEqual(events,['owner1']);assert.equal(errors.length,1);assert.equal(subscriptions[0],first);emitter.dispose();emitter.fire(2);
  assert.throws(()=>Disposable.from(new Disposable(()=>{throw new Error('cleanup');}),new Disposable(()=>++disposed)).dispose(),AggregateError);assert.equal(disposed,2);
});
test('static alias validation rejects foreign, duplicate and missing contributions',()=>{
  assert.equal(verify(manifest()).get('extension.read'),'tests.compat.read');
  for(const mutate of [m=>m.compatibility.commands[0].target='foreign.command',m=>m.compatibility.commands.push({...m.compatibility.commands[0]}),m=>m.compatibility.commands=[],m=>m.runtime.kind='python',m=>m.compatibility.extensionRoot='../outside']){const m=manifest();mutate(m);assert.throws(()=>verify(m));}
});
test('registration returns synchronously, readiness waits, dispose-before-ack permits safe re-registration',async()=>{
  const f=await fixture();try{let release;f.native.registrationHook=()=>new Promise(resolve=>{release=resolve;});const first=f.adapter.api.commands.registerCommand('extension.read',()=>1);assert.ok(first instanceof Disposable);assert.equal(first.then,undefined);first.dispose();await tick();f.native.registrationHook=null;const second=f.adapter.api.commands.registerCommand('extension.read',function(x){return this.base+x;},{base:4});let ready=false;const waiting=f.adapter.ready().then(()=>{ready=true;});await tick();assert.equal(ready,false);release();await waiting;assert.equal(f.native.registrations.size,1);assert.equal(await f.native.registrations.get('tests.compat.read')(3),7);second.dispose();await f.adapter.ready();assert.equal(f.native.registrations.size,0);assert.equal(f.native.disposals,2);}finally{await f.dispose();}
});
test('unknown APIs fail explicitly and the CJS vscode alias is contained and restored',async()=>{
  const f=await fixture();try{const api=f.adapter.api;for(const read of [()=>api.window.showTextDocument,()=>api.workspace.workspaceFolders,()=>api.context,()=>f.adapter.context.globalState.setKeysForSync,()=>api.languages])assert.throws(read,{name:'UnsupportedApi',code:1005});
    assert.equal(f.errors.length,5);assert.ok(f.errors.every(error=>error.data.compatVersion==='0.1.0'));assert.equal(api.__esModule,false);assert.ok(Object.keys(api).includes('Position'));assert.equal(api.version,'0.15.0');assert.throws(()=>f.adapter.context.asAbsolutePath('../escape'),{code:1003});
    const req=createRequire(path.join(f.root,'dist/out/extension.js'));f.adapter.install();assert.equal(req('vscode'),api);assert.throws(()=>require('vscode'),{code:'MODULE_NOT_FOUND'});await f.adapter.dispose();assert.throws(()=>req('vscode'),{code:'MODULE_NOT_FOUND'});assert.throws(()=>api.window.activeTextEditor,{code:1004});
  }finally{await f.dispose();}
});
test('synchronous document facades expose typed ranges and ordered changed events before commands',async()=>{
  const f=await fixture();try{const api=f.adapter.api,doc=api.window.activeTextEditor.document;assert.ok(doc.uri instanceof Uri);assert.ok(doc.lineAt(0).range instanceof Range);assert.ok(doc.positionAt(3) instanceof Position);assert.equal(doc.getText(new Range(0,1,0,3)),'😀');assert.equal(doc.validatePosition(new Position(99,99)).line,2);assert.equal(doc.lineCount,3);assert.equal(doc.lineAt(new Position(0,99)).text,'a😀中');assert.throws(()=>doc.lineAt(new Position(99,0)),RangeError);assert.throws(()=>doc.lineAt({line:0,character:0}),TypeError);
    const observed=[];api.workspace.onDidChangeTextDocument(e=>observed.push([e.document.getText(),e.contentChanges[0].range instanceof Range,e.reason]));const baseVersion=f.native.version;f.native.text='B😀中\r\né\n';++f.native.version;f.native.emit({type:'changed',document:info(2),baseVersion,changes:[{range:{start:{line:0,character:0},end:{line:0,character:1}},rangeOffset:0,rangeLength:1,text:'B'}],reason:'undo'});
    api.commands.registerCommand('extension.read',()=>doc.getText());await f.adapter.ready();assert.equal(await f.native.registrations.get('tests.compat.read')(),'B😀中\r\né\n');assert.deepEqual(observed,[['B😀中\r\né\n',true,1]]);assert.throws(()=>doc.save,{code:1005});
  }finally{await f.dispose();}
});
test('editor edit uses one expected-version transaction and expires its builder',async()=>{
  const f=await fixture();try{const api=f.adapter.api,editor=api.window.activeTextEditor;let builder;assert.equal(await editor.edit(edit=>{builder=edit;edit.replace(new Range(0,1,0,3),'X');edit.insert(new Position(1,2),'!');}),true);assert.equal(editor.document.getText(),'aX中\r\né!\n');assert.equal(f.native.edits.length,2);assert.throws(()=>builder.delete(new Range(0,0,0,1)),/expired/);
    f.native.editFailure=1001;assert.equal(await editor.edit(edit=>edit.insert(new Position(0,0),'z')),false);assert.equal(editor.document.getText(),'aX中\r\né!\n');await assert.rejects(editor.edit(async()=>{}),{code:1005});await assert.rejects(editor.edit(()=>{},{undoStopAfter:false}),{code:1005});f.native.capabilities=[];await assert.rejects(editor.edit(()=>{}),{code:1003});
  }finally{await f.dispose();}
});
test('message promises await choice without blocking activation and reject unsupported overloads',async()=>{
  const f=await fixture();try{let choose;f.native.messageHook=(severity,text,items)=>{assert.equal(severity,'warning');assert.equal(text,'Pick');assert.deepEqual(items,['Yes','No']);return new Promise(resolve=>{choose=resolve;});};const selection=f.adapter.api.window.showWarningMessage('Pick','Yes','No');let completed=false;selection.then(()=>{completed=true;});await f.adapter.ready();assert.equal(completed,false);choose('Yes');assert.equal(await selection,'Yes');assert.throws(()=>f.adapter.api.window.showInformationMessage('Text',{modal:true}),{code:1005});}finally{await f.dispose();}
});
test('persistence permissions and activation publication barrier reject writes explicitly',async()=>{
  const denied=await fixture();try{assert.throws(()=>denied.adapter.context.globalState.get('x'),{code:1003});assert.throws(()=>denied.adapter.context.workspaceState.keys(),{code:1003});assert.throws(()=>denied.adapter.api.workspace.getConfiguration(),{code:1003});await assert.rejects(denied.adapter.context.globalState.update('x',1),{code:1003});}finally{await denied.dispose();}
  const f=await fixture({data:true});try{assert.equal(f.adapter.context.globalState.get('missing','fallback'),'fallback');await assert.rejects(f.adapter.context.globalState.update('activation',1),{code:1004});assert.equal(f.native.backend.size,0);await f.adapter.ready();await f.adapter.context.globalState.update('active',1);assert.equal(f.adapter.context.globalState.get('active'),1);}finally{await f.dispose();}
});
test('Memento separates scopes, preserves null and Unicode keys, clones values and survives reload',async()=>{
  const backend=new Map(),f=await fixture({data:true,backend});try{await f.adapter.ready();const global=f.adapter.context.globalState,workspace=f.adapter.context.workspaceState;
    const input=JSON.parse('{"__proto__":{"safe":true},"label":"😀中"}');await global.update('😀中',input);input.label='mutated';const output=global.get('😀中');output.label='changed';assert.equal(global.get('😀中').label,'😀中');assert.equal({}.safe,undefined);
    await global.update('__proto__',null);await workspace.update('😀中','session');assert.equal(global.get('__proto__','fallback'),null);assert.equal(workspace.get('😀中'),'session');assert.equal(global.get('😀中').label,'😀中');assert.deepEqual(global.keys(),['😀中','__proto__']);await global.update('__proto__',undefined);assert.equal(global.get('__proto__','fallback'),'fallback');
  }finally{await f.dispose();}const reload=await fixture({data:true,backend});try{assert.equal(reload.adapter.context.globalState.get('😀中').label,'😀中');assert.equal(reload.adapter.context.workspaceState.get('😀中'),'session');assert.equal(reload.adapter.context.globalState.get('__proto__'),undefined);}finally{await reload.dispose();}
});
test('Memento rejects lossy JSON and budgets without poisoning later writes or adapter readiness',async()=>{
  const f=await fixture({data:true});try{await f.adapter.ready();const state=f.adapter.context.globalState;await state.update('keep','old');const cycle={};cycle.self=cycle;const accessor={get x(){throw new Error('must not evaluate getter');}};const sparse=Array(1);
    for(const value of [Infinity,NaN,1n,{x:undefined},new Date(),cycle,accessor,sparse])await assert.rejects(state.update('keep',value),{code:-32602});await assert.rejects(state.update('keep','😀'.repeat(20000)),{code:1008});assert.equal(state.get('keep'),'old');
    f.native.dataHook=method=>{if(method==='write')throw new RpcError(1010,'Disk failure');};await assert.rejects(state.update('keep','failed'),{code:1010});assert.equal(state.get('keep'),'old');f.native.dataHook=null;await state.update('next',true);await f.adapter.ready();assert.equal(state.get('next'),true);assert.equal(f.errors.filter(e=>e.code===1010).length,1);
    await assert.rejects(state.update('',1),{code:-32602});await assert.rejects(state.update('😀'.repeat(33),1),{code:-32602});
  }finally{await f.dispose();}
});
test('compare-and-exchange merges concurrent other keys and bounds persistent contention',async()=>{
  const backend=new Map(),first=await fixture({data:true,backend}),second=await fixture({data:true,backend});try{await first.adapter.ready();await second.adapter.ready();await first.adapter.context.globalState.update('one',1);await second.adapter.context.globalState.update('two',2);assert.deepEqual(backend.get('storage:'+KEYS.global).values,{one:1,two:2});assert.equal(second.adapter.context.globalState.get('one'),1);assert.equal(second.native.dataCalls.length,2);
    let failures=0;second.native.dataHook=method=>{if(method==='write'){++failures;throw new RpcError(1001,'Continual contention');}};await assert.rejects(second.adapter.context.globalState.update('three',3),{code:1001});assert.equal(failures,4);assert.equal(second.adapter.context.globalState.get('three'),undefined);second.native.dataHook=null;await second.adapter.context.globalState.update('three',3);assert.equal(second.adapter.context.globalState.get('three'),3);
  }finally{await first.dispose();await second.dispose();}
});
test('Memento bounds namespace keys and queued writes before sending overflowing data',async()=>{
  const f=await fixture({data:true});try{await f.adapter.ready();const state=f.adapter.context.globalState;for(let i=0;i<256;++i)await state.update('key'+i,i);await assert.rejects(state.update('overflow',1),{code:1007});assert.equal(state.keys().length,256);assert.equal(state.get('overflow'),undefined);await state.update('key0',undefined);
    let release;f.native.dataHook=method=>method==='write'?new Promise(resolve=>{release=resolve;}):undefined;const writes=Array.from({length:64},()=>state.update('same',1));await tick();await assert.rejects(state.update('excess',1),{code:1007});f.native.dataHook=null;release();await Promise.all(writes);assert.equal(state.get('same'),1);assert.equal(state.get('excess'),undefined);
  }finally{await f.dispose();}
});
const configuration={title:'Settings',properties:{'sample.enabled':{type:'boolean',default:true},'sample.label':{type:'string',default:'😀中',enum:['😀中','new']},'sample.options':{type:'object',default:{nested:1},enum:[{nested:1},{nested:2}]},'sample.count':{type:'integer'}}};
test('plugin configuration exposes defaults, dotted sections, inspect, confirmed events and deletion',async()=>{
  const backend=new Map(),f=await fixture({data:true,backend,configuration});try{const api=f.adapter.api,cfg=api.workspace.getConfiguration('sample');assert.equal(cfg.get('enabled'),true);assert.equal(cfg.get('count'),0);assert.equal(cfg.get('unknown',7),7);assert.equal(cfg.has('options.nested'),true);assert.equal(cfg.get('options.nested'),1);assert.deepEqual(api.workspace.getConfiguration().get('sample'),{enabled:true,label:'😀中',options:{nested:1},count:0});assert.equal(cfg.inspect('unknown'),undefined);assert.equal(cfg.inspect('enabled').globalValue,undefined);
    const events=[];api.workspace.onDidChangeConfiguration(event=>events.push([event.affectsConfiguration('sample'),event.affectsConfiguration('sample.enabled'),event.affectsConfiguration('unrelated')]));await f.adapter.ready();await cfg.update('enabled',false,api.ConfigurationTarget.Global);assert.equal(cfg.get('enabled'),false);assert.equal(cfg.inspect('enabled').globalValue,false);assert.deepEqual(events,[[true,true,false]]);await cfg.update('enabled',false,true);assert.equal(events.length,1);await cfg.update('enabled',undefined,true);assert.equal(cfg.get('enabled'),true);assert.equal(events.length,2);
    await assert.rejects(cfg.update('label','unknown',true),{code:-32602});await assert.rejects(cfg.update('count',1.5,true),{code:-32602});for(const target of [undefined,false,2,3])await assert.rejects(cfg.update('label','new',target),{code:1005});await assert.rejects(cfg.update('label','new',true,true),{code:1005});await assert.rejects(cfg.update('unregistered',1,true),{code:1005});assert.throws(()=>api.workspace.getConfiguration('sample',Uri.file('/tmp/test')),{code:1005});await cfg.update('label','new',true);await cfg.update('options',Object.assign(Object.create(null),{nested:2}),true);assert.equal(cfg.get('options.nested'),2);await assert.rejects(cfg.update('options',{nested:3},true),{code:-32602});
  }finally{await f.dispose();}const reload=await fixture({data:true,backend,configuration});try{assert.equal(reload.adapter.api.workspace.getConfiguration('sample').get('label'),'new');}finally{await reload.dispose();}
});
test('corrupt or incompatible cached records fail before subscribing and preserve disk values',async()=>{
  const root=await fs.realpath(await fs.mkdtemp(path.join(os.tmpdir(),'nnp-data-corrupt-')));await fs.mkdir(path.join(root,'dist'));await fs.writeFile(path.join(root,'dist/vscode-package.json'),JSON.stringify({contributes:{configuration}}));try{
    for(const [space,key,value] of [['storage',KEYS.global,{schemaVersion:2,values:{}}],['configuration',KEYS.configuration,{schemaVersion:1,values:{'sample.enabled':'wrong'}}]]) {const backend=new Map([[space+':'+key,value]]),native=new Native({data:true,backend});await assert.rejects(createAdapter(native,{manifest:manifest(),root,diagnostic:()=>{}}),{code:1010});assert.deepEqual(backend.get(space+':'+key),value);assert.equal(native.listeners.size,0);}
  }finally{await fs.rm(root,{recursive:true,force:true});}
});
test('dispose drains an in-flight write, rejects queued stale writes and closes cached facades',async()=>{
  const f=await fixture({data:true});try{await f.adapter.ready();let release;f.native.dataHook=method=>method==='write'?new Promise(resolve=>{release=resolve;}):undefined;const state=f.adapter.context.globalState;const first=state.update('first',1),second=state.update('second',2);const rejected=assert.rejects(second,{code:1004});await tick();const disposed=f.adapter.dispose();release();await first;await rejected;await disposed;assert.equal(f.native.dataCalls.length,1);assert.deepEqual(f.native.backend.get('storage:'+KEYS.global).values,{first:1});assert.throws(()=>state.get('first'),{code:1004});await assert.rejects(state.update('third',3),{code:1004});}finally{await f.dispose();}
});

test('SDK message selection before acknowledgement, dismissal and stop reclaim owned handles',async()=>{
  const identity={pluginId:'tests.compat',sessionId:'s',generation:1,grantedCapabilities:['messages@1']},envelope={sessionId:'s',generation:1};
  const caller=new Endpoint(json=>{void host.receive(Buffer.from(json));}),host=new Endpoint(json=>{void caller.receive(Buffer.from(json));}),client=createClient(caller,identity,{},()=>true);const disposed=[];let serial=0;
  host.register('nnp.window.showMessage',()=>{const messageId='message'+(++serial);if(serial===1)host.notify('nnp.window.messageEvent',{...envelope,messageId,selected:'Yes'});return {messageId};});host.register('nnp.resources.dispose',p=>{disposed.push(p.resourceId);return null;});
  try{assert.equal(await client.api.window.showMessage('info','Early',['Yes']),'Yes');const second=client.api.window.showMessage('info','Dismiss');await tick();host.notify('nnp.window.messageEvent',{...envelope,messageId:'message2',selected:null});assert.equal(await second,undefined);const third=client.api.window.showMessage('info','Stop');await tick();await client.dispose();assert.equal(await third,undefined);assert.deepEqual(disposed,['message3']);}finally{caller.close();host.close();}
});




test('host configuration declarations validate bounded schemas',()=>{
  assert.equal(configurationDefinitions(configuration).size,4);
  for(const bad of [{properties:{'sample.x':{type:'string',pattern:'.*'}}},{properties:{'sample.x':{type:'string',default:1}}},{properties:{'sample.x':{type:'string',scope:'resource'}}},{properties:{'sample.x':{type:'string'},'sample.x.child':{type:'string'}}}])assert.throws(()=>configurationDefinitions(bad));
});
