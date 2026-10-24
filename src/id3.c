/* Writes an ID3v2.3 tag in front of an MP3 stream.
 * Text frames use UTF-16LE with a BOM, which car head units and Windows
 * players all understand; an APIC frame carries the cover art when present. */

#include "core.h"
#include "util.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef struct {
    unsigned char *buf;
    unsigned len;
    unsigned cap;
} Tagger;

static void put_be32(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)((v >> 24) & 0xff);
    p[1] = (unsigned char)((v >> 16) & 0xff);
    p[2] = (unsigned char)((v >> 8) & 0xff);
    p[3] = (unsigned char)(v & 0xff);
}

static void put_syncsafe(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)((v >> 21) & 0x7f);
    p[1] = (unsigned char)((v >> 14) & 0x7f);
    p[2] = (unsigned char)((v >> 7) & 0x7f);
    p[3] = (unsigned char)(v & 0x7f);
}

static int tg_reserve(Tagger *t, unsigned extra)
{
    unsigned ncap;
    unsigned char *nb;

    if (t->len + extra <= t->cap)
        return 1;
    ncap = t->cap ? t->cap : 4096;
    while (ncap < t->len + extra)
        ncap *= 2;
    nb = (unsigned char *)realloc(t->buf, ncap);
    if (!nb)
        return 0;
    t->buf = nb;
    t->cap = ncap;
    return 1;
}

static void tg_text(Tagger *t, const char *id, const wchar_t *text)
{
    size_t nchars;
    unsigned flen;

    if (!text || !*text)
        return;
    nchars = wcslen(text);
    if (nchars == 0 || nchars > 1000)
        return;
    flen = (unsigned)(1 + 2 + nchars * 2);
    if (!tg_reserve(t, 10 + flen))
        return;

    memcpy(t->buf + t->len, id, 4);
    put_be32(t->buf + t->len + 4, flen);
    t->buf[t->len + 8] = 0;
    t->buf[t->len + 9] = 0;
    t->buf[t->len + 10] = 1;              /* encoding: UTF-16 with BOM */
    t->buf[t->len + 11] = 0xFF;
    t->buf[t->len + 12] = 0xFE;
    memcpy(t->buf + t->len + 13, text, nchars * 2);
    t->len += 10 + flen;
}

static void tg_apic(Tagger *t, const unsigned char *data, size_t datalen)
{
    const char *mime;
    unsigned mlen;
    unsigned flen;
    unsigned dlen;

    if (!data || datalen == 0 || datalen > (8u << 20))
        return;

    if (datalen > 4 && data[0] == 0x89 && data[1] == 'P')
        mime = "image/png";
    else
        mime = "image/jpeg";
    mlen = (unsigned)strlen(mime) + 1;
    dlen = (unsigned)datalen;
    flen = 1 + mlen + 1 + 1 + dlen;        /* enc + mime + type + desc + data */

    if (!tg_reserve(t, 10 + flen))
        return;

    memcpy(t->buf + t->len, "APIC", 4);
    put_be32(t->buf + t->len + 4, flen);
    t->buf[t->len + 8] = 0;
    t->buf[t->len + 9] = 0;
    t->buf[t->len + 10] = 0;                      /* ISO-8859-1 */
    memcpy(t->buf + t->len + 11, mime, mlen);     /* includes terminating NUL */
    t->buf[t->len + 11 + mlen] = 3;               /* picture type: cover (front) */
    t->buf[t->len + 12 + mlen] = 0;               /* empty description */
    memcpy(t->buf + t->len + 13 + mlen, data, dlen);
    t->len += 10 + flen;
}

int mu_mp3_write_tagged(const wchar_t *src, const MuMeta *meta, const wchar_t *dst)
{
    FILE *fi = NULL;
    FILE *fo = NULL;
    unsigned char head[10];
    size_t got;
    long long skip = 0;
    Tagger t;
    unsigned char *buf = NULL;
    int rc = 0;
    int have_tag;

    memset(&t, 0, sizeof(t));

    have_tag = meta && (meta->title[0] || meta->artist[0] || meta->album[0] ||
                        (meta->cover && meta->cover_len));

    fi = _wfopen(src, L"rb");
    if (!fi)
        return 0;

    got = fread(head, 1, 10, fi);
    if (got == 10 && memcmp(head, "ID3", 3) == 0) {
        unsigned sz = ((unsigned)(head[6] & 0x7f) << 21) | ((unsigned)(head[7] & 0x7f) << 14) |
                      ((unsigned)(head[8] & 0x7f) << 7) | (unsigned)(head[9] & 0x7f);
        skip = 10 + (long long)sz;
        if (head[5] & 0x10)
            skip += 10;
    }

    if (have_tag) {
        tg_text(&t, "TIT2", meta->title);
        tg_text(&t, "TPE1", meta->artist);
        tg_text(&t, "TALB", meta->album);
        tg_apic(&t, meta->cover, meta->cover_len);
        if (!t.buf)
            have_tag = 0;
    }

    fo = _wfopen(dst, L"wb");
    if (!fo)
        goto done;

    if (have_tag && t.len > 0) {
        unsigned char h[10];
        memcpy(h, "ID3", 3);
        h[3] = 3;                        /* v2.3 */
        h[4] = 0;
        h[5] = 0;
        put_syncsafe(h + 6, t.len);
        fwrite(h, 1, 10, fo);
        fwrite(t.buf, 1, t.len, fo);
    }

    if (_fseeki64(fi, skip, SEEK_SET) != 0)
        goto done;

    buf = (unsigned char *)malloc(1 << 16);
    if (!buf)
        goto done;

    while ((got = fread(buf, 1, 1 << 16, fi)) > 0) {
        if (fwrite(buf, 1, got, fo) != got)
            goto done;
    }
    rc = 1;

done:
    free(buf);
    free(t.buf);
    if (fo) {
        fclose(fo);
        if (!rc)
            DeleteFileW(dst);
    }
    if (fi)
        fclose(fi);
    return rc;
}
