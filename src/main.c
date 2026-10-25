/* Win32 GUI front-end for MusicUnlock. */

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "core.h"
#include "util.h"

#define IDC_LIST     1001
#define IDC_ADD      1002
#define IDC_ADDDIR   1003
#define IDC_REMOVE   1004
#define IDC_CLEAR    1005
#define IDC_START    1006
#define IDC_BROWSE   1007
#define IDC_OPEN     1008
#define IDC_PROGRESS 1009
#define IDC_STATUS   1010
#define IDC_OUTLABEL 1011
#define IDC_OUTTAG   1012

#define WM_MU_ITEM (WM_APP + 1)
#define WM_MU_DONE (WM_APP + 2)
#define WM_MU_PROG (WM_APP + 3)

#define ST_PENDING 0
#define ST_RUNNING 1
#define ST_OK      2
#define ST_FAIL    3

typedef struct {
    wchar_t path[MU_PATH];
    int status;
    wchar_t note[MU_TEXT];
} Item;

static HWND g_hwnd;
static HWND g_hList, g_hProg, g_hStatus, g_hStart, g_hOutLabel;
static HFONT g_font, g_fontBold;
static Item *g_items;
static int g_count, g_cap;
static wchar_t g_outdir[MU_PATH];
static HANDLE g_thread;
static volatile int g_running;
static int g_total, g_done;

static const wchar_t *EXT_LIST[] = { L".ncm", L".kgm", L".kgma", L".vpr" };

static int ext_supported(const wchar_t *path)
{
    wchar_t ext[32];
    size_t i;

    mu_path_ext_lower(path, ext, 32);
    for (i = 0; i < sizeof(EXT_LIST) / sizeof(EXT_LIST[0]); i++) {
        if (wcscmp(ext, EXT_LIST[i]) == 0)
            return 1;
    }
    return 0;
}

static void set_status(const wchar_t *text)
{
    SetWindowTextW(g_hStatus, text);
}

static int add_item(const wchar_t *path)
{
    int i;

    if (!ext_supported(path))
        return 0;
    for (i = 0; i < g_count; i++) {
        if (_wcsicmp(g_items[i].path, path) == 0)
            return 0;
    }
    if (g_count == g_cap) {
        int ncap = g_cap ? g_cap * 2 : 64;
        Item *n = (Item *)realloc(g_items, (size_t)ncap * sizeof(Item));
        if (!n)
            return 0;
        g_items = n;
        g_cap = ncap;
    }
    memset(&g_items[g_count], 0, sizeof(Item));
    wcsncpy(g_items[g_count].path, path, MU_PATH - 1);
    g_items[g_count].status = ST_PENDING;
    g_count++;
    return 1;
}

static void set_path_label(void)
{
    if (g_hOutLabel)
        SetWindowTextW(g_hOutLabel, g_outdir);
}

static void refresh_list(void)
{
    int i;

    SendMessageW(g_hList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_hList);
    for (i = 0; i < g_count; i++) {
        LVITEMW it;
        wchar_t size[64];
        wchar_t name[MU_PATH];

        memset(&it, 0, sizeof(it));
        it.mask = LVIF_TEXT;
        it.iItem = i;
        wcsncpy(name, mu_path_filename(g_items[i].path), MU_PATH - 1);
        it.pszText = name;
        SendMessageW(g_hList, LVM_INSERTITEMW, 0, (LPARAM)&it);

        mu_human_size(mu_file_size(g_items[i].path), size, 64);
        ListView_SetItemText(g_hList, i, 1, size);

        {
            wchar_t tag[32];
            mu_container_tag(g_items[i].path, tag, 32);
            ListView_SetItemText(g_hList, i, 2, tag);
        }
        ListView_SetItemText(g_hList, i, 3, L"等待中");
    }
    SendMessageW(g_hList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_hList, NULL, TRUE);
}

