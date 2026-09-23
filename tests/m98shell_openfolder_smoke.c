/* Direct-call host/guest smoke for the cidl == 0 Explorer selection bridge. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#define CINTERFACE
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>

HRESULT WINAPI m98_SHOpenFolderAndSelectItems(LPCITEMIDLIST,UINT,
                                               LPCITEMIDLIST const *,DWORD);

static void report(const char *message)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),message,lstrlenA(message),&written,NULL);
}

static void fail(const char *message)
{
    report("FAIL: ");report(message);report("\r\n");ExitProcess(1);
}

void mainCRTStartup(void)
{
    char temp_base[MAX_PATH],temp_dir[MAX_PATH],file_path[MAX_PATH];
    WCHAR wide_path[MAX_PATH];
    IShellFolder *desktop=NULL;
    LPITEMIDLIST pidl=NULL,virtual_pidl=NULL;
    HANDLE file;
    HRESULT hr;
    DWORD temp_length;

    if (m98_SHOpenFolderAndSelectItems(NULL,0,NULL,0)!=E_INVALIDARG)
        fail("NULL PIDL argument");
    temp_length=GetTempPathA(MAX_PATH,temp_base);
    if (!temp_length || temp_length>=MAX_PATH) fail("temporary path");
    if (!GetTempFileNameA(temp_base,"m98",0,temp_dir)) fail("unique temp name");
    if (!DeleteFileA(temp_dir) || !CreateDirectoryA(temp_dir,NULL))
        fail("temporary directory");
    if (lstrlenA(temp_dir)+sizeof("\\target, one.txt")>MAX_PATH)
        fail("test path length");
    lstrcpyA(file_path,temp_dir);
    lstrcatA(file_path,"\\target, one.txt");
    file=CreateFileA(file_path,GENERIC_WRITE,0,NULL,CREATE_NEW,
                     FILE_ATTRIBUTE_NORMAL,NULL);
    if (file==INVALID_HANDLE_VALUE) fail("create ordinary test file");
    CloseHandle(file);
    if (!MultiByteToWideChar(CP_ACP,0,file_path,-1,wide_path,MAX_PATH))
        fail("wide test path");
    hr=CoInitialize(NULL);
    if (FAILED(hr)) fail("COM initialization");
    hr=SHGetSpecialFolderLocation(NULL,CSIDL_DRIVES,&virtual_pidl);
    if (FAILED(hr) || !virtual_pidl) fail("virtual drives PIDL");
    if (m98_SHOpenFolderAndSelectItems(virtual_pidl,0,NULL,0)!=E_NOTIMPL)
        fail("non-filesystem PIDL must fail explicitly");
    CoTaskMemFree(virtual_pidl);
    hr=SHGetDesktopFolder(&desktop);
    if (FAILED(hr) || !desktop) fail("desktop folder");
    hr=IShellFolder_ParseDisplayName(desktop,NULL,NULL,wide_path,NULL,&pidl,NULL);
    IShellFolder_Release(desktop);
    if (FAILED(hr) || !pidl) fail("parse file PIDL");
    if (m98_SHOpenFolderAndSelectItems(pidl,1,NULL,0)!=E_NOTIMPL)
        fail("selection arrays must fail explicitly");
    if (m98_SHOpenFolderAndSelectItems(pidl,0,NULL,1)!=E_NOTIMPL)
        fail("unsupported flags must fail explicitly");
    if (!DeleteFileA(file_path)) fail("remove file for missing-item case");
    if (SUCCEEDED(m98_SHOpenFolderAndSelectItems(pidl,0,NULL,0)))
        fail("missing item must not launch Explorer or report success");
    file=CreateFileA(file_path,GENERIC_WRITE,0,NULL,CREATE_NEW,
                     FILE_ATTRIBUTE_NORMAL,NULL);
    if (file==INVALID_HANDLE_VALUE) fail("restore ordinary test file");
    CloseHandle(file);
    report("Selecting ordinary file: ");report(file_path);report("\r\n");
    hr=m98_SHOpenFolderAndSelectItems(pidl,0,NULL,0);
    CoTaskMemFree(pidl);
    CoUninitialize();
    if (hr!=S_OK) fail("Explorer /select dispatch");
    report("PASS: Explorer /select dispatched; inspect the highlighted file in Explorer.\r\n");
    report("The test leaves its unique temporary directory for inspection.\r\n");
    ExitProcess(0);
}
