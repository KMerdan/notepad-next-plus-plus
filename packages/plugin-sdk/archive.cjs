#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
// Writes the bounded stored/deflate ZIP profile consumed by the native
// installer. No external archiver, imports of plugin code or package hooks.
const fs=require('node:fs/promises'),path=require('node:path'),zlib=require('node:zlib'),crypto=require('node:crypto');
const {portable,canonicalOutput}=require('./pack.cjs');
async function archive(directory,output) {
  directory=await fs.realpath(directory);output=await canonicalOutput(output);
  if(output===directory||output.startsWith(directory+path.sep))throw new Error('Archive must be outside the package directory.');
  try{await fs.lstat(output);throw new Error('Output already exists.');}catch(e){if(e.code!=='ENOENT')throw e;}
  const entries=[],aliases=new Set();let total=0;
  async function walk(relative='',depth=0) {
    if(depth>32)throw new Error('Package nesting exceeds 32.');
    for(const name of (await fs.readdir(path.join(directory,relative))).sort()) {
      const item=relative?relative+'/'+name:name;
      if(!portable(item)||item==='.nnp-receipt.json'||aliases.size>=8192||aliases.has(item.normalize('NFC').toLowerCase()))throw new Error('Unsafe, duplicate or excessive package path: '+item);
      aliases.add(item.normalize('NFC').toLowerCase());const stat=await fs.lstat(path.join(directory,item));
      if(stat.isSymbolicLink())throw new Error('Symlinks are unsupported: '+item);
      if(stat.isDirectory()){entries.push({name:item+'/',size:0,directory:true});await walk(item,depth+1);}
      else if(stat.isFile()) {
        if(stat.size>32*1024*1024||(total+=stat.size)>256*1024*1024)throw new Error('Expanded package budget exceeded.');
        entries.push({name:item,size:stat.size});
      } else throw new Error('Unsupported package entry: '+item);
    }
  }
  await walk();if(!entries.some(e=>e.name==='notepadnext-plugin.json')||!entries.some(e=>e.name==='LICENSE'))throw new Error('Missing package metadata or LICENSE.');
  await fs.mkdir(path.dirname(output),{recursive:true});
  const temporary=output+'.'+crypto.randomUUID()+'.tmp';let handle;
  try {
    handle=await fs.open(temporary,'wx',0o600);const central=[];let offset=0;const hash=crypto.createHash('sha256');
    async function write(buffer) {
      if(offset+buffer.length>128*1024*1024)throw new Error('Archive exceeds 128 MiB.');
      let written=0;while(written<buffer.length){const result=await handle.write(buffer,written,buffer.length-written);if(!result.bytesWritten)throw new Error('Cannot write archive.');written+=result.bytesWritten;}
      offset+=buffer.length;hash.update(buffer);
    }
    for(const entry of entries) {
      const bytes=entry.directory?Buffer.alloc(0):await fs.readFile(path.join(directory,entry.name));if(bytes.length!==entry.size)throw new Error('Package changed while archiving.');
      const compressed=entry.directory?bytes:zlib.deflateRawSync(bytes,{level:9}),method=entry.directory?0:8,name=Buffer.from(entry.name),crc=zlib.crc32(bytes),start=offset;
      const local=Buffer.alloc(30);local.writeUInt32LE(0x04034b50);local.writeUInt16LE(20,4);local.writeUInt16LE(0x800,6);local.writeUInt16LE(method,8);local.writeUInt32LE(crc,14);local.writeUInt32LE(compressed.length,18);local.writeUInt32LE(bytes.length,22);local.writeUInt16LE(name.length,26);
      await write(local);await write(name);await write(compressed);
      const record=Buffer.alloc(46);record.writeUInt32LE(0x02014b50);record.writeUInt16LE((3<<8)|20,4);record.writeUInt16LE(20,6);record.writeUInt16LE(0x800,8);record.writeUInt16LE(method,10);record.writeUInt32LE(crc,16);record.writeUInt32LE(compressed.length,20);record.writeUInt32LE(bytes.length,24);record.writeUInt16LE(name.length,28);record.writeUInt32LE(((entry.directory?0o40755:0o100644)<<16)>>>0,38);record.writeUInt32LE(start,42);central.push(Buffer.concat([record,name]));
    }
    const centralOffset=offset;for(const record of central)await write(record);
    const end=Buffer.alloc(22);end.writeUInt32LE(0x06054b50);end.writeUInt16LE(entries.length,8);end.writeUInt16LE(entries.length,10);end.writeUInt32LE(offset-centralOffset,12);end.writeUInt32LE(centralOffset,16);await write(end);
    await handle.sync();await handle.close();handle=null;
    // link() is a no-overwrite publication on the same filesystem; unlinking
    // the temporary name leaves one ordinary archive, never an archive link.
    await fs.link(temporary,output);await fs.unlink(temporary);
    return {file:output,bytes:offset,sha256:hash.digest('hex'),entries:entries.length};
  }catch(e){if(handle)await handle.close();await fs.rm(temporary,{force:true});throw e;}
}
module.exports={archive};
if(require.main===module) {
  if(process.argv.length!==4){console.error('Usage: node archive.cjs <packed-plugin-directory> <new-output.zip>');process.exitCode=2;}
  else archive(process.argv[2],process.argv[3]).then(result=>console.log(JSON.stringify(result,null,2))).catch(e=>{console.error(e.message);process.exitCode=1;});
}
