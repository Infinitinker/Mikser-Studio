"""Writes THIRD-PARTY-NOTICES.md, the license notices of everything a Windows release of Mikser Studio
ships from MSYS2: the libraries the program and the copied MLT and Qt files need, grouped by the
package they come from (tools/msys2_packages.py). A release ships the file as THIRD-PARTY-NOTICES.txt.

Each package gets the license texts it installs. A package that installs none gets the standard text
of each license its SPDX expression names, as other packages install it; one that has neither must be
listed in NO_TEXT, with the reason, until its text is added to tools/licenses/<package>.txt. Versions
are left out: MSYS2 moves on continually, while the texts rarely change.

Run it from the repository root, with MSYS2's packages installed as docs/development.md describes,
and commit the file whenever the libraries a release ships change:
    python tools/notices.py           write THIRD-PARTY-NOTICES.md
    python tools/notices.py --check   fail unless it is up to date
"""

import argparse
import re
import sys
from pathlib import Path, PurePosixPath

from msys2_packages import Package, installed, msys2_root, runtime

NOTICES = Path("THIRD-PARTY-NOTICES.md")
SHIPPED_AS = "THIRD-PARTY-NOTICES.txt"
KEPT_LICENSES = Path("tools/licenses")
"""License texts that MSYS2's packages don't install, as <package>.txt, taken from each project's
source."""
NO_TEXT = {
    "gmp": 'LGPL-3.0-or-later or GPL-2.0-or-later ("LGPL3 AND GPL" in MSYS2\'s metadata); no text installed.',
    "icu": "ICU, whose text carries ICU's own copyright notice; MSYS2's package installs none.",
    "lame": "LGPL-2.0-or-later (\"LGPL\" in MSYS2's metadata); MSYS2's package installs no text.",
    "libass": "ISC, whose text carries libass's own copyright notice; MSYS2's package installs none.",
    "libcaca": "WTFPL and LGPL (\"LGPL\" in MSYS2's metadata); MSYS2's package installs no text.",
    "libmad": "GPL-2.0-or-later (\"GPL\" in MSYS2's metadata); MSYS2's package installs no text.",
    "libtheora": "BSD-3-Clause, whose text carries Xiph's copyright notice; MSYS2's package installs none.",
    "libvorbis": "BSD-3-Clause (custom), whose text carries Xiph's copyright notice; MSYS2's package installs none.",
    "libvpx": "BSD-3-Clause, whose text carries the WebM project's copyright notice; MSYS2's package installs none.",
    "libx264": "GPL-2.0-or-later (custom in MSYS2's metadata); MSYS2's package installs no text.",
    "opencore-amr": "Apache-2.0 (\"Apache\" in MSYS2's metadata), with its NOTICE; MSYS2's package installs no text.",
    "p11-kit": "BSD-3-Clause, whose text carries p11-kit's own copyright notice; MSYS2's package installs none.",
    "x265": "GPL-2.0-or-later (\"GPL\" in MSYS2's metadata); MSYS2's package installs no text.",
    "xvidcore": "GPL-2.0-or-later (\"GPL\" in MSYS2's metadata); MSYS2's package installs no text.",
}
"""TODO: packages whose license text is still to be added to tools/licenses: their license needs the
project's own copyright notice, or MSYS2 names it in a way that doesn't say which text applies. Each is
listed in the notices with its source, so the gap shows; a package that installs a text or gets one in
tools/licenses must leave this list."""
SPDX_ID = re.compile(r"[A-Za-z0-9.+-]+")
NOT_LICENSES = {"AND", "OR", "WITH", "custom", "documentation", "spdx"}

HEADER = f"""# Third-party notices

Mikser Studio's Windows release ships with the third-party libraries listed here, which it takes from
[MSYS2](https://www.msys2.org)'s UCRT64 packages: the C++ run time, Qt, KDDockWidgets, MLT, FFmpeg and
the libraries they need. Their licenses require these notices to come with every copy. Each library's
source is at its project's address below, and MSYS2's build of it at
https://github.com/msys2/MINGW-packages.

This file is written by `python tools/notices.py` from the packages a release is built with; do not edit
it by hand. A release ships it as {SHIPPED_AS}.
"""


