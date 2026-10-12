const page=figma.currentPage;
const collections=await figma.variables.getLocalVariableCollectionsAsync();
const colorCollection=collections.find(c=>c.id==='VariableCollectionId:2:19');
const layoutCollection=collections.find(c=>c.id==='VariableCollectionId:2:36');
const variables=await figma.variables.getLocalVariablesAsync();
const colors=Object.fromEntries(variables.filter(v=>v.variableCollectionId===colorCollection.id).map(v=>[v.name.slice(6),v]));
const layout=Object.fromEntries(variables.filter(v=>v.variableCollectionId===layoutCollection.id).map(v=>[v.name,v]));
const styles=Object.fromEntries((await figma.getLocalTextStylesAsync()).map(s=>[s.name,s]));
await Promise.all(['Regular','Semibold','Medium','Bold'].map(style=>figma.loadFontAsync({family:'SF Pro',style})));
const created=[],mutated=[],components={};
const track=n=>{created.push(n.id);return n;};
for(const [name,value,scope]of [['radius/6',6,'CORNER_RADIUS'],['radius/8',8,'CORNER_RADIUS'],['space/4',4,'GAP'],['space/8',8,'GAP'],['space/12',12,'GAP'],['space/16',16,'GAP'],['space/24',24,'GAP']])if(!layout[name]){
  const v=figma.variables.createVariable(name,layoutCollection,'FLOAT');v.setValueForMode(layoutCollection.modes[0].modeId,value);v.scopes=[scope];layout[name]=v;created.push(v.id);
}
let appearance=variables.find(v=>v.variableCollectionId===colorCollection.id&&v.name==='appearance/mode');
if(!appearance){appearance=figma.variables.createVariable('appearance/mode',colorCollection,'STRING');appearance.scopes=[];appearance.setValueForMode(colorCollection.modes.find(m=>m.name==='Light').modeId,'Light');appearance.setValueForMode(colorCollection.modes.find(m=>m.name==='Dark').modeId,'Dark');created.push(appearance.id);}
async function bind(node,role,border=null,radius=null){
  const bindings={};if(role)bindings.fills=colors[role].id;if(border)bindings.strokes=colors[border].id;if(radius)bindings.cornerRadius=layout['radius/'+radius].id;
  const result=await bindVariablesToComponent(node,bindings);mutated.push(...result.mutatedNodeIds);
}
async function label(parent,value,style='Body',role='text'){
  const node=track(figma.createText());node.fontName={family:'SF Pro',style:'Regular'};await node.setTextStyleIdAsync(styles['Draft/UI/'+style].id);node.characters=value;node.textAutoResize='WIDTH_AND_HEIGHT';parent.appendChild(node);await bind(node,role);return node;
}
const library=track(figma.createAutoLayout('VERTICAL',{name:'macOS / Native Qt control components',itemSpacing:32,paddingLeft:24,paddingRight:24,paddingTop:24,paddingBottom:24}));
library.x=11760;library.y=100;await bind(library,'chrome');
await label(library,'macOS controls · shared Qt skin','Heading');
await label(library,'Flat Qt controls reuse Apple checkbox geometry, SF Pro and the application’s semantic colors.','Help','secondary');
async function family(name,axes,width,height){
  const result=await createComponentWithVariants({name:'Qt/macOS/'+name,description:'Matches NativeAppearance and NativeDialogs. Colors resolve from Draft / UI appearance; text and focus remain distinct in both appearances.',variantAxes:axes,baseProps:{width,height,layoutMode:'HORIZONTAL',radius:6,itemSpacing:8,padding:{left:12,right:12,top:4,bottom:4}},page});
  const set=result.componentSet;library.appendChild(set);created.push(set.id,...set.findAll(()=>true).map(n=>n.id));components[name]={id:set.id,variants:result.variants.map(v=>({id:v.id,name:v.name}))};return result;
}
const buttons=await family('Button',{Role:['Primary','Secondary'],State:['Default','Focus','Disabled']},200,34);
const buttonLabel=buttons.componentSet.addComponentProperty('Label','TEXT','Button');
for(const c of buttons.variants){
  const primary=c.name.includes('Role=Primary'),disabled=c.name.includes('State=Disabled');
  await bind(c,disabled?'chrome':primary?'accent':'surface',disabled?'border':primary||c.name.includes('Focus')?'accent':'border',6);
  const t=await label(c,'Button',primary?'Emphasis':'Body',disabled?'disabled':primary?'onAccent':'text');t.componentPropertyReferences={characters:buttonLabel};
}
const fields=await family('Field',{State:['Default','Focus','Disabled']},300,34);
const value=fields.componentSet.addComponentProperty('Value','TEXT','Value');
for(const c of fields.variants){c.primaryAxisAlignItems='MIN';await bind(c,c.name.includes('Disabled')?'chrome':'surface',c.name.includes('Focus')?'accent':c.name.includes('Disabled')?'border':'fieldBorder',6);const t=await label(c,'Value','Body',c.name.includes('Disabled')?'disabled':'text');t.componentPropertyReferences={characters:value};}
const nativeCheckbox=await figma.importComponentSetByKeyAsync('fe4a666d19ccdf1955485e1289bce2a9b91fee38');
const checks=await family('Checkbox',{Checked:['On','Off'],State:['Default','Disabled']},280,30);
const checkLabel=checks.componentSet.addComponentProperty('Label','TEXT','Option');
for(const c of checks.variants){
  c.primaryAxisAlignItems='MIN';c.paddingLeft=4;c.paddingRight=4;c.fills=[];c.strokes=[];
  const checked=c.name.includes('Checked=On'),disabled=c.name.includes('State=Disabled');
  const master=nativeCheckbox.children.find(v=>v.name===`Active=True, State=${disabled?'Disabled':'Idle'}, Selection=${checked?'On':'Off'}`);
  const indicator=track(master.children.find(n=>n.name==='Checkbox').clone());c.appendChild(indicator);
  indicator.resize(16,16);
  await bind(indicator,checked&&!disabled?'accent':'surface',disabled?'border':checked?'accent':'fieldBorder');indicator.strokeWeight=1.25;
  for(const n of indicator.findAll(()=>true)){
    if(n.name==='BG')n.visible=false;
    if(n.type==='VECTOR')await bind(n,disabled?'disabled':'onAccent');
  }
  created.push(...indicator.findAll(()=>true).map(n=>n.id));
  const t=await label(c,'Option','Body',disabled?'disabled':'text');t.componentPropertyReferences={characters:checkLabel};
}
const nav=await family('Navigation row',{State:['Default','Selected']},176,40);
const navLabel=nav.componentSet.addComponentProperty('Label','TEXT','General');
for(const c of nav.variants){c.primaryAxisAlignItems='MIN';await bind(c,c.name.includes('Selected')?'selected':'chrome');const t=await label(c,'General','Body');t.componentPropertyReferences={characters:navLabel};}
const feedback=await family('Feedback',{Tone:['Information','Success','Error']},500,34);
const message=feedback.componentSet.addComponentProperty('Message','TEXT','Search status');
for(const c of feedback.variants){c.primaryAxisAlignItems='MIN';const success=c.name.includes('Success'),error=c.name.includes('Error');await bind(c,error?'errorSurface':success?'successSurface':'surface',null,6);const t=await label(c,'Search status','Help',error?'errorText':success?'successText':'text');t.componentPropertyReferences={characters:message};}
const all=library.findAll(()=>true);
return {createdNodeIds:[...new Set(created)],mutatedNodeIds:[...new Set(mutated)],library:library.id,components,appearance:appearance.id,layout:Object.fromEntries(Object.entries(layout).map(([k,v])=>[k,v.id])),count:all.length,fonts:[...new Set(all.filter(n=>n.type==='TEXT'&&n.visible).map(n=>n.fontName.family))]};
