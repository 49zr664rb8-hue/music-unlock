#include "util.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

const wchar_t *mu_path_filename(const wchar_t *path)
{
    const wchar_t *p;
    const wchar_t *last;

    if (!path)
        return L"";
    p = path;
    last = path;
    while (*p) {
        if (*p == L'\\' || *p == L'/')
            last = p + 1;
        p++;
    }
    return last;
}

void mu_path_ext_lower(const wchar_t *path, wchar_t *ext, size_t cch)
{
    const wchar_t *p;
    const wchar_t *dot = NULL;
    const wchar_t *fn;
    size_t i = 0;

    if (!ext || cch == 0)
        return;
    ext[0] = 0;
    if (!path)
        return;

    fn = mu_path_filename(path);
    p = path;
    while (*p) {
        if (*p == L'.')
            dot = p;
        p++;
    }
    if (!dot || dot < fn)
        return;

    while (dot[i] && i + 1 < cch) {
        wchar_t c = dot[i];
        if (c >= L'A' && c <= L'Z')
            c = (wchar_t)(c - L'A' + L'a');
        ext[i] = c;
        i++;
    }
    ext[i] = 0;
}

void mu_stem(const wchar_t *path, wchar_t *stem, size_t cch)
{
    const wchar_t *fn;
    size_t i = 0;

    if (!stem || cch == 0)
        return;
    stem[0] = 0;
    fn = mu_path_filename(path);
    while (fn[i] && i + 1 < cch) {
        if (fn[i] == L'.')
            break;
        stem[i] = fn[i];
        i++;
    }
    stem[i] = 0;
}

int mu_file_exists(const wchar_t *path)
{
    DWORD attr;

    if (!path)
        return 0;
    attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

int mu_mkdir_recursive(const wchar_t *dir)
{
    wchar_t buf[1024];
    size_t i;
    size_t n;

    if (!dir || !*dir)
        return 0;
    n = wcslen(dir);
    if (n >= sizeof(buf) / sizeof(buf[0]))
        return 0;
    wcscpy(buf, dir);

    for (i = 0; i <= n; i++) {
        if (buf[i] == L'\\' || buf[i] == L'/' || buf[i] == 0) {
            wchar_t saved = buf[i];
            buf[i] = 0;
            if (i > 0 && !(i == 2 && buf[1] == L':')) {
                DWORD attr = GetFileAttributesW(buf);
                if (attr == INVALID_FILE_ATTRIBUTES) {
                    if (!CreateDirectoryW(buf, NULL))
                        return 0;
                } else if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
                    return 0;
                }
            }
            buf[i] = saved;
            if (saved == 0)
                break;
        }
    }
    return 1;
}

int mu_read_head(const wchar_t *path, unsigned char *buf, size_t n, size_t *got)
{
    FILE *f;
    size_t r;

    if (got)
        *got = 0;
    if (!path || !buf)
        return 0;
    f = _wfopen(path, L"rb");
    if (!f)
        return 0;
    r = fread(buf, 1, n, f);
    fclose(f);
    if (got)
        *got = r;
    return 1;
}

static int b64val(unsigned char c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return -1;
}

int mu_b64_decode(const unsigned char *in, size_t inlen, unsigned char **out, size_t *outlen)
{
    unsigned char *buf;
    size_t cap;
    size_t o = 0;
    int quad[4];
    int qn = 0;
    size_t i;

    if (outlen)
        *outlen = 0;
    if (!in || !out)
        return 0;

    cap = (inlen / 4 + 1) * 3;
    buf = (unsigned char *)malloc(cap);
    if (!buf)
        return 0;

    for (i = 0; i < inlen; i++) {
        unsigned char c = in[i];
        int v;
        if (c == '=')
            break;
        v = b64val(c);
        if (v < 0)
            continue;
        quad[qn++] = v;
        if (qn == 4) {
            unsigned int w = (unsigned int)((quad[0] << 18) | (quad[1] << 12) | (quad[2] << 6) | quad[3]);
            if (o + 3 > cap)
                break;
            buf[o++] = (unsigned char)((w >> 16) & 0xff);
            buf[o++] = (unsigned char)((w >> 8) & 0xff);
            buf[o++] = (unsigned char)(w & 0xff);
            qn = 0;
        }
    }
    if (qn == 2) {
        unsigned int w = (unsigned int)((quad[0] << 18) | (quad[1] << 12));
        if (o + 1 <= cap)
            buf[o++] = (unsigned char)((w >> 16) & 0xff);
    } else if (qn == 3) {
        unsigned int w = (unsigned int)((quad[0] << 18) | (quad[1] << 12) | (quad[2] << 6));
        if (o + 2 <= cap) {
            buf[o++] = (unsigned char)((w >> 16) & 0xff);
            buf[o++] = (unsigned char)((w >> 8) & 0xff);
        }
    }

    *out = buf;
    if (outlen)
        *outlen = o;
    return 1;
}

void mu_join_path(wchar_t *dst, size_t cch, const wchar_t *dir, const wchar_t *name)
{
    size_t n;

    if (!dst || cch == 0)
        return;
    dst[0] = 0;
    if (dir && *dir) {
        wcsncpy(dst, dir, cch - 1);
        dst[cch - 1] = 0;
        n = wcslen(dst);
        if (n > 0 && dst[n - 1] != L'\\' && dst[n - 1] != L'/') {
            if (n + 1 < cch) {
                dst[n] = L'\\';
                dst[n + 1] = 0;
            }
        }
    }
    if (name && *name)
        wcsncat(dst, name, cch - wcslen(dst) - 1);
}

unsigned long long mu_file_size(const wchar_t *path)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;

    if (!path || !GetFileAttributesExW(path, GetFileExInfoStandard, &fad))
        return 0;
    return ((unsigned long long)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
}

void mu_human_size(unsigned long long bytes, wchar_t *buf, size_t cch)
{
    if (!buf || cch == 0)
        return;
    if (bytes >= (1024ull * 1024ull * 1024ull))
        _snwprintf(buf, cch, L"%.2f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    else if (bytes >= (1024ull * 1024ull))
        _snwprintf(buf, cch, L"%.2f MB", (double)bytes / (1024.0 * 1024.0));
    else if (bytes >= 1024ull)
        _snwprintf(buf, cch, L"%.1f KB", (double)bytes / 1024.0);
    else
        _snwprintf(buf, cch, L"%llu B", bytes);
    buf[cch - 1] = 0;
}
