/** DESIGN EXAMPLE: requires the proposed capture and selection-guard APIs. */
import type { PluginContext } from '../native-api';
export async function activate(context: PluginContext): Promise<void> {
  context.subscriptions.push(await context.commands.register('demo.text-tools.uppercase-selection', async invocation => {
    if (!invocation.editorId) return;
    const captured = await context.editors.capture(invocation, { signal: context.stopping });
    if (captured.selection.mode !== 'stream' || captured.selection.selections.some(s => s.anchorVirtualSpace || s.caretVirtualSpace)) {
      await context.window.showMessage('info', 'This command supports ordinary text selections.');
      return;
    }
    const edits = captured.selection.selections.map((selection, index) => ({ range: selection.range, text: captured.selectedText[index].toUpperCase() }))
      .filter((edit, index) => captured.selectedText[index].length > 0);
    if (!edits.length) return;
    try {
      await context.documents.applyEdits(captured.document.documentId, captured.document.version, edits, {
        selectionGuard: { editorId: captured.editor.editorId, revision: captured.selection.revision },
        signal: context.stopping
      });
    } catch (error) {
      if (typeof error === 'object' && error !== null && 'kind' in error &&
          (error.kind === 'VersionConflict' || error.kind === 'SelectionConflict')) {
        await context.window.showMessage('info', 'The document or selection changed. Run this command again on the new selection.');
      } else {
        throw error; // Preserve permission, transport and unknown-write-outcome errors.
      }
    }
  }));
  context.subscriptions.push(await context.commands.register('demo.text-tools.remove-duplicate-lines', async invocation => {
    if (!invocation.editorId) return;
    const captured = await context.editors.capture(invocation, { signal: context.stopping });
    // Reuse the editor's existing line operation instead of reimplementing its rules.
    await context.core.execute('edit.lines.deduplicate', {
      target: { editorId: captured.editor.editorId, documentId: captured.document.documentId,
        expectedVersion: captured.document.version, selectionRevision: captured.selection.revision }
    }, { signal: context.stopping });
  }));
}
