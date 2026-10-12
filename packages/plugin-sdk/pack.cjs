#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
// Creates a local release directory from already built code and installed
// production dependencies. Reads files only; never imports plugin code or runs
// package hooks. Archive writing is a separate release transport step.
const fs=require('node:fs/promises'),path=require('node:path'),{createRequire}=require('node:module'),crypto=require('node:crypto');
const portable=(relative)=>relative.length<=512&&relative.split('/').every(part=>part&&part!=='.'&&part!=='..'&&!/[\\:\x00-\x1f<>"|?*]/.test(part)&&!/[. ]$/.test(part)&&!/^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)/i.test(part));
async function canonicalOutput(output) {
  output=path.resolve(output);
  // Canonicalize the nearest existing ancestor, also for a not-yet-created
  // target. /var and /private/var on macOS must denote the same location.
  let ancestor=path.dirname(output),suffix=[path.basename(output)];
  for(;;) {
    try{output=path.join(await fs.realpath(ancestor),...suffix);break;}
    catch(e){if(e.code!=='ENOENT')throw e;suffix.unshift(path.basename(ancestor));ancestor=path.dirname(ancestor);}
  }
  return output;
}
async function pack(source,output) {
  source=await fs.realpath(source);output=await canonicalOutput(output);
  if(output===source||output.startsWith(source+path.sep))throw new Error('Output must be outside the plugin source.');
  try{await fs.lstat(output);throw new Error('Output already exists.');}catch(e){if(e.code!=='ENOENT')throw e;}
  await fs.mkdir(path.dirname(output),{recursive:true});
  const stage=await fs.mkdtemp(path.join(path.dirname(output),'.nnp-package-'));
  let totalBytes=0;const aliases=new Set(),visited=new Set(),licenses=[];
  const json=async(file)=>JSON.parse(await fs.readFile(file,'utf8'));
  async function copy(from,relative,excludeModules=false) {
    if(!portable(relative)||aliases.has(relative.normalize('NFC').toLowerCase())||aliases.size>=8192)throw new Error('Unsafe, duplicate or excessive package path: '+relative);
    aliases.add(relative.normalize('NFC').toLowerCase());
    const stat=await fs.lstat(from);
    if(stat.isSymbolicLink())throw new Error('Symlinks are unsupported: '+relative);
    if(stat.isDirectory()) {
      await fs.mkdir(path.join(stage,relative),{recursive:true});
      for(const name of (await fs.readdir(from)).sort()) {
        if(excludeModules&&name==='node_modules')continue;
        await copy(path.join(from,name),relative+'/'+name,excludeModules);
      }
    } else if(stat.isFile()) {
      if(/\.node$/i.test(relative))throw new Error('Native Node addons are unsupported.');
      if(stat.size>32*1024*1024||(totalBytes+=stat.size)>256*1024*1024)throw new Error('Package byte budget exceeded.');
      await fs.mkdir(path.dirname(path.join(stage,relative)),{recursive:true});
      await fs.copyFile(from,path.join(stage,relative),require('node:fs').constants.COPYFILE_EXCL);
    } else throw new Error('Unsupported package file: '+relative);
  }
  async function moduleRoot(name,from) {
    if(!/^(?:@[a-z0-9._-]+\/)?[a-z0-9._-]+$/i.test(name))throw new Error('Invalid dependency name: '+name);
    // Resolve the directory as Node would, without evaluating its entry or
    // bypassing package exports. This also retains nested conflicting versions.
    for(let dir=from;;dir=path.dirname(dir)) {
      const candidate=path.join(dir,'node_modules',name);
      try{if((await fs.lstat(candidate)).isDirectory())return candidate;}catch(e){if(e.code!=='ENOENT')throw e;}
      if(path.dirname(dir)===dir)break;
    }
    // Included to produce Node's familiar resolution error, without requiring it.
    createRequire(path.join(from,'package.json')).resolve(name);throw new Error('Dependency must be an installed directory: '+name);
  }
  async function dependency(name,from,optional=false) {
    let folder;
    try{folder=await moduleRoot(name,from);}catch(e){if(optional&&e.code==='MODULE_NOT_FOUND')return;throw e;}
    if(visited.has(folder))return;visited.add(folder);
    const relative=path.relative(source,folder).split(path.sep).join('/');
    if(!relative.startsWith('node_modules/'))throw new Error('Production dependency is outside the plugin source: '+name);
    const info=await json(path.join(folder,'package.json'));
    if(info.name!==name)throw new Error('Dependency identity mismatch: '+name);
    await copy(folder,relative,true);
    licenses.push({name:info.name,version:info.version,license:info.license||null,path:relative});
    for(const dep of Object.keys(info.dependencies||{}).sort())await dependency(dep,folder,Object.hasOwn(info.optionalDependencies||{},dep));
    for(const dep of Object.keys(info.optionalDependencies||{}).sort())await dependency(dep,folder,true);
  }
  try {
    const metadata=await json(path.join(source,'notepadnext-plugin.json')),info=await json(path.join(source,'package.json'));
    const iconProfile=require('./schemas/manifest.schema.json').$defs.iconPath;
    for(const command of metadata.contributes?.commands||[])if(command.icon!==undefined) {
      if(typeof command.icon!=='string'||command.icon.length>iconProfile.maxLength||!new RegExp(iconProfile.pattern).test(command.icon)||!portable(command.icon))
        throw new Error('Command icons must use package-relative PNG paths supported by the metadata profile.');
    }
    if(metadata.manifestVersion!==1||metadata.list!==false||!['node','python'].includes(metadata.runtime?.kind))throw new Error('Expected one native Node or Python plugin.');
    if(metadata.runtime.kind==='python'&&(Object.keys(info.dependencies||{}).length||Object.keys(info.optionalDependencies||{}).length))throw new Error('Python packages must vendor modules in dist/ and cannot declare npm runtime dependencies.');
    if(metadata.version!==info.version)throw new Error('Package and plugin versions differ.');
    const entry=metadata.runtime.entry;
    if(!portable(entry)||!entry.startsWith('dist/'))throw new Error('Release entry must be a built file inside dist/.');
    await copy(path.join(source,'notepadnext-plugin.json'),'notepadnext-plugin.json');
    await copy(path.join(source,'dist'),'dist');
    try{await fs.lstat(path.join(source,'resources'));await copy(path.join(source,'resources'),'resources');}catch(e){if(e.code!=='ENOENT')throw e;}
    for(const command of metadata.contributes?.commands||[])if(command.icon&&!aliases.has(command.icon.normalize('NFC').toLowerCase()))await copy(path.join(source,command.icon),command.icon);
    await copy(path.join(source,'LICENSE'),'LICENSE');
    const runtimeInfo={...info};delete runtimeInfo.scripts;delete runtimeInfo.devDependencies;
    await fs.writeFile(path.join(stage,'package.json'),JSON.stringify(runtimeInfo,null,2)+'\n',{flag:'wx'});
    for(const dep of Object.keys(info.dependencies||{}).sort())await dependency(dep,source,Object.hasOwn(info.optionalDependencies||{},dep));
    for(const dep of Object.keys(info.optionalDependencies||{}).sort())await dependency(dep,source,true);
    await fs.writeFile(path.join(stage,'third-party-dependencies.json'),JSON.stringify(licenses,null,2)+'\n',{flag:'wx'});
    const built=await fs.lstat(path.join(stage,entry));if(!built.isFile())throw new Error('Built runtime entry does not exist.');
    // Rename publishes the completed release tree. Existing targets never change.
    await fs.rename(stage,output);
    return {directory:output,id:metadata.id,version:metadata.version,dependencies:licenses.length,sourceMetadataSha256:crypto.createHash('sha256').update(await fs.readFile(path.join(source,'notepadnext-plugin.json'))).digest('hex')};
  } catch(e) {await fs.rm(stage,{recursive:true,force:true});throw e;}
}
module.exports={pack,portable,canonicalOutput};
if(require.main===module) {
  if(process.argv.length!==4){console.error('Usage: node pack.cjs <built-plugin-directory> <new-output-directory>');process.exitCode=2;}
  else pack(process.argv[2],process.argv[3]).then(result=>console.log(JSON.stringify(result,null,2))).catch(e=>{console.error(e.message);process.exitCode=1;});
}
