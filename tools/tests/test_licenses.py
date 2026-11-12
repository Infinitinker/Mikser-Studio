"""licenses.py: reading SPDX expressions, and which of them the allow list lets a release ship."""

import importlib.util
import sys
from pathlib import Path
from types import ModuleType

TOOLS = Path(__file__).parents[1]


def load() -> ModuleType:
    sys.path.insert(0, str(TOOLS))  # licenses.py imports msys2_packages from its own folder.
    try:
        spec = importlib.util.spec_from_file_location("licenses", TOOLS / "licenses.py")
        assert spec and spec.loader
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module
    finally:
        sys.path.remove(str(TOOLS))


licenses = load()


def test_exceptions_and_pacman_prefixes_are_dropped() -> None:
    assert licenses.licenses_of("GPL-3.0-or-later WITH GCC-exception-3.1 AND GFDL-1.3-or-later") == {
        "GPL-3.0-or-later",
        "GFDL-1.3-or-later",
    }
    assert licenses.licenses_of("LGPL-2.1-or-later AND documentation:spdx:GPL-3.0-or-later") == {
        "LGPL-2.1-or-later",
        "GPL-3.0-or-later",
    }


def test_one_allowed_alternative_is_enough() -> None:
    assert licenses.is_allowed("LGPL-2.1-or-later OR MPL-1.1")
    assert licenses.is_allowed("BSD-3-Clause OR GPL-2.0-or-later")
    assert not licenses.is_allowed("MPL-1.1 OR GPL-2.0-only")


def test_and_binds_tighter_than_or() -> None:
    assert licenses.is_allowed("MPL-1.1 AND MIT OR Zlib")
    assert not licenses.is_allowed("MIT AND GPL-2.0-only OR MPL-1.1")
    assert licenses.alternatives("GPL3, LGPL") == [{"GPL3", "LGPL"}]


def test_licenses_that_cannot_join_the_gpl_3_are_refused() -> None:
    for expression in ("GPL-2.0-only", "AFL-2.1", "MPL-1.1", "custom", ""):
        assert not licenses.is_allowed(expression)


def test_every_exception_has_a_reason() -> None:
    assert all(reason.strip().endswith(".") for reason in licenses.EXCEPTIONS.values())
