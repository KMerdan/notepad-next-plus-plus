// SPDX-License-Identifier: GPL-3.0-or-later
import type {PluginContext} from '@notepadnext/plugin-sdk';
export async function activate(context:PluginContext) {
  context.subscriptions.push(await context.commands.register('__PLUGIN_ID__.open',async()=>{
    console.info(context.pluginId + ': command invoked');
    await context.window.showMessage('info','Hello from NotepadNext!');
  }));
}
export async function deactivate() {
  // The host releases context.subscriptions. Dispose your own timers/watchers here.
}
