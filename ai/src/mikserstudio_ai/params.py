"""Checks job params against an operation's JSON Schema.

Only the subset the backends use is supported: type, enum, minimum, maximum, minLength, maxLength,
properties, required, additionalProperties (bool) and default. Unknown keywords are ignored, the
same way the Mikser Studio core treats them, so schemas stay forward compatible.
"""

from collections.abc import Mapping
from typing import Any

from mikserstudio_ai.backend import JsonObject

_TYPES: dict[str, tuple[type, ...]] = {
    "string": (str,),
    "integer": (int,),
    "number": (int, float),
    "boolean": (bool,),
    "object": (dict,),
    "array": (list,),
}


def with_defaults(params: Mapping[str, Any], schema: JsonObject) -> JsonObject:
    """params plus the top-level defaults of the schema for missing properties."""
    result = dict(params)
    for name, prop in schema.get("properties", {}).items():
        if name not in result and "default" in prop:
            result[name] = prop["default"]
    return result


def validate(params: Mapping[str, Any], schema: JsonObject) -> list[str]:
    """One English message per problem, each starting with the property name; empty when valid."""
    problems: list[str] = []
    properties: dict[str, JsonObject] = schema.get("properties", {})
    for name in schema.get("required", []):
        if name not in params:
            problems.append(f"{name}: is required")
    if schema.get("additionalProperties") is False:
        problems.extend(f"{name}: is not a known parameter" for name in params if name not in properties)
    for name, value in params.items():
        if name in properties:
            problems.extend(f"{name}: {problem}" for problem in _check_value(value, properties[name]))
    return problems


def _check_value(value: Any, schema: JsonObject) -> list[str]:
    expected = schema.get("type")
    if isinstance(expected, str) and not _has_type(value, expected):
        return [f"must be of type {expected}"]
    problems: list[str] = []
    if "enum" in schema and value not in schema["enum"]:
        problems.append("must be one of " + ", ".join(str(item) for item in schema["enum"]))
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        if "minimum" in schema and value < schema["minimum"]:
            problems.append(f"must be >= {schema['minimum']}")
        if "maximum" in schema and value > schema["maximum"]:
            problems.append(f"must be <= {schema['maximum']}")
    if isinstance(value, str):
        if "minLength" in schema and len(value) < schema["minLength"]:
            problems.append(f"must have at least {schema['minLength']} characters")
        if "maxLength" in schema and len(value) > schema["maxLength"]:
            problems.append(f"must have at most {schema['maxLength']} characters")
    return problems


def _has_type(value: Any, expected: str) -> bool:
    if expected == "null":
        return value is None
    if isinstance(value, bool) and expected != "boolean":
        return False  # bool is an int in Python but not in JSON.
    if expected == "integer" and isinstance(value, float):
        return value.is_integer()
    return isinstance(value, _TYPES.get(expected, (object,)))