static void set_item_status(int idx, int status, const wchar_t *note)
{
    const wchar_t *label = L"";
    wchar_t buf[MU_TEXT];

    switch (status) {
    case ST_PENDING: label = L"等待中"; break;
    case ST_RUNNING: label = L"转换中…"; break;
    case ST_OK:      label = L"完成"; break;
    case ST_FAIL:    label = L"失败"; break;
    }

    if (status == ST_FAIL && note && *note)
        _snwprintf(buf, MU_TEXT, L"失败：%s", note);
    else if (status == ST_OK && note && *note)
        _snwprintf(buf, MU_TEXT, L"完成 → %s", note);
    else
        wcsncpy(buf, label, MU_TEXT - 1);
    buf[MU_TEXT - 1] = 0;

    ListView_SetItemText(g_hList, idx, 3, buf);
    if (status == ST_OK)
        ListView_SetItemText(g_hList, idx, 2, L"MP3");
}

static void progress_cb(void *ud, int percent, const wchar_t *stage)
{
    (void)ud;
    (void)stage;
    PostMessageW(g_hwnd, WM_MU_PROG, (WPARAM)percent, 0);
}

static DWORD WINAPI worker(LPVOID param)
{
    int i;

    (void)param;
    mu_cancel_flag = 0;

    for (i = 0; i < g_count; i++) {
        MuJob job;
        wchar_t err[512];
        int ok;

        if (mu_cancel_flag)
            break;
        if (g_items[i].status == ST_OK)
            continue;

        g_items[i].status = ST_RUNNING;
        PostMessageW(g_hwnd, WM_MU_ITEM, (WPARAM)i, 0);

        memset(&job, 0, sizeof(job));
        wcsncpy(job.input, g_items[i].path, MU_PATH - 1);
        wcsncpy(job.outdir, g_outdir, MU_PATH - 1);

        ok = mu_convert(&job, progress_cb, NULL, err, 512);

        g_items[i].status = ok ? ST_OK : ST_FAIL;
        if (ok)
            wcsncpy(g_items[i].note, mu_path_filename(job.output), MU_TEXT - 1);
        else
            wcsncpy(g_items[i].note, err, MU_TEXT - 1);
        g_items[i].note[MU_TEXT - 1] = 0;

        PostMessageW(g_hwnd, WM_MU_ITEM, (WPARAM)i, 1);
    }

    PostMessageW(g_hwnd, WM_MU_DONE, 0, 0);
    return 0;
}

static void add_files_dialog(void)
{
    OPENFILENAMEW ofn;
    wchar_t buf[64 * 1024];
    wchar_t dir[MU_PATH];
    int added = 0;
    wchar_t *p;

    memset(&ofn, 0, sizeof(ofn));
    memset(buf, 0, sizeof(buf));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"加密音频 (*.ncm;*.kgm;*.kgma;*.vpr)\0*.ncm;*.kgm;*.kgma;*.vpr\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = 64 * 1024;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_HIDEREADONLY;
    ofn.lpstrTitle = L"选择加密音频文件";

    if (!GetOpenFileNameW(&ofn))
        return;

    p = buf;
    wcsncpy(dir, p, MU_PATH - 1);
    dir[MU_PATH - 1] = 0;
    p += wcslen(p) + 1;

    if (*p == 0) {
        if (add_item(dir))
            added = 1;
    } else {
        while (*p) {
            wchar_t full[MU_PATH];
            mu_join_path(full, MU_PATH, dir, p);
            if (add_item(full))
                added++;
            p += wcslen(p) + 1;
        }
    }

    refresh_list();
    if (added == 0)
        set_status(L"没有新增可处理的文件（只支持 .ncm / .kgm / .kgma / .vpr）");
    else
        set_status(L"已添加文件");
}

static int CALLBACK browse_cb(HWND hwnd, UINT msg, LPARAM lp, LPARAM data)
{
    (void)lp;
    (void)data;
    if (msg == BFFM_INITIALIZED)
        SendMessageW(hwnd, BFFM_SETSELECTIONW, TRUE, (LPARAM)g_outdir);
    return 0;
}

