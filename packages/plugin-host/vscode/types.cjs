// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const path=require('node:path');
const {pathToFileURL,fileURLToPath}=require('node:url');
class Disposable {
  #callback;
  constructor(callback){if(typeof callback!=='function')throw new TypeError('Disposable requires a function');this.#callback=callback;}
  dispose(){const callback=this.#callback;this.#callback=undefined;callback?.();}
  static from(...values){return new Disposable(()=>{const errors=[];for(const value of values)try{value?.dispose();}catch(error){errors.push(error);}if(errors.length)throw new AggregateError(errors,'Disposable cleanup failed');});}
}
class EventEmitter {
  #listeners=new Set();#closed=false;
  constructor(diagnostic=console.error){this.diagnostic=diagnostic;this.event=(listener,thisArg,disposables)=>{if(typeof listener!=='function')throw new TypeError('Expected an event listener');const entry={listener,thisArg};if(!this.#closed)this.#listeners.add(entry);const disposable=new Disposable(()=>this.#listeners.delete(entry));disposables?.push(disposable);return disposable;};}
  fire(value){if(this.#closed)return;for(const entry of [...this.#listeners]){if(!this.#listeners.has(entry))continue;try{entry.listener.call(entry.thisArg,value);}catch(error){this.diagnostic(error);}}}
  dispose(){this.#closed=true;this.#listeners.clear();}
}
const integer=value=>{if(!Number.isSafeInteger(value)||value<0)throw new TypeError('Expected a non-negative integer');return value;};
class Position {
  constructor(line,character){this.line=integer(line);this.character=integer(character);Object.freeze(this);}
  compareTo(other){return Math.sign(this.line-other.line||this.character-other.character);}
  isEqual(other){return this.compareTo(other)===0;}isBefore(other){return this.compareTo(other)<0;}isBeforeOrEqual(other){return this.compareTo(other)<=0;}
  isAfter(other){return this.compareTo(other)>0;}isAfterOrEqual(other){return this.compareTo(other)>=0;}
  with(line=this.line,character=this.character){if(typeof line==='object')({line=this.line,character=this.character}=line);return line===this.line&&character===this.character?this:new Position(line,character);}
  translate(lineDelta=0,characterDelta=0){if(typeof lineDelta==='object')({lineDelta=0,characterDelta=0}=lineDelta);return this.with(this.line+lineDelta,this.character+characterDelta);}
  toJSON(){return {line:this.line,character:this.character};}
}
const pos=value=>value instanceof Position?value:new Position(value.line,value.character);
class Range {
  constructor(start,end,endLine,endCharacter){if(typeof start==='number'){start=new Position(start,end);end=new Position(endLine,endCharacter);}else{start=pos(start);end=pos(end);}if(start.isAfter(end))[start,end]=[end,start];this.start=start;this.end=end;Object.freeze(this);}
  get isEmpty(){return this.start.isEqual(this.end);}get isSingleLine(){return this.start.line===this.end.line;}
  isEqual(other){return this.start.isEqual(other.start)&&this.end.isEqual(other.end);}
  contains(value){return value.start?this.contains(value.start)&&this.contains(value.end):this.start.isBeforeOrEqual(value)&&this.end.isAfterOrEqual(value);}
  intersection(other){const start=this.start.isAfter(other.start)?this.start:other.start,end=this.end.isBefore(other.end)?this.end:other.end;return start.isAfter(end)?undefined:new Range(start,end);}
  union(other){return new Range(this.start.isBefore(other.start)?this.start:other.start,this.end.isAfter(other.end)?this.end:other.end);}
  with(start=this.start,end=this.end){if(arguments.length===1&&start&&!('line' in start))({start=this.start,end=this.end}=start);start=pos(start);end=pos(end);return this.start.isEqual(start)&&this.end.isEqual(end)?this:new Range(start,end);}
  toJSON(){return {start:this.start.toJSON(),end:this.end.toJSON()};}
}
function encodePath(value){return value.split('/').map(part=>encodeURIComponent(part).replace(/%3A/gi,':')).join('/');}
class Uri {
  constructor(scheme,authority,pathValue,query='',fragment=''){if(!/^[A-Za-z][A-Za-z0-9+.-]*$/.test(scheme)||[authority,pathValue,query,fragment].some(v=>typeof v!=='string'))throw new TypeError('Invalid URI components');Object.assign(this,{scheme:scheme.toLowerCase(),authority,path:pathValue,query,fragment});Object.freeze(this);}
  static file(value){if(typeof value!=='string'||!path.isAbsolute(value))throw new TypeError('Uri.file requires an absolute local path in this profile');return Uri.parse(pathToFileURL(value).href,true);}
  static parse(value,strict=false){if(typeof value!=='string')throw new TypeError('Expected a URI string');if(!/^[A-Za-z][A-Za-z0-9+.-]*:/.test(value)){if(strict)throw new TypeError('URI needs a scheme');value='file:///'+value.replace(/^\/+/, '');}const u=new URL(value);if(u.username||u.password||u.port)throw new TypeError('URI credentials and ports are outside this profile');return new Uri(u.protocol.slice(0,-1),u.host,decodeURIComponent(u.pathname),decodeURIComponent(u.search.slice(1)),decodeURIComponent(u.hash.slice(1)));}
  static from(value){return new Uri(value.scheme,value.authority??'',value.path??'',value.query??'',value.fragment??'');}
  static joinPath(base,...parts){if(parts.some(part=>typeof part!=='string'))throw new TypeError('Expected path segments');return base.with({path:path.posix.join(base.path,...parts)});}
  with(change={}){return new Uri(change.scheme??this.scheme,change.authority??this.authority,change.path??this.path,change.query??this.query,change.fragment??this.fragment);}
  get fsPath(){return this.scheme==='file'?fileURLToPath(this.toString()):this.path;}
  toString(skipEncoding=false){const authority=this.authority||this.scheme==='file'?'//'+this.authority:'',pathname=skipEncoding?this.path:encodePath(this.path),query=this.query?'?'+(skipEncoding?this.query:encodeURIComponent(this.query).replace(/%3D/gi,'=').replace(/%26/gi,'&')):'',fragment=this.fragment?'#'+(skipEncoding?this.fragment:encodeURIComponent(this.fragment)):'';return this.scheme+':'+authority+pathname+query+fragment;}
  toJSON(){return {scheme:this.scheme,authority:this.authority,path:this.path,query:this.query,fragment:this.fragment};}
}
module.exports={Disposable,EventEmitter,Position,Range,Uri};
