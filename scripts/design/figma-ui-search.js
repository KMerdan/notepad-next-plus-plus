const screens=[],tabs=[];
async function search(rowId,selected,dark=false){
 const card=await captionCard(rowId,selected+' · '+(dark?'Dark':'Light'));
 const screen=frame(card,selected+' / '+(dark?'Dark':'Light'),'VERTICAL',740,540,'chrome',16,24);mode(screen,dark);radius(screen,8);
 const modes=frame(screen,'Search modes','HORIZONTAL',692,32,null,0);const targets=[];
 for(const name of ['Find','Replace','Mark']){const item=frame(modes,name,'HORIZONTAL',692/3,32,name===selected?'selected':'chrome',0,0);item.primaryAxisAlignItems='CENTER';item.counterAxisAlignItems='CENTER';fill(item,name===selected?'selected':'chrome',name===selected?'accent':'border');await text(item,name,'Body',name===selected?'accent':'secondary');targets.push({name,id:item.id});}
 const viewport=frame(screen,'Scroll search options','VERTICAL',692,null,null,16);viewport.layoutSizingVertical='FILL';viewport.clipsContent=true;viewport.overflowDirection='VERTICAL';
 const row=frame(viewport,'Find field','HORIZONTAL',692,34,null,12);row.counterAxisAlignItems='CENTER';await text(row,'Find:');await field(row,'apple',692-50-100-24,true,selected==='Find');await button(row,selected==='Mark'?'Mark All':'Find Next',selected==='Find'||selected==='Mark',100);
 if(selected==='Replace'){const row=frame(viewport,'Replace field','HORIZONTAL',692,34,null,12);row.counterAxisAlignItems='CENTER';await text(row,'Replace:');await field(row,'pear',692-70-100-24,true);await button(row,'Replace',true,100);}
 const options=frame(viewport,'Matching and mode','HORIZONTAL',692,null,null,16);
 const matching=await group(options,'Matching',338);
 for(const [label,on]of [['Match case',true],['Match whole word only',false],['Wrap around',true],['Backward direction',false]])await check(matching,label,on,314);
 if(selected==='Mark'){await check(matching,'Bookmark line',false,314);await check(matching,'Purge for each search',true,314);}
 const searchMode=await group(options,'Search Mode',338);
 await radio(searchMode,'Normal',true,314);await radio(searchMode,'Extended (\\n, \\r, \\t, \\0, \\x…)',false,314);await radio(searchMode,'Regular expression',false,314);await check(searchMode,'. matches newline',false,314,true);
 const advanced=frame(viewport,'More Options','HORIZONTAL',126,34,'surface',4,6);radius(advanced,6);fill(advanced,'surface','border');advanced.counterAxisAlignItems='CENTER';await icon(advanced,'chevron_right',12);await text(advanced,'More Options');
 await instance(screen,'39:11970',{'Message#39:19':selected==='Mark'?'Marked 2 matches':'Found 2 matches'},692);
 const footer=frame(screen,'Stable search actions','HORIZONTAL',692,34,null,8);
 for(const label of selected==='Replace'?['Replace All','Replace All in Open Documents']:selected==='Mark'?['Clear Marks','Copy Marked Text']:['Count','Find All in This Document','Find All in Open Documents'])await button(footer,label,false,label.length>20?208:label.length>10?136:90);
 const space=frame(footer,'Flexible space','HORIZONTAL',1,1);space.layoutSizingHorizontal='FILL';await button(footer,'Close',false,90);
 screens.push({id:screen.id,name:screen.name,w:screen.width,h:screen.height,dark});tabs.push({selected,dark,screen:screen.id,targets});
}
await search(REVISION.sections.find,'Find');await search(REVISION.sections.find,'Find',true);
await search(REVISION.sections.modes,'Replace');await search(REVISION.sections.modes,'Mark',true);
return ledger(screens,{tabs});
