/* Console front-end: also the harness used for automated testing. */

#include "core.h"
#include "util.h"
#include "aes.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static void out(const wchar_t *fmt, ...)
{
    wchar_t buf[2048];
    char utf8[8192];
    va_list ap;

    va_start(ap, fmt);
    _vsnwprintf(buf, 2048, fmt, ap);
    va_end(ap);
    buf[2047] = 0;

    if (WideCharToMultiByte(CP_UTF8, 0, buf, -1, utf8, sizeof(utf8), NULL, NULL) > 0)
        fputs(utf8, stdout);
    fflush(stdout);
}

static void progress_cb(void *ud, int percent, const wchar_t *stage)
{
    (void)ud;
    (void)percent;
    (void)stage;
}

static void aes_test(void)
{
    unsigned char key[16];
    unsigned char in[16];
    unsigned char enc[16];
    unsigned char dec[16];
    int i;

    for (i = 0; i < 16; i++) {
        key[i] = (unsigned char)i;
        in[i] = (unsigned char)(i * 0x11);
    }
    mu_aes128_ecb_encrypt(key, in, enc, 16);
    mu_aes128_ecb_decrypt(key, enc, dec, 16);

    out(L"AES-ENC ");
    for (i = 0; i < 16; i++)
        out(L"%02x", enc[i]);
    out(L"\nAES-DEC ");
    for (i = 0; i < 16; i++)
        out(L"%02x", dec[i]);
    out(L"\n");
}

static int run_one(const wchar_t *path, const wchar_t *outdir)
{
    MuJob job;
    wchar_t err[512];

    memset(&job, 0, sizeof(job));
    wcsncpy(job.input, path, MU_PATH - 1);
    wcsncpy(job.outdir, outdir, MU_PATH - 1);

    if (mu_convert(&job, progress_cb, NULL, err, 512)) {
        out(L"[OK]   %s -> %s\n", mu_path_filename(path), mu_path_filename(job.output));
        return 0;
    }
    out(L"[FAIL] %s : %s\n", mu_path_filename(path), err);
    return 1;
}

int wmain(int argc, wchar_t **argv)
{
    int i;
    int rc = 0;
    wchar_t outdir[MU_PATH];

    SetConsoleOutputCP(CP_UTF8);

    if (argc >= 2 && wcscmp(argv[1], L"--aes-test") == 0) {
        aes_test();
        return 0;
    }

    if (argc < 2) {
        out(L"MusicUnlock CLI\n");
        out(L"  unlock-cli.exe [-o 输出目录] <文件...>\n");
        out(L"  unlock-cli.exe --aes-test\n");
        return 2;
    }

    {
        wchar_t ff[MU_PATH];
        if (mu_ffmpeg_path(ff, MU_PATH))
            out(L"ffmpeg: %s\n", ff);
        else
            out(L"ffmpeg: 未找到（FLAC/OGG 等格式将无法转码）\n");
    }

    GetCurrentDirectoryW(MU_PATH, outdir);

    for (i = 1; i < argc; i++) {
        if (wcscmp(argv[i], L"-o") == 0 && i + 1 < argc) {
            wcsncpy(outdir, argv[++i], MU_PATH - 1);
            outdir[MU_PATH - 1] = 0;
            continue;
        }
        if (wcscmp(argv[i], L"--aes-test") == 0)
            continue;
        if (run_one(argv[i], outdir) != 0)
            rc = 1;
    }
    return rc;
}
