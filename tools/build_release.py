"""Builds the Windows release files of Mikser Studio from the package build (cmake --preset package, then
cmake --build --preset package) into build/dist:

- MikserStudio-<version>-windows-x64.zip: the program with everything it needs from MSYS2
  (tools/msys2_packages.py) and THIRD-PARTY-NOTICES.txt, unpacked and run anywhere;
- MikserStudio-<version>-windows-x64-setup.exe: the same files in an installer (tools/installer.iss),
  built with Inno Setup 7's ISCC.exe, found through ISCC or in Program Files;
- MikserStudio-<version>-third-party-notices.md: THIRD-PARTY-NOTICES.md, to read without the app.

It fails unless THIRD-PARTY-NOTICES.md covers exactly what ships: when the program or a bundled plugin
imports an MSYS2 library that PROGRAM_LIBRARIES in tools/msys2_packages.py does not list, when what it
copies is not what tools/notices.py walks, or when the committed notices are not what tools/notices.py
writes from this MSYS2. The AI service (ai/) is not part of a release yet; see docs/releasing.md.

Run it from the repository root, with MSYS2's packages installed as docs/development.md describes:
    python tools/build_release.py              build the release files
    python tools/build_release.py --version    print the version, the one way the workflows read it
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

from msys2_packages import ENVIRONMENT, PROGRAM_LIBRARIES, copied_files, imports, msys2_root, runtime
from notices import NOTICES, render

NAME = "MikserStudio"
"""The product's name without spaces, as release files are named."""
BUILD = Path("build/package")
CONFIGURE = "run cmake --preset package"
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
        raise SystemExit(f"No package build in {BUILD}: {CONFIGURE}, then cmake --build --preset package.")
    # Without it the release would run as Mikser Studio Dev (src/core/include/mikserstudio/core/Instance.h).
    if "MIKSERSTUDIO_INSTALLED_APP:BOOL=ON" not in (BUILD / "CMakeCache.txt").read_text(encoding="utf-8"):
        raise SystemExit(f"The build in {BUILD} would run as Mikser Studio Dev: {CONFIGURE}, then build it again.")
    shutil.rmtree(STAGE.parent, ignore_errors=True)
    subprocess.run(["cmake", "--install", str(BUILD), "--prefix", str(STAGE)], check=True)


def check_notices(programs: list[Path], root: Path) -> list[Path]:
    """The libraries the program and plugins need, once THIRD-PARTY-NOTICES.md is known to cover them: every
    MSYS2 library they import is in PROGRAM_LIBRARIES, so tools/notices.py, which walks from that list
    without a build, walks to the same libraries; and the committed notices are what it writes now."""
    available = {p.name.lower() for p in (root / ENVIRONMENT / "bin").glob("*.dll")}
    listed = {name.lower() for name in PROGRAM_LIBRARIES}
    for program in programs:
        unlisted = sorted(n for n in imports(program) if n.lower() in available and n.lower() not in listed)
        if unlisted:
            raise SystemExit(
                f"{program.name} imports {', '.join(unlisted)}, which PROGRAM_LIBRARIES in tools/msys2_packages.py "
                "does not list: add them there, then run python tools/notices.py and commit THIRD-PARTY-NOTICES.md."
            )
    shipped = runtime(programs, root)
    if shipped.packages != runtime(root=root).packages:
        raise SystemExit("What the release copies differs from what tools/notices.py walks: report this as a bug.")
    if NOTICES.read_text(encoding="utf-8") != render():
        raise SystemExit(
            f"{NOTICES} does not match this MSYS2's packages: run python tools/notices.py and commit {NOTICES}."
        )
    return shipped.libraries


def add_runtime() -> None:
    """The libraries from MSYS2 the program and the plugins need, and MLT's and Qt's files."""
    root = msys2_root()
    plugins = sorted((STAGE / "bin/plugins").rglob("*.dll"))
    for library in check_notices([PROGRAM, *plugins], root):
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
    parser = argparse.ArgumentParser(description="Build the Windows release files.")
    parser.add_argument("--version", action="store_true", help="print the version in CMakeLists.txt and stop")
    if parser.parse_args().version:
        print(version())
        return 0
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
