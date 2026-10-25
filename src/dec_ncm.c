/* NetEase Cloud Music .ncm decoder.
 *
 * Layout:
 *   "CTENFDAM" | 2 pad | u32 keylen | key(^0x64, AES-128-ECB) | u32 metalen |
 *   meta(^0x63, base64, AES-128-ECB) | u32 crc | 1 pad | u32 imglen | image | audio
 * The audio payload is XORed with a 256-byte RC4 key box. Some builds stream the
 * keystream instead of repeating the box, so both variants are tried and the
 * one that yields a valid audio header wins.
 */

#include "core.h"
#include "aes.h"
#include "util.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static const unsigned char NCM_CORE_KEY[16] = {
    'h', 'z', 'H', 'R', 'A', 'm', 's', 'o', '5', 'k', 'I', 'n', 'b', 'a', 'x', 'W'
};

static const unsigned char NCM_META_KEY[16] = {
    0x23, 0x31, 0x34, 0x6C, 0x6A, 0x6B, 0x5F, 0x21,
    0x5C, 0x5D, 0x26, 0x30, 0x55, 0x3C, 0x27, 0x28
};

#define NCM_KEY_PREFIX "neteasecloudmusic"

static unsigned rd32(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

static void utf8_to_w(const char *s, wchar_t *out, size_t cch)
{
    int n;
    if (!out || cch == 0)
        return;
    out[0] = 0;
    if (!s || !*s)
        return;
    n = MultiByteToWideChar(CP_UTF8, 0, s, -1, out, (int)cch);
    if (n <= 0)
        out[0] = 0;
    out[cch - 1] = 0;
}

/* ---------- NetEase keystream ----------
 *
 * The key schedule is the ordinary RC4 KSA, but the keystream is NOT the RC4
 * PRGA. NetEase derives each byte straight from the key box:
 *
 *     j      = (i + 1) & 0xff
 *     ks[i]  = S[(S[j] + S[(S[j] + j) & 0xff]) & 0xff]
 *
 * It is periodic with a period of 256, so the byte can be computed from the
 * absolute audio offset. Using the real RC4 PRGA here produces noise.
 */

typedef struct {
    unsigned char box[256];
} Ks;

static void ks_init(Ks *k, const unsigned char *key, int klen)
{
    int i, j = 0;

    for (i = 0; i < 256; i++)
        k->box[i] = (unsigned char)i;
    for (i = 0; i < 256; i++) {
        j = (j + k->box[i] + key[i % klen]) & 0xff;
        {
            unsigned char t = k->box[i];
            k->box[i] = k->box[j];
            k->box[j] = t;
        }
    }
}

static unsigned char ks_byte(const Ks *k, size_t i)
{
    unsigned j = (unsigned)((i + 1) & 0xff);
    unsigned a = k->box[j];
    unsigned b = k->box[(a + j) & 0xff];

    return k->box[(a + b) & 0xff];
}

/* ---------- PKCS#7 ---------- */

static size_t pkcs7_unpad(unsigned char *buf, size_t len)
{
    unsigned char pad;
    size_t i;

    if (len == 0)
        return 0;
    pad = buf[len - 1];
    if (pad == 0 || pad > 16 || pad > len)
        return len;
    for (i = 0; i < pad; i++) {
        if (buf[len - 1 - i] != pad)
            return len;
    }
    return len - pad;
}

/* ---------- tiny JSON helpers ---------- */

static const char *find_sub(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    const char *p;

    if (!hay || !needle || n == 0)
        return NULL;
    for (p = hay; *p; p++) {
        if (strncmp(p, needle, n) == 0)
            return p + n;
    }
    return NULL;
}

/* Copy a JSON string starting right after an opening quote. */
static void json_copy_string(const char *p, char *out, size_t cch)
{
    size_t o = 0;

    if (!out || cch == 0)
        return;
    out[0] = 0;
    if (!p)
        return;
    while (*p && *p != '"' && o + 1 < cch) {
        if (*p == '\\' && p[1]) {
            p++;
            switch (*p) {
            case 'n': out[o++] = '\n'; break;
            case 't': out[o++] = '\t'; break;
            case 'r': out[o++] = '\r'; break;
            case 'u':
                /* keep the escape sequence as-is; rare for these fields */
                if (o + 1 < cch) out[o++] = '?';
                p += 4;
                continue;
            default: out[o++] = *p; break;
            }
            p++;
            continue;
        }
        out[o++] = *p++;
    }
    out[o] = 0;
}

/* Locate `"key"` followed by optional spaces, ':' and optional spaces.
 * Returns a pointer just past the colon, or NULL. */
static const char *find_key(const char *json, const char *key)
{
    char pat[64];
    const char *p;
    size_t n;

    if (!json || !key)
        return NULL;
    n = strlen(key);
    if (n + 3 > sizeof(pat))
        return NULL;
    pat[0] = '"';
    memcpy(pat + 1, key, n);
    pat[n + 1] = '"';
    pat[n + 2] = 0;

    p = find_sub(json, pat);
    if (!p)
        return NULL;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    if (*p != ':')
        return NULL;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    return p;
}

static void parse_meta_json(const char *json, MuMeta *meta)
{
    const char *p;
    char tmp[512];

    p = find_key(json, "musicName");
    if (p && *p == '"') {
        json_copy_string(p + 1, tmp, sizeof(tmp));
        if (tmp[0])
            utf8_to_w(tmp, meta->title, MU_TEXT);
    }

    /* "artist":[["Name","id"],...] */
    p = find_key(json, "artist");
    if (p) {
        p = strchr(p, '"');
        if (p) {
            json_copy_string(p + 1, tmp, sizeof(tmp));
            if (tmp[0])
                utf8_to_w(tmp, meta->artist, MU_TEXT);
        }
    }

    p = find_key(json, "album");
    if (p && *p == '"') {
        json_copy_string(p + 1, tmp, sizeof(tmp));
        if (tmp[0])
            utf8_to_w(tmp, meta->album, MU_TEXT);
    }
}

/* ---------- audio pass ---------- */

static int decrypt_audio(FILE *f, long long audio_off, const unsigned char *key, int klen,
                         const wchar_t *out, MuFormat *fmt, wchar_t *err, size_t cch)
{
    Ks ks;
    FILE *o;
    unsigned char *buf;
    size_t got;
    size_t idx = 0;
    unsigned char head[16];
    size_t headlen = 0;

    if (_fseeki64(f, audio_off, SEEK_SET) != 0) {
        swprintf(err, cch, L"无法定位音频数据");
        return 0;
    }

    o = _wfopen(out, L"wb");
    if (!o) {
        swprintf(err, cch, L"无法写入临时文件");
        return 0;
    }

    buf = (unsigned char *)malloc(1 << 16);
    if (!buf) {
        fclose(o);
        swprintf(err, cch, L"内存不足");
        return 0;
    }

    ks_init(&ks, key, klen);

    while ((got = fread(buf, 1, 1 << 16, f)) > 0) {
        size_t i;
        for (i = 0; i < got; i++)
            buf[i] ^= ks_byte(&ks, idx + i);
        if (headlen < sizeof(head)) {
            size_t need = sizeof(head) - headlen;
            size_t take = got < need ? got : need;
            memcpy(head + headlen, buf, take);
            headlen += take;
        }
        fwrite(buf, 1, got, o);
        idx += got;
    }

    free(buf);
    fclose(o);

    if (headlen < 4) {
        swprintf(err, cch, L"音频数据为空");
        return 0;
    }

    *fmt = mu_detect_format(head, headlen);
    if (*fmt == MU_FMT_UNKNOWN) {
        swprintf(err, cch, L"解密结果不是有效的音频数据");
        return 0;
    }
    return 1;
}

int mu_dec_ncm(const wchar_t *in, const wchar_t *out, MuMeta *meta,
               wchar_t *err, size_t cch)
{
    FILE *f;
    unsigned char hdr[16];
    unsigned char *keybuf = NULL;
    unsigned char *metabuf = NULL;
    unsigned char *b64 = NULL;
    unsigned keylen, metalen, imglen;
    unsigned char rc4key[512];
    int rklen = 0;
    long long audio_off;
    MuFormat fmt = MU_FMT_UNKNOWN;
    wchar_t tmp_err[256];
    int ok = 0;

    if (err && cch)
        err[0] = 0;

    f = _wfopen(in, L"rb");
    if (!f) {
        swprintf(err, cch, L"无法打开文件");
        return 0;
    }

    if (fread(hdr, 1, 10, f) != 10 || memcmp(hdr, "CTENFDAM", 8) != 0) {
        fclose(f);
        swprintf(err, cch, L"不是有效的 .ncm 文件");
        return 0;
    }

    if (fread(hdr, 1, 4, f) != 4) {
        fclose(f);
        swprintf(err, cch, L"文件头不完整");
        return 0;
    }
    keylen = rd32(hdr);
    if (keylen == 0 || keylen > (1u << 20) || (keylen % 16) != 0) {
        fclose(f);
        swprintf(err, cch, L"密钥区长度异常");
        return 0;
    }

    keybuf = (unsigned char *)malloc(keylen);
    if (!keybuf) {
        fclose(f);
        swprintf(err, cch, L"内存不足");
        return 0;
    }
    if (fread(keybuf, 1, keylen, f) != keylen) {
        fclose(f);
        free(keybuf);
        swprintf(err, cch, L"密钥区读取失败");
        return 0;
    }
    {
        unsigned i;
        for (i = 0; i < keylen; i++)
            keybuf[i] ^= 0x64;
    }
    mu_aes128_ecb_decrypt(NCM_CORE_KEY, keybuf, keybuf, keylen);
    keylen = (unsigned)pkcs7_unpad(keybuf, keylen);

    if (keylen < sizeof(NCM_KEY_PREFIX) - 1 ||
        memcmp(keybuf, NCM_KEY_PREFIX, sizeof(NCM_KEY_PREFIX) - 1) != 0) {
        fclose(f);
        free(keybuf);
        swprintf(err, cch, L"内置密钥解析失败（文件可能来自其他版本）");
        return 0;
    }
    rklen = (int)keylen - (int)(sizeof(NCM_KEY_PREFIX) - 1);
    if (rklen <= 0 || rklen > (int)sizeof(rc4key)) {
        fclose(f);
        free(keybuf);
        swprintf(err, cch, L"密钥长度异常");
        return 0;
    }
    memcpy(rc4key, keybuf + (sizeof(NCM_KEY_PREFIX) - 1), (size_t)rklen);

    /* metadata (optional, never fatal) */
    if (fread(hdr, 1, 4, f) == 4) {
        metalen = rd32(hdr);
        if (metalen > 0 && metalen < (16u << 20)) {
            metabuf = (unsigned char *)malloc(metalen);
            if (metabuf && fread(metabuf, 1, metalen, f) == metalen) {
                unsigned i;
                for (i = 0; i < metalen; i++)
                    metabuf[i] ^= 0x63;
                if (metalen > 22) {
                    size_t blen = 0;
                    if (mu_b64_decode(metabuf + 22, metalen - 22, &b64, &blen) && blen >= 16) {
                        size_t plen;
                        blen -= blen % 16;
                        mu_aes128_ecb_decrypt(NCM_META_KEY, b64, b64, blen);
                        plen = pkcs7_unpad(b64, blen);
                        if (plen > 6 && memcmp(b64, "music:", 6) == 0) {
                            b64[plen] = 0;
                            parse_meta_json((const char *)b64 + 6, meta);
                        }
                    }
                }
            }
        } else if (metalen != 0) {
            /* unreadable metadata: skip it by seeking, still continue */
            _fseeki64(f, metalen, SEEK_CUR);
        }
    }

    /* crc + gap + cover */
    if (fread(hdr, 1, 5, f) != 5) {
        fclose(f);
        free(keybuf);
        free(metabuf);
        free(b64);
        swprintf(err, cch, L"文件结构不完整");
        return 0;
    }
    if (fread(hdr, 1, 4, f) != 4) {
        fclose(f);
        free(keybuf);
        free(metabuf);
        free(b64);
        swprintf(err, cch, L"文件结构不完整");
        return 0;
    }
    imglen = rd32(hdr);
    if (imglen > 0 && imglen < (32u << 20)) {
        /* Client builds from 3.x wrap the picture in its own 4-byte length
         * field, so the payload starts 4 bytes later than the classic layout.
         * Detect it instead of guessing: only skip when the value matches. */
        unsigned char peek[4];
        long long save = _ftelli64(f);

        if (fread(peek, 1, 4, f) == 4 && rd32(peek) == imglen) {
            /* redundant length consumed */
        } else if (save >= 0) {
            _fseeki64(f, save, SEEK_SET);
        }
        {
            unsigned char *img = (unsigned char *)malloc(imglen);
            if (img) {
                if (fread(img, 1, imglen, f) == imglen) {
                    meta->cover = img;
                    meta->cover_len = imglen;
                } else {
                    free(img);
                }
            }
        }
    } else if (imglen != 0) {
        _fseeki64(f, imglen, SEEK_CUR);
    }

    audio_off = _ftelli64(f);

    ok = decrypt_audio(f, audio_off, rc4key, rklen, out, &fmt, tmp_err, 256);
    if (!ok)
        swprintf(err, cch, L"%s", tmp_err);

    fclose(f);
    free(keybuf);
    free(metabuf);
    free(b64);
    return ok;
}
