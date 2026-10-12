# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate the same checked-in schemas used by the Qt and Node hosts."""
from decimal import Decimal
import inspect
import json
from pathlib import Path
import re
from nnp_rpc import RpcError

BASE = Path(__file__).resolve().parent.parent
SCHEMA = json.loads((BASE / "wire.schema.json").read_text())
CONTRACT = json.loads((BASE / "wire-contract.json").read_text())
LIMITS = json.loads((BASE / "protocol-profile.json").read_text())


def equal(a, b):
    if type(a) is bool or type(b) is bool:
        return type(a) is type(b) and a == b
    if isinstance(a, dict) or isinstance(b, dict):
        return isinstance(a, dict) and isinstance(b, dict) and a.keys() == b.keys() and all(equal(a[key], b[key]) for key in a)
    if isinstance(a, list) or isinstance(b, list):
        return isinstance(a, list) and isinstance(b, list) and len(a) == len(b) and all(equal(x, y) for x, y in zip(a, b))
    return a == b


def valid_json(value, depth=0, seen=None):
    seen = set() if seen is None else seen
    if depth > 128:
        return False
    if isinstance(value, (dict, list)):
        if id(value) in seen:
            return False
        seen.add(id(value))
        ok = (not isinstance(value, dict) or all(type(key) is str for key in value)) and all(valid_json(item, depth + 1, seen) for item in (value.values() if isinstance(value, dict) else value))
        seen.remove(id(value))
        return ok
    return value is None or type(value) in (bool, str, int) or (isinstance(value, Decimal) and value.is_finite()) or (type(value) is float and value not in (float("inf"), -float("inf")) and value == value)


def matches(value, rule, budget, depth=0):
    if depth > 192:
        return False
    if "$ref" in rule:
        name = rule["$ref"][8:] if rule["$ref"].startswith("#/$defs/") else None
        if name not in SCHEMA["$defs"] or not matches(value, SCHEMA["$defs"][name], budget, depth + 1):
            return False
    if "const" in rule and not equal(value, rule["const"]):
        return False
    if "enum" in rule and not any(equal(value, item) for item in rule["enum"]):
        return False
    if "oneOf" in rule and sum(matches(value, branch, budget, depth + 1) for branch in rule["oneOf"]) != 1:
        return False
    number = type(value) in (int, float, Decimal)
    types = {"object": isinstance(value, dict), "array": isinstance(value, list), "string": type(value) is str, "null": value is None, "boolean": type(value) is bool, "integer": number and abs(value) <= 9007199254740991 and value == int(value)}
    if "type" in rule and not types.get(rule["type"], False):
        return False
    if isinstance(value, dict):
        if any(key not in value for key in rule.get("required", [])):
            return False
        for key, item in value.items():
            if key in rule.get("properties", {}):
                if not matches(item, rule["properties"][key], budget, depth + 1):
                    return False
            elif rule.get("additionalProperties") is False:
                return False
    if isinstance(value, list):
        if len(value) < rule.get("minItems", 0) or len(value) > rule.get("maxItems", float("inf")):
            return False
        if "items" in rule and not all(matches(item, rule["items"], budget, depth + 1) for item in value):
            return False
        if rule.get("uniqueItems") and any(any(equal(item, other) for other in value[:index]) for index, item in enumerate(value)):
            return False
    if type(value) is str:
        if "x-nnp-maxUtf8Bytes" in rule and len(value.encode("utf-8", errors="surrogatepass")) > rule["x-nnp-maxUtf8Bytes"]:
            budget["tooLarge"] = True
            return False
        if len(value) < rule.get("minLength", 0) or len(value) > rule.get("maxLength", float("inf")) or ("pattern" in rule and not re.search(rule["pattern"], value)):
            return False
    if number and (value < rule.get("minimum", -float("inf")) or value > rule.get("maximum", float("inf"))):
        return False
    return True


def validate(method, part, value, budget=None):
    budget = {"tooLarge": False} if budget is None else budget
    ref = CONTRACT["methods"].get(method, {}).get(part + "Schema")
    return part in ("params", "result") and bool(ref) and valid_json(value) and matches(value, {"$ref": ref}, budget)


def require_valid(method, part, value):
    budget = {"tooLarge": False}
    if not validate(method, part, value, budget):
        raise RpcError(1008 if budget["tooLarge"] else (-32602 if part == "params" else -32603), f"Invalid {part} contract for {method}")


def register(endpoint, method, handler):
    async def checked(params, options):
        definition = CONTRACT["methods"][method]
        if options["notification"] != (definition["kind"] == "notification"):
            raise RpcError(-32600, "Invalid method message kind")
        require_valid(method, "params", params)
        result = handler(params, options)
        if inspect.isawaitable(result):
            result = await result
        if definition["kind"] == "request":
            require_valid(method, "result", result)
        return result
    definition = CONTRACT["methods"][method]
    endpoint.register(method, checked, reliable_notification=definition["kind"] == "notification" and definition.get("effect") in ("orderedDocumentMirror", "ownedMessage", "ownedPanelEvent"))
