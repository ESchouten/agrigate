"""Build the robot drivers with their C APIs (core/) into a library next to this package.

    python -m agrigate.build_core          # needs a C++17 compiler (g++/clang++)

In it: every driver (drivers/<name>/src) with what they share (core/session).
"""
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
LIBNAME = {"darwin": "libagrigate_core.dylib", "win32": "agrigate_core.dll"}.get(sys.platform, "libagrigate_core.so")


def folders() -> list:
    """The C APIs, what the drivers share, and each driver."""
    return [ROOT / "core", ROOT / "core" / "session", *sorted((ROOT / "drivers").glob("*/src"))]


def sources() -> list:
    """Everything the library is built from (to rebuild when one of them changed)."""
    files = [f for d in folders() for f in list(d.glob("*.cpp")) + list(d.glob("*.h"))]
    return files + [p for p in [ROOT / "drivers" / "secrets.h"] if p.exists()]


def build(out: Path = HERE / LIBNAME) -> Path:
    cxx = "clang++" if sys.platform == "darwin" else "g++"
    flags = ["-std=c++17", "-O2", "-fPIC", "-fvisibility=hidden", "-pthread"]
    flags += ["-dynamiclib"] if sys.platform == "darwin" else ["-shared"]
    if sys.platform == "win32":   # MinGW: its runtime inside the DLL, so Python finds no missing libstdc++/libgcc
        flags += ["-static"]
    includes = [arg for d in folders() for arg in ("-I", str(d))]
    part = out.with_name(f"{out.name}.{os.getpid()}")   # whole or not at all: another process may be loading or building it
    subprocess.check_call([cxx, *flags, *includes, *(str(f) for f in sources() if f.suffix == ".cpp"), "-o", str(part)])
    os.replace(part, out)
    return out


if __name__ == "__main__":
    print(build())
