/**
 * createSemanticTokens
 *
 * Creates a batch of Figma variables in the given collection, one per entry in
 * `tokenMap`. Supports raw values, variable alias references, code syntax, and
 * scopes. Returns a map of token name → Variable for use in subsequent steps.
 *
 * @param {VariableCollection} collection - The target variable collection.
 * @param {Record<string, string>} modeIds - Map of {modeName: modeId} from createVariableCollection.
 * @param {Array<{
 *   name: string,
 *   type: 'COLOR' | 'FLOAT' | 'STRING' | 'BOOLEAN' | 'TIMING' | 'EASING',
 *   values: Record<string, string | number | boolean | MotionEasing | {type: 'VARIABLE_ALIAS', id: string}>,
 *   scopes?: VariableScope[],
 *   codeSyntax?: {WEB?: string, ANDROID?: string, iOS?: string}
 * }>} tokenMap - Ordered list of token definitions.
 *   - `name`: Variable name using slash hierarchy (e.g. "color/bg/primary").
 *   - `type`: Figma variable type.
 *   - `values`: Map of {modeName: value}. Values can be raw (hex string for COLOR,
 *     number for FLOAT) or alias objects {type: 'VARIABLE_ALIAS', id: variableId}.
 *     For COLOR, raw values are accepted as hex strings ("#rrggbb" or "#rrggbbaa")
 *     and converted to {r, g, b, a} automatically.
 *   - `scopes`: Array of VariableScope strings. Omit to use [] (hidden/primitive).
 *   - `codeSyntax`: Platform code syntax strings. Omit to skip.
 * @returns {Promise<{variables: Record<string, Variable>}>}
 *   `variables` maps each token name to its created Variable object.
 */
async function createSemanticTokens(collection, modeIds, tokenMap) {
  const variables = {}

  for (const token of tokenMap) {
    // Create the variable
    const variable = figma.variables.createVariable(token.name, collection, token.type)

    // Set values for each mode
    for (const [modeName, rawValue] of Object.entries(token.values)) {
      const modeId = modeIds[modeName]
      if (!modeId) {
        throw new Error(
          `createSemanticTokens: mode "${modeName}" not found in modeIds for token "${token.name}". ` +
            `Available modes: ${Object.keys(modeIds).join(', ')}`,
        )
      }

      let value = rawValue

      // Convert hex strings to Figma RGBA for COLOR type
      if (token.type === 'COLOR' && typeof rawValue === 'string' && rawValue.startsWith('#')) {
        value = hexToFigmaColor(rawValue)
      }

      variable.setValueForMode(modeId, value)
    }

    // Set scopes (default: empty array = hidden from property pickers / primitives)
    variable.scopes = token.scopes || []

    // Set code syntax per platform
    if (token.codeSyntax) {
      if (token.codeSyntax.WEB) {
        variable.setVariableCodeSyntax('WEB', token.codeSyntax.WEB)
      }
      if (token.codeSyntax.ANDROID) {
        variable.setVariableCodeSyntax('ANDROID', token.codeSyntax.ANDROID)
      }
      if (token.codeSyntax.iOS) {
        variable.setVariableCodeSyntax('iOS', token.codeSyntax.iOS)
      }
    }

    variables[token.name] = variable
  }

  return { variables }
}

/**
 * Converts a hex color string to a Figma RGBA object.
 * Supports "#rgb", "#rrggbb", and "#rrggbbaa".
 *
 * @param {string} hex
 * @returns {{ r: number, g: number, b: number, a: number }}
 */
function hexToFigmaColor(hex) {
  let h = hex.replace('#', '')

  // Expand shorthand #rgb → #rrggbb
  if (h.length === 3) {
    h = h
      .split('')
      .map((c) => c + c)
      .join('')
  }

  const r = parseInt(h.substring(0, 2), 16) / 255
  const g = parseInt(h.substring(2, 4), 16) / 255
  const b = parseInt(h.substring(4, 6), 16) / 255
  const a = h.length === 8 ? parseInt(h.substring(6, 8), 16) / 255 : 1

  return { r, g, b, a }
}

