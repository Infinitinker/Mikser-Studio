"""Builds the Windows release files of Mikser Studio from the release build (cmake --preset release, then
cmake --build --preset release) into build/dist:

- MikserStudio-<version>-windows-x64.zip: the program with everything it needs from MSYS2
  (tools/msys2_packages.py) and THIRD-PARTY-NOTICES.txt, unpacked and run anywhere;
- MikserStudio-<version>-windows-x64-setup.exe: the same files in an installer (tools/installer.iss),
  built with Inno Setup 7's ISCC.exe, found through ISCC or in Program Files;
- MikserStudio-<version>-third-party-notices.md: THIRD-PARTY-NOTICES.md, to read without the app.

It fails when the program imports an MSYS2 library that PROGRAM_LIBRARIES in tools/msys2_packages.py
does not list, so THIRD-PARTY-NOTICES.md always covers what ships. The AI service (ai/) is not part of
a release yet; see docs/releasing.md.

Run it from the repository root, with MSYS2's packages installed as docs/development.md describes:
    python tools/build_release.py
"""

import os
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

from msys2_packages import ENVIRONMENT, PROGRAM_LIBRARIES, copied_files, imports, msys2_root, runtime

NAME = "MikserStudio"
"""The product's name without spaces, as release files are named."""
BUILD = Path("build/release")
STAGE = Path("build/stage") / NAME
DIST = Path("build/dist")
PROGRAM = STAGE / "bin/mikser-studio.exe"
QT_CONF = """[Paths]
Prefix = .
Plugins = .
QmlImports = qml
"""
"""Where Qt finds its plugins and QML modules in a release, relative to bin: MSYS2's Qt would look for them
under share/qt6, as it is installed there."""
DEFAULT_ISCC = Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "Inno Setup 7/ISCC.exe"


def version() -> str:
    """The version in CMakeLists.txt's project()."""
    text = Path("CMakeLists.txt").read_text(encoding="utf-8")
    found = re.search(r"project\(MikserStudio\s+VERSION\s+(\d+\.\d+\.\d+)", text)
    if not found:
        raise SystemExit("CMakeLists.txt's project() has no VERSION.")
    return found.group(1)


def install() -> None:
    """The program and the bundled plugins, as CMake installs them."""
    if not (BUILD / "build.ninja").is_file():
        raise SystemExit(
            f"No release build in {BUILD}: run cmake --preset release, then cmake --build --preset release."
        )
    shutil.rmtree(STAGE.parent, ignore_errors=True)
    subprocess.run(["cmake", "--install", str(BUILD), "--prefix", str(STAGE)], check=True)


def add_runtime() -> None:
    """The libraries from MSYS2 the program needs, and MLT's and Qt's files."""
    root = msys2_root()
    available = {p.name.lower() for p in (root / ENVIRONMENT / "bin").glob("*.dll")}
    listed = {name.lower() for name in PROGRAM_LIBRARIES}
    unlisted = sorted(name for name in imports(PROGRAM) if name.lower() in available and name.lower() not in listed)
    if unlisted:
        raise SystemExit(
            f"{PROGRAM.name} imports {', '.join(unlisted)}, which PROGRAM_LIBRARIES in tools/msys2_packages.py does "
            "not list: add them there, then run python tools/notices.py and commit THIRD-PARTY-NOTICES.md."
        )
    plugins = sorted((STAGE / "bin/plugins").rglob("*.dll"))
    for library in runtime([PROGRAM, *plugins], root).libraries:
        shutil.copy2(library, STAGE / "bin" / library.name)
    for source, destination in copied_files(root / ENVIRONMENT):
        target = STAGE / destination
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
    (STAGE / "bin/qt.conf").write_text(QT_CONF, encoding="utf-8")


def add_texts() -> None:
    """The notices, under the name a release ships them with."""
    shutil.copy2("THIRD-PARTY-NOTICES.md", STAGE / "THIRD-PARTY-NOTICES.txt")


def make_zip(name: str) -> Path:
    path = DIST / f"{name}-windows-x64.zip"
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for file in sorted(STAGE.rglob("*")):
            if file.is_file():
                archive.write(file, Path(NAME) / file.relative_to(STAGE))
    return path


def make_notices(name: str) -> Path:
    path = DIST / f"{name}-third-party-notices.md"
    shutil.copy2("THIRD-PARTY-NOTICES.md", path)
    return path


def make_installer(release: str) -> Path:
    iscc = Path(os.environ.get("ISCC", DEFAULT_ISCC))
    if not iscc.is_file():
        raise SystemExit(f"Inno Setup 7's ISCC.exe was not found at {iscc}: install it, or set ISCC to where it is.")
    numeric = ".".join([*release.split("."), "0"])
    subprocess.run(
        [
            str(iscc),
            "/Qp",
            f"/DAppVersion={release}",
            f"/DNumericVersion={numeric}",
            f"/DSourceDir={STAGE.resolve()}",
            f"/DOutputDir={DIST.resolve()}",
            "tools/installer.iss",
        ],
        check=True,
    )
    return DIST / f"{NAME}-{release}-windows-x64-setup.exe"


def main() -> int:
    release = version()
    install()
    add_runtime()
    add_texts()
    shutil.rmtree(DIST, ignore_errors=True)
    DIST.mkdir(parents=True)
    for path in (make_zip(f"{NAME}-{release}"), make_installer(release), make_notices(f"{NAME}-{release}")):
        print(f"Built {path} ({path.stat().st_size / 1e6:.1f} MB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
