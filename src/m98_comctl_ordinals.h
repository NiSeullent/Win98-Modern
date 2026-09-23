/* Win98 KernelEx COMCTL32 ordinal adapters. GPL-2.0-only. */
#ifndef M98_COMCTL_ORDINALS_H
#define M98_COMCTL_ORDINALS_H

#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>

/* x86 TASKDIALOGCONFIG public ABI; do not depend on Vista-only SDK guards. */
typedef HRESULT (CALLBACK *m98_taskdialog_callback)(HWND, UINT, WPARAM,
                                                     LPARAM, LONG_PTR);
typedef struct m98_taskdialog_config {
    UINT cbSize;
    HWND hwndParent;
    HINSTANCE hInstance;
    DWORD dwFlags;
    DWORD dwCommonButtons;
    LPCWSTR pszWindowTitle;
    LPCWSTR pszMainIcon;
    LPCWSTR pszMainInstruction;
    LPCWSTR pszContent;
    UINT cButtons;
    const void *pButtons;
    int nDefaultButton;
    UINT cRadioButtons;
    const void *pRadioButtons;
    int nDefaultRadioButton;
    LPCWSTR pszVerificationText;
    LPCWSTR pszExpandedInformation;
    LPCWSTR pszExpandedControlText;
    LPCWSTR pszCollapsedControlText;
    LPCWSTR pszFooterIcon;
    LPCWSTR pszFooter;
    m98_taskdialog_callback pfCallback;
    LONG_PTR lpCallbackData;
    UINT cxWidth;
} m98_taskdialog_config;

typedef struct m98_ordinal_api {
    unsigned short ord;
    unsigned long addr;
} m98_ordinal_api;

_Static_assert(sizeof(m98_taskdialog_config) == 96,
               "TASKDIALOGCONFIG must be 96 bytes on x86");
_Static_assert(sizeof(m98_ordinal_api) == 8,
               "KernelEx ordinal entry must be 8 bytes on x86");

HRESULT WINAPI m98_LoadIconWithScaleDown(HINSTANCE hinst, LPCWSTR name,
                                         int cx, int cy, HICON *icon);
HRESULT WINAPI m98_TaskDialogIndirect(const m98_taskdialog_config *config,
                                      int *button, int *radio, BOOL *verified);

#endif
