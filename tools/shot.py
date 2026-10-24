"""Launch the GUI and capture the window contents with PrintWindow."""
import ctypes
import subprocess
import sys
import time
from ctypes import wintypes

from PIL import Image

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [
        ("biSize", wintypes.DWORD),
        ("biWidth", wintypes.LONG),
        ("biHeight", wintypes.LONG),
        ("biPlanes", wintypes.WORD),
        ("biBitCount", wintypes.WORD),
        ("biCompression", wintypes.DWORD),
        ("biSizeImage", wintypes.DWORD),
        ("biXPelsPerMeter", wintypes.LONG),
        ("biYPelsPerMeter", wintypes.LONG),
        ("biClrUsed", wintypes.DWORD),
        ("biClrImportant", wintypes.DWORD),
    ]


class BITMAPINFO(ctypes.Structure):
    _fields_ = [("bmiHeader", BITMAPINFOHEADER), ("bmiColors", wintypes.DWORD * 3)]


def capture(hwnd, path):
    rect = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    w = rect.right - rect.left
    h = rect.bottom - rect.top
    if w <= 0 or h <= 0:
        raise RuntimeError("bad window size %dx%d" % (w, h))

    hdc = user32.GetWindowDC(hwnd)
    memdc = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    gdi32.SelectObject(memdc, bmp)

    ok = user32.PrintWindow(hwnd, memdc, 2)  # PW_RENDERFULLCONTENT
    if not ok:
        user32.PrintWindow(hwnd, memdc, 0)

    bi = BITMAPINFO()
    bi.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bi.bmiHeader.biWidth = w
    bi.bmiHeader.biHeight = -h
    bi.bmiHeader.biPlanes = 1
    bi.bmiHeader.biBitCount = 32
    bi.bmiHeader.biCompression = 0

    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(memdc, bmp, 0, h, buf, ctypes.byref(bi), 0)

    img = Image.frombuffer("RGB", (w, h), buf, "raw", "BGRX", 0, 1)
    img.save(path)

    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(memdc)
    user32.ReleaseDC(hwnd, hdc)
    return w, h


def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else "MusicUnlock.exe"
    out = sys.argv[2] if len(sys.argv) > 2 else "build/ui.png"

    try:
        ctypes.windll.user32.SetProcessDPIAware()
    except Exception:
        pass

    proc = subprocess.Popen([exe])
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

    time.sleep(1.0)
    w, h = capture(hwnd, out)
    print("captured %dx%d -> %s" % (w, h, out))
    proc.kill()
    return 0


if __name__ == "__main__":
    sys.exit(main())
