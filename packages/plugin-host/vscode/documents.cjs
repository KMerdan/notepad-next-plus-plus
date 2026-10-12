// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const {RpcError}=require('../rpc.cjs');
function unicode(text) {
  for(let i=0;i<text.length;++i){const n=text.charCodeAt(i);if(n>=0xd800&&n<=0xdbff){const next=text.charCodeAt(++i);if(!(next>=0xdc00&&next<=0xdfff))return false;}else if(n>=0xdc00&&n<=0xdfff)return false;}
  return true;
}
function position(value){if(!value||!Number.isInteger(value.line)||!Number.isInteger(value.character))throw new TypeError('Expected an integer UTF-16 position');return value;}
function compare(a,b){return a.line-b.line||a.character-b.character;}
class TextDocument {
  #info;#text;#lines;#closed=false;#paused=false;
  constructor(snapshot){const {text,positionEncoding,...info}=snapshot;if(typeof text!=='string'||!unicode(text))throw new RpcError(1009,'UnsupportedEncoding');this.#info=Object.freeze(info);this.#text=text;}
  get documentId(){return this.#info.documentId;}get uri(){return this.#info.uri;}get languageId(){return this.#info.languageId;}get version(){return this.#info.version;}
  get isDirty(){return this.#info.isDirty;}get isUntitled(){return this.uri.startsWith('untitled:');}get isReadonly(){return this.#info.isReadonly;}get isClosed(){return this.#closed;}
  get lineCount(){this.#read();return this.#lineData().length;}
  #read(){if(this.#paused)throw new RpcError(1001,'Document mirror is awaiting explicit resynchronization');}
  #lineData(){
    if(!this.#lines){this.#lines=[];let start=0;const pattern=/\r\n|\r|\n/g;for(let match;(match=pattern.exec(this.#text));){this.#lines.push({start,end:match.index,next:pattern.lastIndex});start=pattern.lastIndex;}this.#lines.push({start,end:this.#text.length,next:this.#text.length});}
    return this.#lines;
  }
  validatePosition(value){this.#read();position(value);const lines=this.#lineData(),line=Math.max(0,Math.min(lines.length-1,value.line)),character=value.line<0?0:value.line>=lines.length?lines[line].end-lines[line].start:Math.max(0,Math.min(lines[line].end-lines[line].start,value.character));return {line,character};}
  validateRange(value){if(!value||compare(position(value.start),position(value.end))>0)throw new TypeError('Expected an ordered range');return {start:this.validatePosition(value.start),end:this.validatePosition(value.end)};}
  offsetAt(value){const p=this.validatePosition(value);return this.#lineData()[p.line].start+p.character;}
  positionAt(offset){this.#read();if(typeof offset!=='number'||Number.isNaN(offset))throw new TypeError('Expected an offset');offset=Math.max(0,Math.min(this.#text.length,Math.floor(offset)));const lines=this.#lineData();let low=0,high=lines.length;while(low+1<high){const mid=(low+high)>>1;if(lines[mid].start<=offset)low=mid;else high=mid;}return {line:low,character:Math.min(offset,lines[low].end)-lines[low].start};}
  getText(range){this.#read();if(range===undefined)return this.#text;const r=this.validateRange(range);return this.#text.slice(this.offsetAt(r.start),this.offsetAt(r.end));}
  lineAt(line){this.#read();if(typeof line==='object')line=position(line).line;if(!Number.isInteger(line)||line<0||line>=this.lineCount)throw new RangeError('Line index out of bounds');const data=this.#lineData()[line],text=this.#text.slice(data.start,data.end),start=Object.freeze({line,character:0}),end=Object.freeze({line,character:text.length}),next=Object.freeze(data.next>data.end?{line:line+1,character:0}:{line,character:text.length});const first=text.search(/\S/);return Object.freeze({lineNumber:line,text,range:Object.freeze({start,end}),rangeIncludingLineBreak:Object.freeze({start,end:next}),firstNonWhitespaceCharacterIndex:first<0?text.length:first,isEmptyOrWhitespace:first<0});}
  // Only the owned mirror coordinator mutates these records.
  _pause(){this.#paused=true;}_close(){this.#closed=true;this.#paused=false;}
  _metadata(info){this.#info=Object.freeze({...this.#info,...info});}
  _replace(text,info){if(!unicode(text))throw new RpcError(1009,'UnsupportedEncoding');this.#text=text;this.#info=Object.freeze({...info});this.#lines=undefined;this.#paused=false;}
}
class DocumentMirror {
  constructor(api,{onEvent=()=>{},fault=()=>{},diagnostic=error=>console.error(error)}={}) {
    this.api=api;this.onEvent=onEvent;this.fault=fault;this.diagnostic=diagnostic;
    this.records=new Map();this.excluded=new Map();this.activeId=null;this.watch=null;this.epoch=0;this.booting=false;this.started=false;this.closed=false;this.broken=null;this.sequence=0;this.bytes=0;
    this.early=[];this.queuedCount=0;this.queuedBytes=0;this.chain=Promise.resolve();
  }
  get documents(){return [...this.records.values()].map(record=>record.document);}
  get activeDocument(){return this.records.get(this.activeId)?.document;}
  get unsupported(){return [...this.excluded.values()].map(value=>Object.freeze({...value}));}
  #emit(event){try{this.onEvent(event);}catch(error){this.diagnostic(error);}}
  #fail(error){if(this.closed||this.broken)return;this.broken=error;for(const record of this.records.values())record.document._pause();this.#emit({type:'fault',error});try{this.fault(error);}catch(fault){this.diagnostic(fault);}void this.watch?.dispose().catch(failure=>this.diagnostic(failure));}
  #accept(event,epoch){
    if(this.closed||this.broken||epoch!==this.epoch)return;
    const size=Buffer.byteLength(JSON.stringify(event));
    if(this.queuedCount>=128||this.queuedBytes+size>8*1024*1024){this.#fail(new RpcError(1007,'Reliable mirror event queue overflow'));return;}
    ++this.queuedCount;this.queuedBytes+=size;
    if(this.booting)this.early.push({event,epoch,size});else this.#queue({event,epoch,size});
  }
  #queue(entry){this.chain=this.chain.then(async()=>{if(!this.closed&&!this.broken&&entry.epoch===this.epoch)await this.#process(entry.event);}).catch(error=>this.#fail(error)).finally(()=>{--this.queuedCount;this.queuedBytes-=entry.size;});}
  async start(){if(this.started)throw new RpcError(1004,'Document mirror already started');this.started=true;try{await this.#loadAll('initialize');await this.flush();return this;}catch(error){this.#fail(error);throw error;}}
  async flush(){for(;;){const current=this.chain;await current;if(current===this.chain)break;}if(this.broken)throw this.broken;if(this.closed)throw new RpcError(1004,'Document mirror closed');}
  #drop(id){const old=this.records.get(id);if(old){this.bytes-=old.bytes;old.document._close();this.records.delete(id);this.#emit({type:'closed',document:old.document});}this.excluded.delete(id);return old;}
  async #snapshot(id,info,reason){
    const old=this.records.get(id);old?.document._pause();
    try {
      const watch=this.watch,epoch=this.epoch;const result=await watch.getSnapshot(id),snapshot=result.snapshot;
      if(this.closed||this.broken||epoch!==this.epoch)return;
      if(snapshot.documentId!==id||result.eventSeq<this.sequence)throw new RpcError(1001,'Invalid mirror snapshot barrier');
      const bytes=Buffer.byteLength(snapshot.text);if(bytes>2*1024*1024||this.bytes-(old?.bytes??0)+bytes>this.watch.maxTotalUtf8Bytes)throw new RpcError(1008,'Document mirror aggregate budget exceeded');
      const document=new TextDocument(snapshot);this.#drop(id);this.records.set(id,{document,bytes,floor:result.eventSeq});this.bytes+=bytes;this.#emit({type:'opened',document});
      if(reason!=='initialize'&&reason!=='opened')this.#emit({type:'resynchronized',document,reason,eventSeq:result.eventSeq});
      return document;
    } catch(error) {
      this.#drop(id);
      if(error.code===1002)return;
      if(error.code!==1008&&error.code!==1009)throw error;
      this.excluded.set(id,{...info,errorCode:error.code,reason:error.message});this.#emit({type:'unsupported',documentId:id,error});
    }
  }
  async #loadAll(reason){
    this.booting=true;const oldWatch=this.watch;const epoch=++this.epoch;
    for(const record of this.records.values())record.document._pause();
    try {
      const watch=await this.api.watchAll(event=>this.#accept(event,epoch));
      if(this.closed||this.broken||epoch!==this.epoch){await watch.dispose();throw new RpcError(1004,'Document mirror stopped during initialization');}
      this.watch=watch;if(oldWatch)await oldWatch.dispose();
      for(const id of [...this.records.keys()])this.#drop(id);this.excluded.clear();this.bytes=0;
      this.sequence=watch.eventSeq;this.activeId=watch.activeDocumentId;
      if(watch.maxDocuments!==64||watch.maxTotalUtf8Bytes!==4194304||watch.documents.length>64||new Set(watch.documents.map(doc=>doc.documentId)).size!==watch.documents.length)throw new RpcError(1007,'Invalid document mirror census or limits');
      for(const info of watch.documents){if(this.closed||this.broken)break;await this.#snapshot(info.documentId,info,reason);}
      this.#emit({type:'activeChanged',document:this.activeDocument});
      if(reason!=='initialize')this.#emit({type:'resynchronizedAll',reason,eventSeq:this.sequence});
    } finally {
      this.booting=false;for(const entry of this.early.splice(0))this.#queue(entry);
    }
  }
  async #process(event){
    if(event.eventSeq<=this.sequence)return;
    if(event.eventSeq!==this.sequence+1){await this.#loadAll('sequenceGap');return;}
    this.sequence=event.eventSeq;
    if(event.type==='activeChanged'){this.activeId=event.document?.documentId??null;this.#emit({type:'activeChanged',document:this.activeDocument});return;}
    const id=event.document.documentId,record=this.records.get(id);
    if(record&&event.eventSeq<=record.floor)return;
    if(event.type==='closed'){this.#drop(id);return;}
    if(event.type==='opened'){
      if(this.records.size+this.excluded.size>=64&&!record&&!this.excluded.has(id))throw new RpcError(1007,'Document mirror census exceeds 64 documents');
      await this.#snapshot(id,event.document,'opened');return;
    }
    if(!record){await this.#snapshot(id,event.document,'unsupportedOrMissing');return;}
    if(event.type==='reset'){await this.#snapshot(id,event.document,event.reason);return;}
    if(event.type==='metadataChanged'){
      if(record.document.version!==event.document.version){await this.#snapshot(id,event.document,'versionMismatch');return;}
      if(record.document.uri!==event.document.uri||record.document.languageId!==event.document.languageId){const text=record.document.getText(),floor=record.floor,bytes=record.bytes;this.#drop(id);const document=new TextDocument({...event.document,text});this.records.set(id,{document,floor,bytes});this.bytes+=bytes;this.#emit({type:'opened',document});if(id===this.activeId)this.#emit({type:'activeChanged',document});}
      else {record.document._metadata(event.document);this.#emit({type:'metadataChanged',document:record.document});}
      return;
    }
    if(event.type!=='changed')throw new RpcError(-32602,'Unknown mirror event');
    if(record.document.version!==event.baseVersion||event.document.version<=event.baseVersion){await this.#snapshot(id,event.document,'versionMismatch');return;}
    try {
      const previous=record.document.getText(),changes=[...event.changes].sort((a,b)=>a.rangeOffset-b.rangeOffset);
      for(let index=0;index<changes.length;++index){const change=changes[index],range=record.document.validateRange(change.range),start=record.document.offsetAt(range.start),end=record.document.offsetAt(range.end);if(start!==change.rangeOffset||end-start!==change.rangeLength||compare(range.start,change.range.start)||compare(range.end,change.range.end)||!unicode(change.text)||(index&&(start<changes[index-1].rangeOffset+changes[index-1].rangeLength||start===changes[index-1].rangeOffset)))throw new RpcError(1001,'Unmappable old-text delta range');}
      let text=previous;for(const change of changes.reverse())text=text.slice(0,change.rangeOffset)+change.text+text.slice(change.rangeOffset+change.rangeLength);
      const bytes=Buffer.byteLength(text);if(bytes>2*1024*1024||this.bytes-record.bytes+bytes>this.watch.maxTotalUtf8Bytes)throw new RpcError(1008,'Document mirror aggregate budget exceeded');
      if(!unicode(text))throw new RpcError(1009,'UnsupportedEncoding');this.bytes+=bytes-record.bytes;record.bytes=bytes;record.document._replace(text,event.document);this.#emit({type:'changed',document:record.document,contentChanges:event.changes,reason:event.reason});
    } catch(error){if([1001,1008,1009].includes(error.code))await this.#snapshot(id,event.document,'unmappableChange');else throw error;}
  }
  async dispose(){if(this.closed)return;this.closed=true;++this.epoch;const watch=this.watch;this.watch=null;for(const id of [...this.records.keys()])this.#drop(id);this.excluded.clear();this.activeId=null;this.bytes=0;this.early.splice(0).forEach(entry=>{--this.queuedCount;this.queuedBytes-=entry.size;});try{await watch?.dispose();}catch(error){this.diagnostic(error);}await this.chain;}
}
module.exports={TextDocument,DocumentMirror};
