// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const {test}=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs/promises'),os=require('node:os'),path=require('node:path');
const {pack}=require('./pack.cjs');
const {archive}=require('./archive.cjs');
const {release}=require('./release.cjs');
async function fixture(t) {
  const temp=await fs.mkdtemp(path.join(os.tmpdir(),'nnp-pack-test-'));t.after(()=>fs.rm(temp,{recursive:true,force:true}));
  const source=path.join(temp,'source'),output=path.join(temp,'release');
  const write=async(relative,value)=>{await fs.mkdir(path.dirname(path.join(source,relative)),{recursive:true});await fs.writeFile(path.join(source,relative),typeof value==='string'?value:JSON.stringify(value));};
  await write('notepadnext-plugin.json',{manifestVersion:1,id:'tests.sample',version:'1.0.0',list:false,runtime:{kind:'node',entry:'dist/index.js'}});
  await write('package.json',{name:'sample',version:'1.0.0',scripts:{postinstall:'touch sentinel'},dependencies:{a:'1.0.0',b:'2.0.0'},devDependencies:{dev:'1.0.0'}});
  await write('LICENSE','test license');await write('dist/index.js',"throw new Error('pack must never evaluate plugin code');");
  await write('node_modules/a/package.json',{name:'a',version:'1.0.0',license:'MIT',dependencies:{b:'1.0.0'}});await write('node_modules/a/index.js','a');
  await write('node_modules/a/node_modules/b/package.json',{name:'b',version:'1.0.0',license:'MIT'});await write('node_modules/a/node_modules/b/index.js','nested b');
  await write('node_modules/b/package.json',{name:'b',version:'2.0.0',license:'MIT'});await write('node_modules/b/index.js','root b');
  await write('node_modules/dev/index.js','dev');await write('node_modules/dev/package.json',{name:'dev',version:'1.0.0'});
  return {source,output,write};
}
test('packages built code, runtime dependencies, nested versions and licenses without evaluating code/hooks',async t=> {
  const {source,output}=await fixture(t),result=await pack(source,output);assert.equal(result.dependencies,3);
  assert.equal(await fs.readFile(path.join(output,'node_modules/a/node_modules/b/index.js'),'utf8'),'nested b');
  assert.equal(await fs.readFile(path.join(output,'node_modules/b/index.js'),'utf8'),'root b');
  const info=JSON.parse(await fs.readFile(path.join(output,'package.json'),'utf8'));assert.equal(info.scripts,undefined);assert.equal(info.devDependencies,undefined);
  await assert.rejects(fs.lstat(path.join(output,'node_modules/dev')),{code:'ENOENT'});await assert.rejects(fs.lstat(path.join(source,'sentinel')),{code:'ENOENT'});
  assert.equal(JSON.parse(await fs.readFile(path.join(output,'third-party-dependencies.json'),'utf8')).length,3);
  await assert.rejects(pack(source,output),/already exists/);
});
test('rejects escaping entry and clears failed staging',async t=> {
  const {source,output,write}=await fixture(t);await write('notepadnext-plugin.json',{manifestVersion:1,list:false,version:'1.0.0',runtime:{kind:'node',entry:'../escape.js'}});
  await assert.rejects(pack(source,output),/entry/);await assert.rejects(fs.lstat(output),{code:'ENOENT'});
  assert.equal((await fs.readdir(path.dirname(output))).filter(x=>x.startsWith('.nnp-package-')).length,0);
});
test('rejects icon formats excluded by the registration metadata profile',async t=>{
  const {source,output,write}=await fixture(t);
  await write('notepadnext-plugin.json',{manifestVersion:1,id:'tests.sample',version:'1.0.0',list:false,runtime:{kind:'node',entry:'dist/index.js'},contributes:{commands:[{id:'tests.sample.open',icon:'resources/preview.svg'}]}});
  await assert.rejects(pack(source,output),/PNG paths/);
  await assert.rejects(fs.stat(output),{code:'ENOENT'});
});
test('rejects links and native addons',async t=> {
  const {source,output,write}=await fixture(t);await fs.symlink(path.join(source,'LICENSE'),path.join(source,'dist','link'));
  await assert.rejects(pack(source,output),/Symlinks/);await fs.unlink(path.join(source,'dist/link'));await write('dist/addon.node','native');
  await assert.rejects(pack(source,output),/Native/);
});
test('rejects missing production dependency and source-contained output',async t=> {
  const {source,output}=await fixture(t);await assert.rejects(pack(source,path.join(source,'release')),/outside/);
  await fs.rm(path.join(source,'node_modules/a'),{recursive:true});await assert.rejects(pack(source,output),/Cannot find module/);
});
test('release archive has UTF-8 deflate entries, central directory, CRC and SHA-256',async t=> {
  const {source,output}=await fixture(t);await fs.mkdir(path.join(source,'resources/empty'),{recursive:true});await pack(source,output);
  const filename=output+'.zip',result=await archive(output,filename),bytes=await fs.readFile(filename);
  assert.equal(bytes.readUInt32LE(0),0x04034b50);assert.equal(bytes.readUInt32LE(bytes.length-22),0x06054b50);
  assert.equal(result.sha256,require('node:crypto').createHash('sha256').update(bytes).digest('hex'));assert.equal(result.bytes,bytes.length);
  assert.equal(bytes.readUInt16LE(bytes.length-22+10),result.entries);
  let offset=0;const files=new Map();
  while(bytes.readUInt32LE(offset)===0x04034b50) {
    assert.equal(bytes.readUInt16LE(offset+6),0x800);const nameLength=bytes.readUInt16LE(offset+26),extraLength=bytes.readUInt16LE(offset+28),size=bytes.readUInt32LE(offset+18),start=offset+30+nameLength+extraLength,name=bytes.subarray(offset+30,offset+30+nameLength).toString();
    const compressed=bytes.subarray(start,start+size),zlib=require('node:zlib'),content=bytes.readUInt16LE(offset+8)===8?zlib.inflateRawSync(compressed):compressed;
    assert.equal(zlib.crc32(content),bytes.readUInt32LE(offset+14));assert.equal(content.length,bytes.readUInt32LE(offset+22));files.set(name,content);offset=start+size;
  }
  assert.equal(offset,bytes.readUInt32LE(bytes.length-22+16));assert.ok(files.has('resources/empty/'));assert.ok(files.has('dist/index.js'));assert.ok(files.has('node_modules/a/node_modules/b/index.js'));
  await assert.rejects(archive(output,filename),/already exists/);await assert.rejects(archive(output,path.join(output,'inside.zip')),/outside/);
});
test('release publishes the ZIP and matching author checksums without evaluating plugin code',async t=> {
  const {source,output}=await fixture(t);const result=await release(source,output,'v1.0.0');
  assert.deepEqual((await fs.readdir(output)).sort(),['notepadnext-plugin-checksums.json','notepadnext-plugin.zip']);
  const checksums=JSON.parse(await fs.readFile(path.join(output,'notepadnext-plugin-checksums.json'),'utf8')),bytes=await fs.readFile(path.join(output,'notepadnext-plugin.zip'));
  assert.equal(checksums.pluginId,'tests.sample');assert.equal(checksums.version,'1.0.0');assert.equal(checksums.assets[0].size,bytes.length);
  assert.equal(checksums.assets[0].sha256,require('node:crypto').createHash('sha256').update(bytes).digest('hex'));assert.equal(result.assets[0].sha256,checksums.assets[0].sha256);
  await assert.rejects(fs.lstat(path.join(source,'sentinel')),{code:'ENOENT'});await assert.rejects(release(source,output),/already exists/);
});
test('marketplace release accepts its plugin namespace and rejects another plugin',async t=> {
  const {source,output}=await fixture(t);
  await assert.rejects(release(source,output,'tests.other/v1.0.0'),/tag/);
  await assert.rejects(release(source,output,'tests.sample/v1.0.1'),/tag/);
  const result=await release(source,output,'tests.sample/v1.0.0');
  assert.equal(result.pluginId,'tests.sample');assert.equal(result.version,'1.0.0');
});
test('release rejects mismatched tags, unstable versions and incomplete builds without leaving output',async t=> {
  const {source,output,write}=await fixture(t);await assert.rejects(release(source,output,'v2.0.0'),/tag/);await assert.rejects(fs.lstat(output),{code:'ENOENT'});
  await write('notepadnext-plugin.json',{id:'tests.sample',version:'1.0.0-beta.1'});await assert.rejects(release(source,output),/SemVer/);
  await write('notepadnext-plugin.json',{id:'tests.sample\n',version:'1.0.0'});await assert.rejects(release(source,output),/plugin ID/);
  await write('notepadnext-plugin.json',{id:'tests.sample',version:'1.0.0\n'});await assert.rejects(release(source,output),/SemVer/);
  await write('notepadnext-plugin.json',{manifestVersion:1,id:'tests.sample',version:'1.0.0',list:false,runtime:{kind:'node',entry:'dist/missing.js'}});
  await assert.rejects(release(source,output),/ENOENT/);await assert.rejects(fs.lstat(output),{code:'ENOENT'});
  assert.equal((await fs.readdir(path.dirname(output))).filter(name=>name.startsWith('.nnp-release-')).length,0);
  await assert.rejects(release(source,path.join(source,'release')),/outside/);
});
test('Python release vendors built modules without imports, bytecode or npm hooks',async t=> {
  const {source,output,write}=await fixture(t);
  await write('notepadnext-plugin.json',{manifestVersion:1,id:'tests.sample',version:'1.0.0',list:false,runtime:{kind:'python',entry:'dist/index.py'}});
  await write('package.json',{name:'sample',version:'1.0.0',scripts:{postinstall:'touch sentinel'}});
  await write('dist/index.py',"raise RuntimeError('pack must never import Python code')\n");
  await write('dist/helper.py','value = 42\n');
  const result=await release(source,output,'v1.0.0');assert.equal(result.assets.length,1);
  const checksums=JSON.parse(await fs.readFile(path.join(output,'notepadnext-plugin-checksums.json'),'utf8'));
  assert.equal(checksums.pluginId,'tests.sample');await assert.rejects(fs.lstat(path.join(source,'sentinel')),{code:'ENOENT'});
  await assert.rejects(fs.lstat(path.join(source,'dist/__pycache__')),{code:'ENOENT'});
});
test('Python packages reject npm runtime dependencies and remove failed staging',async t=> {
  const {source,output,write}=await fixture(t);
  await write('notepadnext-plugin.json',{manifestVersion:1,id:'tests.sample',version:'1.0.0',list:false,runtime:{kind:'python',entry:'dist/index.py'}});
  await write('dist/index.py','pass\n');
  await assert.rejects(pack(source,output),/Python packages/);await assert.rejects(fs.lstat(output),{code:'ENOENT'});
  assert.equal((await fs.readdir(path.dirname(output))).filter(name=>name.startsWith('.nnp-package-')).length,0);
});
