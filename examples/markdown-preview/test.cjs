// SPDX-License-Identifier: GPL-3.0-or-later
const test=require('node:test'),assert=require('node:assert/strict');
const {setTimeout:delay}=require('node:timers/promises');
test('Markdown renders the unsaved buffer and reuses its panel',async()=>{
  const plugin=require('./dist/index.js'); let command, listener, restorer, creations=0, watches=0, html='', payload='# unsaved 😀中\n\n- item\n\n```js\nconst a=1;\n```\n\n<script>bad()</script>\n\n![local](image.png)';
  const doc={documentId:'doc',version:1,languageId:'markdown',uri:'file:///tmp/example.md',isDirty:true,isReadonly:false};
  const events=new Set(), panel={panelId:'panel',reveal:async()=>{},setHtml:async(value,context)=>{assert.equal(context.version,doc.version);html=value;},mapResource:async()=> 'nnp-resource:image',postMessage:async()=>true,onEvent:callback=>{events.add(callback);return {dispose:()=>events.delete(callback)};},dispose:async()=>{for(const callback of events)callback({type:'closed',panelId:'panel'});events.clear();}};
  const context={commands:{register:async(id,callback)=>{command=callback;return {dispose(){}};}},documents:{getActive:async()=>doc,getSnapshot:async()=>({...doc,text:payload,positionEncoding:'utf-16'}),watch:async callback=>{listener=callback;++watches;return {document:doc,eventSeq:0,dispose:async()=>{--watches;}};}},webviews:{registerRestorer:(type,callback)=>{assert.equal(type,'markdown-preview');restorer=callback;return {dispose:()=>{restorer=undefined;}};},create:async()=>{++creations;return panel;}},subscriptions:[]};
  await plugin.activate(context);assert.equal(creations,0);assert.equal(typeof restorer,'function');await command();await restorer(); assert.equal(creations,1);assert.equal(watches,1);assert.match(html,/<h1>unsaved 😀中<\/h1>/);assert.match(html,/<li>item<\/li>/);assert.match(html,/language-js/);assert.match(html,/&lt;script&gt;/);assert.doesNotMatch(html,/<script>/);assert.match(html,/nnp-resource:image/);
  await command();assert.equal(creations,1);payload='# changed';doc.version++;listener({type:'contentInvalidated'});await delay(250);assert.match(html,/changed/);
  await panel.dispose();await delay(0);assert.equal(watches,0);await command();assert.equal(creations,2);await plugin.deactivate();for(const resource of context.subscriptions)await resource.dispose();assert.equal(restorer,undefined);assert.equal(watches,0);
});
test('A busy native HTML preparer retries the latest document',async()=>{
  const plugin=require('./dist/index.js');let command,listener,html='',busy=true,attempts=0;
  const doc={documentId:'doc',version:1,languageId:'markdown',uri:'file:///tmp/latest.md'};
  const panel={panelId:'panel',reveal:async()=>{},setHtml:async value=>{++attempts;if(busy){busy=false;throw Object.assign(new Error('Panel HTML preparation is busy'),{code:1007});}html=value;},mapResource:async()=>'',onEvent:()=>({dispose(){}}),dispose:async()=>{}};
  const context={commands:{register:async(_id,callback)=>{command=callback;return {dispose(){}};}},documents:{getActive:async()=>doc,getSnapshot:async()=>({...doc,text:`# latest-${doc.version}`}),watch:async callback=>{listener=callback;return {dispose:async()=>{}};}},webviews:{registerRestorer:()=>({dispose(){}}),create:async()=>panel},subscriptions:[]};
  await plugin.activate(context);await command();doc.version=2;listener({type:'contentInvalidated'});
  await delay(250);assert.match(html,/<h1>latest-2<\/h1>/);assert.equal(attempts,2);await plugin.deactivate();
});
