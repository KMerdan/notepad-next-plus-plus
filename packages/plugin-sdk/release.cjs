#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
// Produces both release assets from built files. No plugin imports, dependency
// installation or package hooks run here. Building belongs to the author.
const fs=require('node:fs/promises'),path=require('node:path');
const {pack,canonicalOutput}=require('./pack.cjs'),{archive}=require('./archive.cjs');
async function release(source,output,expectedTag) {
  source=await fs.realpath(source);output=await canonicalOutput(output);
  if(output===source||output.startsWith(source+path.sep))throw new Error('Release output must be outside the plugin source.');
  try{await fs.lstat(output);throw new Error('Release output already exists.');}catch(error){if(error.code!=='ENOENT')throw error;}
  const metadata=JSON.parse(await fs.readFile(path.join(source,'notepadnext-plugin.json'),'utf8'));
  if(typeof metadata.id!=='string'||!/^[a-z0-9][a-z0-9._-]{0,127}(?![\s\S])/.test(metadata.id)||typeof metadata.version!=='string'||!/^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?![\s\S])/.test(metadata.version))throw new Error('Release needs a valid plugin ID and stable SemVer version.');
  if(expectedTag!==undefined&&expectedTag!=='v'+metadata.version)throw new Error('Release tag must equal v'+metadata.version+'.');
  await fs.mkdir(path.dirname(output),{recursive:true});const stage=await fs.mkdtemp(path.join(path.dirname(output),'.nnp-release-'));
  try {
    const packed=await pack(source,path.join(stage,'package'));
    const result=await archive(packed.directory,path.join(stage,'notepadnext-plugin.zip'));
    // Take the ID/version from the packed copy, and detect metadata changes
    // between the initial tag check and packaging before publishing anything.
    const actual=JSON.parse(await fs.readFile(path.join(packed.directory,'notepadnext-plugin.json'),'utf8'));
    if(actual.id!==metadata.id||actual.version!==metadata.version)throw new Error('Plugin metadata changed while preparing the release.');
    const checksums={schemaVersion:1,pluginId:actual.id,version:actual.version,assets:[{name:'notepadnext-plugin.zip',size:result.bytes,sha256:result.sha256}]};
    await fs.writeFile(path.join(stage,'notepadnext-plugin-checksums.json'),JSON.stringify(checksums,null,2)+'\n',{flag:'wx',mode:0o600});
    await fs.rm(packed.directory,{recursive:true});await fs.rename(stage,output);
    return {directory:output,...checksums,entries:result.entries,sourceMetadataSha256:packed.sourceMetadataSha256};
  }catch(error){await fs.rm(stage,{recursive:true,force:true});throw error;}
}
module.exports={release};
if(require.main===module) {
  if(process.argv.length<4||process.argv.length>5){console.error('Usage: node release.cjs <built-plugin-directory> <new-release-directory> [v<version>]');process.exitCode=2;}
  else release(...process.argv.slice(2)).then(result=>console.log(JSON.stringify(result,null,2))).catch(error=>{console.error(error.message);process.exitCode=1;});
}
