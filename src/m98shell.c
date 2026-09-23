/*
 * Win98 Shell API library for KernelEx: SHCreateItemFromParsingName,
 * SHOpenFolderAndSelectItems, and SHParseDisplayName.
 * Copyright (C) 2026 Win98-Modern contributors. GPL-2.0-only.
 *
 * Contract and PIDL-backed COM design reviewed against Wine
 * dlls/shell32/shellitem.c and pidl.c at
 * df15af3652511150490934682202d45af892f887 and ReactOS
 * dll/win32/shell32/CShellItem.cpp and wine/pidl.c at
 * 9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8. This is a new Win98
 * implementation using its
 * native IShellFolder, task allocator, and ANSI file-system path API; no
 * Wine/ReactOS source code is copied. The same parse-to-PIDL flow is required
 * by Microsoft's public SHCreateItemFromParsingName and SHParseDisplayName
 * contracts.
 *
 * Supported interface: IUnknown and IShellItem, with real namespace parsing,
 * reference counting, parent/attribute/display-name queries, comparison, and
 * native shell-folder/UI-object binding. IShellItem2 and unsupported BHIDs
 * fail explicitly; this DLL does not claim to be a complete modern Shell.
 */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#define CINTERFACE
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#include "kex_abi.h"

typedef struct m98_shell_item {
    IShellItem iface;
    LONG refs;
    LPITEMIDLIST pidl;
} m98_shell_item;

