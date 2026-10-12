// Minimal declarations for the pinned renderer surface used by this example.
// Plugin host APIs come from the separately versioned SDK.
declare module 'markdown-it' {
  export interface Token {
    type: string; content: string; children: Token[] | null; attrs: [string,string][] | null;
    attrGet(name:string):string|null; attrSet(name:string,value:string):void;
  }
  type Rule = (tokens:Token[],index:number,options:object,env:object,renderer:Renderer)=>string;
  export interface Renderer { rules:Record<string,Rule>; render(tokens:Token[],options:object,env:object):string; renderToken(tokens:Token[],index:number,options:object):string; }
  export default class MarkdownIt {
    constructor(options:{html:boolean;xhtmlOut:boolean;linkify:boolean;typographer:boolean});
    parse(text:string,env:object):Token[];
    renderer:Renderer; options:object;
  }
}
