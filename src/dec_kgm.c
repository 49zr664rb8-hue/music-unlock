/* KuGou .kgm / .kgma / .vpr decoder (KGM V2 XOR scheme).
 *
 * The three mask tables below are the community-published constants used by
 * unlock-music and derived projects (see NOTICE). They are data, not code:
 * without them the scheme cannot be implemented at all.
 */

#include "core.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define TABLE_SIZE 272

static const unsigned char table1[TABLE_SIZE] = {
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,33,1,97,1,33,1,225,1,33,1,97,1,33,1,
210,35,2,2,66,66,2,2,194,194,2,2,66,66,2,2,211,211,2,3,99,67,99,3,227,195,227,3,99,67,99,3,
148,180,148,101,4,4,4,4,132,132,132,132,4,4,4,4,149,149,149,149,4,5,37,5,229,133,165,133,229,5,37,5,
214,182,150,182,214,39,6,6,198,198,134,134,198,198,6,6,215,215,151,151,215,215,6,7,231,199,231,135,231,199,231,7,
24,56,24,120,24,56,24,233,8,8,8,8,8,8,8,8,25,25,25,25,25,25,25,25,8,9,41,9,105,9,41,9,
218,58,26,58,90,58,26,58,218,43,10,10,74,74,10,10,219,219,27,27,91,91,27,27,219,219,10,11,107,75,107,11,
156,188,156,124,28,60,28,124,156,188,156,109,12,12,12,12,157,157,157,157,29,29,29,29,157,157,157,157,12,13,45,13,
222,190,158,190,222,62,30,62,222,190,158,190,222,47,14,14,223,223,159,159,223,223,31,31,223,223,159,159,223,223,14,15,
0,32,0,96,0,32,0,224,0,32,0,96,0,32,0,241
};

static const unsigned char table2[TABLE_SIZE] = {
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,35,1,103,1,35,1,239,1,35,1,103,1,35,1,
223,33,2,2,70,70,2,2,206,206,2,2,70,70,2,2,222,222,2,3,101,71,101,3,237,207,237,3,101,71,101,3,
157,191,157,99,4,4,4,4,140,140,140,140,4,4,4,4,156,156,156,156,4,5,39,5,235,141,175,141,235,5,39,5,
219,189,159,189,219,37,6,6,202,202,142,142,202,202,6,6,218,218,158,158,218,218,6,7,233,203,233,143,233,203,233,7,
25,59,25,127,25,59,25,231,8,8,8,8,8,8,8,8,24,24,24,24,24,24,24,24,8,9,43,9,111,9,43,9,
215,57,27,57,95,57,27,57,215,41,10,10,78,78,10,10,214,214,26,26,94,94,26,26,214,214,10,11,109,79,109,11,
149,183,149,123,29,63,29,123,149,183,149,107,12,12,12,12,148,148,148,148,28,28,28,28,148,148,148,148,12,13,47,13,
211,181,151,181,211,61,31,61,211,181,151,181,211,45,14,14,210,210,150,150,210,210,30,30,210,210,150,150,210,210,14,15,
0,34,0,102,0,34,0,238,0,34,0,102,0,34,0,254
};

static const unsigned char tableV2[TABLE_SIZE] = {
184,213,61,178,233,175,120,140,131,51,113,81,118,160,205,55,47,62,53,141,169,190,152,183,231,140,34,206,90,97,223,104,
105,137,254,165,182,222,169,119,252,200,189,189,229,109,62,90,54,239,105,78,190,225,233,102,28,243,217,2,182,242,18,155,
68,208,111,185,53,137,182,70,109,115,130,6,105,193,237,215,133,194,48,223,162,98,190,121,45,98,98,61,13,126,190,72,
137,35,2,160,228,213,117,81,50,2,83,253,22,58,33,59,22,15,195,178,187,179,226,186,58,61,19,236,246,1,69,132,
165,112,15,147,73,12,100,205,49,213,204,76,7,1,158,0,26,35,144,191,136,30,59,171,166,62,196,115,71,16,126,59,
94,188,227,0,132,255,9,212,224,137,15,91,88,112,79,251,101,216,92,83,27,211,200,198,191,239,152,176,80,79,15,234,
229,131,88,140,40,44,132,103,205,208,158,71,219,39,80,202,244,99,99,232,151,127,27,75,12,194,193,33,76,204,88,245,
148,82,163,243,211,224,104,244,0,35,243,94,10,123,147,221,171,18,178,19,232,132,215,167,159,15,50,76,85,29,4,54,
82,220,3,243,249,78,66,233,61,97,239,124,182,179,147,80
};

