#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const fs=require('node:fs/promises'),path=require('node:path');
const {canonicalOutput}=require('./pack.cjs');

async function copyTree(from,to,transform=value=>value) {
  const stat=await fs.lstat(from);
  if(stat.isSymbolicLink())throw new Error('Template and SDK files must not be symlinks.');
  if(stat.isDirectory()) {
    await fs.mkdir(to,{recursive:true});
    for(const name of (await fs.readdir(from)).sort())await copyTree(path.join(from,name),path.join(to,name),transform);
  }else if(stat.isFile()) {
    await fs.mkdir(path.dirname(to),{recursive:true});
    await fs.writeFile(to,transform(await fs.readFile(from)),{flag:'wx',mode:stat.mode&0o777});
  }else throw new Error('Unsupported SDK file: '+from);
}
async function createPlugin(directory,{template='hello',id,name}={}) {
  if(!['hello','markdown'].includes(template))throw new Error('Template must be hello or markdown.');
  id??=template==='markdown'?'demo.markdown-preview':'demo.hello';
  name??=template==='markdown'?'Markdown Preview':'Hello Plugin';
  if(typeof id!=='string'||id.length>128||!/^[a-z0-9][a-z0-9-]*\.[a-z0-9][a-z0-9-]*$/.test(id))throw new Error('Plugin ID must be publisher.name using lowercase letters, digits and hyphens.');
  if(typeof name!=='string'||!name.trim()||name.length>128||/[\x00-\x1f]/.test(name))throw new Error('Plugin name must be 1–128 printable characters.');
  const output=await canonicalOutput(directory),sdk=await fs.realpath(__dirname);
  if(output===sdk||output.startsWith(sdk+path.sep))throw new Error('Create the project outside the SDK directory.');
  try{await fs.lstat(output);throw new Error('Project directory already exists. Choose a new path.');}catch(error){if(error.code!=='ENOENT')throw error;}
  await fs.mkdir(path.dirname(output),{recursive:true});
  const stage=await fs.mkdtemp(path.join(path.dirname(output),'.nnp-create-'));
  try {
    await copyTree(path.join(sdk,'templates',template),stage,bytes=>Buffer.from(bytes.toString('utf8').replaceAll('__PLUGIN_ID__',id)));
    await fs.rename(path.join(stage,'gitignore.txt'),path.join(stage,'.gitignore'));
    await fs.copyFile(path.join(sdk,'LICENSE'),path.join(stage,'LICENSE'));
    const manifestPath=path.join(stage,'notepadnext-plugin.json');
    const manifest=JSON.parse(await fs.readFile(manifestPath,'utf8'));
    manifest.id=id;manifest.name=name;
    await fs.writeFile(manifestPath,JSON.stringify(manifest,null,2)+'\n');
    const packagePath=path.join(stage,'package.json');
    const info=JSON.parse(await fs.readFile(packagePath,'utf8'));
    info.name=id.replace('.','-');info.devDependencies['@notepadnext/plugin-sdk']='file:./tools/plugin-sdk';
    await fs.writeFile(packagePath,JSON.stringify(info,null,2)+'\n');
    // Vendor the exact kit that generated the project. No repository-relative
    // paths or unpublished npm dependency are needed by a standalone author.
    const sdkInfo=JSON.parse(await fs.readFile(path.join(sdk,'package.json'),'utf8'));
    const vendor=path.join(stage,'tools','plugin-sdk');
    await fs.mkdir(vendor,{recursive:true});
    for(const file of sdkInfo.files)await copyTree(path.join(sdk,file),path.join(vendor,file));
    delete sdkInfo.scripts;
    await fs.writeFile(path.join(vendor,'package.json'),JSON.stringify(sdkInfo,null,2)+'\n',{flag:'wx'});
    await fs.rename(stage,output);
    return {directory:output,id,name,template,sdkVersion:sdkInfo.version,next:['npm install --ignore-scripts','npm run build','npm test','npm run debug']};
  }catch(error){await fs.rm(stage,{recursive:true,force:true});throw error;}
}
function parseArguments(args) {
  const directory=args.shift(),options={};
  if(!directory||directory.startsWith('--'))throw new Error('Usage: nnp-create <new-directory> [--template hello|markdown] [--id publisher.name] [--name "Plugin name"]');
  while(args.length) {
    const flag=args.shift(),key={'--template':'template','--id':'id','--name':'name'}[flag];
    if(!key||Object.hasOwn(options,key)||!args.length||args[0].startsWith('--'))throw new Error('Invalid or repeated option: '+flag);
    options[key]=args.shift();
  }
  return {directory,options};
}
module.exports={createPlugin,parseArguments};
if(require.main===module) {
  if(process.argv.includes('--help'))console.log('Usage: nnp-create <new-directory> [--template hello|markdown] [--id publisher.name] [--name "Plugin name"]\nCreates a standalone project with a vendored SDK. Does not install dependencies or run code.');
  else {
    try {
      if(Number(process.versions.node.split('.')[0])!==24)throw new Error('Use Node 24 for this SDK.');
      const {directory,options}=parseArguments(process.argv.slice(2));
      createPlugin(directory,options).then(result=>console.log(JSON.stringify(result,null,2))).catch(error=>{console.error(error.message);process.exitCode=1;});
    }catch(error){console.error(error.message);process.exitCode=1;}
  }
}
