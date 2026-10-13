// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const fs=require('node:fs'),path=require('node:path');
const {startLocalService}=require('./local-service.cjs');
exports.activate=async context=>{
  let service,panel,document={text:'<h1>Hello HTML</h1><button onclick="this.textContent=\'JavaScript works\'">Run JavaScript</button>',version:0};
  const refresh=async()=>{const active=await context.documents.getActive();if(active?.languageId==='html')document=await context.documents.getSnapshot(active.documentId);};
  await refresh();context.subscriptions.push(await context.documents.watch(refresh));
  const open=async()=>{
    if(panel){await panel.reveal();return;}
    if(!service)service=await startLocalService(context,async(request,response,file)=>{
      if(request.method!=='GET'){response.writeHead(405);response.end();return;}
      if(file==='api/document'){response.setHeader('Content-Type','application/json');response.end(JSON.stringify({text:document.text,version:document.version,pid:process.pid}));return;}
      const types={'index.html':'text/html; charset=utf-8','app.js':'text/javascript','style.css':'text/css'};
      if(!types[file]){response.writeHead(404);response.end();return;}
      response.setHeader('Content-Type',types[file]);response.end(fs.readFileSync(path.join(__dirname,'public',file)));
    });
    panel=await context.webviews.createService({viewType:'html-preview',title:'HTML Preview',endpoint:service.endpoint,nonce:service.nonce});
    const current=panel;context.subscriptions.push(current);current.onEvent(event=>{if(event.type==='closed'&&panel===current)panel=undefined;});await current.reveal(true);
  };
  context.subscriptions.push(context.webviews.registerRestorer('html-preview',open));
  context.subscriptions.push(await context.commands.register('__PLUGIN_ID__.open',open));
};
