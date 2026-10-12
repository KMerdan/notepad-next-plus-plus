#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
const {portable,canonicalOutput}=require('./pack.cjs');
const {configurationDefinitions}=require('../plugin-host/vscode/persistence.cjs');
const VERSION='0.1.0',API='1.100.0';
function apiRange(range){
  // Explicit bounded stable-version range grammar. Unsupported expressions
  // fail import rather than being guessed compatible.
  if(typeof range!=='string')throw new Error('Missing engines.vscode');
  const match=/^(\^|~)?(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/.exec(range.trim());
  if(!match)throw new Error('Unsupported engines.vscode range grammar: '+range);
  const [major,minor,patch]=match.slice(2).map(Number);if([major,minor,patch].some(v=>!Number.isSafeInteger(v)))throw new Error('Unsafe version component');
  const compare=(a,b)=>a[0]-b[0]||a[1]-b[1]||a[2]-b[2],target=[1,100,0],lower=[major,minor,patch];
  if(!match[1])return compare(target,lower)===0;
  const upper=match[1]==='~'?[major,minor+1,0]:major?[major+1,0,0]:minor?[0,minor+1,0]:[0,0,patch+1];
  return compare(target,lower)>=0&&compare(target,upper)<0;
}
async function importExtension(input,output,{capabilities=['messages@1'],license='LICENSE',resources=[]}={}) {
  const source=await fs.realpath(input);output=await canonicalOutput(output);
  async function checked(relative){
    if(!portable(relative))throw new Error('Unsafe extension path');
    let current=source;
    for(const part of relative.split('/')){current=path.join(current,part);if((await fs.lstat(current)).isSymbolicLink())throw new Error('Extension symlinks are unsupported');}
    const resolved=await fs.realpath(current);if(!resolved.startsWith(source+path.sep))throw new Error('Extension path escapes source');return current;
  }
  if(output===source||output.startsWith(source+path.sep))throw new Error('Output must be outside extension source');
  try{await fs.lstat(output);throw new Error('Output already exists');}catch(error){if(error.code!=='ENOENT')throw error;}
  const manifestPath=await checked('package.json'),manifestStat=await fs.stat(manifestPath);if(!manifestStat.isFile())throw new Error('Extension manifest must be a regular file');if(manifestStat.size>1024*1024)throw new Error('Extension manifest exceeds 1 MiB');
  const json=await fs.readFile(manifestPath);if(json.length>1024*1024)throw new Error('Extension manifest exceeds 1 MiB');const original=JSON.parse(json);
  const id=(original.publisher+'.'+original.name).toLowerCase();if(id.length>128||!/^[a-z0-9][a-z0-9-]*\.[a-z0-9][a-z0-9-]*$/.test(id))throw new Error('Unsupported extension identity');
  if(!/^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$/.test(original.version))throw new Error('Expected a SemVer extension version');
  const versionParts=original.version.split('+'),pre=versionParts[0].split('-').slice(1).join('-');
  if(original.version.length>128||pre.split('.').some(part=>pre&&(!part||/^\d+$/.test(part)&&part.length>1&&part[0]==='0'))||versionParts[1]?.split('.').some(part=>!part))throw new Error('Invalid SemVer extension version');
  if(typeof (original.displayName??original.name)!=='string'||!(original.displayName??original.name)||(original.displayName??original.name).length>128||typeof (original.description??'')!=='string'||(original.description??'').length>2048)throw new Error('Invalid extension display metadata');
  if(!apiRange(original.engines?.vscode))throw new Error('Extension engine does not include '+API);
  if(original.type==='module'||typeof original.main!=='string'||!original.main.endsWith('.js'))throw new Error('This compatibility profile requires a built CommonJS entry');
  if(Object.keys(original.dependencies??{}).length||Object.keys(original.optionalDependencies??{}).length||original.extensionDependencies?.length||original.extensionPack?.length)throw new Error('Runtime dependencies and cross-extension dependencies need a separate supported import profile');
  for(const key of Object.keys(original.contributes??{}))if(!['commands','configuration'].includes(key))throw new Error('Unsupported contribution: '+key);
  configurationDefinitions(original.contributes?.configuration);
  const commands=original.contributes?.commands;if(!Array.isArray(commands)||!commands.length||commands.length>32)throw new Error('Expected 1..32 declared commands');
  const seen=new Set(),aliases=commands.map((command,index)=>{if(!command||typeof command!=='object'||typeof command.command!=='string'||command.command.length>192||! /^[A-Za-z0-9_.-]+$/.test(command.command)||seen.has(command.command)||typeof command.title!=='string'||!command.title||command.title.length>128||command.category!==undefined&&typeof command.category!=='string'||Object.keys(command).some(key=>!['command','title','category'].includes(key)))throw new Error('Invalid or unsupported command declaration');seen.add(command.command);return {id:command.command,target:id+'.command'+index};});
  if(original.activationEvents!==undefined&&!Array.isArray(original.activationEvents))throw new Error('Invalid activation events');
  for(const activation of original.activationEvents??[])if(typeof activation!=='string'||!activation.startsWith('onCommand:')||!seen.has(activation.slice(10)))throw new Error('Unsupported activation event: '+activation);
  const main=original.main.replace(/^\.\//,'');if(!portable(main)||main.split('/').length<2)throw new Error('Main must be a portable built path inside a folder');
  if(!portable(license))throw new Error('Unsafe license path');
  const extra=new Set(capabilities);for(const capability of extra)if(!['messages@1','documents.write@1','webviews@1','webviews.compat@1','storage@1','configuration@1'].includes(capability))throw new Error('Unsupported importer capability: '+capability);
  if(original.contributes?.configuration!==undefined&&!extra.has('configuration@1'))throw new Error('Configuration contributions require explicit configuration@1');
  if(extra.has('webviews.compat@1')!==extra.has('webviews@1'))throw new Error('Compatible Webviews require both webviews@1 and webviews.compat@1');
  if(!Array.isArray(resources)||resources.length>16)throw new Error('Expected at most 16 explicit resource paths');
  const builtRoot=main.split('/')[0],copyRoots=[builtRoot];
  for(const resource of resources){if(typeof resource!=='string'||!portable(resource)||['vscode-package.json','import-provenance.json'].includes(resource)||copyRoots.some(root=>resource.normalize('NFC').toLowerCase()===root.normalize('NFC').toLowerCase()||resource.normalize('NFC').toLowerCase().startsWith(root.normalize('NFC').toLowerCase()+'/')||root.normalize('NFC').toLowerCase().startsWith(resource.normalize('NFC').toLowerCase()+'/')))throw new Error('Unsafe, reserved or overlapping resource path');copyRoots.push(resource);}
  await fs.mkdir(path.dirname(output),{recursive:true});const stage=await fs.mkdtemp(path.join(path.dirname(output),'.nnp-vscode-'));let files=0,total=0;const names=new Set();
  async function copy(relative,target){if(!portable(relative)||!portable(target)||names.has(target.normalize('NFC').toLowerCase())||++files>2048)throw new Error('Unsafe or excessive extension path');names.add(target.normalize('NFC').toLowerCase());const from=await checked(relative),stat=await fs.lstat(from);if(stat.isDirectory()){await fs.mkdir(path.join(stage,target),{recursive:true});for(const name of (await fs.readdir(from)).sort())await copy(relative+'/'+name,target+'/'+name);}else if(stat.isFile()){if(/\.node$/i.test(relative)||stat.size>16*1024*1024||(total+=stat.size)>64*1024*1024)throw new Error('Extension file/byte budget exceeded');await fs.mkdir(path.dirname(path.join(stage,target)),{recursive:true});await fs.copyFile(from,path.join(stage,target),require('node:fs').constants.COPYFILE_EXCL);}else throw new Error('Unsupported extension file');}
  try {
    for(const relative of copyRoots)await copy(relative,'dist/'+relative);await copy(license,'LICENSE');
    const entry=path.join(stage,'dist',main);if(!(await fs.stat(entry)).isFile())throw new Error('Built entry missing');
    const manifest={manifestVersion:1,id,name:original.displayName??original.name,description:original.description??'',version:original.version,list:false,engines:{notepadnext:'>=0.15.0 <0.16.0',api:'^0.1.0'},runtime:{kind:'node',entry:'dist/'+main},compatibility:{kind:'vscode',version:VERSION,api:API,extensionRoot:'dist',commands:aliases},capabilities:{required:['commands@1','documents.read@1','documents.delta@1',...extra],optional:[]},activationEvents:aliases.map(alias=>'onCommand:'+alias.target),contributes:{commands:commands.map((command,index)=>({id:aliases[index].target,title:command.title})),menus:aliases.map(alias=>({location:'plugins',command:alias.target}))}};
    await fs.writeFile(path.join(stage,'notepadnext-plugin.json'),JSON.stringify(manifest,null,2)+'\n');
    await fs.writeFile(path.join(stage,'package.json'),JSON.stringify({name:id,version:original.version,private:true,type:'commonjs',license:original.license??'SEE LICENSE IN LICENSE'},null,2)+'\n');
    await fs.writeFile(path.join(stage,'dist','vscode-package.json'),json);
    await fs.writeFile(path.join(stage,'dist','import-provenance.json'),JSON.stringify({compatVersion:VERSION,declarationBaseline:API,originalManifestSha256:crypto.createHash('sha256').update(json).digest('hex'),entrySha256:crypto.createHash('sha256').update(await fs.readFile(entry)).digest('hex'),resourcePaths:resources,capabilities:[...extra],builtCodeOnly:true,hooksExecuted:false},null,2)+'\n');
    await fs.rename(stage,output);return {id,directory:output,commands:aliases.length,compatVersion:VERSION};
  }catch(error){await fs.rm(stage,{recursive:true,force:true});throw error;}
}
module.exports={importExtension,apiRange};
if(require.main===module){
  const [input,output,...flags]=process.argv.slice(2),options={resources:[],capabilities:['messages@1']};
  try{if(!input||!output)throw new Error('Usage: nnp-import-vscode <built-extension> <new-output> [--webviews] [--storage] [--configuration] [--resource=media] [--license=LICENSE]');for(const flag of flags){if(flag==='--webviews')options.capabilities.push('webviews@1','webviews.compat@1');else if(flag==='--storage')options.capabilities.push('storage@1');else if(flag==='--configuration')options.capabilities.push('configuration@1');else if(flag.startsWith('--resource='))options.resources.push(flag.slice(11));else if(flag.startsWith('--license='))options.license=flag.slice(10);else throw new Error('Unknown importer option: '+flag);}importExtension(input,output,options).then(result=>console.log(JSON.stringify(result)),error=>{console.error(error.message);process.exitCode=1;});}catch(error){console.error(error.message);process.exitCode=1;}
}
