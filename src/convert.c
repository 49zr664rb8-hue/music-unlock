#include "core.h"
#include "util.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

volatile int mu_cancel_flag = 0;

void mu_meta_init(MuMeta *m)
{
    if (m)
        memset(m, 0, sizeof(*m));
}

void mu_meta_free(MuMeta *m)
{
    if (!m)
        return;
    if (m->cover) {
        free(m->cover);
        m->cover = NULL;
    }
    m->cover_len = 0;
}

void mu_container_tag(const wchar_t *path, wchar_t *tag, size_t cch)
{
    wchar_t ext[32];

    if (!tag || cch == 0)
        return;
    tag[0] = 0;
    mu_path_ext_lower(path, ext, 32);
    if (ext[0] == L'.')
        wcsncpy(tag, ext + 1, cch - 1);
    else
        wcsncpy(tag, ext, cch - 1);
    tag[cch - 1] = 0;
}

int mu_decrypt(const wchar_t *in, const wchar_t *tmp_out, MuMeta *meta,
               wchar_t *err, size_t cch)
{
    wchar_t tag[32];

    mu_container_tag(in, tag, 32);

    if (wcscmp(tag, L"ncm") == 0)
        return mu_dec_ncm(in, tmp_out, meta, err, cch);
    if (wcscmp(tag, L"kgm") == 0 || wcscmp(tag, L"kgma") == 0)
        return mu_dec_kgm(in, tmp_out, 0, err, cch);
    if (wcscmp(tag, L"vpr") == 0)
        return mu_dec_kgm(in, tmp_out, 1, err, cch);

    if (tag[0] == 0)
        swprintf(err, cch, L"无法识别的文件类型（没有扩展名）");
    else
        swprintf(err, cch, L".%s 暂不支持", tag);
    return 0;
}

static void unique_output(const wchar_t *dir, const wchar_t *stem, wchar_t *out, size_t cch)
{
    wchar_t name[MU_PATH];
    wchar_t full[MU_PATH];
    int n;

    mu_join_path(full, MU_PATH, dir, L"");
    _snwprintf(name, MU_PATH, L"%s.mp3", stem);
    name[MU_PATH - 1] = 0;
    mu_join_path(full, MU_PATH, dir, name);
    if (!mu_file_exists(full)) {
        wcsncpy(out, full, cch - 1);
        out[cch - 1] = 0;
        return;
    }
    for (n = 2; n < 1000; n++) {
        _snwprintf(name, MU_PATH, L"%s (%d).mp3", stem, n);
        name[MU_PATH - 1] = 0;
        mu_join_path(full, MU_PATH, dir, name);
        if (!mu_file_exists(full)) {
            wcsncpy(out, full, cch - 1);
            out[cch - 1] = 0;
            return;
        }
    }
    wcsncpy(out, full, cch - 1);
    out[cch - 1] = 0;
}

int mu_convert(MuJob *job, MuProgressFn cb, void *ud, wchar_t *err, size_t cch)
{
    MuMeta meta;
    wchar_t tmpdir[MU_PATH];
    wchar_t tmp1[MU_PATH];
    wchar_t tmp2[MU_PATH];
    wchar_t stem[MU_PATH];
    wchar_t out[MU_PATH];
    unsigned char head[16];
    size_t got = 0;
    MuFormat fmt;
    int rc = 0;
    DWORD pid;

    if (err && cch)
        err[0] = 0;

    mu_meta_init(&meta);
    mu_cancel_flag = 0;

    if (!mu_mkdir_recursive(job->outdir)) {
        swprintf(err, cch, L"无法创建输出目录：%s", job->outdir);
        return 0;
    }

    if (!GetTempPathW(MU_PATH, tmpdir)) {
        swprintf(err, cch, L"无法获取临时目录");
        return 0;
    }
    pid = GetCurrentProcessId();
    _snwprintf(tmp1, MU_PATH, L"%smu_%lu_a.bin", tmpdir, (unsigned long)pid);
    _snwprintf(tmp2, MU_PATH, L"%smu_%lu_b.mp3", tmpdir, (unsigned long)pid);
    tmp1[MU_PATH - 1] = 0;
    tmp2[MU_PATH - 1] = 0;
    DeleteFileW(tmp1);
    DeleteFileW(tmp2);

    mu_stem(job->input, stem, MU_PATH);
    unique_output(job->outdir, stem, out, MU_PATH);

    if (cb)
        cb(ud, 3, L"解密中…");

    if (!mu_decrypt(job->input, tmp1, &meta, err, cch))
        goto cleanup;

    if (mu_cancel_flag) {
        swprintf(err, cch, L"已取消");
        goto cleanup;
    }

    if (cb)
        cb(ud, 55, L"检测音频格式…");

    if (!mu_read_head(tmp1, head, sizeof(head), &got) || got < 4) {
        swprintf(err, cch, L"解密结果为空");
        goto cleanup;
    }
    fmt = mu_detect_format(head, got);
    if (fmt == MU_FMT_UNKNOWN) {
        swprintf(err, cch, L"解密结果不是有效的音频数据");
        goto cleanup;
    }

    if (fmt == MU_FMT_MP3) {
        if (cb)
            cb(ud, 70, L"写入标签…");
        if (!mu_mp3_write_tagged(tmp1, &meta, out)) {
            swprintf(err, cch, L"写入 MP3 失败");
            goto cleanup;
        }
    } else {
        if (cb)
            cb(ud, 60, L"转码为 MP3…");
        if (!mu_ffmpeg_to_mp3(tmp1, tmp2, err, cch))
            goto cleanup;
        if (cb)
            cb(ud, 90, L"写入标签…");
        if (!mu_mp3_write_tagged(tmp2, &meta, out)) {
            swprintf(err, cch, L"写入 MP3 失败");
            goto cleanup;
        }
    }

    wcsncpy(job->output, out, MU_PATH - 1);
    job->output[MU_PATH - 1] = 0;

    if (cb)
        cb(ud, 100, L"完成");
    rc = 1;

cleanup:
    DeleteFileW(tmp1);
    DeleteFileW(tmp2);
    mu_meta_free(&meta);
    return rc;
}
