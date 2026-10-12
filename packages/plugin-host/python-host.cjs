// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
// Controlled byte relay. Python implements the same RPC itself; this process
// owns supervision independently of Python's interpreter/GIL/event loop.
const fs=require('node:fs'),path=require('node:path'),{spawn}=require('node:child_process');
const watch=require('./parent-watch.cjs').start();
if(Number(process.versions.node.split('.')[0])!==24)throw new Error('The supervisor requires Node 24');
const directory=fs.realpathSync(process.argv[2]??''),python=process.argv[3];
if(!python||!path.isAbsolute(python)||!fs.statSync(python).isFile())throw new Error('An explicit Python interpreter is required');
const manifest=JSON.parse(fs.readFileSync(path.join(directory,'notepadnext-plugin.json'),'utf8'));
if(manifest.list!==false||manifest.runtime?.kind!=='python')throw new Error('Expected a validated Python package');
let child;
function fail(error){process.stderr.write(String(error?.message??error).slice(0,16384)+'\n');child?.kill('SIGKILL');process.exitCode=2;process.stdin.destroy();}
async function start() {
  await watch.ready;
  child=spawn(python,['-I','-B','-u',path.join(__dirname,'python','host.py'),directory],{cwd:directory,stdio:['pipe','pipe','pipe'],shell:false});
  child.on('error',fail);
  child.on('close',(code,signal)=>{process.stdin.destroy();process.stdout.end(()=>process.exit(signal?2:(code??2)));});
  // stream.pipe applies bounded OS/stream backpressure in both directions.
  process.stdin.pipe(child.stdin);child.stdout.pipe(process.stdout,{end:false});child.stderr.pipe(process.stderr,{end:false});
  for(const stream of [process.stdin,process.stdout,process.stderr,child.stdin,child.stdout,child.stderr])stream.on('error',fail);
}
void start().catch(fail);
