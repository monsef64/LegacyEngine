#!/usr/bin/env python3
"""Format first-party C++ and GLSL; never touch vendored/generated dependencies."""
import argparse
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
VERSION = "21.1.8"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="report drift without writing files")
    args = parser.parse_args()
    formatter = shutil.which("clang-format")
    if not formatter:
        parser.error(f"Install the pinned formatter: python -m pip install clang-format=={VERSION}")
    version = subprocess.check_output([formatter, "--version"], text=True)
    if f"version {VERSION}" not in version:
        parser.error(f"Expected clang-format {VERSION}, got {version.strip()}")
    sources = sorted(p for folder in ("src", "benchmarks") for p in (ROOT / folder).rglob("*")
                     if p.suffix in {".cpp", ".hpp"})
    shaders = sorted((ROOT / "data" / "shaders").glob("*"))
    mode = ["--dry-run", "--Werror"] if args.check else ["-i"]
    for files, extra in ((sources, []), (shaders, ["--assume-filename=shader.cpp"])):
        result = subprocess.run([formatter, "--style=file", *mode, *extra, *map(str, files)], cwd=ROOT)
        if result.returncode:
            return result.returncode
    return 0


if __name__ == "__main__":
    sys.exit(main())
