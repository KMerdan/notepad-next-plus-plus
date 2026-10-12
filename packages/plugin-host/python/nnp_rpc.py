# SPDX-License-Identifier: GPL-3.0-or-later
"""Dependency-free JSON-RPC 2.0 endpoint for the shared bounded 0.1 profile."""
import asyncio
from decimal import Decimal
import inspect
import json
import re
import uuid

MAX_FRAME = 16 * 1024 * 1024
MAX_HEADER = 8192
MAX_RESULT = MAX_FRAME - 64 * 1024
MISSING = object()


class RpcError(Exception):
    def __init__(self, code, message, data=MISSING):
        super().__init__(message)
        self.code, self.message, self.data = code, message, data

    def value(self):
        result = {"code": self.code, "message": self.message}
        if self.data is not MISSING:
            result["data"] = self.data
        return result


def dumps(value):
    # Decimal preserves numeric IDs without conversion through binary floats.
    if isinstance(value, Decimal):
        if not value.is_finite():
            raise ValueError("Non-finite JSON number")
        return str(value)
    if isinstance(value, list):
        return "[" + ",".join(dumps(item) for item in value) + "]"
    if isinstance(value, dict):
        return "{" + ",".join(json.dumps(key) + ":" + dumps(item) for key, item in value.items()) + "}"
    return json.dumps(value, ensure_ascii=True, allow_nan=False, separators=(",", ":"))


def invalid_constant(_):
    raise ValueError("Invalid JSON number")


DECODER = json.JSONDecoder(parse_float=Decimal, parse_constant=invalid_constant)


def skip(raw, index):
    while index < len(raw) and raw[index] in " \t\r\n":
        index += 1
    return index


def raw_id(raw):
    index, token = skip(raw, 0) + 1, "null"
    while index < len(raw):
        index = skip(raw, index)
        if raw[index] == "}":
            break
        if raw[index] == ",":
            index += 1
            continue
        key, index = DECODER.raw_decode(raw, index)
        index = skip(raw, skip(raw, index) + 1)
        _, end = DECODER.raw_decode(raw, index)
        if key == "id":
            token = raw[index:end]
        index = end
    return token


def raw_items(raw):
    index, items = skip(raw, 0) + 1, []
    while index < len(raw):
        index = skip(raw, index)
        if raw[index] == "]":
            break
        if raw[index] == ",":
            index += 1
            continue
        _, end = DECODER.raw_decode(raw, index)
        items.append(raw[index:end])
        index = end
    return items


def valid_id(value):
    return value is None or type(value) in (str, int, Decimal)


def reply(token, value=None, error=None):
    return '{"jsonrpc":"2.0",' + ('"error":' + dumps(error.value()) if error else '"result":' + dumps(value)) + ',"id":' + token + "}"


def check_depth(raw):
    depth, quoted, escaped = 0, False, False
    for char in raw:
        if quoted:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                quoted = False
        elif char == '"':
            quoted = True
        elif char in "{[":
            depth += 1
            if depth > 128:
                return False
        elif char in "}]":
            depth -= 1
    return True