static const GUID m98_iid_unknown = {0x00000000,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
static const GUID m98_iid_shell_item = {0x43826d1e,0xe718,0x42ee,{0xbc,0x55,0xa1,0xe2,0x61,0xc3,0x7b,0xfe}};
static const GUID m98_iid_shell_folder = {0x000214e6,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
static const GUID m98_iid_data_object = {0x0000010e,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
static const GUID m98_bhid_sf_object = {0x3981e224,0xf559,0x11d3,{0x8e,0x3a,0x00,0xc0,0x4f,0x68,0x37,0xd5}};
static const GUID m98_bhid_sf_ui_object = {0x3981e225,0xf559,0x11d3,{0x8e,0x3a,0x00,0xc0,0x4f,0x68,0x37,0xd5}};
static const GUID m98_bhid_data_object = {0xb8c0bd9f,0xed24,0x455c,{0x83,0xe6,0xd5,0x39,0x0c,0x4f,0xe8,0xc4}};

static int guid_equal(REFGUID left, REFGUID right)
{
    const BYTE *a=(const BYTE *)left,*b=(const BYTE *)right;
    unsigned int i;
    if (!a || !b) return 0;
    for (i=0;i<sizeof(GUID);i++) if (a[i]!=b[i]) return 0;
    return 1;
}

static void byte_copy(BYTE *dst,const BYTE *src,SIZE_T count)
{
    while (count--) *dst++=*src++;
}

static SIZE_T wide_length(LPCWSTR text)
{
    SIZE_T length=0;
    while (text[length]) length++;
    return length;
}

/* Native IShellFolder::ParseDisplayName receives a mutable string even though
 * the public SHCreateItemFromParsingName argument is const. */
static HRESULT parse_name(LPCWSTR name, IBindCtx *context, LPITEMIDLIST *pidl,
                          SFGAOF requested, SFGAOF *attributes)
{
    IShellFolder *desktop=NULL;
    WCHAR *mutable_name;
    SIZE_T length;
    SFGAOF native_attributes=requested;
    HRESULT hr;
    if (attributes) *attributes=0;
    if (!pidl) return E_INVALIDARG;
    *pidl=NULL;
    if (!name) return E_INVALIDARG;
    length=wide_length(name);
    if (length>32767 || length>(SIZE_T)-1/sizeof(WCHAR)-1) return E_INVALIDARG;
    mutable_name=(WCHAR *)CoTaskMemAlloc((length+1)*sizeof(WCHAR));
    if (!mutable_name) return E_OUTOFMEMORY;
    byte_copy((BYTE *)mutable_name,(const BYTE *)name,(length+1)*sizeof(WCHAR));
    hr=SHGetDesktopFolder(&desktop);
    if (SUCCEEDED(hr)) {
        /* The public SHParseDisplayName has no pcchEaten output. Wine's
         * pinned pidl.c and ReactOS's pinned wine/pidl.c use the desktop
         * folder's ParseDisplayName. ReactOS duplicates the const input and
         * masks returned attributes; do both here for Win98's mutable ABI.
         * A NULL attribute output means no attribute query is requested. */
        hr=IShellFolder_ParseDisplayName(desktop,NULL,context,mutable_name,NULL,
                                         pidl,attributes?&native_attributes:NULL);
        IShellFolder_Release(desktop);
    }
    CoTaskMemFree(mutable_name);
    if (SUCCEEDED(hr) && !*pidl) hr=E_FAIL;
    if (FAILED(hr) && *pidl) {
        CoTaskMemFree(*pidl);
        *pidl=NULL;
    }
    if (SUCCEEDED(hr) && attributes) *attributes=native_attributes&requested;
    return hr;
}

/* Return the terminating two bytes too. Cap at 64 KiB to reject malformed
 * lists before copying; parsing only returns system-created PIDLs. */
static SIZE_T pidl_size(LPCITEMIDLIST pidl)
{
    const BYTE *p=(const BYTE *)pidl;
    SIZE_T total=0;
    if (!p) return 0;
    for (;;) {
        WORD cb=(WORD)(p[0]|((WORD)p[1]<<8));
        if (!cb) return total+2;
        if (cb<2 || total+cb>65534) return 0;
        total+=cb;p+=cb;
    }
}

static LPITEMIDLIST clone_pidl(LPCITEMIDLIST pidl)
{
    SIZE_T size=pidl_size(pidl);
    LPITEMIDLIST copy;
    if (!size) return NULL;
    copy=(LPITEMIDLIST)CoTaskMemAlloc(size);
    if (copy) byte_copy((BYTE *)copy,(const BYTE *)pidl,size);
    return copy;
}

static LPCITEMIDLIST last_id(LPCITEMIDLIST pidl)
{
    const BYTE *p=(const BYTE *)pidl,*last=p;
    while (p && (p[0] || p[1])) {
        WORD cb=(WORD)(p[0]|((WORD)p[1]<<8));
        if (cb<2) return NULL;
        last=p;p+=cb;
    }
    return (LPCITEMIDLIST)last;
}

static LPITEMIDLIST parent_pidl(LPCITEMIDLIST pidl)
{
    LPITEMIDLIST parent=clone_pidl(pidl);
    BYTE *last;
    if (!parent) return NULL;
    last=(BYTE *)last_id(parent);
    if (!last || last==(BYTE *)parent) {
        CoTaskMemFree(parent);
        return NULL;
    }
    last[0]=last[1]=0;
    return parent;
}

static HRESULT new_item_owning_pidl(LPITEMIDLIST pidl,IShellItem **out);

static HRESULT WINAPI item_QueryInterface(IShellItem *iface,REFIID iid,void **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    if (!iid) return E_INVALIDARG;
    if (!guid_equal(iid,&m98_iid_unknown) && !guid_equal(iid,&m98_iid_shell_item))
        return E_NOINTERFACE;
    *out=iface;
    IShellItem_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI item_AddRef(IShellItem *iface)
{
    m98_shell_item *item=(m98_shell_item *)iface;
    return (ULONG)InterlockedIncrement(&item->refs);
}

static ULONG WINAPI item_Release(IShellItem *iface)
{
    m98_shell_item *item=(m98_shell_item *)iface;
    ULONG refs=(ULONG)InterlockedDecrement(&item->refs);
    if (!refs) {
        CoTaskMemFree(item->pidl);
        CoTaskMemFree(item);
    }
    return refs;
}

static HRESULT get_parent_folder(m98_shell_item *item,IShellFolder **out)
{
    LPITEMIDLIST parent;
    IShellFolder *desktop=NULL;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out=NULL;
    parent=parent_pidl(item->pidl);
    if (!parent) return E_INVALIDARG;
    hr=SHGetDesktopFolder(&desktop);
    if (SUCCEEDED(hr)) {
        if (!parent->mkid.cb) {
            *out=desktop;
            desktop=NULL;
        } else hr=IShellFolder_BindToObject(desktop,parent,NULL,&m98_iid_shell_folder,(void **)out);
    }
    if (desktop) IShellFolder_Release(desktop);
    CoTaskMemFree(parent);
    return hr;
}

static HRESULT WINAPI item_BindToHandler(IShellItem *iface,IBindCtx *context,
                                          REFGUID bhid,REFIID iid,void **out)
{
    m98_shell_item *item=(m98_shell_item *)iface;
    IShellFolder *folder=NULL,*desktop=NULL;
    LPCITEMIDLIST child;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out=NULL;
    if (!bhid || !iid) return E_INVALIDARG;
    if (guid_equal(bhid,&m98_bhid_sf_object)) {
        hr=SHGetDesktopFolder(&desktop);
        if (FAILED(hr)) return hr;
        if (!item->pidl->mkid.cb) hr=IShellFolder_QueryInterface(desktop,iid,out);
        else hr=IShellFolder_BindToObject(desktop,item->pidl,context,iid,out);
        IShellFolder_Release(desktop);
        return hr;
    }
    if (!guid_equal(bhid,&m98_bhid_sf_ui_object) &&
        !guid_equal(bhid,&m98_bhid_data_object)) return MK_E_NOOBJECT;
    hr=get_parent_folder(item,&folder);
    if (FAILED(hr)) return hr;
    child=last_id(item->pidl);
    if (!child) hr=E_INVALIDARG;
    else hr=IShellFolder_GetUIObjectOf(folder,NULL,1,&child,
        guid_equal(bhid,&m98_bhid_data_object)?&m98_iid_data_object:iid,NULL,out);
    if (SUCCEEDED(hr) && guid_equal(bhid,&m98_bhid_data_object) &&
        !guid_equal(iid,&m98_iid_data_object)) {
        IUnknown *data=(IUnknown *)*out;
        *out=NULL;
        hr=IUnknown_QueryInterface(data,iid,out);
        IUnknown_Release(data);
    }
    IShellFolder_Release(folder);
    return hr;
}

static HRESULT WINAPI item_GetParent(IShellItem *iface,IShellItem **out)
{
    m98_shell_item *item=(m98_shell_item *)iface;
    LPITEMIDLIST parent;
    if (!out) return E_POINTER;
    *out=NULL;
    parent=parent_pidl(item->pidl);
    if (!parent) return E_INVALIDARG;
    return new_item_owning_pidl(parent,out);
}

static HRESULT ansi_to_task_wide(LPCSTR text,LPWSTR *out)
{
    int count;
    WCHAR *wide;
    count=MultiByteToWideChar(CP_ACP,0,text,-1,NULL,0);
    if (!count) return E_FAIL;
    wide=(WCHAR *)CoTaskMemAlloc((SIZE_T)count*sizeof(WCHAR));
    if (!wide) return E_OUTOFMEMORY;
    if (!MultiByteToWideChar(CP_ACP,0,text,-1,wide,count)) {
        CoTaskMemFree(wide);
        return E_FAIL;
    }
    *out=wide;
    return S_OK;
}

static HRESULT strret_to_task_wide(STRRET *ret,LPCITEMIDLIST child,LPWSTR *out)
{
    HRESULT hr;
    if (ret->uType==STRRET_WSTR) {
        SIZE_T length;
        WCHAR *copy;
        if (!ret->pOleStr) return E_FAIL;
        length=wide_length(ret->pOleStr);
        copy=(WCHAR *)CoTaskMemAlloc((length+1)*sizeof(WCHAR));
        if (!copy) hr=E_OUTOFMEMORY;
        else {
            byte_copy((BYTE *)copy,(const BYTE *)ret->pOleStr,(length+1)*sizeof(WCHAR));
            *out=copy;hr=S_OK;
        }
        CoTaskMemFree(ret->pOleStr);
        return hr;
    }
    if (ret->uType==STRRET_CSTR) return ansi_to_task_wide(ret->cStr,out);
    if (ret->uType==STRRET_OFFSET) {
        const BYTE *bytes=(const BYTE *)child;
        WORD size=child->mkid.cb;
        DWORD i=ret->uOffset;
        if (i>=size) return E_FAIL;
        while (i<size && bytes[i]) i++;
        if (i==size) return E_FAIL;
        return ansi_to_task_wide((const char *)(bytes+ret->uOffset),out);
    }
    return E_FAIL;
}

static HRESULT WINAPI item_GetDisplayName(IShellItem *iface,SIGDN kind,LPWSTR *out)
{
    m98_shell_item *item=(m98_shell_item *)iface;
    IShellFolder *parent=NULL;
    LPCITEMIDLIST child;
    STRRET ret;
    HRESULT hr;
    DWORD flags;
    if (!out) return E_POINTER;
    *out=NULL;
    if (kind==SIGDN_FILESYSPATH) {
        char path[MAX_PATH];
        if (!SHGetPathFromIDListA(item->pidl,path)) return E_FAIL;
        return ansi_to_task_wide(path,out);
    }
    switch (kind) {
    case SIGDN_NORMALDISPLAY: flags=SHGDN_NORMAL;break;
    case SIGDN_PARENTRELATIVE: flags=SHGDN_INFOLDER;break;
    case SIGDN_PARENTRELATIVEFORUI: flags=SHGDN_FORADDRESSBAR|SHGDN_INFOLDER;break;
    case SIGDN_PARENTRELATIVEPARSING: flags=SHGDN_FORPARSING|SHGDN_INFOLDER;break;
    case SIGDN_PARENTRELATIVEEDITING: flags=SHGDN_FOREDITING|SHGDN_INFOLDER;break;
    case SIGDN_DESKTOPABSOLUTEPARSING: flags=SHGDN_FORPARSING;break;
    /* SIGDN_DESKTOPABSOLUTEEDITING has low 16 bits 0xc000. Wine's pinned
     * SHGetNameFromIDList passes those bits to GetDisplayNameOf. FOREDITING
     * alone (0x1000) yielded only the basename on the Windows host; 0xc000
     * matched that host's native IShellItem absolute editing name. */
    case SIGDN_DESKTOPABSOLUTEEDITING: flags=SHGDN_FORPARSING|SHGDN_FORADDRESSBAR;break;
    case SIGDN_PARENTRELATIVEFORADDRESSBAR:
        flags=SHGDN_FORPARSING|SHGDN_FORADDRESSBAR|SHGDN_INFOLDER;break;
    default: return E_INVALIDARG;
    }
    hr=get_parent_folder(item,&parent);
    if (FAILED(hr)) return hr;
    child=last_id(item->pidl);
    if (!child) hr=E_INVALIDARG;
    else {
        hr=IShellFolder_GetDisplayNameOf(parent,child,flags,&ret);
        if (SUCCEEDED(hr)) hr=strret_to_task_wide(&ret,child,out);
    }
    IShellFolder_Release(parent);
    return hr;
}

static HRESULT WINAPI item_GetAttributes(IShellItem *iface,SFGAOF mask,SFGAOF *out)
{
    m98_shell_item *item=(m98_shell_item *)iface;
    IShellFolder *parent=NULL;
    LPCITEMIDLIST child;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out=0;
    hr=get_parent_folder(item,&parent);
    if (FAILED(hr)) return hr;
    child=last_id(item->pidl);
    if (!child) hr=E_INVALIDARG;
    else {
        *out=mask;
        hr=IShellFolder_GetAttributesOf(parent,1,&child,out);
        *out&=mask;
        if (SUCCEEDED(hr)) hr=(*out==mask)?S_OK:S_FALSE;
    }
    IShellFolder_Release(parent);
    return hr;
}

static HRESULT WINAPI item_Compare(IShellItem *iface,IShellItem *other,SICHINTF hint,int *order)
{
    m98_shell_item *item=(m98_shell_item *)iface;
    WCHAR *other_name=NULL;
    LPITEMIDLIST other_pidl=NULL;
    IShellFolder *desktop=NULL;
    HRESULT hr,comparison;
    if (!other || !order) return E_POINTER;
    *order=0;
    if (hint) return E_NOTIMPL;
    hr=IShellItem_GetDisplayName(other,SIGDN_DESKTOPABSOLUTEPARSING,&other_name);
    if (FAILED(hr)) return hr;
    hr=parse_name(other_name,NULL,&other_pidl,0,NULL);
    CoTaskMemFree(other_name);
    if (FAILED(hr)) return hr;
    hr=SHGetDesktopFolder(&desktop);
    if (SUCCEEDED(hr)) {
        comparison=IShellFolder_CompareIDs(desktop,0,item->pidl,other_pidl);
        IShellFolder_Release(desktop);
        if (SUCCEEDED(comparison)) {
            *order=(short)(comparison&0xffff);
            hr=*order?S_FALSE:S_OK;
        } else hr=comparison;
    }
    CoTaskMemFree(other_pidl);
    return hr;
}

static IShellItemVtbl shell_item_vtable = {
    item_QueryInterface,item_AddRef,item_Release,item_BindToHandler,
    item_GetParent,item_GetDisplayName,item_GetAttributes,item_Compare
};

static HRESULT new_item_owning_pidl(LPITEMIDLIST pidl,IShellItem **out)
{
    m98_shell_item *item=(m98_shell_item *)CoTaskMemAlloc(sizeof(*item));
    if (!item) {CoTaskMemFree(pidl);return E_OUTOFMEMORY;}
    item->iface.lpVtbl=&shell_item_vtable;
    item->refs=1;
    item->pidl=pidl;
    *out=&item->iface;
    return S_OK;
}

static HRESULT WINAPI m98_SHCreateItemFromParsingName(PCWSTR name,IBindCtx *context,
                                                        REFIID iid,void **out)
{
    LPITEMIDLIST pidl=NULL;
    IShellItem *item=NULL;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out=NULL;
    if (!name || !iid) return E_INVALIDARG;
    hr=parse_name(name,context,&pidl,0,NULL);
    if (FAILED(hr)) return hr;
    if (!pidl) return E_FAIL;
    hr=new_item_owning_pidl(pidl,&item);
    if (FAILED(hr)) return hr;
    hr=IShellItem_QueryInterface(item,iid,out);
    IShellItem_Release(item);
    return hr;
}

static HRESULT WINAPI m98_SHParseDisplayName(PCWSTR name,IBindCtx *context,
                                             LPITEMIDLIST *pidl,SFGAOF requested,
                                             SFGAOF *attributes)
{
    /* The PIDL returned by native IShellFolder::ParseDisplayName uses the
     * COM task allocator. Ownership passes directly to the caller, which
     * may release it with ILFree or CoTaskMemFree. Failure leaves it NULL. */
    return parse_name(name,context,pidl,requested,attributes);
}

HRESULT WINAPI m98_SHOpenFolderAndSelectItems(LPCITEMIDLIST pidlFolder,
                                               UINT cidl,
                                               LPCITEMIDLIST const *apidl,
                                               DWORD flags);

static const m98_named_api shell32_apis[] = {
    { "SHCreateItemFromParsingName", (unsigned long)m98_SHCreateItemFromParsingName },
    { "SHOpenFolderAndSelectItems", (unsigned long)m98_SHOpenFolderAndSelectItems },
    { "SHParseDisplayName", (unsigned long)m98_SHParseDisplayName }
};
static const m98_api_table api_tables[] = {
    { "SHELL32.DLL", shell32_apis, 3, 0, 0 },
    { 0, 0, 0, 0, 0 }
};

__declspec(dllexport) const m98_api_table *get_api_table(void)
{
    return api_tables;
}

BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID reserved)
{
    (void)instance;(void)reason;(void)reserved;
    return TRUE;
}
