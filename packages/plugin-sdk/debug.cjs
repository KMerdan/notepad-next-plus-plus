#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),{pathToFileURL}=require('node:url');
const {createTestHost}=require('./testing.cjs');
const {portable}=require('./pack.cjs');
function parseArguments(args) {
  const directory=args.shift(),options={};
  if(!directory||directory.startsWith('--'))throw new Error('Usage: nnp-debug <project> --input <markdown-file> --output <html-file> [--command <id>] [--watch]');
  while(args.length) {
    const flag=args.shift();
    if(flag==='--watch'&&!options.watch){options.watch=true;continue;}
    const key={'--input':'input','--output':'output','--command':'command'}[flag];
    if(!key||Object.hasOwn(options,key)||!args.length||args[0].startsWith('--'))throw new Error('Invalid or repeated option: '+flag);
    options[key]=args.shift();
  }
  if(!options.input||!options.output)throw new Error('--input and --output are required.');
  return {directory,options};
}
async function runDebug(directory,{input,output,command,watch=false}) {
  directory=await fs.realpath(directory);input=path.resolve(input);output=path.resolve(output);
  if(output===input||output===path.join(directory,'notepadnext-plugin.json')||output===path.join(directory,'package.json'))throw new Error('Choose a separate HTML output file.');
  const manifest=JSON.parse(await fs.readFile(path.join(directory,'notepadnext-plugin.json'),'utf8'));
  if(manifest.list!==false||manifest.runtime?.kind!=='node'||!portable(manifest.runtime.entry)||!manifest.runtime.entry.startsWith('dist/'))throw new Error('Debug requires one built native Node plugin with a dist/ entry.');
  const entry=path.join(directory,manifest.runtime.entry),dist=path.join(directory,'dist')+path.sep;
  if(output.startsWith(dist))throw new Error('Debug HTML output must be outside dist/.');
  command??=manifest.contributes?.commands?.[0]?.id;
  if(!command)throw new Error('Declare a command or provide --command.');
  let host,render=Promise.resolve(),poller,timer,settle,stopping=false;
  function publish(panel) {
    const html='<!doctype html><html><head><meta charset="utf-8"><title>SDK local preview</title><style>body{font:15px system-ui;max-width:900px;margin:32px auto;padding:0 24px}pre{padding:16px;background:#f1f1f1;overflow:auto}code{font-family:monospace}.sdk-notice{color:#626267;border-bottom:1px solid #ddd;padding-bottom:12px}</style></head><body><p class="sdk-notice">NotepadNext SDK simulated host — verify the plugin in the editor before installation.</p>'+panel.html+'</body></html>';
    render=render.then(async()=>{await fs.mkdir(path.dirname(output),{recursive:true});await fs.writeFile(output,html);console.log('Rendered '+output);});
    return render;
  }
  async function start() {
    await host?.dispose();
    render=Promise.resolve();
    for(const file of Object.keys(require.cache))if(file.startsWith(dist))delete require.cache[file];
    host=createTestHost({pluginId:manifest.id,capabilities:manifest.capabilities?.required,text:await fs.readFile(input,'utf8'),document:{uri:pathToFileURL(input).href},onRender:publish});
    await host.activate(require(entry));await host.invoke(command);await render;
    for(const message of host.messages)console.log(message.severity+': '+message.message);
    if(!host.panels.size) {
      const escape=value=>value.replace(/[&<>"]/g,char=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[char]));
      await publish({html:'<h1>Command completed</h1>'+host.messages.map(message=>'<p>'+escape(message.message)+'</p>').join('')});
    }
  }
  async function stop() {
    stopping=true;clearTimeout(timer);clearInterval(poller);
    await host?.dispose();await render;settle?.();
  }
  console.log('SDK simulated host; Qt, JSON-RPC, CSP and host quotas require editor integration testing.');
  try {
    await start();
    if(!watch){await stop();return {output};}
    let queue=Promise.resolve(),rebuild=false;
    function schedule(codeChanged) {
      if(stopping)return;
      rebuild||=codeChanged;clearTimeout(timer);
      timer=setTimeout(()=>{
        if(stopping)return;
        const restart=rebuild;rebuild=false;
        queue=queue.then(async()=>{if(restart)await start();else{host.setDocument(await fs.readFile(input,'utf8'));await new Promise(done=>setTimeout(done,220));await render;}})
          .catch(error=>{console.error(error.stack||error.message);process.exitCode=1;});
      },150);
    }
    // Poll signatures rather than OS watches: works with atomic saves, large
    // author dependency trees and environments without native watch handles.
    async function signature(file) {
      const value=await fs.stat(file);return [value.mtimeMs,value.size,value.ino].join(':');
    }
    async function codeSignature(folder=path.join(directory,'dist')) {
      const values=[];
      for(const item of (await fs.readdir(folder,{withFileTypes:true})).sort((a,b)=>a.name.localeCompare(b.name))) {
        const file=path.join(folder,item.name);
        if(item.isDirectory())values.push(await codeSignature(file));
        else if(item.isFile()&&/\.(?:c?js|json)$/.test(item.name))values.push(file+':'+await signature(file));
      }
      return values.join('|');
    }
    let code=await codeSignature(),document=await signature(input),polling=false,lastError='';
    poller=setInterval(async()=>{
      if(polling||stopping)return;polling=true;
      try {
        const nextCode=await codeSignature(),nextDocument=await signature(input);
        if(nextCode!==code||nextDocument!==document){schedule(nextCode!==code);code=nextCode;document=nextDocument;}
        lastError='';
      }catch(error){if(lastError!==error.message){console.error(error.message);lastError=error.message;}}
      finally{polling=false;}
    },200);
    console.log('Watching dist/ and '+input+'. Ctrl+C stops the local session.');
    await new Promise(done=>{
      settle=done;
      const finish=()=>{stopping=true;clearTimeout(timer);clearInterval(poller);queue=queue.then(stop).catch(error=>{console.error(error.message);process.exitCode=1;done();});};
      process.once('SIGINT',finish);process.once('SIGTERM',finish);
    });
    return {output};
  }finally{await stop();}
}
module.exports={runDebug,parseArguments};
if(require.main===module) {
  if(process.argv.includes('--help'))console.log('Usage: nnp-debug <project> --input <markdown-file> --output <html-file> [--command <id>] [--watch]\nExecutes your built CommonJS plugin using the SDK simulated host. Run Node with --inspect-brk=127.0.0.1:9229 for breakpoints.');
  else {
    try {
      if(Number(process.versions.node.split('.')[0])!==24)throw new Error('Use Node 24 for this SDK.');
      const {directory,options}=parseArguments(process.argv.slice(2));
      runDebug(directory,options).catch(error=>{console.error(error.stack||error.message);process.exitCode=1;});
    }catch(error){console.error(error.message);process.exitCode=1;}
  }
}
