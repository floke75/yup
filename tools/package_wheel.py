"""
  ==============================================================================

   This file is part of the YUP library.
   Copyright (c) 2025 - kunitoki@gmail.com

   YUP is an open source library subject to open-source licensing.

   The code included in this file is provided under the terms of the ISC license
   http://www.isc.org/downloads/software-support-policy/isc-license. Permission
   to use, copy, modify, and/or distribute this software for any purpose with or
   without fee is hereby granted provided that the above copyright notice and
   this permission notice appear in all copies.

   YUP IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
   EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
   DISCLAIMED.

  ==============================================================================
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Iterable, Sequence


DEFAULT_BUILD_DIR = Path("build") / "wheel-release"
TARGET_NAME = "yup_rive_renderer"


class PackagingError(RuntimeError):
    """Raised when an expected build artefact cannot be produced."""


def run_command(command: Sequence[str], *, cwd: Path | None = None) -> None:
    """Execute *command* and raise if it fails."""

    print(f"[package_wheel] Running: {' '.join(command)}", flush=True)
    subprocess.run(command, cwd=cwd, check=True)


def configure_project(source_dir: Path, build_dir: Path) -> None:
    """Configure the CMake project for a Release build."""

    cmake_args = [
        "cmake",
        "-S",
        str(source_dir),
        "-B",
        str(build_dir),
        "-DCMAKE_BUILD_TYPE=Release",
    ]

    if sys.platform.startswith("win"):
        generator = os.environ.get("CMAKE_GENERATOR", "Visual Studio 17 2022")
        architecture = os.environ.get("CMAKE_GENERATOR_PLATFORM", "x64")
        cmake_args.extend(["-G", generator, "-A", architecture])

    build_dir.mkdir(parents=True, exist_ok=True)
    run_command(cmake_args)


def build_targets(build_dir: Path) -> None:
    """Build the renderer module in Release mode."""

    command = [
        "cmake",
        "--build",
        str(build_dir),
        "--target",
        TARGET_NAME,
    ]

    if sys.platform.startswith("win"):
        command.extend(["--config", "Release"])

    run_command(command)


def _iter_extension_candidates(build_dir: Path) -> Iterable[Path]:
    suffixes = [".pyd", ".so", ".dylib"]
    for suffix in suffixes:
        yield from build_dir.rglob(f"{TARGET_NAME}{suffix}")


def locate_extension(build_dir: Path) -> Path:
    """Return the compiled extension module inside *build_dir*."""

    for candidate in _iter_extension_candidates(build_dir):
        if candidate.name.startswith(TARGET_NAME):
            return candidate

    raise PackagingError(
        "Unable to locate the compiled yup_rive_renderer extension in the build directory."
    )


def copy_extension(module_path: Path, destination_root: Path) -> Path:
    """Copy *module_path* into the Python packaging tree."""

    destination_root.mkdir(parents=True, exist_ok=True)
    destination = destination_root / module_path.name
    print(f"[package_wheel] Copying {module_path} -> {destination}")
    shutil.copy2(module_path, destination)
    return destination


def build_wheel(python_dir: Path) -> None:
    """Invoke python -m build to produce the wheel."""

    command = [sys.executable, "-m", "build", "--wheel"]
    run_command(command, cwd=python_dir)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build a distributable wheel for yup_rive_renderer.")
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=DEFAULT_BUILD_DIR,
        help="CMake build directory to use (default: %(default)s)",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repo_root = Path(__file__).resolve().parents[1]
    python_dir = repo_root / "python"

    try:
        configure_project(repo_root, args.build_dir)
        build_targets(args.build_dir)
        module_path = locate_extension(args.build_dir)
        copy_extension(module_path, python_dir)
        build_wheel(python_dir)
    except (subprocess.CalledProcessError, PackagingError) as exc:
        print(f"[package_wheel] ERROR: {exc}", file=sys.stderr)
        return 1

    print("[package_wheel] Wheel build complete.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
