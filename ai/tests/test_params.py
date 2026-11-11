from typing import Any

import pytest

from mikserstudio_ai.params import validate, with_defaults

SCHEMA: dict[str, Any] = {
    "type": "object",
    "properties": {
        "input": {"type": "string", "minLength": 1, "maxLength": 5},
        "scale": {"type": "integer", "enum": [2, 3, 4], "default": 2},
        "strength": {"type": "number", "minimum": 0, "maximum": 1},
        "loop": {"type": "boolean"},
    },
    "required": ["input"],
    "additionalProperties": False,
}


def test_defaults_fill_only_missing_properties() -> None:
    assert with_defaults({"input": "a"}, SCHEMA) == {"input": "a", "scale": 2}
    assert with_defaults({"input": "a", "scale": 4}, SCHEMA) == {"input": "a", "scale": 4}


def test_valid_params_have_no_problems() -> None:
    assert validate({"input": "a.mp4", "scale": 3, "strength": 0.5, "loop": True}, SCHEMA) == []
    assert validate({"input": "a", "scale": 2.0}, SCHEMA) == []


@pytest.mark.parametrize(
    ("params", "problem"),
    [
        ({}, "input: is required"),
        ({"input": ""}, "input: must have at least 1 characters"),
        ({"input": "abcdef"}, "input: must have at most 5 characters"),
        ({"input": 3}, "input: must be of type string"),
        ({"input": "a", "scale": 5}, "scale: must be one of 2, 3, 4"),
        ({"input": "a", "scale": 2.5}, "scale: must be of type integer"),
        ({"input": "a", "scale": True}, "scale: must be of type integer"),
        ({"input": "a", "strength": -1}, "strength: must be >= 0"),
        ({"input": "a", "strength": 2}, "strength: must be <= 1"),
        ({"input": "a", "loop": 1}, "loop: must be of type boolean"),
        ({"input": "a", "other": 1}, "other: is not a known parameter"),
    ],
)
def test_each_problem_is_named(params: dict[str, Any], problem: str) -> None:
    assert validate(params, SCHEMA) == [problem]
