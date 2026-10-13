// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const http=require('node:http'),{randomBytes}=require('node:crypto');
/** Start an activation-owned HTTP service. Never log the proof or expose it to page scripts. */
async function startLocalService(context,handler) {
  if(!context?.sessionId||typeof handler!=='function')throw new TypeError('Expected PluginContext and an HTTP handler');
  if(!context.capabilities.includes('webviews.service@1'))throw new Error('Declare webviews.service@1');
  const nonce=randomBytes(16).toString('hex'),prefix='/nnp/'+randomBytes(16).toString('hex')+'/';
  const server=http.createServer(async(request,response)=>{
    response.setHeader('Cache-Control','no-store');response.setHeader('X-Content-Type-Options','nosniff');
    response.setHeader('Content-Security-Policy',"default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; connect-src 'self'; frame-src 'none'; object-src 'none'; base-uri 'self'; form-action 'none'");
    const pathname=(request.url??'').split('?')[0];
    if(!pathname.startsWith(prefix)||pathname.includes('..')||/%2e|%2f|%5c/i.test(pathname)){response.writeHead(404);response.end();return;}
    if(pathname===prefix+'__nnp_probe') {
      if(request.method!=='GET'){response.writeHead(405);response.end();return;}
      response.setHeader('Content-Type','application/json');response.end(JSON.stringify({sessionId:context.sessionId,nonce}));return;
    }
    try{await handler(request,response,pathname.slice(prefix.length));}
    catch{if(!response.headersSent)response.writeHead(500);response.end('Service request failed');}
  });
  server.requestTimeout=5000;server.headersTimeout=5000;server.maxHeadersCount=64;
  await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(0,'127.0.0.1',()=>{server.off('error',reject);resolve();});});
  let disposed=false;
  const endpoint='http://127.0.0.1:'+server.address().port+prefix+'index.html';
  const service=Object.freeze({endpoint,nonce,baseURL:endpoint.slice(0,-10),async dispose(){
    if(disposed)return;disposed=true;await new Promise(resolve=>{server.close(resolve);server.closeAllConnections();});
  }});
  context.subscriptions.push(service);return service;
}
module.exports={startLocalService};
