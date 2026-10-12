// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const {Worker,isMainThread,parentPort,workerData}=require('node:worker_threads');
if(!isMainThread) {
  const fs=require('node:fs'),{Socket}=require('node:net'),pid=process.pid;
  // Never use worker process.exit(): that only exits this thread. The native
  // signal kills the process (or Qt-created process group) independently of a
  // spinning main JS event loop and does not depend on parent PID identity.
  const stop=()=>{try{process.kill(workerData.group?-pid:pid,'SIGKILL');}catch{process.kill(pid,'SIGKILL');}};
  try {
    const stat=fs.fstatSync(workerData.fd);
    if(!stat.isFIFO()&&!stat.isSocket())throw new Error('Parent lifetime descriptor must be a pipe');
    // libuv supports both inherited FIFO and socket-pair pipe descriptors.
    // Keep the worker event loop asynchronous: a blocking fs.readSync cannot
    // be interrupted during Node's normal worker teardown at process.exit.
    const pipe=new Socket({fd:workerData.fd,readable:true,writable:false});
    pipe.on('end',stop);pipe.on('error',stop);pipe.on('close',stop);pipe.resume();
    parentPort.postMessage('ready');
  } catch(error) {parentPort.postMessage({error:error.message});stop();}
} else {
  module.exports.start=()=>{
    const descriptor=process.env.NNP_PARENT_WATCH_FD,group=process.env.NNP_PARENT_WATCH_GROUP==='1';
    delete process.env.NNP_PARENT_WATCH_FD;delete process.env.NNP_PARENT_WATCH_GROUP;
    if(descriptor===undefined)return {ready:Promise.resolve()};
    if(process.platform==='win32'||descriptor!=='3')throw new Error('Invalid parent lifetime descriptor');
    const worker=new Worker(__filename,{workerData:{fd:3,group},execArgv:[],stdin:false,stdout:true,stderr:true});
    // No forwarding onto protocol stdout. The watcher emits no plugin logs.
    worker.stdout.resume();worker.stderr.resume();worker.unref();
    const ready=new Promise((resolve,reject)=>{
      worker.once('message',message=>message==='ready'?resolve():reject(new Error(message?.error??'Parent watcher failed')));
      worker.once('error',reject);worker.once('exit',code=>reject(new Error(`Parent watcher exited (${code})`)));
    });
    const stop=()=>{try{process.kill(group?-process.pid:process.pid,'SIGKILL');}catch{process.kill(process.pid,'SIGKILL');}};
    worker.on('error',stop);
    worker.on('exit',stop);
    return {ready};
  };
}
