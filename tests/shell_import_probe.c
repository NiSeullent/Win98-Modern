/*
 * Static SHELL32 import probe. On Windows 98 the loader can resolve this
 * post-Win98 entry points only when KernelEx routes them to m98shell's table.
 * The host smoke verifies behavior, but only the guest proves that routing.
 */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#define CINTERFACE
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <shobjidl.h>

__declspec(dllimport) HRESULT WINAPI SHCreateItemFromParsingName(
    PCWSTR name, IBindCtx *context, REFIID iid, void **item);
__declspec(dllimport) HRESULT WINAPI SHParseDisplayName(
    PCWSTR name, IBindCtx *context, LPITEMIDLIST *pidl,
    SFGAOF requested, SFGAOF *attributes);
__declspec(dllimport) HRESULT WINAPI SHOpenFolderAndSelectItems(
    LPCITEMIDLIST pidlFolder, UINT cidl, LPCITEMIDLIST const *apidl,
    DWORD flags);

static const GUID iid_item={0x43826d1e,0xe718,0x42ee,
                            {0xbc,0x55,0xa1,0xe2,0x61,0xc3,0x7b,0xfe}};

static void report(const char *message)
{
    DWORD length=0,written;
    while (message[length]) length++;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),message,length,&written,NULL);
}

static int ends_wide(const WCHAR *text,const WCHAR *suffix)
{
    unsigned a=0,b=0,i;
    while (text[a]) a++;
    while (suffix[b]) b++;
    if (b>a) return 0;
    for (i=0;i<b;i++) {
        WCHAR x=text[a-b+i],y=suffix[i];
        if (x>=L'A' && x<=L'Z') x+=L'a'-L'A';
        if (y>=L'A' && y<=L'Z') y+=L'a'-L'A';
        if (x!=y) return 0;
    }
    return 1;
}

void mainCRTStartup(void)
{
    char path_a[MAX_PATH];
    WCHAR path_w[MAX_PATH];
    IShellItem *item=NULL;
    LPITEMIDLIST pidl=NULL;
    LPWSTR name=NULL;
    SFGAOF attributes=0;
    HRESULT hr;
    if (!GetModuleFileNameA(NULL,path_a,sizeof(path_a)) ||
        !MultiByteToWideChar(CP_ACP,0,path_a,-1,path_w,MAX_PATH)) {
        report("FAIL: shell static import probe path\r\n");
        ExitProcess(1);
    }
    CoInitialize(NULL);
    hr=SHParseDisplayName(path_w,NULL,&pidl,
                          SFGAO_FILESYSTEM|SFGAO_FOLDER,&attributes);
    if (hr!=S_OK || !pidl || attributes!=SFGAO_FILESYSTEM) {
        report("FAIL: SHELL32 static SHParseDisplayName PIDL or attributes\r\n");
        if (pidl) CoTaskMemFree(pidl);
        CoUninitialize();
        ExitProcess(1);
    }
    hr=SHOpenFolderAndSelectItems(pidl,0,NULL,0);
    CoTaskMemFree(pidl);
    if (hr!=S_OK) {
        report("FAIL: SHELL32 static SHOpenFolderAndSelectItems call\r\n");
        CoUninitialize();
        ExitProcess(1);
    }
    hr=SHCreateItemFromParsingName(path_w,NULL,&iid_item,(void **)&item);
    if (hr!=S_OK || !item) {
        report("FAIL: SHELL32 static SHCreateItemFromParsingName call\r\n");
        CoUninitialize();
        ExitProcess(1);
    }
    hr=IShellItem_GetDisplayName(item,SIGDN_FILESYSPATH,&name);
    if (hr!=S_OK || !name || !ends_wide(name,L"shell_import_probe.exe")) {
        report("FAIL: SHELL32 static import item behavior\r\n");
        if (name) CoTaskMemFree(name);
        IShellItem_Release(item);
        CoUninitialize();
        ExitProcess(1);
    }
    CoTaskMemFree(name);
    IShellItem_Release(item);
    CoUninitialize();
    report("PASS: static SHELL32 parsing, item and Explorer selection imports\r\n");
    ExitProcess(0);
}
