const screens=[],navigation=[];
async function settings(rowId,page,dark=false,small=false){
 const card=await captionCard(rowId,page+' · '+(dark?'Dark':'Light')+(small?' · 720 × 500':''));
 const w=small?720:840,h=small?500:590;
 const screen=frame(card,'Settings / '+page+' / '+(dark?'Dark':'Light'),'VERTICAL',w,h,'chrome',16,24);mode(screen,dark);radius(screen,8);
 const header=frame(screen,'Settings header','VERTICAL',w-48,null,null,4);await text(header,'Settings','Heading');await text(header,'Changes are saved automatically.','Help','secondary');
 const body=frame(screen,'Categories and content','HORIZONTAL',w-48,null,null,16);body.layoutSizingVertical='FILL';
 const sidebar=frame(body,'Settings categories','VERTICAL',176,null,null,8);const targets=[];
 for(const [name,symbol]of [['General','gearshape'],['Editor','textformat'],['Files','doc'],['Search','magnifyingglass']]){
  const n=await instance(sidebar,name===page?'39:11965':'39:11964',{'Label#39:16':name,'Icon#43:0':REVISION.icons[symbol].Light},176);targets.push({name,id:n.id});
 }
 const content=frame(body,'Settings content','VERTICAL',w-48-176-16,null,null,16);content.layoutSizingVertical='FILL';content.clipsContent=true;content.overflowDirection='VERTICAL';
 const cw=content.width;
 await text(content,page,'Section');
 if(page==='General'){
  const row=frame(content,'Language','HORIZONTAL',cw,null,null,16);row.counterAxisAlignItems='CENTER';await text(row,'Language');await field(row,'<System Default>',cw-92,true);
  const a=await group(content,'Window appearance',cw);for(const label of ['Show menu bar','Show toolbar','Show status bar'])await check(a,label,true,cw-24);
  const b=await group(content,'Windows',cw);await check(b,'Exit on last tab closed',false,cw-24);
 }else if(page==='Editor'){
  const a=await group(content,'Default font',cw);const row=frame(a,'Font controls','HORIZONTAL',cw-24,null,null,12);row.counterAxisAlignItems='CENTER';await text(row,'Font');await field(row,'Menlo',cw-162,true);await field(row,'10',62,true);
  const b=await group(content,'Editing',cw);for(const label of ['Show line numbers','Highlight URLs','Enable auto-completion'])await check(b,label,true,cw-24);
 }else if(page==='Files'){
  const row=frame(content,'Line endings','HORIZONTAL',cw,null,null,16);row.counterAxisAlignItems='CENTER';await text(row,'New document line endings');await field(row,'System Default',cw-180,true);
  const a=await group(content,'Default directory',cw);for(const [label,on]of [['Follow current document',true],['Remember last used directory',false],['Custom folder',false]])await radio(a,label,on,cw-24);
  const folder=frame(a,'Custom folder input','HORIZONTAL',cw-24,null,null,8);await field(folder,'Choose a folder',cw-64);await icon(folder,'folder');
  const b=await group(content,'Restore previous session',cw);await check(b,'Restore unsaved files',false,cw-24);await check(b,'Restore temporary files',false,cw-24);await text(b,'Experimental feature. Important documents should be saved.','Help','secondary',cw-24);
 }else{
  const a=await group(content,'Search behavior',cw);await check(a,'Recenter the search dialog',true,cw-24);await check(a,'Combine search results',false,cw-24);
 }
 const footer=frame(screen,'Stable actions','HORIZONTAL',w-48,34,null,8);const space=frame(footer,'Flexible space','HORIZONTAL',1,1);space.layoutSizingHorizontal='FILL';await button(footer,'Done',true,90);
 screens.push({id:screen.id,name:screen.name,w:screen.width,h:screen.height,dark});navigation.push({page,dark,small,screen:screen.id,targets});return screen;
}
await settings(REVISION.sections.general,'General');await settings(REVISION.sections.general,'General',true);
await settings(REVISION.sections.categories,'Editor');await settings(REVISION.sections.categories,'Files');
await settings(REVISION.sections.small,'Search');await settings(REVISION.sections.small,'Files',true,true);
const prototypes=[];
for(let i=0;i<navigation.length;++i){const source=navigation[i],original=await figma.getNodeByIdAsync(source.screen);const clone=original.clone();figma.currentPage.appendChild(clone);clone.x=14400+(i%2)*900;clone.y=1300+Math.floor(i/2)*660;clone.name='Prototype / '+original.name;created.push(clone.id,...clone.findAll(()=>true).map(n=>n.id));prototypes.push({...source,screen:clone.id,targets:clone.findAll(n=>n.type==='INSTANCE'&&n.componentProperties['Label#39:16']).map(n=>({id:n.id,name:n.findAll(n=>n.type==='TEXT')[0].characters}))});}
for(const source of [...navigation,...prototypes]){for(const target of source.targets){const destination=prototypes.find(n=>n.page===target.name&&n.dark===source.dark&&!n.small)||prototypes.find(n=>n.page===target.name&&!n.dark&&!n.small);if(destination&&destination.screen!==source.screen){const node=await figma.getNodeByIdAsync(target.id);await node.setReactionsAsync([{trigger:{type:'ON_CLICK'},actions:[{type:'NODE',destinationId:destination.screen,navigation:'NAVIGATE',transition:null,resetScrollPosition:false}]}]);mutated.push(node.id);}}}
return ledger(screens,{navigation,prototypes});
