export interface Disposable { dispose(): void | Promise<void>; }
export interface DocumentInfo {
  documentId: string; uri: string; languageId: string; version: number;
  isDirty: boolean; isReadonly: boolean;
}
export interface TextSnapshot extends DocumentInfo { text: string; positionEncoding: 'utf-16'; }
export interface Position { line: number; character: number; }
export interface TextEdit { range: { start: Position; end: Position }; text: string; }
export interface DocumentEvent { subscriptionId: string; eventSeq: number; type: 'activeChanged' | 'contentInvalidated' | 'metadataChanged' | 'closed' | 'reset' | 'opened'; document: DocumentInfo | null; }
export interface Watch extends Disposable { document: DocumentInfo | null; eventSeq: number; }
export type DeltaEvent = {subscriptionId:string;eventSeq:number} & (
  {type:'changed';document:DocumentInfo;baseVersion:number;changes:Array<TextEdit & {rangeOffset:number;rangeLength:number}>;reason:'edit'|'undo'|'redo'} |
  {type:'opened'|'closed'|'metadataChanged';document:DocumentInfo} |
  {type:'activeChanged';document:DocumentInfo|null} |
  {type:'reset';document:DocumentInfo;reason:'bufferReset'|'unmappableChange'}
);
export interface DeltaWatch extends Disposable {
  documents:DocumentInfo[];activeDocumentId:string|null;eventSeq:number;maxDocuments:64;maxTotalUtf8Bytes:4194304;
  getSnapshot(documentId:string,options?:{signal?:AbortSignal}):Promise<{snapshot:TextSnapshot;eventSeq:number}>;
}
export interface RenderContext { documentId: string | null; version: number | null; }
export interface WebviewEvent { panelId: string; type: 'closed' | 'message' | 'loadFailed' | 'viewState'; payload?: unknown; }
export interface CompatibleWebviewOptions {panelId:string;viewType:string;title:string;enableScripts:boolean;localResourceRoots:string[];}
export interface RestoredPanel {readonly panelId:string;readonly viewType:string;readonly title:string;readonly kind:'fragment'|'html'|'service';readonly visible?:boolean;readonly active?:boolean;readonly hasState:boolean;readonly state:unknown;}
export interface CompatibleWebviewPanel extends Disposable {
  readonly panelId:string;
  reveal(preserveFocus?:boolean):Promise<void>;
  setDocument(html:string):Promise<void>;
  setTitle(title:string):Promise<void>;
  mapAsset(packageRelativePath:string):Promise<string>;
  postMessage(message:unknown):Promise<boolean>;
  onEvent(listener:(event:WebviewEvent)=>void):Disposable;
}
export interface WebviewPanel extends Disposable {
  readonly panelId: string;
  reveal(preserveFocus?: boolean): Promise<void>;
  /** Well-formed, allowlisted XHTML fragment. The host supplies the document and CSP. */
  setHtml(html: string, renderContext: RenderContext): Promise<void>;
  postMessage(message: unknown): Promise<boolean>;
  mapResource(resource: {kind:'package';path:string} | {kind:'documentImage';documentId:string;path:string}): Promise<string>;
  onEvent(listener: (event: WebviewEvent) => void): Disposable;
}
export interface PluginContext {
  readonly pluginId: string; readonly sessionId: string; readonly generation: number;
  readonly subscriptions: Disposable[];
  readonly capabilities: readonly string[];
  readonly commands: {
    register(commandId: string, callback: (...args: unknown[]) => unknown | Promise<unknown>): Promise<Disposable>;
  };
  /** Requires messages@1; choices are strings. Await selection or dismissal.
   * Dialog creation is denied until activation/update publication is complete.
   */
  readonly window:{showMessage(severity:'info'|'warning'|'error',message:string,items?:string[]):Promise<string|undefined>};
  readonly documents: {
    getActive(options?: {signal?:AbortSignal}): Promise<DocumentInfo | null>;
    getSnapshot(documentId: string, options?: {signal?:AbortSignal}): Promise<TextSnapshot>;
    watch(listener: (event: DocumentEvent) => void): Promise<Watch>;
    /** Requires documents.delta@1 and documents.read@1; reliable all-buffer events. */
    watchAll(listener:(event:DeltaEvent)=>void|Promise<void>):Promise<DeltaWatch>;
    applyEdits(documentId: string, expectedVersion: number, edits: TextEdit[]): Promise<{version:number}>;
  };
  readonly webviews: {
    /** Register during activate. Rebuild this view without replaying a user command.
     * The host preserves its dock, browser state, scroll and form values.
     * Requires webviews.restore@1 in the update's manifest.
     */
    registerRestorer(viewType:string,restore:(panel:RestoredPanel)=>void|Promise<void>):Disposable;
    /** Requires webviews@1 + webviews.service@1. Use startLocalService from ./local-service. */
    createService(options:{viewType:string;title:string;endpoint:string;nonce:string}):Promise<ServiceWebviewPanel>;
    create(viewType: string, title: string, position?: 'right' | 'bottom'): Promise<WebviewPanel>;
    /** Requires webviews@1 + webviews.compat@1 and Qt >=6.6. Full trusted HTML,
     * package-local JS/CSS/images, host CSP and owned state/message bridge.
     * A caller-generated 32 lowercase hex panelId lets synchronous proxies
     * calculate resource URIs before this asynchronous creation completes.
     */
    readonly owner:string;
    createCompatible(options:CompatibleWebviewOptions,listener?:(event:WebviewEvent)=>void):Promise<CompatibleWebviewPanel>;
  };
  /** Plugin-owned JSON keys. Missing keys return null; undefined deletes a key.
   * Writes require validated activation, so write from commands/events after activate returns.
   * 128 ASCII key characters, 64 KiB/value, 256 keys/namespace, 1 MiB/plugin.
   */
  readonly storage: { get(key:string):Promise<unknown>; update(key:string,value:unknown):Promise<void>; compareExchange(key:string,expectedValue:unknown,value?:unknown):Promise<void>; };
  /** Plugin-owned configuration only; shares no application or workspace settings. */
  readonly configuration: { get(key:string):Promise<unknown>; update(key:string,value:unknown):Promise<void>; compareExchange(key:string,expectedValue:unknown,value?:unknown):Promise<void>; };
}
export interface PluginModule {
  activate(context: PluginContext): void | Promise<void>;
  deactivate?(): void | Promise<void>;
}

export interface ServiceWebviewPanel extends Disposable {readonly panelId:string;reveal(preserveFocus?:boolean):Promise<void>;postMessage(message:unknown):Promise<boolean>;onEvent(listener:(event:WebviewEvent)=>void):Disposable;}
