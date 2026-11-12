"""Checks that the C++ code includes the modules' headers only in the directions "Import directions"
in docs/architecture.md writes down:

- a module includes the public headers (<mikserstudio/<module>/...>) only of itself and of the
  modules it depends on, as ALLOWED lists them, and the plugin ABI (<mikserstudio/plugin_api.h>)
  only where ALLOWED names sdk;
- a header in quotes ("Logging.h") is the module's own: it is found next to the including file, in
  the module's src/ or in its public include folder. A module's tests may include the private
  headers of the module they test, and the tests' shared support files.

Run it from the repository root:
    python tools/check_includes.py
It names every include that goes the wrong way, or that it cannot find, and then exits with 1.
"""

import re
import subprocess
import sys
from pathlib import PurePosixPath
from typing import NamedTuple

MODULES = ("sdk", "core", "engine", "api", "services", "ui", "app")
"""The modules, each with its folder: sdk/ and src/<module>/."""
ALLOWED = {
    "sdk": set(),
    "core": {"sdk"},
    "engine": {"core"},
    "api": {"core"},
    "services": {"core"},
    "ui": {"core"},
    "app": {"core", "engine", "api", "services", "ui"},
    "plugins": {"sdk"},
}
"""The modules each module may include from, besides itself. plugins/ holds the bundled plugins, which
see only the plugin ABI."""
SOURCES = (".cpp", ".h", ".hpp")
INCLUDE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]')
PUBLIC = re.compile(r"^mikserstudio/(?:(?P<module>[a-z]+)/|(?P<abi>plugin_api\.h)$)")


class Problem(NamedTuple):
    """An include that goes the wrong way, or that cannot be found."""

    path: PurePosixPath
    line: int
    include: str
    reason: str

    def __str__(self) -> str:
        return f"{self.path}:{self.line}: {self.include} {self.reason}"


def module_of(path: PurePosixPath) -> tuple[str, bool] | None:
    """The module a file belongs to, and whether it is one of that module's tests; None outside them."""
    parts = path.parts
    if parts[0] == "sdk":
        return "sdk", False
    if parts[0] == "plugins":
        return "plugins", False
    if parts[0] in ("src", "tests") and len(parts) > 2 and parts[1] in MODULES:
        return parts[1], parts[0] == "tests"
    return None


def own_folders(module: str, is_test: bool, path: PurePosixPath) -> list[PurePosixPath]:
    """Where a quoted include of this file may be found."""
    if module == "sdk":
        return [path.parent, PurePosixPath("sdk/include")]
    if module == "plugins":
        return [path.parent]
    folders = [path.parent, PurePosixPath(f"src/{module}/src"), PurePosixPath(f"src/{module}")]
    if is_test:
        folders.append(PurePosixPath(f"tests/{module}"))
    return folders


def check_file(path: PurePosixPath, text: str, tracked: set[PurePosixPath]) -> list[Problem]:
    found = module_of(path)
    if found is None:
        return []
    module, is_test = found
    allowed = ALLOWED[module] | {module}
    problems = []
    for number, line in enumerate(text.splitlines(), start=1):
        match = INCLUDE.match(line)
        if not match:
            continue
        bracket, name = match.groups()
        written = f"<{name}>" if bracket == "<" else f'"{name}"'
        public = PUBLIC.match(name)
        if public:
            target = public["module"] or "sdk"
            if target not in MODULES:
                problems.append(Problem(path, number, written, "names no module"))
            elif target not in allowed:
                problems.append(Problem(path, number, written, f"goes from {module} to {target}"))
            continue
        if bracket == "<":
            continue  # A system, Qt, MLT or other library header.
        if not any(folder / name in tracked for folder in own_folders(module, is_test, path)):
            problems.append(Problem(path, number, written, f"is not one of {module}'s own headers"))
    return problems


def main() -> int:
    listed = subprocess.run(["git", "ls-files"], capture_output=True, text=True, check=True).stdout
    tracked = {PurePosixPath(line) for line in listed.splitlines()}
    problems = []
    for path in sorted(tracked):
        if path.suffix in SOURCES:
            problems += check_file(path, open(path, encoding="utf-8").read(), tracked)
    for problem in problems:
        print(problem)
    if problems:
        print(f'{len(problems)} include(s) go the wrong way; see "Import directions" in docs/architecture.md.')
        return 1
    print("Every include goes the right way.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
