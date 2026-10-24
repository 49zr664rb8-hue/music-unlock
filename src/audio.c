#include "core.h"
#include "util.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

MuFormat mu_detect_format(const unsigned char *head, size_t n)
{
    if (!head || n < 4)
        return MU_FMT_UNKNOWN;
    if (memcmp(head, "fLaC", 4) == 0)
        return MU_FMT_FLAC;
    if (memcmp(head, "OggS", 4) == 0)
        return MU_FMT_OGG;
    if (memcmp(head, "RIFF", 4) == 0)
        return MU_FMT_WAV;
    if (memcmp(head, "ID3", 3) == 0)
        return MU_FMT_MP3;
    if (n >= 8 && memcmp(head + 4, "ftyp", 4) == 0)
        return MU_FMT_M4A;
    if (head[0] == 0xFF && (head[1] & 0xE0) == 0xE0)
        return MU_FMT_MP3;
    return MU_FMT_UNKNOWN;
}

const wchar_t *mu_format_name(MuFormat f)
{
    switch (f) {
    case MU_FMT_MP3:  return L"MP3";
    case MU_FMT_FLAC: return L"FLAC";
    case MU_FMT_WAV:  return L"WAV";
    case MU_FMT_OGG:  return L"OGG";
    case MU_FMT_M4A:  return L"M4A";
    default:          return L"?";
    }
}

int mu_ffmpeg_path(wchar_t *path, size_t cch)
{
    wchar_t exe[MU_PATH];
    wchar_t cand[MU_PATH];
    DWORD n;
    static const wchar_t *extra[] = {
        L"C:\\ffmpeg\\bin\\ffmpeg.exe",
        L"C:\\Program Files\\ffmpeg\\bin\\ffmpeg.exe",
        L"C:\\ProgramData\\chocolatey\\bin\\ffmpeg.exe"
    };
    size_t i;

    if (!path || cch == 0)
        return 0;
    path[0] = 0;

    /* 1. next to the executable */
    n = GetModuleFileNameW(NULL, exe, MU_PATH);
    if (n > 0 && n < MU_PATH) {
        wchar_t *slash = wcsrchr(exe, L'\\');
        if (slash) {
            *slash = 0;
            mu_join_path(cand, MU_PATH, exe, L"ffmpeg.exe");
            if (mu_file_exists(cand)) {
                wcsncpy(path, cand, cch - 1);
                path[cch - 1] = 0;
                return 1;
            }
        }
    }

    /* 2. PATH */
    n = SearchPathW(NULL, L"ffmpeg.exe", NULL, (DWORD)cch, path, NULL);
    if (n > 0 && n < cch && mu_file_exists(path))
        return 1;

    /* 3. a few usual install spots */
    for (i = 0; i < sizeof(extra) / sizeof(extra[0]); i++) {
        if (mu_file_exists(extra[i])) {
            wcsncpy(path, extra[i], cch - 1);
            path[cch - 1] = 0;
            return 1;
        }
    }

    path[0] = 0;
    return 0;
}

int mu_ffmpeg_to_mp3(const wchar_t *src, const wchar_t *dst, wchar_t *err, size_t cch)
{
    wchar_t ff[MU_PATH];
    wchar_t cmd[MU_PATH * 3];
    wchar_t logpath[MU_PATH];
    wchar_t tmppath[MU_PATH];
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    HANDLE hlog = INVALID_HANDLE_VALUE;
    HANDLE hnul = INVALID_HANDLE_VALUE;
    DWORD code = 1;

    if (err && cch)
        err[0] = 0;

    if (!mu_ffmpeg_path(ff, MU_PATH)) {
        swprintf(err, cch, L"未找到 ffmpeg.exe。请把 ffmpeg.exe 放到本程序同一目录，或加入 PATH。");
        return 0;
    }

    GetTempPathW(MU_PATH, tmppath);
    _snwprintf(logpath, MU_PATH, L"%smu_ff_%lu.log", tmppath, (unsigned long)GetCurrentProcessId());
    logpath[MU_PATH - 1] = 0;

    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    hlog = CreateFileW(logpath, GENERIC_WRITE, FILE_SHARE_READ, &sa,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    hnul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                       OPEN_EXISTING, 0, NULL);

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = (hnul != INVALID_HANDLE_VALUE) ? hnul : GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = (hlog != INVALID_HANDLE_VALUE) ? hlog : GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = si.hStdOutput;

    _snwprintf(cmd, MU_PATH * 3,
               L"\"%s\" -hide_banner -nostdin -loglevel error -y -i \"%s\" -vn "
               L"-map_metadata -1 -codec:a libmp3lame -q:a 0 \"%s\"",
               ff, src, dst);
    cmd[MU_PATH * 3 - 1] = 0;

    memset(&pi, 0, sizeof(pi));
    if (!CreateProcessW(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        swprintf(err, cch, L"无法启动 ffmpeg（错误码 %lu）", (unsigned long)GetLastError());
        if (hlog != INVALID_HANDLE_VALUE)
            CloseHandle(hlog);
        if (hnul != INVALID_HANDLE_VALUE)
            CloseHandle(hnul);
        DeleteFileW(logpath);
        return 0;
    }

    for (;;) {
        DWORD w = WaitForSingleObject(pi.hProcess, 200);
        if (w == WAIT_OBJECT_0)
            break;
        if (mu_cancel_flag) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 3000);
            break;
        }
    }
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (hlog != INVALID_HANDLE_VALUE)
        CloseHandle(hlog);
    if (hnul != INVALID_HANDLE_VALUE)
        CloseHandle(hnul);

    if (code != 0) {
        char line[512];
        char last[512];
        FILE *lf;

        last[0] = 0;
        lf = _wfopen(logpath, L"rb");
        if (lf) {
            while (fgets(line, sizeof(line), lf)) {
                size_t l = strlen(line);
                while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r'))
                    line[--l] = 0;
                if (l > 0) {
                    strncpy(last, line, sizeof(last) - 1);
                    last[sizeof(last) - 1] = 0;
                }
            }
            fclose(lf);
        }
        DeleteFileW(logpath);
        if (mu_cancel_flag) {
            swprintf(err, cch, L"已取消");
            return 0;
        }
        if (last[0]) {
            wchar_t wlast[512];
            MultiByteToWideChar(CP_UTF8, 0, last, -1, wlast, 512);
            wlast[511] = 0;
            swprintf(err, cch, L"转码失败：%s", wlast);
        } else {
            swprintf(err, cch, L"转码失败（ffmpeg 退出码 %lu）", (unsigned long)code);
        }
        DeleteFileW(dst);
        return 0;
    }

    DeleteFileW(logpath);
    if (!mu_file_exists(dst)) {
        swprintf(err, cch, L"转码没有产生输出文件");
        return 0;
    }
    return 1;
}
