#!/usr/bin/env python3

import struct
import subprocess
import shutil
from pathlib import Path

FFMPEG = shutil.which("ffmpeg") or "/opt/homebrew/bin/ffmpeg"
FILES = {
    "opaque_actual": Path("/private/tmp/inputprep_opaque_actual0000.exr"),
    "opaque_reference": Path("/private/tmp/inputprep_opaque_reference0000.exr"),
    "embedded_actual": Path("/private/tmp/inputprep_embedded_actual0000.exr"),
    "embedded_reference": Path("/private/tmp/inputprep_embedded_reference0000.exr"),
}


def pixel(path: Path) -> tuple[float, float, float, float]:
    command = [
        FFMPEG,
        "-v",
        "error",
        "-i",
        str(path),
        "-vf",
        "crop=1:1:8:8",
        "-frames:v",
        "1",
        "-f",
        "rawvideo",
        "-pix_fmt",
        "gbrapf32le",
        "-",
    ]
    raw = subprocess.check_output(command)
    if len(raw) != 16:
        raise RuntimeError(f"unexpected pixel payload for {path}: {len(raw)} bytes")
    green, blue, red, alpha = struct.unpack("<ffff", raw)
    return red, green, blue, alpha


def assert_pixel(actual, expected, label):
    for channel, got, wanted in zip("RGBA", actual, expected):
        if abs(got - wanted) > 1e-5:
            raise AssertionError(
                f"{label}.{channel}: expected {wanted:.8f}, got {got:.8f}"
            )


for label, path in FILES.items():
    if not path.is_file():
        raise FileNotFoundError(f"missing {label} render: {path}")

opaque_actual = pixel(FILES["opaque_actual"])
opaque_reference = pixel(FILES["opaque_reference"])
embedded_actual = pixel(FILES["embedded_actual"])
embedded_reference = pixel(FILES["embedded_reference"])
assert_pixel(opaque_actual, opaque_reference, "opaque policy")
assert_pixel(embedded_actual, embedded_reference, "embedded policy")
if abs(opaque_actual[3] - 1.0) > 1e-5:
    raise AssertionError(f"opaque alpha: expected 1, got {opaque_actual[3]:.8f}")
if abs(embedded_actual[3] - 0.5) > 0.002:
    raise AssertionError(
        f"embedded alpha: expected 0.5, got {embedded_actual[3]:.8f}"
    )

print("INPUTPREP_COLOR_ALPHA_HOST_TEST_OK")
