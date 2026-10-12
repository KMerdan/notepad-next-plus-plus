/**
 * DESIGN PROPOSAL — native host API 0.2 candidate, not the installed SDK.
 * Existing 0.1 plugins keep their negotiated API and behavior.
 * Optional modules are available only when the host advertises their capability.
 */
export type Json = null | boolean | number | string | Json[] | { [key: string]: Json };
export type DocumentId = string & { readonly __kind: 'document' };
export type EditorId = string & { readonly __kind: 'editor' };
export type WindowId = string & { readonly __kind: 'window' };
export type PanelId = string & { readonly __kind: 'panel' };
export interface Disposable { dispose(): void | Promise<void>; }
export interface CallOptions { signal?: AbortSignal; }
export interface PluginError extends Error {
  code: number;
  kind: 'VersionConflict' | 'SelectionConflict' | 'DocumentClosed' | 'ReadOnlyDocument' |
    'UnsupportedCapability' | 'CapabilityDenied' | 'TooLarge' | 'Cancelled' | 'PluginStopping' | 'Timeout' | 'InternalError';
  details?: Json;
  /** A timeout after submission cannot claim that a write did not happen. */
  outcome?: 'notCommitted' | 'committed' | 'unknown';
}
/** Zero-based UTF-16 units; character does not count UTF-8 bytes or visual columns. */
export interface Position { line: number; character: number; }
export interface Range { start: Position; end: Position; }
export interface TextEdit { range: Range; text: string; }
export interface DocumentInfo {
  documentId: DocumentId; uri: string; name: string; version: number;
  languageId: string; isDirty: boolean; isReadonly: boolean;
  lineEnding: 'lf' | 'crlf' | 'cr'; byteLength: number; lineCount: number;
  /** Known file representation; unsupported byte sequences never become guessed text. */
  encoding: 'utf-8' | 'utf-16le' | 'utf-16be' | 'unknown'; bom: boolean;
}
export interface TextSnapshot extends DocumentInfo { text: string; positionEncoding: 'utf-16'; }
export interface EditorInfo { editorId: EditorId; windowId: WindowId; documentId: DocumentId; }
export interface Selection {
  range: Range; anchor: Position; caret: Position;
  anchorVirtualSpace: number; caretVirtualSpace: number;
}
export interface SelectionState {
  editorId: EditorId; documentId: DocumentId; documentVersion: number;
  revision: number; mode: 'stream' | 'rectangle'; mainIndex: number;
  selections: Selection[];
}
/** Targets the originating editor, even if the user activates another tab. */
export interface CommandInvocation {
  commandId: string; windowId: WindowId; editorId: EditorId | null;
  documentId: DocumentId | null; args: Json[];
}
/** Text and selection are captured together on the Qt GUI thread. */
export interface EditorCapture {
  editor: EditorInfo; document: DocumentInfo; snapshot: TextSnapshot;
  selection: SelectionState; selectedText: string[];
}
export interface SelectionGuard { editorId: EditorId; revision: number; }
export interface EditOptions extends CallOptions { selectionGuard?: SelectionGuard; }
export interface EditResult { version: number; operationId: string; }
export interface DocumentEvent {
  eventSeq: number;
  type: 'opened' | 'closed' | 'activeChanged' | 'changed' | 'metadataChanged' | 'reset';
  document: DocumentInfo | null; baseVersion?: number; changes?: TextEdit[];
  origin: { kind: 'user' | 'host' | 'plugin'; pluginId?: string };
}
export interface DocumentWatch extends Disposable { eventSeq: number; documents: DocumentInfo[]; }
export interface Documents {
  getActive(options?: CallOptions): Promise<DocumentInfo | null>;
  list(options?: CallOptions): Promise<DocumentInfo[]>;
  getInfo(id: DocumentId, options?: CallOptions): Promise<DocumentInfo>;
  getSnapshot(id: DocumentId, options?: CallOptions): Promise<TextSnapshot>;
  /** P1: bounded range read; never implemented by silently reading the whole document. */
  readRange(id: DocumentId, expectedVersion: number, range: Range, options?: CallOptions): Promise<{ text: string; version: number }>;
  /** One document transaction and one undo step; no automatic retry on conflict/timeout. */
  applyEdits(id: DocumentId, expectedVersion: number, edits: TextEdit[], options?: EditOptions): Promise<EditResult>;
  watch(listener: (event: DocumentEvent) => void | Promise<void>, options?: CallOptions): Promise<DocumentWatch>;
}
export interface Editors {
  getActive(options?: CallOptions): Promise<EditorInfo | null>;
  capture(invocation: CommandInvocation, options?: CallOptions): Promise<EditorCapture>;
  getSelections(id: EditorId, options?: CallOptions): Promise<SelectionState>;
  setSelections(id: EditorId, expectedDocumentVersion: number, expectedRevision: number, selections: Selection[], mainIndex: number): Promise<SelectionState>;
  revealRange(id: EditorId, documentVersion: number, range: Range, options?: { preserveFocus?: boolean }): Promise<void>;
  /** Moves the existing editor dock; it does not clone a shared-buffer editor view. */
  split(id: EditorId, direction: 'right' | 'bottom'): Promise<void>;
}
export interface CoreTarget {
  editorId: EditorId; documentId: DocumentId;
  expectedVersion: number; selectionRevision?: number;
}
export interface CoreCommandMap {
  'edit.undo': { target: CoreTarget };
  'edit.redo': { target: CoreTarget };
  'edit.case.upper': { target: CoreTarget };
  'edit.case.lower': { target: CoreTarget };
  'edit.comment.toggle': { target: CoreTarget };
  'edit.lines.deduplicate': { target: CoreTarget; consecutiveOnly?: boolean };
  'edit.lines.sort': { target: CoreTarget; order: 'ascending' | 'descending'; compare: 'caseSensitive' | 'caseInsensitive' | 'length' };
  'view.fold.all': { target: CoreTarget; folded: boolean };
}
export interface CoreCommandInfo {
  id: keyof CoreCommandMap; title: string; requiredCapabilities: string[];
  effect: 'document' | 'view'; selectionSemantics: string;
}
export interface CoreCommands {
  list(): Promise<CoreCommandInfo[]>;
  /** Stable catalog IDs call native services; arbitrary QAction names are not accepted. */
  execute<K extends keyof CoreCommandMap>(id: K, request: CoreCommandMap[K], options?: CallOptions): Promise<{ version: number }>;
}
export interface Commands {
  /** IDs must be declared in the plugin manifest. Handler context is a 0.2 contract. */
  register(id: string, handler: (invocation: CommandInvocation) => void | Promise<void>): Promise<Disposable>;
}
export interface RestoredPanel {
  panelId: PanelId; viewType: string; kind: 'fragment' | 'html' | 'service';
  state: Json; visible: boolean; active: boolean;
}
export interface PanelEvent { type: 'closed' | 'message' | 'loadFailed'; payload?: Json; }
export interface Panel extends Disposable {
  panelId: PanelId; reveal(preserveFocus?: boolean): Promise<void>;
  postMessage(value: Json): Promise<boolean>;
  onEvent(listener: (event: PanelEvent) => void): Disposable;
}
export interface FragmentPanel extends Panel {
  setHtml(fragment: string, source: { documentId: DocumentId | null; version: number | null }): Promise<void>;
}
export interface HtmlPanel extends Panel {
  setDocument(html: string): Promise<void>;
  mapAsset(packageRelativePath: string): Promise<string>;
}
export interface Webviews {
  create(viewType: string, title: string, position?: 'right' | 'bottom'): Promise<FragmentPanel>;
  /** Native full HTML/JS capability; the design name carries no external editor contract. */
  createHtml(options: { viewType: string; title: string; enableScripts: boolean; resourceRoots: string[]; position?: 'right' | 'bottom' }): Promise<HtmlPanel>;
  /** Same activation-owned loopback service contract as the current HTML plugin. */
  createService(options: { viewType: string; title: string; endpoint: string; nonce: string }): Promise<Panel>;
  registerRestorer(viewType: string, callback: (saved: RestoredPanel) => void | Promise<void>): Disposable;
}
export interface PluginData {
  get(key: string): Promise<Json>;
  update(key: string, value: Json | undefined): Promise<void>;
  compareExchange(key: string, expected: Json, value: Json | undefined): Promise<void>;
}
export interface NativeWindow {
  showMessage(severity: 'info' | 'warning' | 'error', message: string, choices?: string[]): Promise<string | undefined>;
}
export interface HostInfo {
  product: 'Notepad Next ++'; applicationVersion: string; apiVersion: string;
  platform: 'macos' | 'windows' | 'linux'; capabilities: readonly string[];
  limits: { maxSnapshotUtf8Bytes: number; maxEditsPerTransaction: number; maxPanelStateBytes: number };
  /** Pure local helper over the negotiated capability set. */
  supports(capability: string): boolean;
}
export interface Logger { debug(message: string, details?: Json): void; info(message: string, details?: Json): void; warn(message: string, details?: Json): void; error(message: string, details?: Json): void; }
/** Proposed P1 modules; their presence never implies support in an older host. */
export interface Files {
  open(uri: string, options?: { reveal?: boolean }): Promise<DocumentInfo>;
  create(options?: { name?: string; languageId?: string; text?: string }): Promise<DocumentInfo>;
  save(id: DocumentId, expectedVersion: number, options?: { destinationUri?: string }): Promise<{ saved: boolean; version: number }>;
  close(id: DocumentId, policy: 'prompt' | 'ifClean'): Promise<{ closed: boolean }>;
}
export interface Search {
  /** Literal first; regex requires a separate advertised, validated capability. */
  findAll(id: DocumentId, expectedVersion: number, query: { text: string; kind: 'literal' | 'regex'; caseSensitive?: boolean; wholeWord?: boolean }, options?: CallOptions): Promise<{ version: number; ranges: Range[]; truncated: boolean }>;
}
export interface Languages {
  list(): Promise<{ id: string; name: string }[]>;
  set(id: DocumentId, languageId: string): Promise<void>;
}
export interface Bookmarks {
  list(id: DocumentId): Promise<{ version: number; lines: number[] }>;
  toggle(id: DocumentId, expectedVersion: number, line: number): Promise<void>;
}
export interface Decorations {
  create(options: { style: 'underline' | 'highlight'; color: 'accent' | 'warning' | 'error' }): Promise<Decoration>;
}
export interface Decoration extends Disposable {
  setRanges(id: DocumentId, expectedVersion: number, ranges: Range[]): Promise<void>;
}
export interface PluginContext {
  pluginId: string; sessionId: string; generation: number;
  subscriptions: Disposable[]; stopping: AbortSignal;
  host: HostInfo; commands: Commands; core: CoreCommands;
  documents: Documents; editors: Editors; webviews: Webviews; window: NativeWindow;
  storage: PluginData; configuration: PluginData; log: Logger;
  files?: Files; search?: Search; languages?: Languages; bookmarks?: Bookmarks; decorations?: Decorations;
}
export interface PluginModule { activate(context: PluginContext): void | Promise<void>; deactivate?(): void | Promise<void>; }
