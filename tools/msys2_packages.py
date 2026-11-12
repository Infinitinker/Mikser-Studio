"""What a Windows release of Mikser Studio takes from MSYS2: the run-time files it copies (COPIED: MLT's
modules and data, Qt's plugins and QML modules) and every library they and the program need, followed
through the libraries' imports, with the packages they come from and those packages' licenses, read from
MSYS2's own package database. tools/build_release.py copies these files, tools/licenses.py checks their licenses and
tools/notices.py writes their notices.

MSYS2 is found where MSYS2_ROOT points, else where cygpath says in an MSYS2 shell, else in C:/msys64.
"""

import os
import re
import shutil
import subprocess
from collections.abc import Iterable
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

INSTALL = "docs/development.md lists them"
PREFIX = "mingw-w64-ucrt-x86_64-"
"""The UCRT64 environment's package names start with this."""
ENVIRONMENT = PurePosixPath("ucrt64")
MLT_MODULES = (
    "core",
    "avformat",
    "qt6",
    "plus",
    "resample",
    "sox",
    "rubberband",
    "rnnoise",
    "ladspa",
    "rtaudio",
    "sdl2",
    "xml",
)
"""The MLT modules a release ships (lib/mlt/libmlt<module>.dll): the services playback needs and the
optional ones docs/development.md installs, the audio outputs, and MLT's XML. Kdenlive's, frei0r's and
the other modules' services are not used."""
COPIED = {
    **{f"lib/mlt/libmlt{module}.dll": "lib/mlt" for module in MLT_MODULES},
    "share/mlt/**/*": "share/mlt",
    "share/qt6/plugins/platforms/*": "bin/platforms",
    "share/qt6/plugins/styles/*": "bin/styles",
    "share/qt6/plugins/imageformats/*": "bin/imageformats",
    "share/qt6/plugins/iconengines/*": "bin/iconengines",
    "share/qt6/plugins/tls/*": "bin/tls",
    "share/qt6/plugins/networkinformation/*": "bin/networkinformation",
    "share/qt6/qml/QtQuick/**/*": "bin/qml/QtQuick",
    "share/qt6/qml/QtQml/**/*": "bin/qml/QtQml",
}
"""The files of the UCRT64 environment a release copies besides the libraries, and the folder of the
release they go to, keeping the path under the pattern's fixed part: MLT's modules and data, where the
engine looks for them (lib/mlt and share/mlt next to bin), the Qt plugins a desktop app loads, and the
QML modules the panels import (QtQuick, with its Controls and Layouts, and QtQml), where Qt looks for
them next to the program."""
PROGRAM_LIBRARIES = (
    "Qt6Core.dll",
    "Qt6Gui.dll",
    "Qt6HttpServer.dll",
    "Qt6Network.dll",
    "Qt6Qml.dll",
    "Qt6QuickControls2.dll",
    "Qt6QuickWidgets.dll",
    "Qt6WebSockets.dll",
    "Qt6Widgets.dll",
    "libgcc_s_seh-1.dll",
    "libkddockwidgets-qt62.dll",
    "libmlt++-7.dll",
    "libmlt-7.dll",
    "libstdc++-6.dll",
    "libwinpthread-1.dll",
)
"""The MSYS2 libraries mikser-studio.exe and the bundled plugins import. tools/build_release.py fails when
the program or a plugin it built imports one that is not listed, so the notices, which are written
without a build, never miss a library."""
LOADED_LIBRARIES = (
    "SDL3.dll",
    "libcrypto-3-x64.dll",
    "libssl-3-x64.dll",
)
"""The MSYS2 libraries that copied files load by name while running, which their import tables don't show:
MSYS2's SDL2.dll (sdl2-compat, which MLT's sdl2 module imports) loads SDL3.dll, and Qt's OpenSSL backend
(tls/qopensslbackend.dll) loads libssl and libcrypto. Without SDL3.dll, MLT stops start-up with an error
box."""
LICENSE_FILE = re.compile(r"^ucrt64/share/(licenses/.+[^/]|doc/.*/(licen[cs]e|copying|copyright|notice)[^/]*)$", re.I)
"""A license text among a package's files: anything in share/licenses, or a license file in share/doc."""


@dataclass(frozen=True)
class Package:
    """An installed MSYS2 package."""

    name: str
    """Without the environment's prefix: "qt6-base"."""
    url: str
    license: str
    """Its SPDX expression, or the names pacman gives when it has none."""
    files: tuple[PurePosixPath, ...]
    """Its files, relative to the MSYS2 root."""

    @property
    def license_files(self) -> list[PurePosixPath]:
        """The license texts it installs."""
        return [f for f in self.files if LICENSE_FILE.match(str(f))]


def msys2_root() -> Path:
    """MSYS2_ROOT; else, in an MSYS2 shell, the root cygpath gives (Git Bash has a cygpath too, whose root
    holds no pacman database); else C:/msys64."""
    if root := os.environ.get("MSYS2_ROOT"):
        return Path(root)
    if cygpath := shutil.which("cygpath"):
        found = subprocess.run([cygpath, "-m", "/"], capture_output=True, text=True, check=False).stdout.strip()
        if found and (Path(found) / "var/lib/pacman/local").is_dir():
            return Path(found)
    return Path("C:/msys64")


