"""End-to-end GUI test: preload files, click 开始转换, capture the result."""
import ctypes
import glob
import os
import shutil
import subprocess
import sys
import time
from ctypes import wintypes

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from shot import capture  # noqa: E402

user32 = ctypes.windll.user32
WM_COMMAND = 0x0111
IDC_START = 1006

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "testdata", "gui_out")
FILES = ["sample.ncm", "plain.ncm", "sample.kgm", "sample.vpr", "broken.ncm"]


def main():
    try:
        user32.SetProcessDPIAware()
    except Exception:
        pass

    if os.path.isdir(OUT):
        shutil.rmtree(OUT)

    args = [os.path.join(ROOT, "MusicUnlock.exe"), "-o", OUT]
    args += [os.path.join(ROOT, "testdata", f) for f in FILES]
    proc = subprocess.Popen(args)

    hwnd = None
    for _ in range(40):
        time.sleep(0.25)
        hwnd = user32.FindWindowW("MusicUnlockWnd", None)
        if hwnd:
            break
    if not hwnd:
        proc.kill()
        print("WINDOW NOT FOUND")
        return 1

    time.sleep(0.8)
    user32.SendMessageW(hwnd, WM_COMMAND, IDC_START, 0)

    deadline = time.time() + 30
    while time.time() < deadline:
        time.sleep(0.5)
        if len(glob.glob(os.path.join(OUT, "*.mp3"))) >= 4:
            break
    time.sleep(1.2)

    capture(hwnd, os.path.join(ROOT, "build", "gui_result.png"))
    proc.kill()

    got = sorted(os.path.basename(p) for p in glob.glob(os.path.join(OUT, "*")))
    print("output dir:", OUT)
    for g in got:
        print("  ", g, os.path.getsize(os.path.join(OUT, g)))
    return 0 if len(got) >= 4 else 1


if __name__ == "__main__":
    sys.exit(main())
