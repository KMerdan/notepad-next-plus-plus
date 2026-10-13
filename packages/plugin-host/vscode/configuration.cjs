// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
// Static VS Code configuration contract shared by author tools and the host.
const {isDeepStrictEqual}=require('node:util');
class RpcError extends Error {constructor(code,message){super(message);this.code=code;}}
const own=(value,key)=>Object.hasOwn(value,key);
const plain=value=>value!==null&&typeof value==='object'&&!Array.isArray(value)&&(Object.getPrototypeOf(value)===Object.prototype||Object.getPrototypeOf(value)===null);
function validJson(value,depth=0,seen=new Set()) {
  if(depth>32)return false;
  if(value===null||typeof value==='string'||typeof value==='boolean')return true;
  if(typeof value==='number')return Number.isFinite(value);
  if(!Array.isArray(value)&&!plain(value)||seen.has(value))return false;
  seen.add(value);
  const keys=Reflect.ownKeys(value).filter(key=>!(Array.isArray(value)&&key==='length'));
  const ok=keys.every(key=>typeof key==='string'&&Object.getOwnPropertyDescriptor(value,key).enumerable&&own(Object.getOwnPropertyDescriptor(value,key),'value')&&validJson(value[key],depth+1,seen))&&(!Array.isArray(value)||keys.length===value.length);
  seen.delete(value);return ok;
}
function clone(value) {if(!validJson(value))throw new RpcError(-32602,'Expected finite, acyclic plain JSON');const json=JSON.stringify(value);if(Buffer.byteLength(json)>65536)throw new RpcError(1008,'Compatibility data exceeds 64 KiB');return JSON.parse(json);}
function key(value) {if(typeof value!=='string'||!value||Buffer.byteLength(value)>128)throw new RpcError(-32602,'Expected a nonempty key of at most 128 UTF-8 bytes');return value;}
function envelope(values) {if(Object.keys(values).length>256)throw new RpcError(1007,'Compatibility namespace exceeds 256 keys');return clone({schemaVersion:1,values});}
function accepts(definition,value) {
  const type=definition.type;
  const matches=type==='null'?value===null:type==='array'?Array.isArray(value):type==='object'?plain(value):type==='integer'?Number.isSafeInteger(value):type==='number'?typeof value==='number'&&Number.isFinite(value):typeof value===type;
  if(!matches)return false;
  const normalized=definition.enum?clone(value):value;
  return !definition.enum||definition.enum.some(item=>isDeepStrictEqual(item,normalized));
}
// Shared by the static importer and the adapter: unsupported schemas never
// acquire guessed defaults or pretend to enforce constraints we don't support.
function configurationDefinitions(contribution) {
  const definitions=new Map();if(contribution===undefined)return definitions;
  const blocks=Array.isArray(contribution)?contribution:[contribution];
  if(!blocks.length||blocks.length>16)throw new Error('Unsupported configuration declaration');
  for(const block of blocks) {
    if(!plain(block)||!plain(block.properties)||Object.keys(block).some(name=>!['title','properties','order'].includes(name)))throw new Error('Unsupported configuration declaration');
    for(const [name,definition] of Object.entries(block.properties)) {
      if(!/^[A-Za-z0-9_-]+(?:\.[A-Za-z0-9_-]+)*$/.test(name)||Buffer.byteLength(name)>128||definitions.has(name)||[...definitions.keys()].some(other=>other.startsWith(name+'.')||name.startsWith(other+'.'))||definitions.size>=256)throw new Error('Invalid or overlapping configuration key');
      if(!plain(definition)||Object.keys(definition).some(field=>!['type','default','description','markdownDescription','scope','enum','enumDescriptions'].includes(field))||!['string','boolean','number','integer','object','array','null'].includes(definition.type)||definition.scope!==undefined&&definition.scope!=='application')throw new Error('Unsupported configuration schema: '+name);
      const fallback={string:'',boolean:false,number:0,integer:0,object:{},array:[],null:null};
      const normalized={type:definition.type,default:clone(own(definition,'default')?definition.default:fallback[definition.type])};
      if(own(definition,'enum')){if(!Array.isArray(definition.enum)||!definition.enum.length||definition.enum.length>256)throw new Error('Invalid configuration enum');normalized.enum=clone(definition.enum);if(normalized.enum.some(value=>!accepts({type:normalized.type},value)))throw new Error('Configuration enum type mismatch');}
      if(!accepts(normalized,normalized.default))throw new Error('Configuration default type mismatch: '+name);
      definitions.set(name,normalized);
    }
  }
  envelope(Object.fromEntries([...definitions].map(([name,definition])=>[name,definition.default])));return definitions;
}
module.exports={configurationDefinitions};