class Framer:
    def __init__(self, on_frame):
        self.on_frame, self.buffer, self.length, self.broken = on_frame, bytearray(), None, False

    @staticmethod
    def frame(raw):
        body = raw.encode("utf-8")
        return f"Content-Length: {len(body)}\r\n\r\n".encode("ascii") + body

    def feed(self, chunk):
        if self.broken:
            raise ValueError("RPC stream is already invalid")
        try:
            for offset in range(0, len(chunk), 65536):
                self.buffer.extend(chunk[offset:offset + 65536])
                self.parse()
        except Exception:
            self.broken, self.buffer = True, bytearray()
            raise

    def parse(self):
        while True:
            if self.length is None:
                end = self.buffer.find(b"\r\n\r\n")
                if end < 0:
                    if len(self.buffer) > MAX_HEADER:
                        raise ValueError("RPC header exceeds 8 KiB")
                    return
                if end + 4 > MAX_HEADER:
                    raise ValueError("RPC header exceeds 8 KiB")
                header = bytes(self.buffer[:end])
                if any(byte > 126 or (byte < 32 and byte not in (9, 10, 13)) for byte in header):
                    raise ValueError("RPC headers must be ASCII")
                text = header.decode("ascii")
                if "\n" in text.replace("\r\n", "") or "\r" in text.replace("\r\n", ""):
                    raise ValueError("RPC headers require CRLF")
                length = None
                for line in text.split("\r\n"):
                    match = re.fullmatch(r"([A-Za-z-]+):[ \t]*(.*)", line)
                    if not match:
                        raise ValueError("Malformed RPC header")
                    name, value = match[1].lower(), match[2].strip()
                    if name == "content-length":
                        if length is not None or not re.fullmatch(r"[0-9]+", value):
                            raise ValueError("Invalid or duplicate Content-Length")
                        length = int(value)
                        if length > MAX_FRAME:
                            raise ValueError("RPC frame exceeds 16 MiB")
                    elif name == "content-type":
                        charset = re.search(r'charset\s*=\s*"?([^;"\s]+)', value, re.I)
                        if (re.search("charset", value, re.I) and not charset) or (charset and charset[1].lower() != "utf-8"):
                            raise ValueError("Only UTF-8 RPC content is supported")
                if length is None:
                    raise ValueError("Missing Content-Length")
                self.length = length
                del self.buffer[:end + 4]
            if len(self.buffer) < self.length:
                return
            body = bytes(self.buffer[:self.length])
            del self.buffer[:self.length]
            self.length = None
            self.on_frame(body)

    def finish(self):
        if self.broken or self.length is not None or self.buffer:
            raise ValueError("Truncated RPC frame")


