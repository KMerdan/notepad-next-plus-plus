import type {PluginContext,PluginModule,DocumentInfo,TextSnapshot,WebviewPanel,RenderContext} from './index';

export interface TestPanel extends WebviewPanel {
  readonly title:string;
  readonly html:string;
  readonly visible:boolean;
  readonly renderContext:RenderContext|null;
  readonly messages:unknown[];
}
export interface TestHost {
  readonly context:PluginContext;
  readonly panels:Map<string,TestPanel>;
  readonly messages:Array<{severity:string;message:string;items:string[]}>;
  activate(plugin:PluginModule):Promise<void>;
  invoke(commandId:string,...args:unknown[]):Promise<unknown>;
  setDocument(text:string,info?:Partial<DocumentInfo>):void;
  getSnapshot():TextSnapshot;
  dispose():Promise<void>;
}
/** In-memory test double. No Qt/RPC/CSP/OS isolation or host quota validation.
 * Compatible Webviews, all-document mirrors and resource mapping are unsupported.
 */
export function createTestHost(options?:{
  pluginId?:string;
  capabilities?:string[];
  text?:string;
  document?:Partial<DocumentInfo>;
  onRender?:(panel:TestPanel)=>void;
}):TestHost;
