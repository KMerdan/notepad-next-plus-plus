const screens=[];
async function brand(p,id,size){const n=await instance(p,id);n.resize(size,size);return n;}
async function workspace(dark){
 const card=await captionCard(REVISION.sections.workspace,'Editor + Markdown preview · '+(dark?'Dark':'Light'));
 const screen=frame(card,'Workspace / '+(dark?'Dark':'Light'),'VERTICAL',1200,760,'chrome',0);mode(screen,dark);
 const titlebar=frame(screen,'Native window title bar','HORIZONTAL',1200,34,'chrome',12,12);titlebar.counterAxisAlignItems='CENTER';
 for(const color of ['#ff5f57','#febc2e','#28c840']){const e=track(figma.createEllipse());e.resize(12,12);e.fills=[{type:'SOLID',color:{r:parseInt(color.slice(1,3),16)/255,g:parseInt(color.slice(3,5),16)/255,b:parseInt(color.slice(5),16)/255}}];titlebar.appendChild(e);}await text(titlebar,'NotepadNext','Emphasis','secondary');
 const toolbar=frame(screen,'Frequent commands','HORIZONTAL',1200,40,'chrome',4,6);toolbar.counterAxisAlignItems='CENTER';
 const actions=[];for(const name of ['doc_badge_plus','folder','square_and_arrow_down','arrow_uturn_backward','arrow_uturn_forward','magnifyingglass','arrow_left_arrow_right','arrow_turn_down_left','ellipsis']){const slot=frame(toolbar,name,'HORIZONTAL',32,32,name==='arrow_turn_down_left'?'selected':null,0,6);radius(slot,6);await icon(slot,name,20);actions.push({name,id:slot.id});}
 const spacer=frame(toolbar,'Toolbar stretch','HORIZONTAL',1,1);spacer.layoutSizingHorizontal='FILL';await brand(toolbar,'33:11730',20);await brand(toolbar,'2:100',20);
 const body=frame(screen,'Document and plugin panel','HORIZONTAL',1200,null,null,0);body.layoutSizingVertical='FILL';
 const editor=frame(body,'Markdown document','VERTICAL',640,null,'surface',0);editor.layoutSizingVertical='FILL';
 const tabrow=frame(editor,'Document tabs','HORIZONTAL',640,30,'chrome',0);const tab=frame(tabrow,'Active Markdown tab','HORIZONTAL',190,30,'surface',8,8);tab.counterAxisAlignItems='CENTER';await text(tab,'writing-on-macos.md','Body');await text(tab,'×','Body','secondary');
 const editbody=frame(editor,'Scintilla buffer','HORIZONTAL',640,null,'surface',0);editbody.layoutSizingVertical='FILL';
 const gutter=frame(editbody,'Line numbers','VERTICAL',40,null,'gutter',0,8);gutter.layoutSizingVertical='FILL';await text(gutter,'1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11','Body','secondary');
 const code=frame(editbody,'Editable source text','VERTICAL',600,null,'surface',0,12);code.layoutSizingVertical='FILL';
 const buffer=await text(code,'# Writing on macOS\n\nThe editor keeps your unsaved work.\n\n## A focused workspace\n\n- Native system symbols\n- Clear tabs and readable status\n- Plugins open beside your document\n\n```js\nconsole.log("Hello, Markdown");\n```','Body','text',568);await buffer.setTextStyleIdAsync(styles['Draft/Editor/Code'].id);
 const preview=frame(body,'Markdown Preview plugin','VERTICAL',560,null,'surface',0);preview.layoutSizingVertical='FILL';
 const dock=frame(preview,'Dock title','HORIZONTAL',560,30,'chrome',8,8);dock.counterAxisAlignItems='CENTER';await brand(dock,'2:100',16);await text(dock,'Markdown Preview','Emphasis');
 const context=frame(preview,'Document context','HORIZONTAL',560,40,'gutter',8,12);context.counterAxisAlignItems='CENTER';await text(context,'writing-on-macos.md','Body');const cs=frame(context,'Context stretch','HORIZONTAL',1,1);cs.layoutSizingHorizontal='FILL';await text(context,'Live · unsaved edits','Caption','secondary');
 const document=frame(preview,'Rendered Markdown','VERTICAL',560,null,'surface',20,24);document.layoutSizingVertical='FILL';await text(document,'Writing on macOS','Heading');await text(document,'The editor keeps your unsaved work.','Body','text',512);await text(document,'A focused workspace','Section');await text(document,'• Native system symbols\n• Clear tabs and readable status\n• Plugins open beside your document','Body','text',512);
 const block=frame(document,'Code block','VERTICAL',512,null,'gutter',8,12);radius(block,6);const snippet=await text(block,'console.log("Hello, Markdown");','Body');await snippet.setTextStyleIdAsync(styles['Draft/Editor/Code'].id);
 const pf=frame(preview,'Preview status','HORIZONTAL',560,28,'gutter',8,8);pf.counterAxisAlignItems='CENTER';await text(pf,'Markdown Preview · Connected','Caption','secondary');
 const status=frame(screen,'Editor status','HORIZONTAL',1200,24,'chrome',16,6);status.counterAxisAlignItems='CENTER';await text(status,'Markdown','Caption','secondary');const stretch=frame(status,'Status stretch','HORIZONTAL',1,1);stretch.layoutSizingHorizontal='FILL';await text(status,'Ln 1, Col 1    UTF-8    LF    Spaces: 4','Caption','secondary');
 screens.push({id:screen.id,name:screen.name,w:screen.width,h:screen.height,dark,actions});
}
async function manager(dark){
 const card=await captionCard(REVISION.sections.manager,'Registered resource · '+(dark?'Dark':'Light'));
 const screen=frame(card,'Plugin Manager / Uninstalled / '+(dark?'Dark':'Light'),'VERTICAL',1080,664,'chrome',0);mode(screen,dark);
 const header=frame(screen,'Manager heading','HORIZONTAL',1080,76,'chrome',12,16);header.counterAxisAlignItems='CENTER';await brand(header,'33:11730',24);const title=frame(header,'Identity','VERTICAL',520,null,null,4);await text(title,'Plugin Manager','Heading');await text(title,'Register resources, then choose what to install.','Caption','secondary');const stretch=frame(header,'Header stretch','HORIZONTAL',1,1);stretch.layoutSizingHorizontal='FILL';await button(header,'Register Resources…',false,172);
 const navigation=frame(screen,'Views and filters','HORIZONTAL',1080,52,'chrome',8,16);navigation.counterAxisAlignItems='CENTER';await button(navigation,'Installed (0)',false,120);const current=await button(navigation,'Uninstalled (1)',false,134);fill(current,'selected','border');await button(navigation,'Sources (1)',false,112);const space=frame(navigation,'Navigation stretch','HORIZONTAL',1,1);space.layoutSizingHorizontal='FILL';await field(navigation,'Search plugins',270);
 const body=frame(screen,'Catalog and details','HORIZONTAL',1080,null,'surface',0);body.layoutSizingVertical='FILL';
 const list=frame(body,'Registered resources','VERTICAL',336,null,'surface',0);list.layoutSizingVertical='FILL';const listhead=frame(list,'Catalog context','HORIZONTAL',336,40,'gutter',0,16);listhead.counterAxisAlignItems='CENTER';await text(listhead,'Registered resources','Emphasis');
 const resource=frame(list,'Markdown Preview','HORIZONTAL',336,94,'selected',16,16);resource.counterAxisAlignItems='CENTER';await brand(resource,'2:100',24);const rc=frame(resource,'Resource identity','VERTICAL',256,null,null,4);await text(rc,'Markdown Preview','Emphasis');await text(rc,'0.1.3 · KMerdan','Caption','secondary');await text(rc,'Registered · Not installed','Caption','secondary');
 await spacer(list);const hint=frame(list,'Registration hint','VERTICAL',336,52,'gutter',0,16);await text(hint,'Resources are registered, not installed.','Caption','secondary',304);
 const details=frame(body,'Resource details','VERTICAL',744,null,'surface',16,24);details.layoutSizingVertical='FILL';
 const identity=frame(details,'Plugin identity','HORIZONTAL',696,null,null,12);identity.counterAxisAlignItems='CENTER';await brand(identity,'2:100',40);const ic=frame(identity,'Plugin name and origin','VERTICAL',636,null,null,4);await text(ic,'Markdown Preview','Heading');await text(ic,'Registered · Not installed','Body','secondary');
 await text(details,'Preview the current Markdown buffer, including unsaved edits.','Body','text',696);
 const facts=frame(details,'Metadata overview','VERTICAL',696,null,null,12);for(const [label,value]of [['Plugin ID','notepadnext.markdown-preview'],['Catalog version','0.1.3'],['Plugin origin','github.com/KMerdan/notepadnext-markdown-preview'],['Registered source','Markdown Preview']]){const row=frame(facts,label,'HORIZONTAL',696,null,null,16);await text(row,label,'Caption','secondary',128);await text(row,value,'Body','text',552);}
 const actions=frame(details,'Explicit install actions','HORIZONTAL',696,34,null,8);await button(actions,'Install from GitHub…',true,176);await button(actions,'Local Package…',false,144);await button(actions,'Inspect Metadata',false,152);
 await spacer(details);const footer=frame(screen,'Manager status and close','HORIZONTAL',1080,48,'chrome',16,16);footer.counterAxisAlignItems='CENTER';await text(footer,'Source refreshed. 1 resource available.','Caption','secondary');const fs=frame(footer,'Footer stretch','HORIZONTAL',1,1);fs.layoutSizingHorizontal='FILL';await button(footer,'Close',false,90);
 screens.push({id:screen.id,name:screen.name,w:screen.width,h:screen.height,dark});
}
await workspace(false);await workspace(true);await manager(false);await manager(true);
return ledger(screens);
