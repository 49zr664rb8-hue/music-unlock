#!/usr/bin/env python3
"""Build synthetic .ncm / .kgm / .vpr containers around a real MP3 so the C
decoder can be verified byte-for-byte. Uses pycryptodome for AES so the test is
independent of the C implementation."""

import base64
import json
import os
import struct
import subprocess
import sys

from Crypto.Cipher import AES

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
WORK = os.path.join(ROOT, "testdata")

NCM_CORE_KEY = b"hzHRAmso5kInbaxW"
NCM_META_KEY = bytes([0x23, 0x31, 0x34, 0x6C, 0x6A, 0x6B, 0x5F, 0x21,
                      0x5C, 0x5D, 0x26, 0x30, 0x55, 0x3C, 0x27, 0x28])

# --- KuGou tables (same constants as the C decoder) -------------------------

T1 = [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,33,1,97,1,33,1,225,1,33,1,97,1,33,1,
210,35,2,2,66,66,2,2,194,194,2,2,66,66,2,2,211,211,2,3,99,67,99,3,227,195,227,3,99,67,99,3,
148,180,148,101,4,4,4,4,132,132,132,132,4,4,4,4,149,149,149,149,4,5,37,5,229,133,165,133,229,5,37,5,
214,182,150,182,214,39,6,6,198,198,134,134,198,198,6,6,215,215,151,151,215,215,6,7,231,199,231,135,231,199,231,7,
24,56,24,120,24,56,24,233,8,8,8,8,8,8,8,8,25,25,25,25,25,25,25,25,8,9,41,9,105,9,41,9,
218,58,26,58,90,58,26,58,218,43,10,10,74,74,10,10,219,219,27,27,91,91,27,27,219,219,10,11,107,75,107,11,
156,188,156,124,28,60,28,124,156,188,156,109,12,12,12,12,157,157,157,157,29,29,29,29,157,157,157,157,12,13,45,13,
222,190,158,190,222,62,30,62,222,190,158,190,222,47,14,14,223,223,159,159,223,223,31,31,223,223,159,159,223,223,14,15,
0,32,0,96,0,32,0,224,0,32,0,96,0,32,0,241]

T2 = [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,35,1,103,1,35,1,239,1,35,1,103,1,35,1,
223,33,2,2,70,70,2,2,206,206,2,2,70,70,2,2,222,222,2,3,101,71,101,3,237,207,237,3,101,71,101,3,
157,191,157,99,4,4,4,4,140,140,140,140,4,4,4,4,156,156,156,156,4,5,39,5,235,141,175,141,235,5,39,5,
219,189,159,189,219,37,6,6,202,202,142,142,202,202,6,6,218,218,158,158,218,218,6,7,233,203,233,143,233,203,233,7,
25,59,25,127,25,59,25,231,8,8,8,8,8,8,8,8,24,24,24,24,24,24,24,24,8,9,43,9,111,9,43,9,
215,57,27,57,95,57,27,57,215,41,10,10,78,78,10,10,214,214,26,26,94,94,26,26,214,214,10,11,109,79,109,11,
149,183,149,123,29,63,29,123,149,183,149,107,12,12,12,12,148,148,148,148,28,28,28,28,148,148,148,148,12,13,47,13,
211,181,151,181,211,61,31,61,211,181,151,181,211,45,14,14,210,210,150,150,210,210,30,30,210,210,150,150,210,210,14,15,
0,34,0,102,0,34,0,238,0,34,0,102,0,34,0,254]

TV2 = [184,213,61,178,233,175,120,140,131,51,113,81,118,160,205,55,47,62,53,141,169,190,152,183,231,140,34,206,90,97,223,104,
105,137,254,165,182,222,169,119,252,200,189,189,229,109,62,90,54,239,105,78,190,225,233,102,28,243,217,2,182,242,18,155,
68,208,111,185,53,137,182,70,109,115,130,6,105,193,237,215,133,194,48,223,162,98,190,121,45,98,98,61,13,126,190,72,
137,35,2,160,228,213,117,81,50,2,83,253,22,58,33,59,22,15,195,178,187,179,226,186,58,61,19,236,246,1,69,132,
165,112,15,147,73,12,100,205,49,213,204,76,7,1,158,0,26,35,144,191,136,30,59,171,166,62,196,115,71,16,126,59,
94,188,227,0,132,255,9,212,224,137,15,91,88,112,79,251,101,216,92,83,27,211,200,198,191,239,152,176,80,79,15,234,
229,131,88,140,40,44,132,103,205,208,158,71,219,39,80,202,244,99,99,232,151,127,27,75,12,194,193,33,76,204,88,245,
148,82,163,243,211,224,104,244,0,35,243,94,10,123,147,221,171,18,178,19,232,132,215,167,159,15,50,76,85,29,4,54,
82,220,3,243,249,78,66,233,61,97,239,124,182,179,147,80]

VPRK = [37,223,232,166,117,30,117,14,47,128,243,45,184,182,227,17,0]

assert len(T1) == 272 and len(T2) == 272 and len(TV2) == 272


def mask_v1(o):
    v = 0
    while o >= 0x11:
        v ^= T1[o % 272]
        o >>= 4
        v ^= T2[o % 272]
        o >>= 4
    return v


