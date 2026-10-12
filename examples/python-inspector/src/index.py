# SPDX-License-Identifier: GPL-3.0-or-later
import html
import os

context = None
panel = None


async def show(*_, record=True):
    global panel
    document = await context.documents.get_active()
    if document is None:
        return None
    snapshot = await context.documents.get_snapshot(document["documentId"])
    if panel is None:
        panel = await context.webviews.create("python-buffer", "Python Buffer Inspector")
        context.subscriptions.append(panel)
        def event(value):
            global panel
            if value["type"] == "closed":
                panel = None
        panel.on_event(event)
    content = "<h1>Python buffer snapshot</h1><pre>" + html.escape(snapshot["text"], quote=False) + "</pre>"
    await panel.set_html(content, {"documentId": snapshot["documentId"], "version": snapshot["version"]})
    await panel.reveal()
    # Retain only a small demonstration record, never the whole large buffer.
    if record:
        await context.storage.update("lastPreview", {"documentId": snapshot["documentId"], "version": snapshot["version"], "text": snapshot["text"][:2048], "pid": os.getpid()})
    return None


async def activate(ctx):
    global context
    context = ctx
    async def restore(descriptor):
        await show(record=False)
    context.subscriptions.append(ctx.webviews.register_restorer("python-buffer", restore))
    context.subscriptions.append(await ctx.commands.register("notepadnext.python-inspector.open", show))


async def deactivate():
    global panel
    if panel is not None:
        await panel.dispose()
        panel = None