def _fields(text: str) -> dict[str, list[str]]:
    """The fields of a pacman database file: %NAME% and the lines under it up to a blank one."""
    fields: dict[str, list[str]] = {}
    current: list[str] | None = None
    for line in text.splitlines():
        if line.startswith("%") and line.endswith("%"):
            current = fields.setdefault(line.strip("%"), [])
        elif not line:
            current = None
        elif current is not None:
            current.append(line)
    return fields


def installed(root: Path) -> list[Package]:
    """Every installed UCRT64 package."""
    database = root / "var/lib/pacman/local"
    if not database.is_dir():
        raise SystemExit(f"MSYS2's package database was not found in {database}: set MSYS2_ROOT to where MSYS2 is.")
    packages = []
    for folder in sorted(database.iterdir()):
        if folder.name.startswith(PREFIX):
            desc = _fields((folder / "desc").read_text(encoding="utf-8"))
            files = _fields((folder / "files").read_text(encoding="utf-8")).get("FILES", [])
            packages.append(
                Package(
                    name=desc["NAME"][0].removeprefix(PREFIX),
                    url=(desc.get("URL") or [""])[0],
                    license=" AND ".join(desc.get("LICENSE", [])).removeprefix("spdx:"),
                    files=tuple(PurePosixPath(f) for f in files if not f.endswith("/")),
                )
            )
    return packages


def _number(data: bytes, at: int, size: int) -> int:
    """A little-endian unsigned number of size bytes."""
    return int.from_bytes(data[at : at + size], "little")


def imports(library: Path) -> list[str]:
    """The names of the DLLs a Windows program or library imports, read from its PE import table."""
    data = library.read_bytes()
    pe = _number(data, 0x3C, 4)
    sections_count = _number(data, pe + 6, 2)
    optional_size = _number(data, pe + 20, 2)
    optional = pe + 24
    directories = optional + (112 if _number(data, optional, 2) == 0x20B else 96)
    import_rva = _number(data, directories + 8, 4)
    headers = optional + optional_size
    sections = [
        tuple(_number(data, headers + 40 * i + 8 + 4 * field, 4) for field in range(4)) for i in range(sections_count)
    ]

    def offset(rva: int) -> int:
        for virtual_size, address, raw_size, raw_offset in sections:
            if address <= rva < address + max(virtual_size, raw_size):
                return rva - address + raw_offset
        raise ValueError(f"{library}: an address outside its sections")

    names = []
    if import_rva:
        entry = offset(import_rva)
        while name_rva := _number(data, entry + 12, 4):
            start = offset(name_rva)
            names.append(data[start : data.index(b"\0", start)].decode("ascii"))
            entry += 20
    return names


def copied_files(environment: Path) -> list[tuple[Path, PurePosixPath]]:
    """The COPIED files of the environment, each with its path in the release."""
    files = []
    for pattern, destination in COPIED.items():
        fixed = PurePosixPath(*[part for part in PurePosixPath(pattern).parent.parts if "*" not in part])
        found = sorted(f for f in environment.glob(pattern) if f.is_file())
        if not found:
            raise SystemExit(f"Nothing matches {environment / pattern}: install MSYS2's packages ({INSTALL}).")
        files += [(f, PurePosixPath(destination) / f.relative_to(environment / fixed).as_posix()) for f in found]
    return files


@dataclass(frozen=True)
class Runtime:
    """What a release takes from MSYS2."""

    libraries: list[Path]
    """The DLLs from the environment's bin folder that the program and the copied folders need."""
    packages: list[Package]
    """The packages these files come from, sorted by name."""


def runtime(programs: Iterable[Path] = (), root: Path | None = None) -> Runtime:
    """The libraries that the program (PROGRAM_LIBRARIES), the COPIED files (with LOADED_LIBRARIES) and
    any other programs and libraries given need from MSYS2, followed through their imports, and the
    packages they come from. DLLs that are not in MSYS2's bin folder belong to Windows."""
    root = root or msys2_root()
    environment = root / ENVIRONMENT
    owners = {f: p for p in installed(root) for f in p.files}
    bin_folder = environment / "bin"
    available = {path.name.lower(): path for path in bin_folder.glob("*.dll")}

    copied = [source for source, _ in copied_files(environment)]
    missing = [name for name in (*PROGRAM_LIBRARIES, *LOADED_LIBRARIES) if name.lower() not in available]
    if missing:
        raise SystemExit(f"Not in {bin_folder}: {', '.join(missing)}. Install MSYS2's packages ({INSTALL}).")
    libraries = [available[name.lower()] for name in (*PROGRAM_LIBRARIES, *LOADED_LIBRARIES)]
    seen = set(libraries)
    waiting = [*libraries, *(p for p in [*programs, *copied] if p.suffix.lower() in (".exe", ".dll"))]
    while waiting:
        current = waiting.pop()
        for name in imports(current):
            library = available.get(name.lower())
            if library and library not in seen:
                seen.add(library)
                libraries.append(library)
                waiting.append(library)

    files = (PurePosixPath(f.relative_to(root).as_posix()) for f in [*libraries, *copied])
    used = {owners[f] for f in files if f in owners}
    return Runtime(sorted(libraries), sorted(used, key=lambda p: p.name))
