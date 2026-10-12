# SPDX-License-Identifier: GPL-3.0-or-later
import inspect
from types import SimpleNamespace
import traceback
import uuid
from nnp_rpc import RpcError, MISSING
from nnp_wire import register, require_valid


async def awaited(value):
    return await value if inspect.isawaitable(value) else value


class Resource:
    def __init__(self, client, identifier, release):
        self.client, self.identifier, self.release, self.closed = client, identifier, release, False
        if client.closed:
            raise RpcError(1004, "PluginStopping")
        client.resources.append(self)

    async def dispose(self):
        if self.closed:
            return
        self.closed = True
        if self in self.client.resources:
            self.client.resources.remove(self)
        self.release()
        await self.client.call("nnp.resources.dispose", {"resourceId": self.identifier})


class Client:
    def __init__(self, endpoint, identity, manifest, can_call):
        self.endpoint, self.identity, self.manifest, self.can_call = endpoint, identity, manifest, can_call
        self.resources, self.commands, self.watches, self.panels = [], {}, {}, {}
        self.panel_handles = {}
        self.closed, self.awaiting_watch, self.early_events = False, 0, []
        self.api = SimpleNamespace(plugin_id=identity["pluginId"], session_id=identity["sessionId"], generation=identity["generation"], capabilities=tuple(identity["grantedCapabilities"]), subscriptions=[])
        self.api.commands = SimpleNamespace(register=self.register_command)
        self.api.documents = SimpleNamespace(get_active=lambda: self.call("nnp.documents.getActive"), get_snapshot=lambda document_id: self.call("nnp.documents.getSnapshot", {"documentId": document_id}), apply_edits=lambda document_id, version, edits: self.call("nnp.documents.applyEdits", {"documentId": document_id, "expectedVersion": version, "edits": edits}), watch=self.watch)
        self.api.webviews = SimpleNamespace(create=self.create_panel)
        for namespace in ("storage", "configuration"):
            setattr(self.api, namespace, SimpleNamespace(
                get=lambda key, namespace=namespace: self.call(f"nnp.{namespace}.get", {"key": key}),
                update=lambda key, value=MISSING, namespace=namespace: self.call(f"nnp.{namespace}.update", {"key": key} if value is MISSING else {"key": key, "value": value}),
                compare_exchange=lambda key, expected_value, value=MISSING, namespace=namespace: self.call(f"nnp.{namespace}.update", {"key": key, "expectedValue": expected_value} if value is MISSING else {"key": key, "expectedValue": expected_value, "value": value})))
        register(endpoint, "nnp.commands.invoke", self.invoke)
        register(endpoint, "nnp.documents.event", self.document_event)
        register(endpoint, "nnp.webviews.event", self.panel_event)

    def current(self, params):
        return params.get("sessionId") == self.identity["sessionId"] and params.get("generation") == self.identity["generation"]

    async def call(self, method, params=None):
        if not self.can_call(method):
            raise RpcError(1004, "PluginStopping")
        payload = {**(params or {}), "sessionId": self.identity["sessionId"], "generation": self.identity["generation"]}
        require_valid(method, "params", payload)
        result = await self.endpoint.call(method, payload)
        require_valid(method, "result", result)
        return result

    async def register_command(self, command_id, callback):
        if not callable(callback) or not any(item["id"] == command_id for item in self.manifest.get("contributes", {}).get("commands", [])):
            raise RpcError(-32602, "Command is not declared by this plugin")
        callback_id = uuid.uuid4().hex
        self.commands[callback_id] = callback
        try:
            result = await self.call("nnp.commands.register", {"commandId": command_id, "callbackId": callback_id})
            return Resource(self, result["registrationId"], lambda: self.commands.pop(callback_id, None))
        except Exception:
            self.commands.pop(callback_id, None)
            raise

    async def invoke(self, params, _):
        if not self.current(params):
            raise RpcError(1004, "Stale plugin session")
        if not self.can_call("nnp.commands.invoke"):
            raise RpcError(1004, "PluginStopping")
        callback = self.commands.get(params["callbackId"])
        if not callback:
            raise RpcError(-32602, "Unknown command callback")
        try:
            return await awaited(callback(*params["args"]))
        except Exception:
            traceback.print_exc()
            raise

    async def deliver_document(self, params):
        subscription = self.watches.get(params["subscriptionId"])
        if subscription is None:
            if self.awaiting_watch:
                if len(self.early_events) >= 128:
                    raise RpcError(1007, "Document event buffer overflow")
                self.early_events.append(params)
            return
        if params["eventSeq"] > subscription["sequence"]:
            subscription["sequence"] = params["eventSeq"]
            await awaited(subscription["listener"](params))

    async def document_event(self, params, _):
        if self.current(params):
            await self.deliver_document(params)

    async def watch(self, listener):
        if not callable(listener):
            raise RpcError(-32602, "Invalid document listener")
        self.awaiting_watch += 1
        try:
            result = await self.call("nnp.documents.watch", {"target": "active", "mode": "invalidate"})
            identifier = result["subscriptionId"]
            self.watches[identifier] = {"listener": listener, "sequence": result["eventSeq"]}
            handle = Resource(self, identifier, lambda: self.watches.pop(identifier, None))
            handle.document, handle.event_seq = result["document"], result["eventSeq"]
            pending, self.early_events = self.early_events, []
            for event in pending:
                if event["subscriptionId"] == identifier:
                    await self.deliver_document(event)
                else:
                    self.early_events.append(event)
            return handle
        finally:
            self.awaiting_watch -= 1
            if not self.awaiting_watch:
                self.early_events.clear()

    async def create_panel(self, view_type, title, position="right"):
        result = await self.call("nnp.webviews.create", {"viewType": view_type, "title": title, "position": position})
        identifier, listeners = result["panelId"], []
        handle = Resource(self, identifier, lambda: (self.panels.pop(identifier, None), self.panel_handles.pop(identifier, None), listeners.clear()))
        self.panels[identifier] = listeners
        self.panel_handles[identifier] = handle
        handle.panel_id = identifier
        def panel_call(method, params):
            if identifier not in self.panels:
                raise RpcError(1004, "Panel closed")
            return self.call(method, {"panelId": identifier, **params})
        handle.reveal = lambda preserve_focus=False: panel_call("nnp.webviews.reveal", {"preserveFocus": preserve_focus})
        handle.set_html = lambda html, render_context: panel_call("nnp.webviews.setHtml", {"html": html, "renderContext": render_context})
        handle.post_message = lambda message: panel_call("nnp.webviews.postMessage", {"message": message})
        handle.map_resource = lambda resource: panel_call("nnp.webviews.mapResource", {"resource": resource})
        def on_event(listener):
            if not callable(listener) or identifier not in self.panels:
                raise RpcError(-32602, "Invalid panel listener")
            listeners.append(listener)
            return SimpleNamespace(dispose=lambda: listeners.remove(listener) if listener in listeners else None)
        handle.on_event = on_event
        return handle

    async def panel_event(self, params, _):
        if not self.current(params):
            return
        listeners = self.panels.get(params["panelId"], [])
        for listener in list(listeners):
            try:
                await awaited(listener(params))
            except Exception:
                traceback.print_exc()
        if params["type"] == "closed":
            listeners.clear()
            self.panels.pop(params["panelId"], None)
            handle = self.panel_handles.pop(params["panelId"], None)
            if handle:
                handle.closed = True
                if handle in self.resources:
                    self.resources.remove(handle)
                if handle in self.api.subscriptions:
                    self.api.subscriptions.remove(handle)

    async def dispose(self):
        if self.closed:
            return
        self.closed = True
        for resource in list(reversed(self.api.subscriptions)) + list(reversed(self.resources)):
            try:
                await awaited(resource.dispose())
            except Exception:
                pass
        self.api.subscriptions.clear()
        self.resources.clear(); self.commands.clear(); self.watches.clear(); self.panels.clear(); self.panel_handles.clear(); self.early_events.clear()
