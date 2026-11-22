"""Build the robot drivers with their C APIs (core/) into a library next to this package.

    python -m scraper_control.build_core          # needs a C++17 compiler (g++/clang++)

In it: one scraper driver (a folder in drivers/ with a scraper_c.cpp: $SCRAPER_DRIVER, or the
only one there) and every driver for a farm's server (core/robot/Drivers.cpp).
"""
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
LIBNAME = {"darwin": "libscraper_core.dylib", "win32": "scraper_core.dll"}.get(sys.platform, "libscraper_core.so")


def driver_src() -> Path:
    name = os.environ.get("SCRAPER_DRIVER")
    found = [f.parent for f in sorted((ROOT / "drivers").glob("*/src/scraper_c.cpp")) if not name or f.parent.parent.name == name]
    if len(found) != 1:
        raise FileNotFoundError("set SCRAPER_DRIVER to one of: " + ", ".join(d.parent.name for d in found))
    return found[0]


def folders() -> list:
    """The scraper driver, what the farm server drivers share, and each of those drivers."""
    servers = [d for d in sorted((ROOT / "drivers").glob("*/src")) if not (d / "scraper_c.cpp").exists()]
    return [driver_src(), ROOT / "core", ROOT / "core" / "robot", *servers]


def sources() -> list:
    """Everything the library is built from (to rebuild when one of them changed)."""
    files = [f for d in folders() for f in list(d.glob("*.cpp")) + list(d.glob("*.h"))]
    return files + [p for p in [ROOT / "drivers" / "secrets.h"] if p.exists()]


def build(out: Path = HERE / LIBNAME) -> Path:
    cxx = "clang++" if sys.platform == "darwin" else "g++"
    flags = ["-std=c++17", "-O2", "-fPIC", "-fvisibility=hidden", "-pthread"]
    flags += ["-dynamiclib"] if sys.platform == "darwin" else ["-shared"]
    includes = [arg for d in folders() for arg in ("-I", str(d))]
    subprocess.check_call([cxx, *flags, *includes, *(str(f) for f in sources() if f.suffix == ".cpp"), "-o", str(out)])
    return out


if __name__ == "__main__":
    print(build())
