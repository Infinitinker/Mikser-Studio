"""Checks the licenses of everything a Windows release of Mikser Studio ships from MSYS2
(tools/msys2_packages.py) against the allow list: every license named in a package's SPDX expression
must be on ALLOWED, or the package must be in EXCEPTIONS, with the reason. It names every package that
fails, and then exits with 1.

TODO: Mikser Studio has no license yet, so ALLOWED is empty and the check fails, listing every license
a release ships. Once the license is chosen, list here the licenses compatible with it (CLAUDE.md's
"Open source" writes them down too); see "Licenses" in docs/releasing.md.

Run it from the repository root, with MSYS2's packages installed as docs/development.md describes:
    python tools/licenses.py
"""

import re
import sys
from collections import defaultdict

from msys2_packages import runtime

ALLOWED: frozenset[str] = frozenset()
"""The SPDX identifiers of the licenses a shipped package may have. Empty until the project's own license
is chosen."""
EXCEPTIONS: dict[str, str] = {}
"""Packages allowed whatever their license says, each with the reason: those whose metadata names no
SPDX license, after checking the license by hand."""
SPDX_ID = re.compile(r"[A-Za-z0-9.+:-]+")
OPERATORS = {"AND", "OR", "WITH"}


def licenses_of(expression: str) -> set[str]:
    """The license identifiers an SPDX expression names, without the exceptions added WITH them. pacman's
    prefixes go: "documentation:spdx:GPL-3.0-or-later", the license of a package's manual, counts as
    GPL-3.0-or-later."""
    words = SPDX_ID.findall(expression)
    names = set()
    for index, word in enumerate(words):
        if word in OPERATORS or (index and words[index - 1] == "WITH"):
            continue
        names.add(word.rsplit("spdx:", 1)[-1])
    return names


def main() -> int:
    by_license: dict[str, list[str]] = defaultdict(list)
    failing = []
    for package in runtime().packages:
        names = licenses_of(package.license) or {"(none stated)"}
        for name in names:
            by_license[name].append(package.name)
        if package.name not in EXCEPTIONS and not names <= ALLOWED:
            failing.append(f"{package.name}: {package.license or '(none stated)'}")
    print("Licenses a release ships, with their packages:")
    for name in sorted(by_license):
        print(f"  {name}: {', '.join(sorted(by_license[name]))}")
    if not ALLOWED:
        print(
            "\nMikser Studio has no license yet, so nothing is allowed: choose it, then list the compatible "
            "licenses in ALLOWED in tools/licenses.py."
        )
        return 1
    if failing:
        print("\nNot allowed:")
        for line in failing:
            print(f"  {line}")
        return 1
    print("\nEvery license is allowed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