class Endpoint:
    def __init__(self, send, diagnostic=lambda _: None, fault=None):
        self.send, self.diagnostic = send, diagnostic
        self.fault = fault or self.raise_fault
        self.methods, self.pending, self.incoming, self.closed = {}, {}, {}, False
        self.reliable_notifications = set()

    @staticmethod
    def raise_fault(error):
        raise error

    def register(self, method, handler, reliable_notification=False):
        if method.startswith("rpc."):
            raise ValueError("Reserved RPC method namespace")
        self.methods[method] = handler
        if reliable_notification:
            self.reliable_notifications.add(method)
        else:
            self.reliable_notifications.discard(method)

    async def receive(self, body):
        if self.closed:
            return
        try:
            raw = body.decode("utf-8", errors="strict")
            value = DECODER.decode(raw)
        except RecursionError:
            self.fault(ValueError("RPC JSON exceeds 128 nesting levels"))
            return
        except ValueError:
            self.send(reply("null", error=RpcError(-32700, "Parse error")))
            return
        if not check_depth(raw):
            self.fault(ValueError("RPC JSON exceeds 128 nesting levels"))
            return
        values, raws = (value, raw_items(raw)) if isinstance(value, list) else ([value], [raw])
        if isinstance(value, list) and not value:
            self.send(reply("null", error=RpcError(-32600, "Invalid Request")))
            return
        if len(values) > 64 or any(isinstance(item, dict) and "id" in item and len(raw_id(text).encode("utf-8")) > 1024 for item, text in zip(values, raws)):
            self.fault(ValueError("RPC batch or ID exceeds the profile budget"))
            return
        results = await asyncio.gather(*(self.handle(text, item) for item, text in zip(values, raws)))
        bounded, size = [], 0
        for result in results:
            if result is None:
                continue
            if size + len(result.encode("utf-8")) > MAX_RESULT:
                result = reply(raw_id(result), error=RpcError(1008, "ContentTooLarge"))
            size += len(result.encode("utf-8"))
            bounded.append(result)
        if bounded and not self.closed:
            self.send("[" + ",".join(bounded) + "]" if isinstance(value, list) else bounded[0])

    async def handle(self, raw, value):
        obj = isinstance(value, dict)
        token = raw_id(raw) if obj and "id" in value and valid_id(value["id"]) else "null"
        if obj and "method" not in value and ("result" in value or "error" in value):
            error = value.get("error")
            valid = value.get("jsonrpc") == "2.0" and "id" in value and valid_id(value["id"]) and (("result" in value) != ("error" in value))
            code = error.get("code") if isinstance(error, dict) else None
            integer = type(code) is int or (type(code) is Decimal and code.is_finite() and code == code.to_integral_value())
            valid = valid and ("error" not in value or (isinstance(error, dict) and integer and isinstance(error.get("message"), str)))
            pending = self.pending.get(value.get("id")) if valid else None
            if not pending:
                self.diagnostic("Ignored invalid, unknown or late RPC response")
                return None
            self.pending.pop(value["id"])
            if error is not None:
                pending.set_exception(RpcError(error["code"], error["message"], error.get("data", MISSING)))
            else:
                pending.set_result(value["result"])
            return None
        if not obj or value.get("jsonrpc") != "2.0" or not isinstance(value.get("method"), str) or ("id" in value and not valid_id(value["id"])) or ("params" in value and not isinstance(value["params"], (dict, list))):
            return reply(token, error=RpcError(-32600, "Invalid Request"))
        notification, method = "id" not in value, value["method"]
        if method == "nnp.request.cancel" and notification:
            request_id = value.get("params", {}).get("requestId") if isinstance(value.get("params"), dict) else None
            for request, event in self.incoming.values():
                if isinstance(request_id, str) and request == request_id:
                    event.set()
            return None
        handler = self.methods.get(method)
        if handler is None:
            return None if notification else reply(token, error=RpcError(-32601, "Method not found"))
        if len(self.incoming) >= 64:
            if notification:
                if method in self.reliable_notifications:
                    self.fault(RpcError(1007, "Reliable RPC notification queue overflow"))
                else:
                    self.diagnostic("RPC notification rejected: overloaded")
                return None
            return reply(token, error=RpcError(1007, "Overloaded"))
        key, cancellation = uuid.uuid4().hex, asyncio.Event()
        self.incoming[key] = (value.get("id"), cancellation)
        try:
            result = handler(value.get("params"), {"notification": notification, "cancellation": cancellation})
            if inspect.isawaitable(result):
                result = await result
            if notification or self.closed:
                return None
            output = reply(token, result)
            return output if len(output.encode("utf-8")) <= MAX_RESULT else reply(token, error=RpcError(1008, "ContentTooLarge"))
        except Exception as error:
            if notification or self.closed:
                if notification:
                    if method in self.reliable_notifications and not self.closed:
                        self.fault(error)
                    else:
                        self.diagnostic("RPC notification handler failed")
                return None
            return reply(token, error=error if isinstance(error, RpcError) else RpcError(-32603, "Internal error"))
        finally:
            self.incoming.pop(key, None)

    async def call(self, method, params=MISSING, timeout=5):
        if self.closed or len(self.pending) >= 64:
            raise RpcError(1004 if self.closed else 1007, "RPC connection closed or overloaded")
        key, future = "p:" + uuid.uuid4().hex, asyncio.get_running_loop().create_future()
        self.pending[key] = future
        request = {"jsonrpc": "2.0", "id": key, "method": method}
        if params is not MISSING:
            request["params"] = params
        try:
            self.send(dumps(request))
            return await asyncio.wait_for(asyncio.shield(future), timeout)
        except asyncio.TimeoutError as error:
            raise RpcError(1006, "RPC request timed out") from error
        except asyncio.CancelledError:
            self.notify("nnp.request.cancel", {"requestId": key})
            raise
        finally:
            self.pending.pop(key, None)
            if not future.done():
                future.cancel()

    def notify(self, method, params=MISSING):
        if not self.closed:
            message = {"jsonrpc": "2.0", "method": method}
            if params is not MISSING:
                message["params"] = params
            self.send(dumps(message))

    def close(self):
        self.closed = True
        for future in self.pending.values():
            if not future.done():
                future.set_exception(RpcError(1004, "Plugin connection closed"))
        self.pending.clear()
        for _, cancellation in self.incoming.values():
            cancellation.set()
        self.incoming.clear()