static const unsigned char vprKey[17] = {
37,223,232,166,117,30,117,14,47,128,243,45,184,182,227,17,0
};

_Static_assert(sizeof(table1) == TABLE_SIZE, "table1 size mismatch");
_Static_assert(sizeof(table2) == TABLE_SIZE, "table2 size mismatch");
_Static_assert(sizeof(tableV2) == TABLE_SIZE, "tableV2 size mismatch");

static unsigned rd32(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

static unsigned char mask_v1(unsigned offset)
{
    unsigned char v = 0;

    while (offset >= 0x11) {
        v ^= table1[offset % TABLE_SIZE];
        offset >>= 4;
        v ^= table2[offset % TABLE_SIZE];
        offset >>= 4;
    }
    return v;
}

int mu_dec_kgm(const wchar_t *in, const wchar_t *out, int is_vpr,
               wchar_t *err, size_t cch)
{
    FILE *f;
    FILE *o;
    unsigned char header[0x3C];
    unsigned char filekey[17];
    unsigned char *buf;
    unsigned header_len;
    unsigned long long fsize;
    size_t got;
    size_t i;
    size_t index = 0;
    unsigned char head[16];
    size_t headlen = 0;
    MuFormat fmt;
    unsigned char cur_v1 = 0;
    unsigned cur_o = 0xFFFFFFFFu;

    if (err && cch)
        err[0] = 0;

    fsize = mu_file_size(in);
    f = _wfopen(in, L"rb");
    if (!f) {
        swprintf(err, cch, L"无法打开文件");
        return 0;
    }
    if (fread(header, 1, sizeof(header), f) != sizeof(header)) {
        fclose(f);
        swprintf(err, cch, L"不是有效的酷狗加密文件（头部过短）");
        return 0;
    }

    memcpy(filekey, header + 0x1C, 16);
    filekey[16] = 0;

    header_len = rd32(header + 0x10);
    if (header_len < sizeof(header) || (unsigned long long)header_len >= fsize) {
        fclose(f);
        swprintf(err, cch, L"文件头长度异常");
        return 0;
    }

    if (_fseeki64(f, (long long)header_len, SEEK_SET) != 0) {
        fclose(f);
        swprintf(err, cch, L"无法定位音频数据");
        return 0;
    }

    o = _wfopen(out, L"wb");
    if (!o) {
        fclose(f);
        swprintf(err, cch, L"无法写入临时文件");
        return 0;
    }

    buf = (unsigned char *)malloc(1 << 16);
    if (!buf) {
        fclose(o);
        fclose(f);
        swprintf(err, cch, L"内存不足");
        return 0;
    }

    while ((got = fread(buf, 1, 1 << 16, f)) > 0) {
        for (i = 0; i < got; i++) {
            size_t abs = index + i;
            unsigned o16 = (unsigned)(abs >> 4);
            unsigned char v1;
            unsigned char m;

            if (o16 != cur_o) {
                cur_o = o16;
                cur_v1 = mask_v1(o16);
            }
            v1 = cur_v1;
            m = (unsigned char)(tableV2[abs % TABLE_SIZE] ^ v1);
            {
                unsigned char x = (unsigned char)(m ^ buf[i] ^ filekey[abs % 17]);
                x = (unsigned char)(x ^ ((x & 0x0f) << 4));
                if (is_vpr)
                    x ^= vprKey[abs % 17];
                buf[i] = x;
            }
        }
        if (headlen < sizeof(head)) {
            size_t need = sizeof(head) - headlen;
            size_t take = got < need ? got : need;
            memcpy(head + headlen, buf, take);
            headlen += take;
        }
        fwrite(buf, 1, got, o);
        index += got;
    }

    free(buf);
    fclose(o);
    fclose(f);

    if (headlen < 4) {
        swprintf(err, cch, L"音频数据为空");
        return 0;
    }

    fmt = mu_detect_format(head, headlen);
    if (fmt == MU_FMT_UNKNOWN) {
        swprintf(err, cch, L"解密结果不是有效的音频数据");
        return 0;
    }
    return 1;
}
