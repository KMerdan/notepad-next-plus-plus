# SPDX-License-Identifier: GPL-3.0-or-later
"""Controlled Python 3.12 adapter; plugin import occurs only on activation."""
import asyncio
import importlib.util
import json
import os
from pathlib import Path
import sys
import traceback

# -I omits script/cwd/site paths. Add only the controlled host directory before
# importing the adapter, and reserve a private protocol FD before plugin code.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from nnp_rpc import Endpoint, Framer, RpcError
from nnp_wire import register, LIMITS
from nnp_client import Client, awaited

if sys.version_info[:2] != (3, 12):
    raise RuntimeError("The Python prototype requires a configured Python 3.12 runtime")
PROTOCOL = os.fdopen(os.dup(1), "wb", buffering=0)
os.dup2(2, 1)
root = Path(sys.argv[1]).resolve(strict=True)
manifest = json.loads((root / "notepadnext-plugin.json").read_text())
if manifest.get("list") is not False or manifest.get("runtime", {}).get("kind") != "python":
    raise RuntimeError("Expected a validated Python package")
relative = manifest["runtime"]["entry"]
if not isinstance(relative, str) or Path(relative).is_absolute() or ".." in relative.split("/"):
    raise RuntimeError("Invalid Python entry")
entry = (root / relative).resolve(strict=True)
if not entry.is_relative_to(root) or not entry.is_file():
    raise RuntimeError("Python entry escapes the package")


async def main():
    state, identity, client, plugin, stopping = "new", None, None, None, None
    queue, finished, readers = asyncio.Queue(maxsize=128), asyncio.Event(), set()
    queued_bytes, input_bytes = 0, 0

    def send(raw):
        nonlocal queued_bytes
        framed = Framer.frame(raw)
        if len(framed) > 16 * 1024 * 1024 + 8192 or queued_bytes + len(framed) > 32 * 1024 * 1024 or queue.full():
            raise RpcError(1007, "Python output queue exceeds its budget")
        queued_bytes += len(framed)
        queue.put_nowait(framed)

    def fatal(error):
        print("Python protocol fault:", error, file=sys.stderr)
        # The external supervisor observes this exit and the Qt process scope
        # cleans up the group; this code cannot rescue a blocked Python thread.
        os._exit(2)

    endpoint = Endpoint(send, diagnostic=lambda message: print(message, file=sys.stderr), fault=fatal)

    async def write_all(body):
        def write():
            view = memoryview(body)
            while view:
                count = PROTOCOL.write(view)
                if not count:
                    raise BrokenPipeError("Python protocol pipe closed")
                view = view[count:]
        await asyncio.to_thread(write)

    async def writer():
        nonlocal queued_bytes
        while True:
            body = await queue.get()
            try:
                await write_all(body)
                queued_bytes -= len(body)
            except Exception as error:
                fatal(error)
            finally:
                queue.task_done()

    def current(params):
        if not identity or params.get("sessionId") != identity["sessionId"] or params.get("generation") != identity["generation"]:
            raise RpcError(1004, "Stale plugin session")

    def can_call(method):
        if method == "nnp.resources.dispose":
            return state not in ("new", "closed")
        return state == "active" if method == "nnp.commands.invoke" else state in ("activating", "active")

    async def initialize(params, _):
        nonlocal state, identity, client
        if state != "new" or params["pluginId"] != manifest["id"] or "0.1" not in params["protocolVersions"] or params["apiVersion"] != "0.1.0" or params["limits"] != LIMITS:
            raise RpcError(-32602, "Incompatible initialize contract")
        required = manifest["capabilities"]["required"]
        declared = set(required + manifest["capabilities"]["optional"])
        if any(item not in declared for item in params["grantedCapabilities"]) or any(item not in params["grantedCapabilities"] for item in required):
            raise RpcError(1005, "UnsupportedCapability")
        identity = dict(params)
        client = Client(endpoint, identity, manifest, can_call)
        state = "initialized"
        return {"protocolVersion": "0.1", "apiVersion": "0.1.0", "capabilities": params["grantedCapabilities"], "limits": params["limits"]}

    async def activate(params, _):
        nonlocal state, plugin
        current(params)
        if state != "initialized":
            raise RpcError(1004, "Plugin cannot activate in this state")
        state = "activating"
        try:
            sys.path.insert(0, str(root))
            spec = importlib.util.spec_from_file_location("nnp_plugin", entry)
            if spec is None or spec.loader is None:
                raise RuntimeError("Cannot load the Python entry")
            plugin = importlib.util.module_from_spec(spec)
            sys.modules["nnp_plugin"] = plugin
            spec.loader.exec_module(plugin)
            if not callable(getattr(plugin, "activate", None)):
                raise RuntimeError("Python module must export activate(context)")
            await awaited(plugin.activate(client.api))
            if state != "activating":
                raise RpcError(1004, "PluginStopping")
            state = "active"
            return None
        except Exception:
            state = "failed"
            await client.dispose()
            traceback.print_exc()
            raise RpcError(-32603, "Plugin activation failed")

    async def deactivate():
        nonlocal state, stopping
        if stopping:
            return await stopping
        state = "stopping"
        async def run():
            try:
                if plugin and callable(getattr(plugin, "deactivate", None)):
                    await awaited(plugin.deactivate())
            finally:
                if client:
                    await client.dispose()
        stopping = asyncio.create_task(run())
        return await stopping

    async def stop(params, _):
        current(params)
        await deactivate()
        return None

    async def shutdown(params, _):
        current(params)
        await deactivate()
        # Let Endpoint publish this request's response before draining output.
        asyncio.get_running_loop().call_later(0.01, finished.set)
        return None

    def ping(params, _):
        current(params)
        return None

    for method, handler in [("nnp.initialize", initialize), ("nnp.plugin.activate", activate), ("nnp.plugin.deactivate", stop), ("nnp.shutdown", shutdown), ("nnp.health.ping", ping)]:
        register(endpoint, method, handler)

    def on_frame(body):
        nonlocal input_bytes
        if len(readers) >= 128 or input_bytes + len(body) > 32 * 1024 * 1024:
            fatal(RuntimeError("Python input task queue exceeds its budget"))
        input_bytes += len(body)
        task = asyncio.create_task(endpoint.receive(body))
        readers.add(task)
        def completed(task):
            nonlocal input_bytes
            input_bytes -= len(body)
            readers.discard(task)
            if not task.cancelled() and task.exception():
                fatal(task.exception())
        task.add_done_callback(completed)

    async def read():
        framer = Framer(on_frame)
        try:
            while not finished.is_set():
                body = await asyncio.to_thread(sys.stdin.buffer.read1, 65536)
                if not body:
                    framer.finish()
                    await deactivate()
                    finished.set()
                    return
                framer.feed(body)
        except Exception as error:
            fatal(error)

    asyncio.create_task(writer()); asyncio.create_task(read())
    await finished.wait()
    await queue.join()
    endpoint.close()
    # A pending stdin reader cannot keep the interpreter alive at shutdown.
    PROTOCOL.close()
    os._exit(0)


asyncio.run(main())
