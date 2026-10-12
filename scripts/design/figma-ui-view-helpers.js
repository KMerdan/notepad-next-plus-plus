// Reusable composition functions. REVISION is the scaffold ledger.
const created=[],mutated=[];
const track=n=>{created.push(n.id);return n;};
const vars=await figma.variables.getLocalVariablesAsync();
const roles=Object.fromEntries(vars.filter(v=>v.variableCollectionId==='VariableCollectionId:2:19').map(v=>[v.name.replace('color/',''),v]));
const spaces=Object.fromEntries(vars.filter(v=>v.variableCollectionId==='VariableCollectionId:2:36').map(v=>[v.name,v]));
const styles=Object.fromEntries((await figma.getLocalTextStylesAsync()).map(s=>[s.name,s]));
await Promise.all(['Regular','Semibold','Medium','Bold'].map(style=>figma.loadFontAsync({family:'SF Pro',style})));
await Promise.all(['Regular','Bold'].map(style=>figma.loadFontAsync({family:'Cousine',style})));
function paint(n,role){const v=roles[role].resolveForConsumer(n).value;return figma.variables.setBoundVariableForPaint({type:'SOLID',color:{r:v.r,g:v.g,b:v.b}},'color',roles[role]);}
function fill(n,role,border=null){n.fills=role?[paint(n,role)]:[];if(border){n.strokes=[paint(n,border)];n.strokeWeight=1;}mutated.push(n.id);}
function spacing(n,gap){n.itemSpacing=gap;if(spaces['space/'+gap])n.setBoundVariable('itemSpacing',spaces['space/'+gap]);}
function pad(n,p){n.paddingLeft=n.paddingRight=n.paddingTop=n.paddingBottom=p;for(const k of ['paddingLeft','paddingRight','paddingTop','paddingBottom'])if(spaces['space/'+p])n.setBoundVariable(k,spaces['space/'+p]);}
function radius(n,r){n.cornerRadius=r;if(spaces['radius/'+r])n.setBoundVariable('cornerRadius',spaces['radius/'+r]);}
function frame(p,name,direction='VERTICAL',width=null,height=null,role=null,gap=8,padding=0){const n=track(figma.createAutoLayout(direction,{name}));fill(n,role);spacing(n,gap);pad(n,padding);p.appendChild(n);if(width&&height)n.resize(width,height);else if(width)n.resize(width,1);if(width)n.layoutSizingHorizontal='FIXED';if(height)n.layoutSizingVertical='FIXED';else n.layoutSizingVertical='HUG';return n;}
async function text(p,value,style='Body',role='text',width=null){const n=track(figma.createText());n.fontName={family:'SF Pro',style:'Regular'};await n.setTextStyleIdAsync(styles['Draft/UI/'+style].id);n.characters=value;p.appendChild(n);fill(n,role);if(width){n.textAutoResize='HEIGHT';n.resize(width,18);}else n.textAutoResize='WIDTH_AND_HEIGHT';return n;}
async function icon(p,name,size=18){const n=track((await figma.getNodeByIdAsync(REVISION.icons[name].Light)).createInstance());p.appendChild(n);n.resize(size,size);n.setProperties({Mode:figma.variables.createVariableAlias(roles['appearance/mode'])});return n;}
async function instance(p,id,properties={},width=null){const n=track((await figma.getNodeByIdAsync(id)).createInstance());p.appendChild(n);n.setProperties(properties);if(width)n.resize(width,n.height);return n;}
async function button(p,label,primary=false,width=100,disabled=false){return instance(p,disabled?(primary?'39:11704':'39:11707'):(primary?'39:11702':'39:11705'),{'Label#39:0':label},width);}
async function field(p,value,width,dropdown=false,focus=false){return instance(p,focus?'39:11716':'39:11715',{'Value#39:7':value,'Dropdown#43:3':dropdown},width);}
async function check(p,label,on=false,width=480,disabled=false){return instance(p,on?(disabled?'39:11941':'39:11940'):(disabled?'39:11943':'39:11942'),{'Label#39:11':label},width);}
async function radio(p,label,on=false,width=480){return instance(p,on?'45:11701':'45:11698',{'Label#45:0':label},width);}
async function group(p,title,width){const n=frame(p,title,'VERTICAL',width,null,'surface',8,12);radius(n,8);await text(n,title,'Emphasis');return n;}
async function spacer(p){const n=frame(p,'Flexible space','VERTICAL',1,1);n.layoutSizingVertical='FILL';return n;}
async function captionCard(rowId,label){const row=await figma.getNodeByIdAsync(rowId);mutated.push(row.id);const card=frame(row,label,'VERTICAL',1200,null,null,12);await text(card,label,'Help','secondary');return card;}
function mode(n,dark){n.setExplicitVariableModeForCollection('VariableCollectionId:2:19',dark?'37:0':'2:1');}
function ledger(screens,extra={}){return {createdNodeIds:[...new Set(created)],mutatedNodeIds:[...new Set(mutated)],screens,...extra};}
