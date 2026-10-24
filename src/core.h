#ifndef MU_CORE_H
#define MU_CORE_H

#include <stddef.h>
#include <wchar.h>

#define MU_PATH 1024
#define MU_TEXT 256

typedef enum {
    MU_FMT_UNKNOWN = 0,
    MU_FMT_MP3,
    MU_FMT_FLAC,
    MU_FMT_WAV,
    MU_FMT_OGG,
    MU_FMT_M4A
} MuFormat;

typedef struct {
    wchar_t title[MU_TEXT];
    wchar_t artist[MU_TEXT];
    wchar_t album[MU_TEXT];
    unsigned char *cover;
    size_t cover_len;
} MuMeta;

typedef void (*MuProgressFn)(void *ud, int percent, const wchar_t *stage);

typedef struct {
    wchar_t input[MU_PATH];
    wchar_t outdir[MU_PATH];
    wchar_t output[MU_PATH];
} MuJob;

void mu_meta_init(MuMeta *m);
void mu_meta_free(MuMeta *m);

/* Sniff a container by magic bytes / extension. Returns a lowercase tag such
 * as L"ncm", L"kgm", L"vpr", L"qmc0", L"mflac", L"kwm" or L"" when unknown. */
void mu_container_tag(const wchar_t *path, wchar_t *tag, size_t cch);

/* Decrypt any supported container into `tmp_out`. Returns 1 on success. */
int mu_decrypt(const wchar_t *in, const wchar_t *tmp_out, MuMeta *meta,
               wchar_t *err, size_t cch);

/* Decrypt + normalise to MP3 + write ID3 tags. */
int mu_convert(MuJob *job, MuProgressFn cb, void *ud, wchar_t *err, size_t cch);

MuFormat mu_detect_format(const unsigned char *head, size_t n);
const wchar_t *mu_format_name(MuFormat f);

int mu_dec_ncm(const wchar_t *in, const wchar_t *out, MuMeta *meta,
               wchar_t *err, size_t cch);
int mu_dec_kgm(const wchar_t *in, const wchar_t *out, int is_vpr,
               wchar_t *err, size_t cch);

int mu_mp3_write_tagged(const wchar_t *src, const MuMeta *meta, const wchar_t *dst);

int mu_ffmpeg_path(wchar_t *path, size_t cch);
int mu_ffmpeg_to_mp3(const wchar_t *src, const wchar_t *dst, wchar_t *err, size_t cch);

/* Self test used by the console build: verifies AES + KGM mask tables. */
int mu_selftest(void);

/* Set to 1 from the UI thread to abort a running conversion. */
extern volatile int mu_cancel_flag;

#endif
