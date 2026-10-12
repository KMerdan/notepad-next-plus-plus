// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const {randomUUID}=require('node:crypto'),{isDeepStrictEqual}=require('node:util');
function createTestHost({pluginId='tests.plugin',capabilities=[],text='',document={},onRender=()=>{}}={}) {
  let active=false,closed=false,plugin,sequence=0,buffer=text;
  let info={documentId:'test-document',uri:'file:///test/example.md',languageId:'markdown',version:1,isDirty:true,isReadonly:false,...document};
  const commands=new Map(),watches=new Set(),resources=new Set(),panels=new Map(),messages=[],restorers=new Map();
  const snapshot=()=>({...info,text:buffer,positionEncoding:'utf-16'});
  const assertOpen=()=>{if(closed)throw new Error('Test host is disposed.');};
  function own(dispose) {
    let disposed=false;
    const handle={async dispose(){if(disposed)return;disposed=true;resources.delete(handle);await dispose();}};
    resources.add(handle);return handle;
  }
  const unsupported=feature=>{throw new Error(feature+' is not implemented by the SDK test host. Verify it in NotepadNext.');};
  function store() {
    const values=new Map();
    return {async get(key){return values.has(key)?structuredClone(values.get(key)):null;},
      async update(key,value){assertOpen();if(!active)throw new Error('Write from a command after activation.');if(value===undefined)values.delete(key);else values.set(key,structuredClone(value));},
      async compareExchange(key,expected,value){const actual=values.has(key)?values.get(key):null;if(!isDeepStrictEqual(actual,expected))throw new Error('Configuration/storage conflict.');await this.update(key,value);}
    };
  }
  const context={
    pluginId,sessionId:randomUUID(),generation:1,subscriptions:[],capabilities:Object.freeze([...capabilities]),
    commands:{async register(id,callback){assertOpen();if(commands.has(id))throw new Error('Command already registered: '+id);commands.set(id,callback);return own(()=>commands.delete(id));}},
    window:{async showMessage(severity,message,items=[]){assertOpen();if(!active)throw new Error('Show messages from a command after activation.');messages.push({severity,message,items:[...items]});return items[0];}},
    documents:{
      async getActive(){assertOpen();return {...info};},
      async getSnapshot(id){assertOpen();if(id!==info.documentId)throw new Error('Unknown document.');return snapshot();},
      async watch(listener){assertOpen();watches.add(listener);return Object.assign(own(()=>watches.delete(listener)),{document:{...info},eventSeq:sequence});},
      async watchAll(){return unsupported('documents.watchAll');},
      async applyEdits(){return unsupported('documents.applyEdits');}
    },
    webviews:{
      registerRestorer(type,callback){restorers.set(type,callback);return {dispose:()=>restorers.delete(type)};},
      owner:'test-host',
      async create(viewType,title) {
        assertOpen();if(!active)throw new Error('Open panels from a command after activation.');
        const panelId=randomUUID(),listeners=new Set(),panel=Object.assign(own(()=>{
          panels.delete(panelId);for(const listener of [...listeners])listener({panelId,type:'closed'});listeners.clear();
        }),{
          panelId,viewType,title,html:'',visible:false,renderContext:null,messages:[],
          async reveal(){assertOpen();if(!panels.has(panelId))throw new Error('Panel is closed.');panel.visible=true;},
          async setHtml(html,renderContext){
            assertOpen();if(!panels.has(panelId))throw new Error('Panel is closed.');
            if(renderContext.documentId!==null&&(renderContext.documentId!==info.documentId||renderContext.version!==info.version))throw Object.assign(new Error('Stale document render.'),{code:1002});
            panel.html=html;panel.renderContext={...renderContext};onRender(panel);
          },
          async postMessage(message){panel.messages.push(structuredClone(message));return true;},
          async mapResource(){return unsupported('webviews.mapResource');},
          onEvent(listener){listeners.add(listener);return {dispose(){listeners.delete(listener);}};}
        });
        panels.set(panelId,panel);return panel;
      },
      async createCompatible(){return unsupported('webviews.createCompatible');}
    },storage:store(),configuration:store()
  };
  const host={context,panels,messages,getSnapshot:snapshot,
    async activate(module){assertOpen();if(plugin)throw new Error('Test host has already activated a plugin.');plugin=module;await plugin.activate(context);active=true;},
    async invoke(id,...args){assertOpen();if(!active)throw new Error('Plugin is not active.');if(!commands.has(id))throw new Error('Command is not registered: '+id);return commands.get(id)(...args);},
    setDocument(value,metadata={}) {
      assertOpen();buffer=value;info={...info,...metadata,version:info.version+1,isDirty:true};
      const event={subscriptionId:'test-watch',eventSeq:++sequence,type:'contentInvalidated',document:{...info}};
      for(const listener of [...watches])listener(event);
    },
    async dispose() {
      if(closed)return;
      let failure;
      try{await plugin?.deactivate?.();}catch(error){failure=error;}
      active=false;
      for(const item of [...new Set([...context.subscriptions.splice(0),...resources])].reverse())try{await item.dispose();}catch(error){failure??=error;}
      commands.clear();watches.clear();closed=true;
      if(failure)throw failure;
    }
  };
  return host;
}
module.exports={createTestHost};