const palettes={"chrome": {"Light": "#f1f1f1", "Dark": "#29292c"}, "surface": {"Light": "#ffffff", "Dark": "#202023"}, "text": {"Light": "#242424", "Dark": "#eeeeef"}, "secondary": {"Light": "#626267", "Dark": "#b6b6bd"}, "border": {"Light": "#cbcbcb", "Dark": "#4b4b50"}, "hover": {"Light": "#e4e4e8", "Dark": "#3a3a3f"}, "selected": {"Light": "#e7effb", "Dark": "#253c57"}, "accent": {"Light": "#005ec5", "Dark": "#85baff"}, "fieldBorder": {"Light": "#85858c", "Dark": "#777780"}, "disabled": {"Light": "#85858c", "Dark": "#898990"}, "onAccent": {"Light": "#ffffff", "Dark": "#202023"}, "successText": {"Light": "#22643a", "Dark": "#85d0a1"}, "successSurface": {"Light": "#edf6ef", "Dark": "#243b2d"}, "errorText": {"Light": "#b3261e", "Dark": "#ffb4b0"}, "errorSurface": {"Light": "#fff0ef", "Dark": "#482c30"}};
const collections=await figma.variables.getLocalVariableCollectionsAsync();
const primitives=collections.find(c=>c.id==='VariableCollectionId:2:2');
const ui=collections.find(c=>c.id==='VariableCollectionId:2:19');
if(!primitives||!ui)throw new Error('Existing draft foundations were not found.');
const all=await figma.variables.getLocalVariablesAsync();
const pm=primitives.modes[0].modeId,light=ui.modes.find(m=>m.name==='Light').modeId;
const dark=ui.modes.find(m=>m.name==='Dark')?.modeId||ui.addMode('Dark');
ui.name='Draft / UI appearance';
const created=[],mutated=[ui.id],byHex=new Map();
for(const v of all.filter(v=>v.variableCollectionId===primitives.id&&v.resolvedType==='COLOR')){const c=v.valuesByMode[pm];if(c&&typeof c==='object'&&'r'in c){const hex='#'+[c.r,c.g,c.b].map(n=>Math.round(n*255).toString(16).padStart(2,'0')).join('');byHex.set(hex,v);}}
for(const hex of new Set(Object.values(palettes).flatMap(p=>Object.values(p))))if(!byHex.has(hex)){const name=(hex[1]===hex[3]&&hex[3]===hex[5]?'neutral/':'tone/')+hex.slice(1);const {variables}=await createSemanticTokens(primitives,{Value:pm},[{name,type:'COLOR',values:{Value:hex},scopes:[]}]);const variable=variables[name];byHex.set(hex,variable);created.push(variable.id);}
const aliases={},roleNames=Object.keys(palettes);
for(const role of roleNames){let variable=all.find(v=>v.variableCollectionId===ui.id&&v.name==='color/'+role);const values=palettes[role];if(!variable){const {variables}=await createSemanticTokens(ui,{Light:light,Dark:dark},[{name:'color/'+role,type:'COLOR',values:{Light:{type:'VARIABLE_ALIAS',id:byHex.get(values.Light).id},Dark:{type:'VARIABLE_ALIAS',id:byHex.get(values.Dark).id}},scopes:role.includes('Text')||['text','secondary','disabled','onAccent'].includes(role)?['TEXT_FILL']:role.includes('Border')||role==='border'?['STROKE_COLOR']:['FRAME_FILL','SHAPE_FILL'],codeSyntax:{WEB:'NativeAppearance.colors.'+role}}]);variable=variables['color/'+role];created.push(variable.id);}else{variable.setValueForMode(dark,{type:'VARIABLE_ALIAS',id:byHex.get(values.Dark).id});mutated.push(variable.id);}aliases[role]=variable.id;}
const legacy={white:'surface',gutter:'chrome',controlBorder:'fieldBorder',blue:'accent',blueSoft:'selected',success:'successText',successSoft:'successSurface',error:'errorText',errorSoft:'errorSurface'};
for(const v of all.filter(v=>v.variableCollectionId===ui.id&&v.resolvedType==='COLOR')){const key=v.name.split('/')[1],role=legacy[key]||key;if(palettes[role]){v.setValueForMode(dark,{type:'VARIABLE_ALIAS',id:byHex.get(palettes[role].Dark).id});mutated.push(v.id);}else{v.setValueForMode(dark,v.valuesByMode[light]);mutated.push(v.id);}}
const styles=await figma.getLocalTextStylesAsync();
await Promise.all(['Regular','Semibold','Bold','Medium'].map(style=>figma.loadFontAsync({family:'SF Pro',style})));
for(const [name,size,line,weight]of [['Draft/UI/Section',16,22,'Semibold'],['Draft/UI/Help',12,18,'Regular']]){let style=styles.find(s=>s.name===name);if(!style){style=figma.createTextStyle();style.name=name;created.push(style.id);}else mutated.push(style.id);style.fontName={family:'SF Pro',style:weight};style.fontSize=size;style.lineHeight={unit:'PIXELS',value:line};}
const textStyles=(await figma.getLocalTextStylesAsync()).filter(s=>s.name.startsWith('Draft/UI/')).map(s=>({id:s.id,name:s.name,font:s.fontName,size:s.fontSize,lineHeight:s.lineHeight}));
return {createdNodeIds:created,mutatedNodeIds:[...new Set(mutated)],collection:ui.id,modes:{Light:light,Dark:dark},aliases,textStyles,primitiveCount:(await figma.variables.getLocalVariablesAsync()).filter(v=>v.variableCollectionId===primitives.id).length,clearX:Math.max(...figma.currentPage.children.map(n=>n.x+n.width))+240};
