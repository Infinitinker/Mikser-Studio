"""check_includes.py: the import directions, the include style, and files in folders it does not know."""

import importlib.util
from pathlib import Path, PurePosixPath
from types import ModuleType


def load() -> ModuleType:
    path = Path(__file__).parents[1] / "check_includes.py"
    spec = importlib.util.spec_from_file_location("check_includes", path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


check = load()


def problems(path: str, text: str, tracked: tuple[str, ...] = ()) -> list[str]:
    files = {PurePosixPath(name) for name in tracked}
    return [str(problem) for problem in check.check_file(PurePosixPath(path), text, files)]


def test_a_module_includes_the_modules_it_depends_on() -> None:
    assert problems("src/ui/src/Shell.cpp", '#include "mikserstudio/core/CommandBus.h"\n') == []


def test_an_include_against_the_directions_is_named() -> None:
    assert problems("src/ui/src/Shell.cpp", '#include "mikserstudio/engine/MltRuntime.h"\n') == [
        'src/ui/src/Shell.cpp:1: "mikserstudio/engine/MltRuntime.h" goes from ui to engine'
    ]


def test_public_headers_take_quotes_and_the_plugin_abi_angle_brackets() -> None:
    assert problems("src/core/src/PluginHost.h", "#include <mikserstudio/plugin_api.h>\n") == []
    assert problems("src/ui/src/Shell.cpp", "#include <mikserstudio/core/CommandBus.h>\n") == [
        "src/ui/src/Shell.cpp:1: <mikserstudio/core/CommandBus.h> is a public header: include it in quotes"
    ]
    assert problems("src/core/src/PluginHost.h", '#include "mikserstudio/plugin_api.h"\n') == [
        'src/core/src/PluginHost.h:1: "mikserstudio/plugin_api.h" is the plugin ABI: include it in angle brackets'
    ]


def test_a_private_header_is_found_in_the_module() -> None:
    tracked = ("src/ui/src/Logging.h",)
    assert problems("src/ui/src/Shell.cpp", '#include "Logging.h"\n', tracked) == []
    assert problems("src/app/Studio.cpp", '#include "Logging.h"\n', tracked) == [
        'src/app/Studio.cpp:1: "Logging.h" is not one of app\'s own headers'
    ]


def test_the_end_to_end_tests_include_no_module() -> None:
    assert problems("tests/e2e/HeadlessTests.cpp", "#include <QProcess>\n") == []
    assert problems("tests/e2e/HeadlessTests.cpp", '#include "mikserstudio/core/CommandBus.h"\n') == [
        'tests/e2e/HeadlessTests.cpp:1: "mikserstudio/core/CommandBus.h" goes from e2e to core'
    ]


def test_a_file_in_a_folder_the_check_does_not_know_is_a_problem() -> None:
    for path in ("tests/benchmarks/Bench.cpp", "src/scripting/Lua.cpp", "tools/native/helper.cpp", "tests/Main.cpp"):
        assert problems(path, "#include <QString>\n") == [f"{path}: {check.UNKNOWN_FOLDER}"]