static void choose_outdir(void)
{
    BROWSEINFOW bi;
    LPITEMIDLIST pidl;
    wchar_t path[MU_PATH];

    memset(&bi, 0, sizeof(bi));
    bi.hwndOwner = g_hwnd;
    bi.lpszTitle = L"选择输出目录";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.lpfn = browse_cb;

    pidl = SHBrowseForFolderW(&bi);
    if (!pidl)
        return;
    if (SHGetPathFromIDListW(pidl, path)) {
        wcsncpy(g_outdir, path, MU_PATH - 1);
        g_outdir[MU_PATH - 1] = 0;
        mu_mkdir_recursive(g_outdir);
        set_path_label();
        set_status(L"输出目录已更新");
    }
    CoTaskMemFree(pidl);
}

static void add_folder_dialog(void)
{
    BROWSEINFOW bi;
    LPITEMIDLIST pidl;
    wchar_t path[MU_PATH];
    int added = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h;
    wchar_t pattern[MU_PATH];

    memset(&bi, 0, sizeof(bi));
    bi.hwndOwner = g_hwnd;
    bi.lpszTitle = L"选择包含加密音频的文件夹";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    pidl = SHBrowseForFolderW(&bi);
    if (!pidl)
        return;
    if (!SHGetPathFromIDListW(pidl, path)) {
        CoTaskMemFree(pidl);
        return;
    }
    CoTaskMemFree(pidl);

    mu_join_path(pattern, MU_PATH, path, L"*");
    h = FindFirstFileW(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            wchar_t full[MU_PATH];
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            mu_join_path(full, MU_PATH, path, fd.cFileName);
            if (add_item(full))
                added++;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    refresh_list();
    {
        wchar_t msg[128];
        _snwprintf(msg, 128, L"从文件夹添加了 %d 个文件", added);
        msg[127] = 0;
        set_status(msg);
    }
}

static void start_or_stop(void)
{
    int i;
    int todo = 0;

    if (g_running) {
        mu_cancel_flag = 1;
        set_status(L"正在停止…");
        EnableWindow(g_hStart, FALSE);
        return;
    }

    for (i = 0; i < g_count; i++) {
        if (g_items[i].status != ST_OK)
            todo++;
    }
    if (todo == 0) {
        set_status(g_count ? L"所有文件都已转换完成" : L"请先添加文件");
        return;
    }

    mu_mkdir_recursive(g_outdir);

    g_running = 1;
    g_total = todo;
    g_done = 0;
    SetWindowTextW(g_hStart, L"停止");
    SendMessageW(g_hProg, PBM_SETPOS, 0, 0);
    EnableWindow(GetDlgItem(g_hwnd, IDC_ADD), FALSE);
    EnableWindow(GetDlgItem(g_hwnd, IDC_ADDDIR), FALSE);
    EnableWindow(GetDlgItem(g_hwnd, IDC_REMOVE), FALSE);
    EnableWindow(GetDlgItem(g_hwnd, IDC_CLEAR), FALSE);
    EnableWindow(GetDlgItem(g_hwnd, IDC_BROWSE), FALSE);

    g_thread = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    if (!g_thread) {
        g_running = 0;
        SetWindowTextW(g_hStart, L"开始转换");
        set_status(L"无法创建转换线程");
    }
}

static void finish_run(void)
{
    int ok = 0, fail = 0, i;
    wchar_t msg[160];

    g_running = 0;
    SetWindowTextW(g_hStart, L"开始转换");
    EnableWindow(g_hStart, TRUE);
    EnableWindow(GetDlgItem(g_hwnd, IDC_ADD), TRUE);
    EnableWindow(GetDlgItem(g_hwnd, IDC_ADDDIR), TRUE);
    EnableWindow(GetDlgItem(g_hwnd, IDC_REMOVE), TRUE);
    EnableWindow(GetDlgItem(g_hwnd, IDC_CLEAR), TRUE);
    EnableWindow(GetDlgItem(g_hwnd, IDC_BROWSE), TRUE);
    if (g_thread) {
        CloseHandle(g_thread);
        g_thread = NULL;
    }

    for (i = 0; i < g_count; i++) {
        if (g_items[i].status == ST_OK)
            ok++;
        else if (g_items[i].status == ST_FAIL)
            fail++;
    }

    if (mu_cancel_flag) {
        _snwprintf(msg, 160, L"已停止（成功 %d，失败 %d）", ok, fail);
    } else if (fail == 0) {
        SendMessageW(g_hProg, PBM_SETPOS, 100, 0);
        _snwprintf(msg, 160, L"全部完成：成功 %d 个", ok);
    } else {
        SendMessageW(g_hProg, PBM_SETPOS, 100, 0);
        _snwprintf(msg, 160, L"完成：成功 %d，失败 %d（失败原因见列表）", ok, fail);
    }
    msg[159] = 0;
    set_status(msg);
}

static void remove_selected(void)
{
    int i;
    int w = 0;

    for (i = 0; i < g_count; i++) {
        if (ListView_GetItemState(g_hList, i, LVIS_SELECTED) & LVIS_SELECTED)
            continue;
        if (w != i)
            g_items[w] = g_items[i];
        w++;
    }
    g_count = w;
    refresh_list();
}

static void clear_all(void)
{
    g_count = 0;
    refresh_list();
    set_status(L"列表已清空");
}

static HWND mk_button(HWND parent, const wchar_t *text, int id, int x, int y, int w, int h)
{
    HWND b = CreateWindowExW(0, L"BUTTON", text,
                             WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                             x, y, w, h, parent, (HMENU)(INT_PTR)id, NULL, NULL);
    SendMessageW(b, WM_SETFONT, (WPARAM)g_font, TRUE);
    return b;
}

static void layout(HWND hwnd)
{
    RECT rc;
    int pad = 12;
    int y, listh;
    int w;
    int bottom;
    int btnh = 30;

    GetClientRect(hwnd, &rc);
    w = rc.right;
    bottom = rc.bottom - 100;

    {
        int x = pad;
        static const int ids[] = { IDC_ADD, IDC_ADDDIR, IDC_REMOVE, IDC_CLEAR };
        static const int wids[] = { 96, 108, 96, 72 };
        size_t k;

        for (k = 0; k < sizeof(ids) / sizeof(ids[0]); k++) {
            MoveWindow(GetDlgItem(hwnd, ids[k]), x, pad, wids[k], btnh, TRUE);
            x += wids[k] + 6;
        }
    }

    MoveWindow(g_hStart, w - pad - 128, pad, 128, btnh, TRUE);

    y = pad + btnh + 10;
    listh = bottom - y;
    if (listh < 80)
        listh = 80;
    MoveWindow(g_hList, pad, y, w - pad * 2, listh, TRUE);

    y = bottom + 8;
    MoveWindow(GetDlgItem(hwnd, IDC_OUTTAG), pad, y + 4, 60, 20, TRUE);
    MoveWindow(g_hOutLabel, pad + 62, y + 4, w - pad * 2 - 62 - 208, 20, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_BROWSE), w - pad - 200, y, 96, 28, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_OPEN), w - pad - 96, y, 96, 28, TRUE);

    MoveWindow(g_hProg, pad, y + 36, w - pad * 2, 16, TRUE);
    MoveWindow(g_hStatus, pad, y + 60, w - pad * 2, 22, TRUE);
}

