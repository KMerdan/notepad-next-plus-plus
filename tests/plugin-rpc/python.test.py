# SPDX-License-Identifier: GPL-3.0-or-later
import asyncio
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "packages/plugin-host/python"))
from nnp_rpc import Endpoint, Framer, RpcError, MAX_FRAME
from nnp_wire import validate, register
from nnp_client import Client


def normalize(value):
    if isinstance(value, list):
        return sorted((normalize(item) for item in value), key=lambda item: json.dumps(item, sort_keys=True))
    if isinstance(value, dict):
        return {key: {"code": item["code"]} if key == "error" else normalize(item) for key, item in value.items()}
    return value


class SharedRpc(unittest.IsolatedAsyncioTestCase):
    pass


for index, fixture in enumerate(json.loads((ROOT / "tests/plugin-rpc/fixtures.json").read_text())):
    async def check(self, fixture=fixture):
        output = []
        endpoint = Endpoint(output.append)
        endpoint.register("echo", lambda params, _: params)
        def total(params, _):
            if not isinstance(params, list):
                raise RpcError(-32602, "Invalid params")
            return sum(params)
        def fail(*_):
            raise ValueError("failure")
        endpoint.register("sum", total)
        endpoint.register("fail", fail)
        await endpoint.receive(fixture["input"].encode())
        if "idRaw" in fixture:
            self.assertEqual(len(output), 1)
            self.assertIn('"id":' + fixture["idRaw"] + "}", output[0])
        elif fixture["expected"] is None:
            self.assertEqual(output, [])
        else:
            self.assertEqual(len(output), 1)
            self.assertEqual(normalize(json.loads(output[0])), normalize(fixture["expected"]))
        endpoint.close()
    setattr(SharedRpc, f"test_{index:03d}_{fixture['name'].replace(' ', '_')}", check)


class SharedWire(unittest.TestCase):
    pass


for index, fixture in enumerate(json.loads((ROOT / "tests/plugin-rpc/wire-fixtures.json").read_text())):
    def check(self, fixture=fixture):
        self.assertEqual(validate(fixture["method"], fixture["part"], fixture["value"]), fixture["valid"])
    setattr(SharedWire, f"test_{index:03d}_{fixture['name'].replace(' ', '_')}", check)


