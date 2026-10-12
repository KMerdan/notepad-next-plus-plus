// NATIVE_ICONS is injected from Qt's exported SF Symbol assets.
const created=[],mutated=[];
const track=n=>{created.push(n.id);return n;};
const vars=await figma.variables.getLocalVariablesAsync();
const roles=Object.fromEntries(vars.filter(v=>v.variableCollectionId==='VariableCollectionId:2:19').map(v=>[v.name,v]));
const appearance=roles['appearance/mode'];
const styles=Object.fromEntries((await figma.getLocalTextStylesAsync()).map(s=>[s.name,s]));
await Promise.all(['Regular','Semibold','Medium','Bold'].map(style=>figma.loadFontAsync({family:'SF Pro',style})));
function bind(n,role){n.fills=[figma.variables.setBoundVariableForPaint({type:'SOLID',color:{r:1,g:1,b:1}},'color',roles['color/'+role])];mutated.push(n.id);}
async function text(p,value,style='Body',role='text'){const t=track(figma.createText());t.fontName={family:'SF Pro',style:'Regular'};await t.setTextStyleIdAsync(styles['Draft/UI/'+style].id);t.characters=value;t.textAutoResize='WIDTH_AND_HEIGHT';p.appendChild(t);bind(t,role);return t;}
function decode(str){const abc='ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';let bits=0,value=0,out=[];for(const c of str){const digit=abc.indexOf(c);if(digit<0)continue;value=(value<<6)|digit;bits+=6;if(bits>=8){bits-=8;out.push((value>>bits)&255);}}return new Uint8Array(out);}
const iconLibrary=track(figma.createAutoLayout('VERTICAL',{name:'macOS / SF Symbols from native renderer',paddingLeft:24,paddingRight:24,paddingTop:24,paddingBottom:24,itemSpacing:16}));iconLibrary.x=13700;iconLibrary.y=100;bind(iconLibrary,'chrome');
await text(iconLibrary,'Native symbol assets','Section');
const icons={};
for(const [name,modes]of Object.entries(NATIVE_ICONS)){
 const row=track(figma.createAutoLayout('HORIZONTAL',{itemSpacing:16}));row.fills=[];iconLibrary.appendChild(row);
 const components=[];
 for(const [mode,data]of Object.entries(modes)){const c=track(figma.createComponent());c.name='Mode='+mode;c.resize(20,20);const hash=figma.createImage(decode(data)).hash;c.fills=[{type:'IMAGE',imageHash:hash,scaleMode:'FIT'}];components.push(c);}
 const set=track(figma.combineAsVariants(components,figma.currentPage));set.name='macOS/Symbol/'+name;set.layoutMode='HORIZONTAL';set.itemSpacing=16;set.paddingLeft=8;set.paddingRight=8;set.paddingTop=8;set.paddingBottom=8;set.primaryAxisSizingMode='AUTO';set.counterAxisSizingMode='AUTO';row.appendChild(set);
 icons[name]={set:set.id,Light:components[0].id,Dark:components[1].id};await text(row,name,'Help','secondary');
}
const board=track(figma.createAutoLayout('VERTICAL',{name:'macOS UI revision · colors, layout, typography · 2026-10-12',itemSpacing:32,paddingLeft:32,paddingRight:32,paddingTop:32,paddingBottom:32}));board.x=11760;board.y=1300;bind(board,'chrome');
await text(board,'NotepadNext · macOS UI review','Heading');
await text(board,'Shared appearance tokens · native symbols · one primary action · compact desktop density','Help','secondary');
await text(board,'Editable screens below reflect Qt implementation. Earlier plugin lifecycle flows remain available on this canvas.','Help','secondary');
const sections={};
for(const [key,title,description]of [
 ['workspace','01 · Editor workspace','Keep frequent commands visible; group the rest in More. Markdown stays tied to its document.'],
 ['manager','02 · Plugin resources','Registration leads to Uninstalled. Install remains an explicit user action.'],
 ['general','03 · Settings / General','Title 20 · section 16 · body 13 · help 12. Align fields to the content edge.'],
 ['categories','04 · Settings / Editor and Files','Group related options with 8 px gaps and separate groups by 16 px.'],
 ['small','05 · Settings / Search and minimum width','Category navigation stays visible; scroll the content while keeping Done reachable.'],
 ['find','06 · Find / light and dark','Fields, focus, matching options and search results have distinct visual roles.'],
 ['modes','07 · Replace and Mark','The main action follows the mode; secondary actions stay neutral.']
 ]){
 const section=track(figma.createAutoLayout('VERTICAL',{name:key,itemSpacing:12}));section.fills=[];board.appendChild(section);
 await text(section,title,'Section');await text(section,description,'Help','secondary');
 const row=track(figma.createAutoLayout('HORIZONTAL',{name:key+' screens',itemSpacing:48}));row.fills=[];section.appendChild(row);sections[key]=row.id;
}
return {createdNodeIds:created,mutatedNodeIds:mutated,board:board.id,iconLibrary:iconLibrary.id,icons,sections,appearance:appearance.id};
