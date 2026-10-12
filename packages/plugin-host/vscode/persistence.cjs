// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const fs=require('node:fs'),path=require('node:path');
const {isDeepStrictEqual}=require('node:util');
const {RpcError}=require('../rpc.cjs');
const {EventEmitter}=require('./types.cjs');
const KEYS=Object.freeze({global:'_vscode.globalState.v1',workspace:'_vscode.workspaceState.mainSession.v1',configuration:'_vscode.configuration.v1'});
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
function decode(raw) {
  if(raw===null)return Object.create(null);
  if(!plain(raw)||Object.keys(raw).length!==2||raw.schemaVersion!==1||!plain(raw.values)||Object.keys(raw.values).length>256)throw new RpcError(1010,'Compatibility data format is invalid; existing data was preserved');
  try{clone(raw);for(const name of Object.keys(raw.values))key(name);}catch{throw new RpcError(1010,'Compatibility data is invalid; existing data was preserved');}
  return Object.assign(Object.create(null),raw.values);
}
function envelope(values) {if(Object.keys(values).length>256)throw new RpcError(1007,'Compatibility namespace exceeds 256 keys');return clone({schemaVersion:1,values});}
function accepts(definition,value) {
  const type=definition.type;
  const matches=type==='null'?value===null:type==='array'?Array.isArray(value):type==='object'?plain(value):type==='integer'?Number.isSafeInteger(value):type==='number'?typeof value==='number'&&Number.isFinite(value):typeof value===type;
  if(!matches)return false;
  const normalized=definition.enum?clone(value):value;
  return !definition.enum||definition.enum.some(item=>isDeepStrictEqual(item,normalized));
}
// Historical adapter contract is isolated from the native SDK/runtime.
const {configurationDefinitions:parseConfigurationDefinitions}=require('./configuration.cjs');
function configurationDefinitions(contribution) {
  try{return parseConfigurationDefinitions(contribution);}
  catch(error){if(typeof error.code==='number')throw new RpcError(error.code,error.message);throw error;}
}
function readDefinitions(extensionRoot) {
  const file=path.join(extensionRoot,'vscode-package.json');if(!fs.existsSync(file))return new Map();
  const real=fs.realpathSync(file);if(!real.startsWith(extensionRoot+path.sep)||!fs.statSync(real).isFile()||fs.statSync(real).size>1024*1024)throw new RpcError(1003,'Configuration manifest is outside the bounded package');
  const bytes=fs.readFileSync(real);if(bytes.length>1024*1024)throw new RpcError(1008,'Configuration manifest exceeds 1 MiB');
  return configurationDefinitions(JSON.parse(bytes).contributes?.configuration);
}
async function createPersistence(native,{extensionRoot,assertOpen,facade,unsupported,owned,diagnostic}) {
  let active=false,closed=false;const pending=new Set(),stores=[];
  const changed=new EventEmitter(diagnostic);owned.add(changed);
  function grant(capability){assertOpen();if(!native.capabilities.includes(capability))throw new RpcError(1003,'CapabilityDenied: '+capability);}
  async function store(namespace,remoteKey,capability,onChange=()=>{},validateValues=()=>{}) {
    if(!native.capabilities.includes(capability))return null;
    const remote=native[namespace];let raw=await remote.get(remoteKey),values=decode(raw),tail=Promise.resolve(),queued=0;validateValues(values);
    const result={get:key=>own(values,key)?clone(values[key]):undefined,keys:()=>Object.keys(values),async flush(){await tail;},update(name,value) {
      grant(capability);if(closed||!active)return Promise.reject(new RpcError(1004,'Data writes require a validated active plugin'));
      name=key(name);const saved=value===undefined?undefined:clone(value);
      if(queued>=64)return Promise.reject(new RpcError(1007,'Compatibility data queue is full'));
      ++queued;
      const operation=tail.then(async()=>{
        for(let attempt=0;attempt<4;++attempt) {
          assertOpen();if(closed)throw new RpcError(1004,'PluginStopping');
          const next=Object.assign(Object.create(null),values);if(saved===undefined)delete next[name];else next[name]=saved;
          const replacement=envelope(next);
          try{await remote.compareExchange(remoteKey,raw,replacement);const previous=values;raw=replacement;values=next;onChange(previous,values);return;}
          catch(error){if(error.code!==1001||attempt===3)throw error;const refreshed=await remote.get(remoteKey);const latest=decode(refreshed);validateValues(latest);const previous=values;raw=refreshed;values=latest;onChange(previous,values);}
        }
      });
      pending.add(operation);tail=operation.catch(()=>{});operation.then(()=>{--queued;pending.delete(operation);},error=>{--queued;pending.delete(operation);diagnostic(error);});return operation;
    }};stores.push(result);return result;
  }
  const global=await store('storage',KEYS.global,'storage@1'),workspace=await store('storage',KEYS.workspace,'storage@1');
  const definitions=native.capabilities.includes('configuration@1')?readDefinitions(extensionRoot):new Map();
  function effective(values,name){return own(values,name)?values[name]:definitions.get(name)?.default;}
  const configuration=await store('configuration',KEYS.configuration,'configuration@1',(previous,next)=>{
    const affected=[...definitions.keys()].filter(name=>!isDeepStrictEqual(effective(previous,name),effective(next,name)));if(!affected.length)return;
    changed.fire(facade('ConfigurationChangeEvent',{affectsConfiguration(section,scope){if(scope!==undefined&&scope!==null)throw unsupported('ConfigurationChangeEvent.scope');key(section);return affected.some(name=>name===section||name.startsWith(section+'.')||section.startsWith(name+'.'));}}));
  },values=>{for(const name of Object.keys(values))if(definitions.has(name)&&!accepts(definitions.get(name),values[name]))throw new RpcError(1010,'Stored configuration no longer matches its declaration; data was preserved');});
  function memento(name,value) {return facade(name,{get(name,defaultValue){grant('storage@1');name=key(name);const result=value.get(name);return result===undefined?defaultValue:result;},keys(){grant('storage@1');return Object.freeze(value.keys());},async update(name,data){grant('storage@1');return value.update(name,data);}});}
  function getConfiguration(section='',scope) {
    grant('configuration@1');if(scope!==undefined&&scope!==null)throw unsupported('workspace.getConfiguration.scope');if(section!=='')key(section);
    const absolute=name=>{if(name!=='')key(name);return [section,name].filter(Boolean).join('.');};
    const read=name=>{
      const tree=Object.create(null);
      for(const [full,definition] of definitions) {const parts=full.split('.');let target=tree;for(const part of parts.slice(0,-1))target=target[part]??=(Object.create(null));target[parts.at(-1)]=configuration.keys().includes(full)?configuration.get(full):clone(definition.default);}
      let result=tree;for(const part of name?name.split('.'):[]){if(result===null||typeof result!=='object'||!own(result,part))return undefined;result=result[part];}return clone(result);
    };
    return facade('WorkspaceConfiguration',{get(name,defaultValue){assertOpen();const value=read(absolute(name));return value===undefined?defaultValue:value;},has(name){assertOpen();return read(absolute(name))!==undefined;},inspect(name){assertOpen();const full=absolute(name),definition=definitions.get(full);if(!definition)return undefined;return Object.freeze({key:full,defaultValue:clone(definition.default),globalValue:configuration.get(full)});},async update(name,value,target,overrideInLanguage){
      assertOpen();if(target!==true&&target!==1)throw unsupported('WorkspaceConfiguration.update target (requires Global)');if(overrideInLanguage!==undefined&&overrideInLanguage!==false)throw unsupported('WorkspaceConfiguration.update language override');
      const full=absolute(name),definition=definitions.get(full);if(!definition)throw unsupported('WorkspaceConfiguration.update undeclared '+full);if(value!==undefined&&!accepts(definition,value))throw new RpcError(-32602,'Configuration value does not match its declaration');return configuration.update(full,value);
    }});
  }
  return {globalState:memento('globalState',global),workspaceState:memento('workspaceState',workspace),getConfiguration,onDidChangeConfiguration:changed.event,activate(){active=true;},async flush(){await Promise.all(stores.map(store=>store.flush()));},async dispose(){closed=true;active=false;await Promise.allSettled([...pending]);changed.dispose();}};
}
module.exports={createPersistence,configurationDefinitions,KEYS};