class Boundaries(unittest.IsolatedAsyncioTestCase):
    async def test_reliable_notification_overload_faults_instead_of_dropping(self):
        output, faults = [], []
        endpoint = Endpoint(output.append, fault=lambda error: faults.append(error.code))
        release, ready = asyncio.Event(), asyncio.Event()
        started = 0
        async def blocked(*_):
            nonlocal started
            started += 1
            if started == 64:
                ready.set()
            await release.wait()
        endpoint.register("delta", blocked, reliable_notification=True)
        message = b'{"jsonrpc":"2.0","method":"delta","params":{}}'
        tasks = [asyncio.create_task(endpoint.receive(message)) for _ in range(64)]
        await asyncio.wait_for(ready.wait(), 1)
        self.assertEqual(len(endpoint.incoming), 64)
        await endpoint.receive(message)
        self.assertEqual(faults, [1007]); self.assertEqual(output, [])
        release.set(); await asyncio.gather(*tasks)
        self.assertEqual(len(endpoint.incoming), 0)
        register(endpoint, "nnp.documents.delta", lambda *_: None)
        await endpoint.receive(b'{"jsonrpc":"2.0","method":"nnp.documents.delta","params":{}}')
        self.assertEqual(faults, [1007, -32602]); self.assertEqual(output, [])
        endpoint.close()

    def test_fragmentation_and_invalid_framing(self):
        wire = Framer.frame('{"text":"a😀中"}') + Framer.frame("{}")
        for split in range(len(wire) + 1):
            output = []
            framer = Framer(output.append)
            framer.feed(wire[:split]); framer.feed(wire[split:]); framer.finish()
            self.assertEqual([body.decode() for body in output], ['{"text":"a😀中"}', "{}"])
        for bad in [b"Content-Length: -1\r\n\r\n", b"Content-Length: 1\r\nContent-Length: 1\r\n\r\nx", b"Content-Length: 16777217\r\n\r\n", b"X-Test: x\r\n\r\n", b"x" * 8193, b"Content-Length: 0\r\nContent-Type: application/json; charset=latin1\r\n\r\n", b"Content-Length: 0\r\nContent-Type: application/json; charset=\r\n\r\n", b"Content-Length: 0\r\nX-Test: \xff\r\n\r\n"]:
            with self.subTest(bad=bad[:100]):
                framer = Framer(lambda _: None)
                with self.assertRaises(ValueError):
                    framer.feed(bad)
                with self.assertRaises(ValueError):
                    framer.feed(Framer.frame("{}"))
        framer = Framer(lambda _: None); framer.feed(b"Content-Length: 10\r\n\r\n{}")
        with self.assertRaises(ValueError):
            framer.finish()

    async def test_utf8_and_profile_budgets(self):
        output, faults = [], []
        endpoint = Endpoint(output.append, fault=faults.append)
        await endpoint.receive(b"\xff")
        self.assertEqual(json.loads(output[0])["error"]["code"], -32700)
        for body in [json.dumps([1] * 65), json.dumps({"jsonrpc": "2.0", "method": "echo", "id": "x" * 1024}), "[" * 129 + "0" + "]" * 129]:
            await endpoint.receive(body.encode())
        self.assertEqual(len(faults), 3)

    async def test_bidirectional_timeout_cancel_late_response_and_close(self):
        tasks = set()
        def deliver(peer, raw):
            task = asyncio.create_task(peer.receive(raw.encode())); tasks.add(task); task.add_done_callback(tasks.discard)
        a = Endpoint(lambda raw: deliver(b, raw)); b = Endpoint(lambda raw: deliver(a, raw))
        a.register("host.echo", lambda params, _: params)
        b.register("roundtrip", lambda params, _: b.call("host.echo", params))
        self.assertEqual(await a.call("roundtrip", {"text": "😀中"}), {"text": "😀中"})
        cancelled = asyncio.Event()
        async def hang(_, options):
            await options["cancellation"].wait(); cancelled.set(); raise RpcError(1006, "Cancelled")
        b.register("hang", hang)
        call = asyncio.create_task(a.call("hang", {})); await asyncio.sleep(0.01); call.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await call
        await asyncio.wait_for(cancelled.wait(), 1)
        with self.assertRaises(RpcError) as result:
            await a.call("hang", {}, timeout=0.01)
        self.assertEqual(result.exception.code, 1006); self.assertEqual(len(a.pending), 0)
        output = []; endpoint = Endpoint(output.append)
        call = asyncio.create_task(endpoint.call("hang", {})); await asyncio.sleep(0)
        identity = json.loads(output[0])["id"]
        await endpoint.receive(json.dumps({"jsonrpc": "2.0", "id": identity, "result": 1, "error": {"code": 1, "message": "invalid"}}).encode())
        self.assertEqual(len(endpoint.pending), 1); endpoint.close()
        with self.assertRaises(RpcError) as result:
            await call
        self.assertEqual(result.exception.code, 1004)
        a.close(); b.close()
        if tasks:
            await asyncio.gather(*tasks, return_exceptions=True)

    async def test_request_kind_and_result_contract(self):
        output = []; endpoint = Endpoint(output.append)
        register(endpoint, "nnp.health.ping", lambda *_: {"wrong": True})
        await endpoint.receive(b'{"jsonrpc":"2.0","id":"x","method":"nnp.health.ping","params":{"sessionId":"s","generation":1}}')
        self.assertEqual(json.loads(output[-1])["error"]["code"], -32603)
        before = len(output)
        await endpoint.receive(b'{"jsonrpc":"2.0","method":"nnp.health.ping","params":{"sessionId":"s","generation":1}}')
        self.assertEqual(len(output), before)

    async def test_integer_error_response_and_decoder_depth(self):
        output, faults = [], []
        endpoint = Endpoint(output.append, fault=faults.append)
        call = asyncio.create_task(endpoint.call("test", {}))
        await asyncio.sleep(0)
        identifier = json.loads(output[-1])["id"]
        await endpoint.receive(('{"jsonrpc":"2.0","id":' + json.dumps(identifier) + ',"error":{"code":-32603.0,"message":"integer JSON number"}}').encode())
        with self.assertRaises(RpcError) as failure:
            await call
        self.assertEqual(failure.exception.code, -32603)
        await endpoint.receive(("[" * 2000 + "0" + "]" * 2000).encode())
        self.assertEqual(len(faults), 1)
        endpoint.close()

    async def test_data_compare_exchange_preserves_namespace_null_and_deletion(self):
        calls = []
        class Peer:
            def register(self, *_, **options):
                pass
            async def call(self, method, params):
                calls.append((method, params))
                if len(calls) == 4:
                    raise RpcError(1001, "Data changed")
                return None
        identity = {"pluginId": "tests.python", "sessionId": "session", "generation": 1, "grantedCapabilities": ["storage@1", "configuration@1"]}
        client = Client(Peer(), identity, {}, lambda _: True)
        await client.api.storage.compare_exchange("state", None, {"count": 1})
        await client.api.configuration.compare_exchange("state", {"count": 1})
        await client.api.storage.compare_exchange("state", {"count": 1}, None)
        with self.assertRaises(RpcError) as failure:
            await client.api.configuration.compare_exchange("state", None, 2)
        self.assertEqual(failure.exception.code, 1001)
        self.assertEqual(calls[0][0], "nnp.storage.update")
        self.assertIsNone(calls[0][1]["expectedValue"])
        self.assertEqual(calls[0][1]["value"], {"count": 1})
        self.assertEqual(calls[1][0], "nnp.configuration.update")
        self.assertNotIn("value", calls[1][1])
        self.assertIn("value", calls[2][1])
        self.assertIsNone(calls[2][1]["value"])
        self.assertEqual(calls[3][0], "nnp.configuration.update")

    async def test_closed_panels_retire_python_handles(self):
        class Peer:
            def register(self, *_, **options):
                pass
            async def call(self, method, params):
                if method == "nnp.webviews.create":
                    return {"panelId": "panel"}
                raise AssertionError("Closed panel must not call the host")
        identity = {"pluginId": "tests.python", "sessionId": "session", "generation": 1, "grantedCapabilities": ["webviews@1"]}
        client = Client(Peer(), identity, {}, lambda _: True)
        for _ in range(100):
            panel = await client.create_panel("test", "Test")
            client.api.subscriptions.append(panel)
            await client.panel_event({"sessionId": "session", "generation": 1, "panelId": "panel", "type": "closed"}, {})
            self.assertEqual(client.resources, [])
            self.assertEqual(client.api.subscriptions, [])
            self.assertEqual(client.panels, {})
            self.assertEqual(client.panel_handles, {})
            with self.assertRaises(RpcError):
                await panel.reveal()
            await panel.dispose()


if __name__ == "__main__":
    unittest.main(verbosity=2)
