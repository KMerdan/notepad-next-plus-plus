// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const {TextDocument,DocumentMirror}=require('../../packages/plugin-host/vscode/documents.cjs');
const {RpcError}=require('../../packages/plugin-host/rpc.cjs');
const info=(id='a',version=1)=>({documentId:id,uri:'untitled:'+id,languageId:'text',version,isDirty:true,isReadonly:false});
const snap=(text,id='a',version=1)=>({...info(id,version),text,positionEncoding:'utf-16'});
const range=(line,start,end)=>({start:{line,character:start},end:{line,character:end}});
class Host {
  constructor(documents=[snap('a😀中\r\né\n')]){this.documents=new Map(documents.map(doc=>[doc.documentId,doc]));this.active='a';this.sequence=0;this.listeners=new Map();this.serial=0;this.disposals=0;this.snapshotHook=null;}
  async watchAll(listener){const id='watch'+(++this.serial);this.listeners.set(id,listener);return {subscriptionId:id,eventSeq:this.sequence,maxDocuments:64,maxTotalUtf8Bytes:4194304,documents:[...this.documents.values()].map(({text,positionEncoding,...value})=>value),activeDocumentId:this.active,getSnapshot:async id=>{const snapshot=this.documents.get(id);if(!snapshot)throw new RpcError(1002,'DocumentClosed');const result={snapshot:{...snapshot},eventSeq:this.sequence};if(this.snapshotHook){const hook=this.snapshotHook;this.snapshotHook=null;await hook(id,result);}return result;},dispose:async()=>{if(this.listeners.delete(id))++this.disposals;}};}
  emit(event){const eventSeq=++this.sequence;for(const [subscriptionId,listener]of this.listeners)listener({sessionId:'s',generation:1,subscriptionId,eventSeq,...event});}
}
test('synchronous Unicode/CRLF text, line objects and position clamping',()=>{
  const doc=new TextDocument(snap('a😀中\r\né\n\n'));
  assert.equal(doc.getText(),'a😀中\r\né\n\n');assert.equal(doc.lineCount,4);assert.equal(doc.lineAt(0).text,'a😀中');assert.deepEqual(doc.lineAt(0).range.end,{line:0,character:4});assert.deepEqual(doc.lineAt(0).rangeIncludingLineBreak.end,{line:1,character:0});
  assert.equal(doc.offsetAt({line:1,character:2}),8);assert.equal(doc.offsetAt({line:0,character:99}),4);assert.equal(doc.offsetAt({line:-1,character:99}),0);assert.equal(doc.offsetAt({line:99,character:0}),10);
  assert.deepEqual(doc.positionAt(5),{line:0,character:4});assert.deepEqual(doc.positionAt(6),{line:1,character:0});assert.deepEqual(doc.positionAt(Infinity),{line:3,character:0});assert.equal(doc.getText(range(0,1,3)),'😀');assert.throws(()=>doc.lineAt(4),RangeError);assert.throws(()=>doc.positionAt(NaN),TypeError);
  const line=doc.lineAt(0);doc._replace('new',info('a',2));assert.equal(line.text,'a😀中');assert.equal(doc.lineAt(0).text,'new');assert.equal(doc.version,2);
});
test('initial snapshot barrier applies early changes once and before listeners read',async()=>{
  const host=new Host([snap('old')]),observed=[];host.snapshotHook=async()=>{host.documents.set('a',snap('new','a',2));host.emit({type:'changed',document:info('a',2),baseVersion:1,changes:[{range:range(0,0,3),rangeOffset:0,rangeLength:3,text:'new'}],reason:'edit'});};
  const mirror=await new DocumentMirror(host,{onEvent:event=>{if(event.type==='changed')observed.push(event.document.getText());}}).start();assert.equal(mirror.activeDocument.getText(),'new');assert.deepEqual(observed,['new']);await mirror.dispose();assert.equal(host.listeners.size,0);
  const fresh=new Host([snap('latest','a',3)]);fresh.snapshotHook=async(_,result)=>{fresh.sequence=1;result.eventSeq=1;fresh.emit({type:'metadataChanged',document:info('a',3)});};const other=await new DocumentMirror(fresh).start();await other.flush();assert.equal(other.activeDocument.getText(),'latest');await other.dispose();
});
test('atomic old-text edits preserve UTF-16 coordinates and publish the final version',async()=>{
  const host=new Host(),observed=[];const mirror=await new DocumentMirror(host,{onEvent:event=>{if(event.type==='changed')observed.push([event.document.getText(),event.document.version]);}}).start();
  host.documents.set('a',snap('B😀中\r\nnew\n','a',5));host.emit({type:'changed',document:info('a',5),baseVersion:1,changes:[{range:range(1,0,2),rangeOffset:6,rangeLength:2,text:'new'},{range:range(0,0,1),rangeOffset:0,rangeLength:1,text:'B'}],reason:'edit'});await mirror.flush();assert.deepEqual(observed,[['B😀中\r\nnew\n',5]]);assert.equal(mirror.documents.length,1);await mirror.dispose();
});
test('version mismatch pauses synchronous reads and explicitly closes/reopens on resync',async()=>{
  const host=new Host([snap('old')]),events=[];const mirror=await new DocumentMirror(host,{onEvent:event=>events.push(event.type)}).start(),old=mirror.activeDocument;
  let resolve;host.snapshotHook=()=>new Promise(done=>{resolve=done;});host.documents.set('a',snap('recovered','a',7));host.emit({type:'changed',document:info('a',6),baseVersion:5,changes:[{range:range(0,0,3),rangeOffset:0,rangeLength:3,text:'bad'}],reason:'edit'});await new Promise(setImmediate);assert.throws(()=>old.getText(),{code:1001});resolve();await mirror.flush();assert.equal(old.isClosed,true);assert.equal(mirror.activeDocument.getText(),'recovered');assert.ok(events.includes('resynchronized'));assert.equal(events.filter(type=>type==='changed').length,0);await mirror.dispose();
});
test('sequence gap rebuilds the complete census including an unseen opened document',async()=>{
  const host=new Host([snap('one')]),events=[];const mirror=await new DocumentMirror(host,{onEvent:event=>events.push(event.type)}).start();host.documents.set('b',snap('two','b'));host.active='b';host.sequence=1;host.emit({type:'activeChanged',document:info('b')});await mirror.flush();assert.equal(mirror.documents.length,2);assert.equal(mirror.activeDocument.getText(),'two');assert.ok(events.includes('resynchronizedAll'));assert.equal(host.listeners.size,1);assert.equal(host.disposals,1);await mirror.dispose();
});
test('rename and reset create explicit document identities, close remains readable',async()=>{
  const host=new Host([snap('before')]),events=[];const mirror=await new DocumentMirror(host,{onEvent:event=>events.push(event.type)}).start(),old=mirror.activeDocument;
  const renamed={...info(),uri:'file:///tmp/renamed.md',languageId:'markdown'};host.documents.set('a',{...renamed,text:'before',positionEncoding:'utf-16'});host.emit({type:'metadataChanged',document:renamed});await mirror.flush();assert.equal(old.isClosed,true);assert.equal(old.getText(),'before');assert.equal(mirror.activeDocument.uri,renamed.uri);
  host.documents.set('a',{...renamed,version:2,text:'reloaded',positionEncoding:'utf-16'});host.emit({type:'reset',document:{...renamed,version:2},reason:'bufferReset'});await mirror.flush();assert.equal(mirror.activeDocument.getText(),'reloaded');assert.ok(events.includes('resynchronized'));host.documents.delete('a');host.emit({type:'closed',document:{...renamed,version:2}});await mirror.flush();assert.equal(mirror.documents.length,0);await mirror.dispose();
});
test('aggregate budget excludes a document explicitly and recovers after space is released',async()=>{
  const host=new Host([snap('x'.repeat(2*1024*1024),'a'),snap('y'.repeat(2*1024*1024),'b'),snap('z','c')]);const mirror=await new DocumentMirror(host).start();assert.equal(mirror.documents.length,2);assert.equal(mirror.unsupported[0].documentId,'c');assert.equal(mirror.unsupported[0].errorCode,1008);
  host.documents.delete('a');host.emit({type:'closed',document:info('a')});host.emit({type:'changed',document:info('c',2),baseVersion:1,changes:[{range:range(0,0,1),rangeOffset:0,rangeLength:1,text:'z'}],reason:'edit'});await mirror.flush();assert.equal(mirror.documents.length,2);assert.equal(mirror.unsupported.length,0);await mirror.dispose();
});
test('invalid old range uses resync and never publishes a fabricated change',async()=>{
  const host=new Host([snap('a😀')]),events=[];const mirror=await new DocumentMirror(host,{onEvent:event=>events.push(event.type)}).start();host.documents.set('a',snap('valid','a',2));host.emit({type:'changed',document:info('a',2),baseVersion:1,changes:[{range:range(0,0,1),rangeOffset:1,rangeLength:1,text:'x'}],reason:'edit'});await mirror.flush();assert.equal(mirror.activeDocument.getText(),'valid');assert.equal(events.filter(type=>type==='changed').length,0);await mirror.dispose();
});
test('reliable queue overflow faults the mirror and releases the watch',async()=>{
  const host=new Host([snap('old')]),failures=[];let release;host.snapshotHook=()=>new Promise(resolve=>{release=resolve;});const mirror=new DocumentMirror(host,{fault:error=>failures.push(error.code)}),starting=mirror.start();await new Promise(setImmediate);for(let n=0;n<129;++n)host.emit({type:'metadataChanged',document:info()});release();await assert.rejects(starting,{code:1007});assert.deepEqual(failures,[1007]);assert.equal(host.listeners.size,0);await mirror.dispose();assert.equal(mirror.queuedCount,0);
});
test('dispose during a snapshot prevents late document resurrection',async()=>{
  const host=new Host([snap('old')]);let release;host.snapshotHook=()=>new Promise(resolve=>{release=resolve;});const mirror=new DocumentMirror(host),starting=mirror.start();await new Promise(setImmediate);await mirror.dispose();release();await assert.rejects(starting,{code:1004});assert.equal(mirror.documents.length,0);assert.equal(host.listeners.size,0);
});
