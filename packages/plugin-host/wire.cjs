// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const {isDeepStrictEqual}=require('node:util');
const schema=require('./wire.schema.json'), contract=require('./wire-contract.json');
const {RpcError}=require('./rpc.cjs');
const own=(object,key)=>Object.prototype.hasOwnProperty.call(object,key);
const object=value=>value!==null&&typeof value==='object'&&!Array.isArray(value);
function json(value,depth=0,seen=new Set()) {
  if(depth>128||value===undefined||typeof value==='bigint'||typeof value==='function'||typeof value==='symbol'||(typeof value==='number'&&!Number.isFinite(value)))return false;
  if(value!==null&&typeof value==='object') {
    if(seen.has(value)||(!Array.isArray(value)&&Object.getPrototypeOf(value)!==Object.prototype&&Object.getPrototypeOf(value)!==null))return false;
    seen.add(value);const ok=Object.values(value).every(item=>json(item,depth+1,seen));seen.delete(value);return ok;
  }
  return true;
}
function matches(value,rule,budget,depth=0) {
  if(depth>192)return false;
  if(rule.$ref){const name=rule.$ref.startsWith('#/$defs/')&&rule.$ref.slice(8);if(!name||!own(schema.$defs,name)||!matches(value,schema.$defs[name],budget,depth+1))return false;}
  if(own(rule,'const')&&!isDeepStrictEqual(value,rule.const))return false;
  if(rule.enum&&!rule.enum.some(item=>isDeepStrictEqual(value,item)))return false;
  if(rule.oneOf&&rule.oneOf.filter(branch=>matches(value,branch,budget,depth+1)).length!==1)return false;
  if(rule.type) {
    const valid={object:object(value),array:Array.isArray(value),string:typeof value==='string',null:value===null,boolean:typeof value==='boolean',integer:Number.isSafeInteger(value)};
    if(!valid[rule.type])return false;
  }
  if(object(value)) {
    if(rule.required?.some(key=>!own(value,key)))return false;
    for(const [key,item] of Object.entries(value)) {
      if(rule.properties&&own(rule.properties,key)){if(!matches(item,rule.properties[key],budget,depth+1))return false;}
      else if(rule.additionalProperties===false)return false;
    }
  }
  if(Array.isArray(value)) {
    if((own(rule,'minItems')&&value.length<rule.minItems)||(own(rule,'maxItems')&&value.length>rule.maxItems))return false;
    if(rule.items&&!value.every(item=>matches(item,rule.items,budget,depth+1)))return false;
    if(rule.uniqueItems&&value.some((item,index)=>value.slice(0,index).some(other=>isDeepStrictEqual(item,other))))return false;
  }
  if(typeof value==='string') {
    if(own(rule,'x-nnp-maxUtf8Bytes')&&Buffer.byteLength(value)>rule['x-nnp-maxUtf8Bytes']){budget.tooLarge=true;return false;}
    let length=0;for(const character of value){++length;if(own(rule,'maxLength')&&length>rule.maxLength)return false;}
    if((own(rule,'minLength')&&length<rule.minLength)||(own(rule,'maxLength')&&length>rule.maxLength)||(rule.pattern&&!new RegExp(rule.pattern,'u').test(value)))return false;
  }
  if(typeof value==='number'&&((own(rule,'minimum')&&value<rule.minimum)||(own(rule,'maximum')&&value>rule.maximum)))return false;
  return true;
}
function validate(method,part,value,budget={tooLarge:false}) {
  const ref=contract.methods[method]?.[part+'Schema'];return (part==='params'||part==='result')&&!!ref&&json(value)&&matches(value,{$ref:ref},budget);
}
function requireValid(method,part,value) {const budget={tooLarge:false};if(!validate(method,part,value,budget))throw new RpcError(budget.tooLarge?1008:(part==='params'?-32602:-32603),`Invalid ${part} contract for ${method}`);}
function register(endpoint,method,handler) {
  endpoint.register(method,async(params,options)=>{
    const definition=contract.methods[method];
    if(options.notification!==(definition?.kind==='notification'))throw new RpcError(-32600,'Invalid method message kind');
    requireValid(method,'params',params);const value=(await handler(params,options))??null;
    if(definition.kind==='request')requireValid(method,'result',value);return value;
  },{reliableNotification:contract.methods[method]?.kind==='notification'&&['orderedDocumentMirror','ownedMessage','ownedPanelEvent'].includes(contract.methods[method]?.effect)});
}
module.exports={validate,requireValid,register,contract};
