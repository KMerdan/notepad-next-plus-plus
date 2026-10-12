// SPDX-License-Identifier: GPL-3.0-or-later
import MarkdownIt, {type Token} from 'markdown-it';
import type {PluginContext, WebviewPanel, Watch, RenderContext} from '@notepadnext/plugin-sdk';

let context: PluginContext;
let panel: WebviewPanel | undefined;
let watch: Watch | undefined;
let timer: ReturnType<typeof setTimeout> | undefined;
let maximumWait: ReturnType<typeof setTimeout> | undefined;
let controller: AbortController | undefined;
let revision = 0;
let stopped = false;
const escape = (text:string) => text.replace(/[&<>"']/g,char=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&apos;'}[char]!));
const md = new MarkdownIt({html:false,xhtmlOut:true,linkify:true,typographer:false});
md.renderer.rules.image=(tokens,index,_options,env)=>{
  const token=tokens[index], source=token.attrGet('src') ?? '', mapped=(env as {images:Map<string,string>}).images.get(source);
  return mapped ? `<img src="${escape(mapped)}" alt="${escape(token.content)}" />` : `<span class="image-error">Image unavailable: ${escape(token.content || source)}</span>`;
};
for(const name of ['th_open','td_open']) md.renderer.rules[name]=(tokens,index,options,_env,renderer)=>{
  const token=tokens[index], style=token.attrGet('style');
  token.attrs=token.attrs?.filter(([name])=>name!=='style') ?? null;
  const align=/^text-align:(left|right|center);?$/.exec(style ?? ''); if(align) token.attrSet('align',align[1]);
  return renderer.renderToken(tokens,index,options);
};
function collectImages(tokens:Token[],result:Set<string>) {
  for(const token of tokens) { if(token.type==='image') result.add(token.attrGet('src') ?? ''); if(token.children) collectImages(token.children,result); }
}
function clearTimers() { if(timer) clearTimeout(timer); if(maximumWait) clearTimeout(maximumWait); timer=maximumWait=undefined; }
function invalidate() {
  if(stopped || !panel) return;
  ++revision; controller?.abort(); if(timer) clearTimeout(timer);
  timer=setTimeout(()=>{clearTimers();void render();},150);
  maximumWait ??= setTimeout(()=>{clearTimers();void render();},800);
}
async function render() {
  const currentPanel=panel, currentRevision=revision;
  if(stopped || !currentPanel) return;
  controller?.abort(); const task=new AbortController(); controller=task;
  let renderContext:RenderContext={documentId:null,version:null};
  try {
    const document=await context.documents.getActive({signal:task.signal});
    if(document) renderContext={documentId:document.documentId,version:document.version};
    let html='<p>Open a Markdown document to preview it.</p>';
    if(document && (document.languageId==='markdown' || /\.md(?:own|arkdown)?$/i.test(new URL(document.uri).pathname))) {
      const snapshot=await context.documents.getSnapshot(document.documentId,{signal:task.signal}); renderContext.version=snapshot.version;
      const tokens=md.parse(snapshot.text,{}), sources=new Set<string>(), images=new Map<string,string>(); collectImages(tokens,sources);
      for(const source of sources) {
        if(task.signal.aborted) return;
        if(/^[a-z][a-z0-9+.-]*:/i.test(source) || source.startsWith('/')) continue;
        try { images.set(source,await currentPanel.mapResource({kind:'documentImage',documentId:document.documentId,path:decodeURIComponent(source)})); }
        catch { /* Missing, remote, untitled and escaping images remain labelled text. */ }
      }
      html=md.renderer.render(tokens,md.options,{images});
    }
    if(task.signal.aborted || currentRevision!==revision || panel!==currentPanel || stopped) return;
    await currentPanel.setHtml(html,renderContext);
  } catch(error) {
    if(task.signal.aborted || currentRevision!==revision || panel!==currentPanel || stopped) return;
    const code=(error as {code?:number}).code;
    if(code===1001 || code===1002 || code===1007) { invalidate(); return; }
    const message=code===1009 ? 'This buffer contains text that cannot be decoded as UTF-8.' : code===1008 ? 'This document exceeds the preview size limit.' : 'Markdown preview could not render this document.';
    try { await currentPanel.setHtml(`<p class="preview-error">${escape(message)}</p>`,renderContext); } catch { /* A closed or newer document owns the next refresh. */ }
  }
}
async function open() {
  if(panel) { await panel.reveal(); return; }
  const created=await context.webviews.create('markdown-preview','Markdown Preview'); panel=created;
  created.onEvent(event=>{
    if(event.type==='closed' && panel===created) {
      panel=undefined; ++revision; controller?.abort(); clearTimers();
      const oldWatch=watch; watch=undefined; void Promise.resolve().then(()=>oldWatch?.dispose()).catch(error=>console.warn(error));
    }
  });
  watch=await context.documents.watch(()=>invalidate());
  await created.reveal(); ++revision; await render();
}
export async function activate(api:PluginContext) {
  context=api; stopped=false;
  console.info(api.pluginId + ": activated");
  await context.commands.register('__PLUGIN_ID__.open',open);
}
export async function deactivate() {
  stopped=true; ++revision; controller?.abort(); clearTimers();
  await watch?.dispose(); watch=undefined; await panel?.dispose(); panel=undefined;
}
