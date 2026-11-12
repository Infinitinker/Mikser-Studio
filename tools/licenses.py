"""Checks the licenses of everything a Windows release of Mikser Studio ships from MSYS2
(tools/msys2_packages.py) against the allow list. Mikser Studio is under the GPL-3.0-or-later, so a
shipped package must be usable under a license that can be combined with it: one of the alternatives
of its SPDX expression (joined by OR) must name only licenses on ALLOWED, or the package must be in
EXCEPTIONS, with the reason. It names every package that fails, and then exits with 1.

Run it from the repository root, with MSYS2's packages installed as docs/development.md describes:
    python tools/licenses.py
"""

import re
import sys
from collections import defaultdict

from msys2_packages import runtime

ALLOWED: frozenset[str] = frozenset(
    {
        # Permissive licenses, which the GPL-3.0 can take in.
        "0BSD",
        "Apache-2.0",
        "BSD-2-Clause",
        "BSD-3-Clause",
        "BSD-3-Clause-Clear",
        "BSL-1.0",
        "CC0-1.0",
        "FTL",
        "ICU",
        "ISC",
        "MIT",
        "WTFPL",
        "Zlib",
        # Copyleft licenses that allow, or are, the GPL-3.0.
        "GPL-2.0-or-later",
        "GPL-3.0-only",
        "GPL-3.0-or-later",
        "LGPL-2.0-or-later",
        "LGPL-2.1-or-later",
        "LGPL-3.0-only",
        "LGPL-3.0-or-later",
        "MPL-2.0",
        # The packages' manuals and images: shipped with the libraries' licenses, not combined with code.
        "CC-BY-4.0",
        "GFDL-1.3-no-invariants-only",
        "GFDL-1.3-or-later",
    }
)
"""The SPDX identifiers of the licenses that can be combined with the GPL-3.0-or-later, as far as a
release ships them. CLAUDE.md's "Open source" lists them too."""

_UPSTREAM = "MSYS2's package installs no license text (a gap in THIRD-PARTY-NOTICES.md); the project releases it"
EXCEPTIONS: dict[str, str] = {
    # MSYS2 names no SPDX license, or none with a version: each checked in the text it installs.
    "bzip2": "bzip2-1.0.6, a BSD-style license (its LICENSE).",
    "fontconfig": "MIT-style licenses and public-domain code (its COPYING).",
    "gsm": "A permissive license: any use, with the notice kept (its license.txt).",
    "libpng": "libpng-2.0, a permissive license (its LICENSE).",
    "libjpeg-turbo": "IJG, BSD-3-Clause and Zlib (its LICENSE.md).",
    "libb2": "CC0-1.0 (its LICENSE).",
    "opusfile": "BSD-3-Clause (its LICENSE).",
    "speex": "BSD-3-Clause (its LICENSE).",
    "libmodplug": "Public domain (its LICENSE).",
    "jbigkit": "GPL-2.0-or-later: its COPYING says version 2 or any later version.",
    "libdatrie": "LGPL-2.1-or-later (its COPYING).",
    "libgme": "LGPL-2.1-or-later (its license.txt).",
    "twolame": "LGPL-2.1-or-later (its COPYING).",
    "libplacebo": "LGPL-2.1-or-later (its LICENSE).",
    "pango": "LGPL-2.0-or-later: its COPYING is the GNU Library General Public License, version 2.",
    "libtasn1": "The library is LGPL-2.1-or-later (its COPYING.LESSERv2); the GPL-3.0 covers its tools.",
    "flac": (
        "libFLAC is BSD-3-Clause (its COPYING.Xiph); MSYS2's LGPL, GPL and FDL are for its tools and manual, "
        "under the LGPL-2.1-or-later, the GPL-2.0-or-later and the GFDL."
    ),
    # MSYS2's package installs no license text: the license as the project releases it.
    "libvorbis": f"BSD-3-Clause. {_UPSTREAM} under the Xiph BSD license.",
    "libx264": f"GPL-2.0-or-later. {_UPSTREAM} under the GPL-2.0-or-later.",
    "x265": f"GPL-2.0-or-later. {_UPSTREAM} under the GPL-2.0-or-later.",
    "xvidcore": f"GPL-2.0-or-later. {_UPSTREAM} under the GPL-2.0-or-later.",
    "libmad": f"GPL-2.0-or-later. {_UPSTREAM} under the GPL-2.0-or-later.",
    "lame": f"LGPL-2.0-or-later. {_UPSTREAM} under the LGPL-2.0-or-later.",
    "gmp": f"LGPL-3.0-or-later OR GPL-2.0-or-later. {_UPSTREAM} under both, at the user's choice.",
    "libcaca": f"WTFPL. {_UPSTREAM} under the WTFPL.",
    "opencore-amr": f"Apache-2.0. {_UPSTREAM} under the Apache-2.0.",
    # MSYS2 joins licenses offered as alternatives with AND.
    "kddockwidgets": "GPL-2.0-only OR GPL-3.0-only (both texts in its license folder): used under the GPL-3.0-only.",
    "qt6-base": (
        "Qt offers each module under the LGPL-3.0-only, the GPL-2.0-only or the GPL-3.0-only, and is used "
        "under the GPL-3.0-only. MSYS2 lists every file's license: AFL-2.1 is the freedesktop.org MIME "
        "database, also under the GPL-2.0-or-later; the rest is third-party code Qt ships under licenses it "
        "documents as compatible (doc.qt.io, Licenses Used in Qt)."
    ),
    "qt6-declarative": "LGPL-3.0-only, GPL-2.0-only or GPL-3.0-only: used under the GPL-3.0-only.",
    "qt6-svg": "LGPL-3.0-only, GPL-2.0-only or GPL-3.0-only: used under the GPL-3.0-only.",
    "qt6-websockets": "LGPL-3.0-only, GPL-2.0-only or GPL-3.0-only: used under the GPL-3.0-only.",
}
"""Packages allowed whatever their metadata says, each with the reason, after checking the license by
hand: those whose metadata names no SPDX license, or joins alternatives with AND."""
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


def alternatives(expression: str) -> list[set[str]]:
    """The licenses of each alternative of an SPDX expression: AND binds tighter than OR, and pacman's
    commas join like AND. An expression with parentheses counts as one alternative naming all of them."""
    if "(" in expression:
        return [licenses_of(expression)]
    return [licenses_of(part) for part in re.split(r"\s+OR\s+", expression)]


def is_allowed(expression: str) -> bool:
    """Whether one alternative of the expression names only allowed licenses."""
    return any(names and names <= ALLOWED for names in alternatives(expression))


def main() -> int:
    by_license: dict[str, list[str]] = defaultdict(list)
    failing = []
    for package in runtime().packages:
        for name in licenses_of(package.license) or {"(none stated)"}:
            by_license[name].append(package.name)
        if package.name not in EXCEPTIONS and not is_allowed(package.license):
            failing.append(f"{package.name}: {package.license or '(none stated)'}")
    print("Licenses a release ships, with their packages:")
    for name in sorted(by_license):
        print(f"  {name}: {', '.join(sorted(by_license[name]))}")
    if failing:
        print("\nNot allowed with the GPL-3.0-or-later (add the license to ALLOWED, or the package to EXCEPTIONS):")
        for line in failing:
            print(f"  {line}")
        return 1
    print("\nEvery license can be combined with the GPL-3.0-or-later.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
