"""Build the preserved OTA harness against the firmware's current handlers."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


version_dir = Path(__file__).resolve().parents[1]
source_dir = version_dir / "source"
image = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else version_dir / "SDP-Minimal-Clean64.bin"
source = (source_dir / "src" / "main.cpp").read_text(encoding="utf-8")
harness = (source_dir / "tests" / "ota_harness.cpp").read_text(encoding="utf-8")


def section(start, end):
    first = source.index(start)
    return source[first:source.index(end, first)]


for marker, text in (
    ("// CYCLE_IMPLEMENTATION", section("constexpr int CYCLE_ADDR", "// Every deliberate reboot")),
    ("// OTA_IMPLEMENTATION", section("FirmwareValidation firmware;", "// ---------------------------- WiFi")),
):
    if harness.count(marker) != 1:
        raise RuntimeError(f"Expected exactly one {marker} marker")
    harness = harness.replace(marker, text)

if not image.is_file():
    raise SystemExit(f"Firmware image not found: {image}")
compiler = shutil.which("cl") or shutil.which("g++") or shutil.which("clang++")
if not compiler:
    raise SystemExit("A C++17 compiler (cl, g++, or clang++) is required")

with tempfile.TemporaryDirectory(prefix="clean64-ota-") as temp:
    temp_dir = Path(temp)
    cpp = temp_dir / "ota.cpp"
    exe = temp_dir / ("ota.exe" if sys.platform == "win32" else "ota")
    cpp.write_text(harness, encoding="utf-8")
    include = source_dir / "include"
    if Path(compiler).name.lower() in ("cl", "cl.exe"):
        command = [compiler, "/nologo", "/EHsc", "/std:c++17", f"/I{include}", str(cpp), f"/Fe:{exe}"]
    else:
        command = [compiler, "-std=c++17", "-I", str(include), str(cpp), "-o", str(exe)]
    subprocess.run(command, cwd=temp_dir, check=True)
    subprocess.run([str(exe), str(image)], cwd=temp_dir, check=True)
