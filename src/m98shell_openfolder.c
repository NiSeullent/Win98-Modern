/*
 * Windows 98 Shell selection bridge for SHOpenFolderAndSelectItems.
 * Copyright (C) 2026 Win98-Modern contributors. GPL-2.0-only.
 *
 * Contract reviewed against Wine dlls/shell32/shlfolder.c at
 * df15af3652511150490934682202d45af892f887 and ReactOS
 * dll/win32/shell32/shlfolder.cpp at
 * 9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8. Both reference files
 * carry LGPL-2.1-or-later notices. No source was copied.
 * Wine's WM_COPYDATA selection packet is private to Wine Explorer and cannot
 * be sent to Windows 98 Explorer. Microsoft's KB Q130510 documents native
 * Win98 Explorer /select,<item> syntax, which opens the parent and selects the
 * item. This bridge implements that filesystem, cidl == 0, flags == 0 case.
 */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

/* This older spelling has the same four-argument, stdcall x86 ABI as the
 * modern PCIDLIST_ABSOLUTE / PCUITEMID_CHILD_ARRAY declaration. It builds
 * with the Win98-targeted MinGW headers used by m98shell.c. */
HRESULT WINAPI m98_SHOpenFolderAndSelectItems(LPCITEMIDLIST pidlFolder,
                                               UINT cidl,
                                               LPCITEMIDLIST const *apidl,
                                               DWORD flags)
{
    static const char explorer_name[] = "\\explorer.exe";
    static const char select_prefix[] = "/select,\"";
    char path[MAX_PATH];
    char explorer[MAX_PATH];
    char parameters[MAX_PATH + sizeof(select_prefix) + 1];
    SHELLEXECUTEINFOA sei;
    volatile BYTE *sei_bytes;
    DWORD attributes, error;
    UINT windows_length;
    SIZE_T i,path_length;

    (void)apidl;
    if (!pidlFolder || !pidlFolder->mkid.cb) return E_INVALIDARG;
    if (cidl || flags) return E_NOTIMPL;

    /* A non-filesystem PIDL cannot be represented by Explorer's /select
     * command. Do not report success for an item we cannot select. */
    if (!SHGetPathFromIDListA(pidlFolder,path) || !path[0]) return E_NOTIMPL;
    path_length=lstrlenA(path);
    if (path_length<3) return E_NOTIMPL;
    if (path_length>=MAX_PATH) return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
    if (!(((path[0]>='A' && path[0]<='Z') ||
           (path[0]>='a' && path[0]<='z')) && path[1]==':' && path[2]=='\\') &&
        !(path[0]=='\\' && path[1]=='\\' && path[2]!='?' && path[2]!='.'))
        return E_NOTIMPL;
    for (i=0;i<path_length;i++)
        if (path[i]=='"' || (unsigned char)path[i]<32) return E_INVALIDARG;

    attributes=GetFileAttributesA(path);
    if (attributes==INVALID_FILE_ATTRIBUTES) {
        error=GetLastError();
        return HRESULT_FROM_WIN32(error ? error : ERROR_FILE_NOT_FOUND);
    }

    windows_length=GetWindowsDirectoryA(explorer,MAX_PATH);
    if (!windows_length || windows_length>=MAX_PATH-sizeof(explorer_name))
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    if (explorer[windows_length-1]=='\\') explorer[windows_length-1]=0;
    lstrcatA(explorer,explorer_name);
    attributes=GetFileAttributesA(explorer);
    if (attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_DIRECTORY))
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

    lstrcpyA(parameters,select_prefix);
    lstrcatA(parameters,path);
    lstrcatA(parameters,"\"");
    sei_bytes=(volatile BYTE *)&sei;
    for (i=0;i<sizeof(sei);i++) sei_bytes[i]=0;
    sei.cbSize=sizeof(sei);
    /* The DDEWAIT flag is the old name of NOASYNC. It waits for Explorer's
     * DDE handoff on Win98 instead of returning while a short-lived caller
     * can still be required to pump the launch. */
    sei.fMask=SEE_MASK_FLAG_DDEWAIT | SEE_MASK_FLAG_NO_UI;
    sei.lpFile=explorer;
    sei.lpParameters=parameters;
    sei.nShow=SW_SHOWNORMAL;
    SetLastError(0);
    if (!ShellExecuteExA(&sei)) {
        error=GetLastError();
        return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
    }
    return S_OK;
}