def kgm_encrypt(data, filekey, is_vpr=False):
    """Inverse of the decoder: c = mask ^ key ^ T(plain)  (T is an involution,
    but T(a^b) != T(a)^T(b), so the transform must be applied before the XORs)."""
    out = bytearray(len(data))
    for i, b in enumerate(data):
        m = TV2[i % 272] ^ mask_v1(i >> 4)
        x = b
        if is_vpr:
            x ^= VPRK[i % 17]
        x ^= (x & 0x0F) << 4
        x ^= m ^ filekey[i % 17]
        out[i] = x & 0xFF
    return bytes(out)


def ncm_keystream(key):
    """NetEase keystream: ordinary RC4 KSA, but the byte is read straight out
    of the key box instead of using the RC4 PRGA:
        j = (i + 1) & 0xff
        ks[i] = S[(S[j] + S[(S[j] + j) & 0xff]) & 0xff]
    """
    s = list(range(256))
    j = 0
    for i in range(256):
        j = (j + s[i] + key[i % len(key)]) & 0xFF
        s[i], s[j] = s[j], s[i]
    ks = []
    for i in range(256):
        jj = (i + 1) & 0xFF
        a = s[jj]
        b = s[(a + jj) & 0xFF]
        ks.append(s[(a + b) & 0xFF])
    return bytes(ks)


def pkcs7(data):
    pad = 16 - (len(data) % 16)
    return data + bytes([pad]) * pad


def build_ncm(mp3, meta, cover):
    rc4key = b"testkey_0123456789"
    plain = b"neteasecloudmusic" + rc4key
    enc = AES.new(NCM_CORE_KEY, AES.MODE_ECB).encrypt(pkcs7(plain))
    keyblob = bytes(b ^ 0x64 for b in enc)

    if meta:
        mplain = b"music:" + json.dumps(meta, ensure_ascii=False).encode("utf-8")
        menc = AES.new(NCM_META_KEY, AES.MODE_ECB).encrypt(pkcs7(mplain))
        mb64 = base64.b64encode(menc)
        mblob = bytes(b ^ 0x63 for b in (b"163 key(Don't modify):" + mb64))
    else:
        mblob = b""

    ks = ncm_keystream(rc4key)
    audio = bytes(b ^ ks[i % 256] for i, b in enumerate(mp3))

    out = bytearray()
    out += b"CTENFDAM"
    out += b"\x01\x70"
    out += struct.pack("<I", len(keyblob)) + keyblob
    out += struct.pack("<I", len(mblob)) + mblob
    out += struct.pack("<I", 0)          # crc
    out += b"\x01"
    out += struct.pack("<I", len(cover)) + cover
    out += audio
    return bytes(out)


def build_kgm(mp3, is_vpr=False):
    header_len = 0x400
    filekey = bytes(range(0x40, 0x50)) + b"\x00"
    header = bytearray(header_len)
    header[0:16] = bytes.fromhex("7cd532eb86027f4ba8afa68e0fff9914")
    struct.pack_into("<I", header, 0x10, header_len)
    header[0x1C:0x2C] = filekey[:16]
    return bytes(header) + kgm_encrypt(mp3, filekey, is_vpr)


def main():
    os.makedirs(WORK, exist_ok=True)
    src = os.path.join(WORK, "source.mp3")
    if not os.path.exists(src):
        subprocess.check_call([
            "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
            "-f", "lavfi", "-i", "sine=frequency=440:duration=4",
            "-codec:a", "libmp3lame", "-b:a", "128k",
            "-id3v2_version", "0", "-write_xing", "0", src,
        ])
    mp3 = open(src, "rb").read()
    print("source mp3: %d bytes" % len(mp3))

    cover = b""
    cover_path = os.path.join(WORK, "cover.jpg")
    if os.path.exists(cover_path):
        cover = open(cover_path, "rb").read()

    meta = {
        "musicName": "测试曲目 Test Song",
        "artist": [["测试歌手", "12345"]],
        "album": "测试专辑",
        "format": "mp3",
    }

    open(os.path.join(WORK, "sample.ncm"), "wb").write(build_ncm(mp3, meta, cover))
    open(os.path.join(WORK, "plain.ncm"), "wb").write(build_ncm(mp3, None, b""))
    open(os.path.join(WORK, "sample.kgm"), "wb").write(build_kgm(mp3, False))
    open(os.path.join(WORK, "sample.vpr"), "wb").write(build_kgm(mp3, True))

    # FLAC payload: exercises the ffmpeg transcode path
    flac_src = os.path.join(WORK, "source.flac")
    if not os.path.exists(flac_src):
        subprocess.check_call([
            "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
            "-f", "lavfi", "-i", "sine=frequency=660:duration=4",
            "-codec:a", "flac", flac_src,
        ])
    flac = open(flac_src, "rb").read()
    open(os.path.join(WORK, "lossless.ncm"), "wb").write(build_ncm(flac, meta, cover))

    # awkward file name: spaces, parentheses and Chinese characters
    awkward = os.path.join(WORK, "测试 歌曲 (Live).ncm")
    open(awkward, "wb").write(build_ncm(mp3, meta, cover))

    # a deliberately broken file: must be rejected, never produce output
    open(os.path.join(WORK, "broken.ncm"), "wb").write(b"NOTANCMFILE" + b"\x00" * 200)
    print("test containers written to", WORK)


if __name__ == "__main__":
    main()