def spdx_texts(packages: list[Package], root: Path) -> dict[str, Path]:
    """The standard license texts that MSYS2's packages install as <SPDX id>.txt, by SPDX id."""
    texts: dict[str, Path] = {}
    for package in packages:
        for file in package.license_files:
            if file.suffix == ".txt" and file.parent.parent.name == "licenses":
                texts.setdefault(file.stem, root / file)
    return texts


def standard_text(license_id: str, texts: dict[str, Path]) -> Path | None:
    """The text of a license; GPL-3.0-or-later and GPL-3.0-only share one."""
    base = license_id.removesuffix("-or-later").removesuffix("-only").removesuffix("+")
    for candidate in (license_id, f"{base}-only", f"{base}-or-later", base):
        if candidate in texts:
            return texts[candidate]
    return None


def notice(package: Package, root: Path, texts: dict[str, Path], printed: dict[str, str]) -> tuple[str, bool]:
    """The section of one package, and whether it has every text it needs. A text printed before, as
    Qt's modules share theirs, is named rather than printed again; printed maps each text to where."""
    lines = [f"## {package.name} ({package.license or 'license not stated'})", ""]
    if package.url:
        lines += [f"Source: {package.url}", ""]
    kept = KEPT_LICENSES / f"{package.name}.txt"
    files: list[tuple[str, Path]] = [(str(PurePosixPath(*f.parts[-2:])), root / f) for f in package.license_files]
    if kept.is_file():
        files.append((kept.as_posix(), kept))
    complete = bool(files)
    if not files:
        ids = [i for i in SPDX_ID.findall(package.license) if i not in NOT_LICENSES and "-exception" not in i]
        found = [(i, standard_text(i, texts)) for i in ids]
        files = [(f"the standard text of {i}", path) for i, path in found if path]
        complete = bool(ids) and all(path for _, path in found)
    if not complete:
        lines += [f"Its license text is still to be added: {NO_TEXT.get(package.name, 'no text was found.')}", ""]
    for title, path in files:
        text = path.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n").strip("\n")
        if text in printed:
            lines += [f"### {title}", "", f"The same text as {printed[text]}.", ""]
            continue
        printed[text] = f"{title} under {package.name}"
        lines += [f"### {title}", "", "````text", text, "````", ""]
    return "\n".join(lines), complete


def render() -> str:
    root = msys2_root()
    packages = runtime().packages
    texts = spdx_texts(installed(root), root)
    sections = []
    unexplained = []
    printed: dict[str, str] = {}
    for package in packages:
        section, complete = notice(package, root, texts, printed)
        sections.append(section)
        if not complete and package.name not in NO_TEXT:
            unexplained.append(package.name)
    stale = sorted(set(NO_TEXT) - {p.name for p in packages})
    if unexplained or stale:
        problems = [f"no license text found for {', '.join(unexplained)}: add it to {KEPT_LICENSES}"] * bool(
            unexplained
        ) + [f"NO_TEXT lists packages a release no longer ships: {', '.join(stale)}"] * bool(stale)
        raise SystemExit("tools/notices.py: " + "; ".join(problems) + ".")
    return HEADER + "\n" + "\n".join(sections)


def main() -> int:
    parser = argparse.ArgumentParser(description="Write THIRD-PARTY-NOTICES.md.")
    parser.add_argument("--check", action="store_true", help="fail unless THIRD-PARTY-NOTICES.md is up to date")
    arguments = parser.parse_args()
    text = render()
    if arguments.check:
        current = NOTICES.read_text(encoding="utf-8") if NOTICES.is_file() else ""
        if current != text:
            print(f"{NOTICES} is out of date: run python tools/notices.py and commit it.")
            return 1
        print(f"{NOTICES} is up to date.")
        return 0
    NOTICES.write_text(text, encoding="utf-8", newline="\n")
    print(f"Wrote {NOTICES}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
