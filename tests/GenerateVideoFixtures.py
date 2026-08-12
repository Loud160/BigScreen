"""Generate tiny H.264 MP4 fixtures for the host FrameDecoder tests."""

from __future__ import annotations

import pathlib
import shutil
import subprocess
import sys


destination = pathlib.Path(sys.argv[1])
destination.mkdir(parents=True, exist_ok=True)
ffmpeg = shutil.which("ffmpeg")
if not ffmpeg:
    raise SystemExit("ffmpeg CLI is required for decoder fixture generation")


def generate(name: str, size: str, rate: int) -> None:
    subprocess.run(
        [
            ffmpeg, "-hide_banner", "-loglevel", "error", "-y",
            "-f", "lavfi", "-i", f"testsrc2=size={size}:rate={rate}:duration=1.5",
            "-an", "-c:v", "libx264", "-pix_fmt", "yuv420p",
            "-movflags", "+faststart", str(destination / name),
        ],
        check=True,
    )


generate("landscape.mp4", "96x54", 10)
generate("portrait.mp4", "54x96", 12)
print(f"Generated decoder fixtures in {destination}")
