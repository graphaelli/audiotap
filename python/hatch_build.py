"""Custom hatch build hook to compile and bundle libaudiotap.dylib into wheels."""

from __future__ import annotations

import platform
import subprocess
from pathlib import Path

from hatchling.builders.hooks.plugin.interface import BuildHookInterface


class CustomBuildHook(BuildHookInterface):
    def initialize(self, version, build_data):
        if self.target_name != "wheel":
            return

        repo_root = Path(self.root).resolve().parent
        dylib = repo_root / "build" / "libaudiotap.dylib"
        makefile = repo_root / "Makefile"

        # Build the C library if it hasn't been built yet
        if not dylib.exists() and makefile.exists():
            try:
                subprocess.check_call(
                    ["make", "-C", str(repo_root), "build/libaudiotap.dylib"]
                )
            except (subprocess.CalledProcessError, FileNotFoundError):
                # Can't build — produce a pure-Python wheel that requires
                # libaudiotap to be installed separately on the system.
                return

        if not dylib.exists():
            return

        # Bundle the dylib inside the wheel
        build_data["force_include"][str(dylib)] = "audiotap/libaudiotap.dylib"

        # Tag as a platform-specific macOS wheel (system tap needs 14.2+)
        machine = platform.machine()  # arm64 or x86_64
        build_data["tag"] = f"py3-none-macosx_14_0_{machine}"
