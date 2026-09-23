/*
 * KernelEx API-library ABI, mirrored from common/kexcoresdk.h in KernelEx.
 * Copyright (C) 2009 Xeno86. GPL-2.0-only.
 * This minimal declaration keeps the new DLL independent of the VC6 SDK.
 */
#ifndef M98_KEX_ABI_H
#define M98_KEX_ABI_H

#if defined(_WIN64)
#error KernelEx API libraries are 32-bit only
#endif

typedef struct m98_named_api {
    const char *name;
    unsigned long addr;
} m98_named_api;

typedef struct m98_api_table {
    const char *target_library;
    const m98_named_api *named_apis;
    int named_apis_count;
    const void *ordinal_apis;
    int ordinal_apis_count;
} m98_api_table;

#endif