static void init_outdir(void)
{
    wchar_t base[MU_PATH];

    base[0] = 0;
    if (SHGetFolderPathW(NULL, CSIDL_MYMUSIC, NULL, 0, base) != S_OK || !base[0]) {
        if (GetModuleFileNameW(NULL, base, MU_PATH) > 0) {
            wchar_t *slash = wcsrchr(base, L'\\');
            if (slash)
                *slash = 0;
        }
    }
    mu_join_path(g_outdir, MU_PATH, base, L"已解锁");
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        LVCOLUMNW col;
        g_hwnd = hwnd;

        g_font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        if (!g_font)
            g_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        g_fontBold = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");

        mk_button(hwnd, L"添加文件", IDC_ADD, 0, 0, 10, 10);
        mk_button(hwnd, L"添加文件夹", IDC_ADDDIR, 0, 0, 10, 10);
        mk_button(hwnd, L"移除选中", IDC_REMOVE, 0, 0, 10, 10);
        mk_button(hwnd, L"清空", IDC_CLEAR, 0, 0, 10, 10);
        g_hStart = mk_button(hwnd, L"开始转换", IDC_START, 0, 0, 10, 10);
        mk_button(hwnd, L"输出目录", IDC_BROWSE, 0, 0, 10, 10);
        mk_button(hwnd, L"打开目录", IDC_OPEN, 0, 0, 10, 10);

        g_hOutLabel = CreateWindowExW(0, L"STATIC", L"",
                                      WS_CHILD | WS_VISIBLE | SS_LEFT | SS_PATHELLIPSIS,
                                      0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)IDC_OUTLABEL, NULL, NULL);
        SendMessageW(g_hOutLabel, WM_SETFONT, (WPARAM)g_font, TRUE);

        {
            HWND tag = CreateWindowExW(0, L"STATIC", L"输出到",
                                       WS_CHILD | WS_VISIBLE | SS_LEFT,
                                       0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)IDC_OUTTAG, NULL, NULL);
            SendMessageW(tag, WM_SETFONT, (WPARAM)g_font, TRUE);
        }

        g_hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                  WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
                                  0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)IDC_LIST, NULL, NULL);
        SendMessageW(g_hList, WM_SETFONT, (WPARAM)g_font, TRUE);
        ListView_SetExtendedListViewStyle(g_hList,
            LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);

        memset(&col, 0, sizeof(col));
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        col.pszText = L"文件";
        col.cx = 380;
        SendMessageW(g_hList, LVM_INSERTCOLUMNW, 0, (LPARAM)&col);
        col.pszText = L"大小";
        col.cx = 90;
        SendMessageW(g_hList, LVM_INSERTCOLUMNW, 1, (LPARAM)&col);
        col.pszText = L"格式";
        col.cx = 70;
        SendMessageW(g_hList, LVM_INSERTCOLUMNW, 2, (LPARAM)&col);
        col.pszText = L"状态";
        col.cx = 320;
        SendMessageW(g_hList, LVM_INSERTCOLUMNW, 3, (LPARAM)&col);

        g_hProg = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE,
                                  0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)IDC_PROGRESS, NULL, NULL);
        SendMessageW(g_hProg, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

        g_hStatus = CreateWindowExW(0, L"STATIC", L"支持 .ncm / .kgm / .kgma / .vpr，解密后统一输出 MP3",
                                    WS_CHILD | WS_VISIBLE | SS_LEFT,
                                    0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)IDC_STATUS, NULL, NULL);
        SendMessageW(g_hStatus, WM_SETFONT, (WPARAM)g_font, TRUE);

        DragAcceptFiles(hwnd, TRUE);
        init_outdir();
        set_path_label();
        layout(hwnd);
        return 0;
    }

    case WM_SIZE:
        if (g_hList)
            layout(hwnd);
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        mm->ptMinTrackSize.x = 720;
        mm->ptMinTrackSize.y = 460;
        return 0;
    }

    case WM_DROPFILES: {
        HDROP hd = (HDROP)wp;
        UINT n = DragQueryFileW(hd, 0xFFFFFFFF, NULL, 0);
        UINT i;
        int added = 0;

        if (g_running) {
            DragFinish(hd);
            set_status(L"转换进行中，请先停止再添加文件");
            return 0;
        }
        for (i = 0; i < n; i++) {
            wchar_t path[MU_PATH];
            DWORD attr;

            DragQueryFileW(hd, i, path, MU_PATH);
            attr = GetFileAttributesW(path);
            if (attr == INVALID_FILE_ATTRIBUTES)
                continue;
            if (attr & FILE_ATTRIBUTE_DIRECTORY) {
                WIN32_FIND_DATAW fd;
                HANDLE h;
                wchar_t pattern[MU_PATH];

                mu_join_path(pattern, MU_PATH, path, L"*");
                h = FindFirstFileW(pattern, &fd);
                if (h != INVALID_HANDLE_VALUE) {
                    do {
                        wchar_t full[MU_PATH];
                        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                            continue;
                        mu_join_path(full, MU_PATH, path, fd.cFileName);
                        if (add_item(full))
                            added++;
                    } while (FindNextFileW(h, &fd));
                    FindClose(h);
                }
            } else if (add_item(path)) {
                added++;
            }
        }
        DragFinish(hd);
        refresh_list();
        {
            wchar_t msg[128];
            _snwprintf(msg, 128, L"已添加 %d 个文件", added);
            msg[127] = 0;
            set_status(msg);
        }
        return 0;
    }

    case WM_MU_ITEM:
        if (lp == 0)
            set_item_status((int)wp, ST_RUNNING, NULL);
        else {
            int idx = (int)wp;
            set_item_status(idx, g_items[idx].status, g_items[idx].note);
            g_done++;
            {
                wchar_t msg[256];
                _snwprintf(msg, 256, L"正在转换 %d / %d", g_done, g_total);
                msg[255] = 0;
                set_status(msg);
            }
        }
        return 0;

    case WM_MU_PROG:
        SendMessageW(g_hProg, PBM_SETPOS, wp, 0);
        return 0;

    case WM_MU_DONE:
        finish_run();
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_ADD:
            add_files_dialog();
            break;
        case IDC_ADDDIR:
            add_folder_dialog();
            break;
        case IDC_REMOVE:
            remove_selected();
            break;
        case IDC_CLEAR:
            clear_all();
            break;
        case IDC_START:
            start_or_stop();
            break;
        case IDC_BROWSE:
            choose_outdir();
            break;
        case IDC_OPEN:
            mu_mkdir_recursive(g_outdir);
            ShellExecuteW(NULL, L"open", g_outdir, NULL, NULL, SW_SHOWNORMAL);
            break;
        default:
            break;
        }
        return 0;

    case WM_DESTROY:
        mu_cancel_flag = 1;
        if (g_thread) {
            WaitForSingleObject(g_thread, 3000);
            CloseHandle(g_thread);
        }
        if (g_fontBold)
            DeleteObject(g_fontBold);
        if (g_font)
            DeleteObject(g_font);
        free(g_items);
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, LPWSTR cmd, int show)
{
    WNDCLASSEXW wc;
    HWND hwnd;
    MSG msg;
    INITCOMMONCONTROLSEX icc;

    (void)hPrev;
    (void)cmd;

    /* Per-monitor DPI awareness, resolved at runtime so the binary still runs
     * on older Windows builds. Without this the UI gets bitmap-scaled. */
    {
        HMODULE u32 = GetModuleHandleW(L"user32.dll");
        typedef BOOL (WINAPI *SetCtxFn)(void *);
        SetCtxFn setctx = u32 ? (SetCtxFn)(void *)GetProcAddress(u32, "SetProcessDpiAwarenessContext") : NULL;
        if (!setctx || !setctx((void *)(INT_PTR)-4))   /* PER_MONITOR_AWARE_V2 */
            SetProcessDPIAware();
    }

    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndproc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"MusicUnlockWnd";
    wc.hIcon = LoadIconW(hInst, L"APPICON");
    wc.hIconSm = wc.hIcon;
    if (!wc.hIcon)
        wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);

    hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, L"MusicUnlockWnd", L"音乐解锁 · 加密音频转 MP3",
                           WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                           CW_USEDEFAULT, CW_USEDEFAULT, 960, 620,
                           NULL, NULL, hInst, NULL);
    if (!hwnd)
        return 0;

    /* Optional command line: MusicUnlock.exe [-o 输出目录] <文件...> */
    {
        int argc = 0;
        LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        int i;
        int added = 0;

        if (argv) {
            for (i = 1; i < argc; i++) {
                if (wcscmp(argv[i], L"-o") == 0 && i + 1 < argc) {
                    wcsncpy(g_outdir, argv[++i], MU_PATH - 1);
                    g_outdir[MU_PATH - 1] = 0;
                } else if (GetFileAttributesW(argv[i]) != INVALID_FILE_ATTRIBUTES) {
                    if (add_item(argv[i]))
                        added++;
                }
            }
            LocalFree(argv);
        }
        if (added > 0) {
            refresh_list();
            {
                wchar_t m[128];
                _snwprintf(m, 128, L"已载入 %d 个文件，点“开始转换”即可", added);
                m[127] = 0;
                set_status(m);
            }
        }
        set_path_label();
    }

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return 0;
}
