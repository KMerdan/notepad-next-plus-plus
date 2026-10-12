#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
// Exercise the published file list, not the source tree. All outputs are
// created under a new directory, and no registry publication is performed.
const fs=require('node:fs/promises'),nativeFs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),crypto=require('node:crypto'),{execFileSync}=require('node:child_process');
async function main() {
  if(Number(process.versions.node.split('.')[0])!==24)throw new Error('Use Node 24.');
  const root=path.resolve(process.argv[2]||'build/plugin-sdk-validation');
  const repository=path.resolve(__dirname,'../..'),sdk=path.join(repository,'packages/plugin-sdk');
  const npm=process.env.npm_execpath||[
    path.resolve(path.dirname(process.execPath),'../lib/node_modules/npm/bin/npm-cli.js'),
    path.join(path.dirname(process.execPath),'node_modules/npm/bin/npm-cli.js')
  ].find(file=>nativeFs.existsSync(file));
  if(!npm)throw new Error('Run from npm or configure a Node distribution that includes npm.');
  await fs.mkdir(path.dirname(root),{recursive:true});await fs.mkdir(root);
  const cache=process.env.NNP_SDK_NPM_CACHE||path.join(root,'npm-cache');
  const npmOptions=['--ignore-scripts','--cache',cache];
  if(process.env.NNP_SDK_OFFLINE==='1')npmOptions.push('--offline');
  const env={...process.env,PATH:path.dirname(process.execPath)+path.delimiter+process.env.PATH,npm_config_cache:cache,npm_config_audit:'false',npm_config_fund:'false'};
  const steps=[];
  async function run(label,args,cwd,options={}) {
    console.log(label);
    try {
      const output=execFileSync(process.execPath,args,{cwd,env,timeout:60000,maxBuffer:8*1024*1024,...options}).toString();
      await fs.writeFile(path.join(root,label+'.log'),output);steps.push({name:label,result:'passed'});return output;
    }catch(error){await fs.writeFile(path.join(root,label+'.log'),String(error.stdout||'')+'\n'+String(error.stderr||''));throw error;}
  }
  const sdkOutput=path.join(root,'sdk');await fs.mkdir(sdkOutput);
  const packed=JSON.parse(await run('01-sdk-pack',[npm,'pack','--json','--ignore-scripts','--pack-destination',sdkOutput],sdk))[0];
  const files=new Set(packed.files.map(item=>item.path));
  for(const file of ['create.cjs','debug.cjs','testing.cjs','testing.d.ts','docs/MarkdownWalkthrough.zh-CN.md','schemas/manifest.schema.json','templates/markdown/src/index.ts','templates/hello/src/index.ts','templates/markdown/.vscode/launch.json'])assert.ok(files.has(file),'Missing SDK asset: '+file);
  assert.ok(!files.has('developer.test.cjs'));assert.ok(![...files].some(file=>file.includes('node_modules/')||file.startsWith('examples/')));
  const tooling=path.join(root,'tooling');await fs.mkdir(tooling);
  await fs.writeFile(path.join(tooling,'package.json'),JSON.stringify({private:true,dependencies:{'@notepadnext/plugin-sdk':'file:../sdk/'+packed.filename}},null,2));
  await run('02-install-sdk-tarball',[npm,'install',...npmOptions],tooling);
  const create=path.join(tooling,'node_modules/@notepadnext/plugin-sdk/create.cjs');
  const stage=path.join(root,'generated-markdown'),markdown=path.join(root,'markdown-demo'),hello=path.join(root,'hello-demo');
  await run('03-create-markdown',[create,stage,'--template','markdown','--id','demo.markdown-preview','--name','Markdown Preview Demo'],tooling);
  await fs.rename(stage,markdown);
  await run('04-markdown-install',[npm,'install',...npmOptions],markdown);
  await run('05-markdown-locked-install',[npm,'ci',...npmOptions],markdown);
  const typecheck=path.join(markdown,'sdk-typecheck.ts');
  await fs.writeFile(typecheck,"import {createTestHost} from '@notepadnext/plugin-sdk/testing';\nimport type {PluginContext} from '@notepadnext/plugin-sdk';\nconst host=createTestHost({text:'# typed',onRender(panel){const html:string=panel.html;void html;}});\nconst context:PluginContext=host.context;void context;\n// @ts-expect-error text must be a string\ncreateTestHost({text:42});\n");
  await run('05a-sdk-typecheck',[path.join(markdown,'node_modules/typescript/bin/tsc'),'--noEmit','--strict','--target','ES2022','--module','CommonJS','--moduleResolution','Node',typecheck],markdown);
  await fs.unlink(typecheck);
  await run('06-markdown-build',[npm,'run','build'],markdown);
  await run('07-markdown-test',[npm,'test'],markdown);
  await run('08-markdown-debug',[npm,'run','debug'],markdown);
  const preview=await fs.readFile(path.join(markdown,'.debug/preview.html'),'utf8');assert.match(preview,/<h1>SDK Markdown 演示 😀<\/h1>/);assert.match(preview,/language-ts/);assert.doesNotMatch(preview,/<script>/);
  const input=await fs.readFile(path.join(markdown,'demo.md'),'utf8');await fs.writeFile(path.join(markdown,'demo.md'),input.replace('SDK Markdown 演示 😀','本地修改 😀中'));
  await run('09-markdown-edited-debug',[npm,'run','debug'],markdown);
  assert.match(await fs.readFile(path.join(markdown,'.debug/preview.html'),'utf8'),/<h1>本地修改 😀中<\/h1>/);
  await fs.writeFile(path.join(markdown,'demo.md'),input);
  const artifacts=path.join(root,'artifacts');
  await run('10-local-package',[npm,'run','package','--',path.join(artifacts,'markdown-preview-0.1.0')],markdown);
  await run('11-plugin-bundle',[npm,'run','bundle','--',path.join(artifacts,'markdown-preview-0.1.0-zip'),'v0.1.0'],markdown);
  const installedInfo=JSON.parse(await fs.readFile(path.join(artifacts,'markdown-preview-0.1.0/package.json'),'utf8'));
  assert.equal(installedInfo.devDependencies,undefined);assert.equal(installedInfo.scripts,undefined);
  for(const excluded of ['tools','src','test.cjs','node_modules/@notepadnext','node_modules/typescript'])await assert.rejects(fs.stat(path.join(artifacts,'markdown-preview-0.1.0',excluded)),{code:'ENOENT'});
  const checksum=JSON.parse(await fs.readFile(path.join(artifacts,'markdown-preview-0.1.0-zip/notepadnext-plugin-checksums.json'),'utf8'));
  const zip=await fs.readFile(path.join(artifacts,'markdown-preview-0.1.0-zip/notepadnext-plugin.zip'));
  assert.equal(checksum.assets[0].size,zip.length);assert.equal(checksum.assets[0].sha256,crypto.createHash('sha256').update(zip).digest('hex'));
  await run('12-create-hello',[create,hello,'--template','hello','--id','demo.hello'],tooling);
  await run('13-hello-install',[npm,'install',...npmOptions],hello);
  await run('14-hello-build',[npm,'run','build'],hello);
  await run('15-hello-test',[npm,'test'],hello);
  await run('16-hello-debug',[npm,'run','debug'],hello);
  assert.match(await fs.readFile(path.join(hello,'.debug/preview.html'),'utf8'),/Hello from NotepadNext!/);
  const record={sdk:{version:packed.version,path:path.join(sdkOutput,packed.filename),sha256:crypto.createHash('sha256').update(await fs.readFile(path.join(sdkOutput,packed.filename))).digest('hex'),files:files.size},markdownProject:markdown,helloProject:hello,localPackage:path.join(artifacts,'markdown-preview-0.1.0'),zip:checksum.assets[0],steps,notes:['SDK installed from its actual npm tarball; template moved before dependency installation.','Locked install, build, tests, local debug and packaging use only the standalone project.','Editor integration is a separate native check; no publication or signing performed.']};
  await fs.writeFile(path.join(root,'validation.json'),JSON.stringify(record,null,2)+'\n');
  console.log(JSON.stringify(record,null,2));
}
main().catch(error=>{console.error(error.stack||error.message);process.exitCode=1;});
