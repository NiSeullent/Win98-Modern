/* Direct-call ABI/behavior smoke for the KernelEx SHELL32 API library. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#define CINTERFACE
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#include "../src/kex_abi.h"

typedef HRESULT (WINAPI *create_fn)(PCWSTR,IBindCtx *,REFIID,void **);
typedef HRESULT (WINAPI *parse_fn)(PCWSTR,IBindCtx *,LPITEMIDLIST *,SFGAOF,SFGAOF *);
typedef HRESULT (WINAPI *select_fn)(LPCITEMIDLIST,UINT,LPCITEMIDLIST const *,DWORD);
typedef const m98_api_table *(*table_fn)(void);

static const GUID iid_unknown={0,0,0,{0xc0,0,0,0,0,0,0,0x46}};
static const GUID iid_item={0x43826d1e,0xe718,0x42ee,{0xbc,0x55,0xa1,0xe2,0x61,0xc3,0x7b,0xfe}};
static const GUID iid_item2={0x7e9fb0d3,0x919f,0x4307,{0xab,0x2e,0x9b,0x18,0x60,0x31,0x0c,0x93}};
static const GUID iid_shell_folder={0x000214e6,0,0,{0xc0,0,0,0,0,0,0,0x46}};
static const GUID bhid_sf_object={0x3981e224,0xf559,0x11d3,{0x8e,0x3a,0,0xc0,0x4f,0x68,0x37,0xd5}};

static void report(const char *message)
{
    DWORD length=0,written;
    while (message[length]) length++;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),message,length,&written,NULL);
}

static void fail(const char *message)
{
    report("FAIL: ");report(message);report("\r\n");ExitProcess(1);
}

static int equal_ascii(const char *left,const char *right)
{
    while (*left && *left==*right) {left++;right++;}
    return *left==*right;
}

static int equal_wide(const WCHAR *left,const WCHAR *right)
{
    while (*left && *left==*right) {left++;right++;}
    return *left==*right;
}

static int has_separator(const WCHAR *text)
{
    while (*text) {
        if (*text==L'\\' || *text==L'/') return 1;
        text++;
    }
    return 0;
}

static int ends_ascii(const char *text,const char *suffix)
{
    unsigned a=0,b=0,i;
    while (text[a]) a++;
    while (suffix[b]) b++;
    if (b>a) return 0;
    for (i=0;i<b;i++) {
        char x=text[a-b+i],y=suffix[i];
        if (x>='A' && x<='Z') x+='a'-'A';
        if (y>='A' && y<='Z') y+='a'-'A';
        if (x!=y) return 0;
    }
    return 1;
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
    HMODULE shim=LoadLibraryA("m98shell.dll");
    table_fn get_table;
    const m98_api_table *tables;
    create_fn create;
    parse_fn parse;
    select_fn select;
    char path_a[MAX_PATH];
    char parsed_path[MAX_PATH];
    WCHAR path_w[MAX_PATH];
    LPITEMIDLIST pidl=NULL;
    IShellItem *item=NULL,*parent=NULL;
    IUnknown *unknown=NULL;
    IShellFolder *folder=NULL;
    LPWSTR name=NULL;
    LPWSTR native_name=NULL;
    IShellItem *native_item=NULL;
    create_fn native_create=NULL;
    SFGAOF attributes;
    int order=42;
    void *unsupported=(void *)0x1234;
    if (!shim) fail("load m98shell.dll");
    get_table=(table_fn)GetProcAddress(shim,"get_api_table");
    if (!get_table) fail("get_api_table export");
    tables=get_table();
    if (!tables || !equal_ascii(tables[0].target_library,"SHELL32.DLL") ||
        tables[0].named_apis_count!=3 ||
        !equal_ascii(tables[0].named_apis[0].name,"SHCreateItemFromParsingName") ||
        !tables[0].named_apis[0].addr ||
        !equal_ascii(tables[0].named_apis[1].name,"SHOpenFolderAndSelectItems") ||
        !tables[0].named_apis[1].addr ||
        !equal_ascii(tables[0].named_apis[2].name,"SHParseDisplayName") ||
        !tables[0].named_apis[2].addr || tables[1].target_library)
        fail("KernelEx SHELL32 API table");
    create=(create_fn)tables[0].named_apis[0].addr;
    select=(select_fn)tables[0].named_apis[1].addr;
    parse=(parse_fn)tables[0].named_apis[2].addr;
    if (select(NULL,0,NULL,0)!=E_INVALIDARG)
        fail("SHOpenFolderAndSelectItems invalid argument");
    if (!GetModuleFileNameA(NULL,path_a,sizeof(path_a)) ||
        !MultiByteToWideChar(CP_ACP,0,path_a,-1,path_w,MAX_PATH))
        fail("own module path");
    CoInitialize(NULL);
    /* Windows 98 has no native SHCreateItemFromParsingName. On modern hosts,
     * use the native IShellItem as a behavioral reference for editing names. */
    if ((GetVersion()&0xff)>=6) {
        native_create=(create_fn)GetProcAddress(GetModuleHandleA("SHELL32.DLL"),
                                                "SHCreateItemFromParsingName");
        if (!native_create || native_create(path_w,NULL,&iid_item,
                                             (void **)&native_item)!=S_OK || !native_item)
            fail("modern host native IShellItem reference");
    }
    pidl=(LPITEMIDLIST)1;
    attributes=0x12345678;
    if (parse(NULL,NULL,&pidl,SFGAO_FILESYSTEM,&attributes)!=E_INVALIDARG ||
        pidl || attributes!=0) fail("parse invalid name outputs");
    attributes=0x12345678;
    if (parse(path_w,NULL,NULL,SFGAO_FILESYSTEM,&attributes)!=E_INVALIDARG ||
        attributes!=0) fail("parse invalid PIDL pointer");
    attributes=0x12345678;
    if (parse(path_w,NULL,&pidl,SFGAO_FILESYSTEM|SFGAO_FOLDER,&attributes)!=S_OK ||
        !pidl || attributes!=SFGAO_FILESYSTEM ||
        !SHGetPathFromIDListA(pidl,parsed_path) ||
        !ends_ascii(parsed_path,"m98shell_smoke.exe"))
        fail("parse PIDL and requested attribute mask");
    CoTaskMemFree(pidl);pidl=NULL;
    if (parse(path_w,NULL,&pidl,SFGAO_FILESYSTEM,NULL)!=S_OK || !pidl)
        fail("parse without optional attribute output");
    CoTaskMemFree(pidl);pidl=NULL;
    attributes=0x12345678;
    if (parse(path_w,NULL,&pidl,0,&attributes)!=S_OK || !pidl || attributes!=0)
        fail("parse zero requested attributes");
    CoTaskMemFree(pidl);pidl=NULL;
    if (create(path_w,NULL,&iid_item,NULL)!=E_POINTER ||
        create(NULL,NULL,&iid_item,(void **)&item)!=E_INVALIDARG || item ||
        create(path_w,NULL,NULL,(void **)&item)!=E_INVALIDARG || item)
        fail("creation argument validation");
    if (create(path_w,NULL,&iid_item2,&unsupported)!=E_NOINTERFACE || unsupported)
        fail("IShellItem2 unsupported explicitly");
    if (create(path_w,NULL,&iid_item,(void **)&item)!=S_OK || !item)
        fail("create existing file shell item");
    if (IShellItem_QueryInterface(item,&iid_unknown,(void **)&unknown)!=S_OK ||
        unknown!=(IUnknown *)item) fail("IUnknown identity");
    IUnknown_Release(unknown);
    if (IShellItem_GetDisplayName(item,SIGDN_FILESYSPATH,&name)!=S_OK ||
        !name || !ends_wide(name,L"m98shell_smoke.exe"))
        fail("file-system path name");
    CoTaskMemFree(name);name=NULL;
    if (IShellItem_GetDisplayName(item,SIGDN_NORMALDISPLAY,&name)!=S_OK ||
        !name || !*name) fail("normal display name");
    CoTaskMemFree(name);name=NULL;
    if (IShellItem_GetDisplayName(item,SIGDN_PARENTRELATIVEEDITING,&name)!=S_OK ||
        !name || !*name || has_separator(name)) fail("parent-relative editing name");
    if (native_item &&
        (IShellItem_GetDisplayName(native_item,SIGDN_PARENTRELATIVEEDITING,&native_name)!=S_OK ||
         !native_name || !equal_wide(name,native_name)))
        fail("parent-relative editing differs from native host");
    if (native_name) CoTaskMemFree(native_name);
    native_name=NULL;
    CoTaskMemFree(name);name=NULL;
    if (IShellItem_GetDisplayName(item,SIGDN_DESKTOPABSOLUTEEDITING,&name)!=S_OK ||
        !name || !*name || !has_separator(name)) fail("desktop-absolute editing name");
    if (native_item &&
        (IShellItem_GetDisplayName(native_item,SIGDN_DESKTOPABSOLUTEEDITING,&native_name)!=S_OK ||
         !native_name || !equal_wide(name,native_name)))
        fail("desktop-absolute editing differs from native host");
    if (native_name) CoTaskMemFree(native_name);
    native_name=NULL;
    CoTaskMemFree(name);name=NULL;
    if (IShellItem_GetDisplayName(item,SIGDN_URL,&name)!=E_INVALIDARG || name)
        fail("unsupported name kind");
    attributes=0;
    if (IShellItem_GetAttributes(item,SFGAO_FILESYSTEM|SFGAO_FOLDER,&attributes)!=S_FALSE ||
        !(attributes&SFGAO_FILESYSTEM) || (attributes&SFGAO_FOLDER))
        fail("native file attributes");
    if (IShellItem_Compare(item,item,0,&order)!=S_OK || order!=0)
        fail("same item comparison");
    if (IShellItem_GetParent(item,&parent)!=S_OK || !parent)
        fail("parent item");
    if (IShellItem_BindToHandler(parent,NULL,&bhid_sf_object,&iid_shell_folder,
                                (void **)&folder)!=S_OK || !folder)
        fail("bind parent to native shell folder");
    IShellFolder_Release(folder);
    if (IShellItem_Release(parent)!=0 || IShellItem_Release(item)!=0)
        fail("COM reference ownership");
    if (native_item) IShellItem_Release(native_item);
    CoUninitialize();
    FreeLibrary(shim);
    report("PASS: KernelEx SHELL32 item and SHParseDisplayName ABI, PIDL, attributes, editing names\r\n");
    ExitProcess(0);
}
