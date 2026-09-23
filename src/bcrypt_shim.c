/*
 * App-local BCRYPT.DLL hash subset for Windows 98 SE.
 * Copyright (C) 2026 Win98-Modern contributors. GPL-2.0-only.
 *
 * API/ownership/error paths were checked against Wine's
 * dlls/bcrypt/bcrypt_main.c at df15af3652511150490934682202d45af892f887
 * (LGPL-2.1-or-later), ReactOS's dll/win32/bcrypt/bcrypt_main.c at
 * 9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8 (LGPL-2.1-or-later file),
 * and Microsoft's BCrypt API contracts. No Wine/ReactOS code is copied.
 * SHA-256 follows FIPS 180-4; MD5 follows RFC 1321; HMAC follows RFC 2104.
 * Only MD5 and SHA256 hash/HMAC providers are advertised. Unsupported
 * algorithms and properties fail explicitly, not with a success-shaped stub.
 *
 * Unlike Wine/ReactOS's pointer handles, opaque monotonically increasing
 * tokens are looked up under a Win98 CRITICAL_SECTION. This prevents a stale
 * or arbitrary handle from being dereferenced and avoids address reuse after
 * BCryptDestroyHash. Caller-owned hash buffers are actually used as storage.
 */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>

typedef LONG M98_NTSTATUS;
#define ST_OK          ((M98_NTSTATUS)0x00000000L)
#define ST_NOT_FOUND   ((M98_NTSTATUS)0xC0000225L)
#define ST_NO_MEMORY   ((M98_NTSTATUS)0xC0000017L)
#define ST_INVALID_HANDLE ((M98_NTSTATUS)0xC0000008L)
#define ST_INVALID_PARAMETER ((M98_NTSTATUS)0xC000000DL)
#define ST_BUFFER_TOO_SMALL ((M98_NTSTATUS)0xC0000023L)
#define ST_NOT_SUPPORTED ((M98_NTSTATUS)0xC00000BBL)
#define HMAC_FLAG 0x00000008UL
#define REUSABLE_FLAG 0x00000020UL

enum { ALG_MD5 = 1, ALG_SHA256 = 2, OBJ_ALG = 3, OBJ_HASH = 4 };
typedef struct m98_object {
    struct m98_object *next;
    DWORD token;
    DWORD type;
    DWORD owned;
    DWORD algorithm;
    DWORD flags;
} m98_object;
typedef struct {
    DWORD h[8];
    ULONGLONG total;
    BYTE block[64];
    DWORD used;
} m98_hash_state;
typedef struct {
    m98_object object;
    m98_hash_state state;
    BYTE ipad[64];
    BYTE opad[64];
    DWORD finished;
} m98_hash_object;

static CRITICAL_SECTION object_lock;
static m98_object *objects;
static DWORD next_token = 0x10000UL;
static HANDLE process_heap;

static void wipe(void *memory, DWORD count)
{
    volatile BYTE *p = (volatile BYTE *)memory;
    while (count--) *p++ = 0;
}

static void bytes_copy(BYTE *dst, const BYTE *src, DWORD count)
{
    while (count--) *dst++ = *src++;
}

static void bytes_zero(BYTE *dst, DWORD count)
{
    while (count--) *dst++ = 0;
}

static int wide_equal(const WCHAR *a, const WCHAR *b)
{
    if (!a || !b) return 0;
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static DWORD digest_length(DWORD algorithm)
{
    return algorithm == ALG_MD5 ? 16UL : 32UL;
}

static DWORD rotate_left(DWORD x, DWORD n)
{
    return (x << n) | (x >> (32U - n));
}

static DWORD rotate_right(DWORD x, DWORD n)
{
    return (x >> n) | (x << (32U - n));
}

/* FIPS 180-4 section 6.2.2; all words are assembled bytewise so callers need
 * no alignment and Win98 x86 little-endian storage cannot change the digest. */
static const DWORD sha256_k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static void sha256_transform(m98_hash_state *s, const BYTE *block)
{
    DWORD w[64], a,b,c,d,e,f,g,h,t1,t2,i;
    for (i=0;i<16;i++) {
        DWORD j=i*4;
        w[i]=((DWORD)block[j]<<24)|((DWORD)block[j+1]<<16)|((DWORD)block[j+2]<<8)|block[j+3];
    }
    for (i=16;i<64;i++) {
        DWORD x=w[i-15], y=w[i-2];
        w[i]=(rotate_right(y,17)^rotate_right(y,19)^(y>>10))+w[i-7]
             +(rotate_right(x,7)^rotate_right(x,18)^(x>>3))+w[i-16];
    }
    a=s->h[0];b=s->h[1];c=s->h[2];d=s->h[3];
    e=s->h[4];f=s->h[5];g=s->h[6];h=s->h[7];
    for (i=0;i<64;i++) {
        t1=h+(rotate_right(e,6)^rotate_right(e,11)^rotate_right(e,25))
          +((e&f)^(~e&g))+sha256_k[i]+w[i];
        t2=(rotate_right(a,2)^rotate_right(a,13)^rotate_right(a,22))
          +((a&b)^(a&c)^(b&c));
        h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    s->h[0]+=a;s->h[1]+=b;s->h[2]+=c;s->h[3]+=d;
    s->h[4]+=e;s->h[5]+=f;s->h[6]+=g;s->h[7]+=h;
    wipe(w,sizeof(w));
}

/* RFC 1321 sections 3.4 and 3.5. T[i] values are the specified sine table. */
static const DWORD md5_k[64] = {
    0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
    0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
    0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
    0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
    0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
    0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
    0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
    0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391
};
static const BYTE md5_shift[16] = {7,12,17,22,5,9,14,20,4,11,16,23,6,10,15,21};

static void md5_transform(m98_hash_state *s, const BYTE *block)
{
    DWORD w[16],a=s->h[0],b=s->h[1],c=s->h[2],d=s->h[3],i;
    for (i=0;i<16;i++) {
        DWORD j=i*4;
        w[i]=(DWORD)block[j]|((DWORD)block[j+1]<<8)|((DWORD)block[j+2]<<16)|((DWORD)block[j+3]<<24);
    }
    for (i=0;i<64;i++) {
        DWORD f,g,shift,temp;
        if (i<16) { f=(b&c)|(~b&d);g=i;shift=md5_shift[i&3]; }
        else if (i<32) { f=(d&b)|(~d&c);g=(5*i+1)&15;shift=md5_shift[4+(i&3)]; }
        else if (i<48) { f=b^c^d;g=(3*i+5)&15;shift=md5_shift[8+(i&3)]; }
        else { f=c^(b|~d);g=(7*i)&15;shift=md5_shift[12+(i&3)]; }
        temp=d;d=c;c=b;b=b+rotate_left(a+f+md5_k[i]+w[g],shift);a=temp;
    }
    s->h[0]+=a;s->h[1]+=b;s->h[2]+=c;s->h[3]+=d;
    wipe(w,sizeof(w));
}

static void hash_init(m98_hash_state *s, DWORD algorithm)
{
    bytes_zero((BYTE *)s,sizeof(*s));
    if (algorithm==ALG_MD5) {
        s->h[0]=0x67452301;s->h[1]=0xefcdab89;s->h[2]=0x98badcfe;s->h[3]=0x10325476;
    } else {
        s->h[0]=0x6a09e667;s->h[1]=0xbb67ae85;s->h[2]=0x3c6ef372;s->h[3]=0xa54ff53a;
        s->h[4]=0x510e527f;s->h[5]=0x9b05688c;s->h[6]=0x1f83d9ab;s->h[7]=0x5be0cd19;
    }
}

static void hash_update(m98_hash_state *s, DWORD algorithm, const BYTE *data, DWORD count)
{
    s->total += count;
    while (count) {
        DWORD space=64-s->used, take=count<space?count:space;
        bytes_copy(s->block+s->used,data,take);
        s->used+=take;data+=take;count-=take;
        if (s->used==64) {
            if (algorithm==ALG_MD5) md5_transform(s,s->block);
            else sha256_transform(s,s->block);
            s->used=0;
        }
    }
}

static void hash_final(m98_hash_state *s, DWORD algorithm, BYTE *output)
{
    ULONGLONG bits=s->total*8;
    BYTE pad[64], length[8];
    DWORD i,pad_count;
    bytes_zero(pad,sizeof(pad));pad[0]=0x80;
    pad_count=s->used<56?56-s->used:120-s->used;
    hash_update(s,algorithm,pad,pad_count);
    for (i=0;i<8;i++) length[i]=(BYTE)(bits>>(8*i));
    if (algorithm==ALG_SHA256) {
        for (i=0;i<4;i++) { BYTE t=length[i];length[i]=length[7-i];length[7-i]=t; }
    }
    hash_update(s,algorithm,length,8);
    for (i=0;i<digest_length(algorithm);i++) {
        DWORD word=s->h[i/4], shift=(algorithm==ALG_MD5)?8*(i&3):8*(3-(i&3));
        output[i]=(BYTE)(word>>shift);
    }
    wipe(pad,sizeof(pad));wipe(length,sizeof(length));
}

static m98_object *find_locked(void *handle, DWORD type)
{
    DWORD token=(DWORD)(UINT_PTR)handle;
    m98_object *p=objects;
    while (p) {
        if (p->token==token && (!type || p->type==type)) return p;
        p=p->next;
    }
    return NULL;
}

/* Microsoft's predefined CNG algorithm handles are integer constants, not
 * pointers. Recognize only the algorithms this DLL implements; never attempt
 * to dereference an unrecognized caller-supplied handle. */
static int pseudo_algorithm(void *handle, DWORD *algorithm, DWORD *flags)
{
    switch ((DWORD)(UINT_PTR)handle) {
    case 0x21UL: *algorithm=ALG_MD5; *flags=0; return 1;
    case 0x41UL: *algorithm=ALG_SHA256; *flags=0; return 1;
    case 0x91UL: *algorithm=ALG_MD5; *flags=HMAC_FLAG; return 1;
    case 0xb1UL: *algorithm=ALG_SHA256; *flags=HMAC_FLAG; return 1;
    default: return 0;
    }
}

static DWORD register_locked(m98_object *object)
{
    if (next_token<0x10000UL || next_token>0xfffffffbUL) return 0;
    object->token=next_token;
    next_token+=4;
    object->next=objects;
    objects=object;
    return object->token;
}

static void unlink_locked(m98_object *object)
{
    m98_object **p=&objects;
    while (*p && *p!=object) p=&(*p)->next;
    if (*p) *p=object->next;
}

static void prepare_hash(m98_hash_object *hash)
{
    hash_init(&hash->state,hash->object.algorithm);
    if (hash->object.flags&HMAC_FLAG)
        hash_update(&hash->state,hash->object.algorithm,hash->ipad,64);
    hash->finished=0;
}

M98_NTSTATUS WINAPI m98_BCryptOpenAlgorithmProvider(void **result, LPCWSTR id,
                                                     LPCWSTR implementation, ULONG flags)
{
    m98_object *object;
    DWORD algorithm, token;
    if (!result || !id) return ST_INVALID_PARAMETER;
    if (flags&~(HMAC_FLAG|REUSABLE_FLAG)) return ST_NOT_SUPPORTED;
    if (wide_equal(id,L"MD5")) algorithm=ALG_MD5;
    else if (wide_equal(id,L"SHA256")) algorithm=ALG_SHA256;
    else return ST_NOT_FOUND;
    if (implementation && !wide_equal(implementation,L"Microsoft Primitive Provider"))
        return ST_NOT_FOUND;
    object=(m98_object *)HeapAlloc(process_heap,HEAP_ZERO_MEMORY,sizeof(*object));
    if (!object) return ST_NO_MEMORY;
    object->type=OBJ_ALG;object->owned=1;object->algorithm=algorithm;object->flags=flags;
    EnterCriticalSection(&object_lock);
    token=register_locked(object);
    LeaveCriticalSection(&object_lock);
    if (!token) {HeapFree(process_heap,0,object);return ST_NO_MEMORY;}
    *result=(void *)(UINT_PTR)token;
    return ST_OK;
}

M98_NTSTATUS WINAPI m98_BCryptCloseAlgorithmProvider(void *handle, ULONG flags)
{
    m98_object *object;
    if (flags) return ST_INVALID_PARAMETER;
    EnterCriticalSection(&object_lock);
    object=find_locked(handle,OBJ_ALG);
    if (!object) {LeaveCriticalSection(&object_lock);return ST_INVALID_HANDLE;}
    unlink_locked(object);
    LeaveCriticalSection(&object_lock);
    wipe(object,sizeof(*object));
    HeapFree(process_heap,0,object);
    return ST_OK;
}

M98_NTSTATUS WINAPI m98_BCryptGetProperty(void *handle, LPCWSTR property, BYTE *output,
                                            ULONG output_size, ULONG *result_size, ULONG flags)
{
    m98_object *object;
    DWORD value=0, count=4, i, algorithm, pseudo_flags;
    const WCHAR *name=NULL;
    M98_NTSTATUS status;
    EnterCriticalSection(&object_lock);
    object=find_locked(handle,0);
    if (object) algorithm=object->algorithm;
    else if (!pseudo_algorithm(handle,&algorithm,&pseudo_flags))
        {status=ST_INVALID_HANDLE;goto done;}
    if (!property || !result_size || flags) {status=ST_INVALID_PARAMETER;goto done;}
    if (wide_equal(property,L"ObjectLength")) value=sizeof(m98_hash_object);
    else if (wide_equal(property,L"HashDigestLength")) value=digest_length(algorithm);
    else if (wide_equal(property,L"HashBlockLength")) value=64;
    else if (wide_equal(property,L"AlgorithmName")) {
        name=algorithm==ALG_MD5?L"MD5":L"SHA256";
        count=(algorithm==ALG_MD5?4:7)*sizeof(WCHAR);
    } else {status=ST_NOT_SUPPORTED;goto done;}
    *result_size=count;
    if (!output) {status=ST_OK;goto done;}
    if (output_size<count) {status=ST_BUFFER_TOO_SMALL;goto done;}
    if (name) bytes_copy(output,(const BYTE *)name,count);
    else for (i=0;i<4;i++) output[i]=(BYTE)(value>>(8*i));
    status=ST_OK;
done:
    LeaveCriticalSection(&object_lock);
    return status;
}

M98_NTSTATUS WINAPI m98_BCryptCreateHash(void *provider, void **result, BYTE *object_buffer,
                                           ULONG object_size, BYTE *secret, ULONG secret_size,
                                           ULONG flags)
{
    m98_object *algorithm;
    m98_hash_object *hash;
    DWORD token,alg_id,alg_flags,owned;
    BYTE key[64], digest[32];
    DWORD i,key_size=secret_size;
    if (flags&~REUSABLE_FLAG) return ST_NOT_SUPPORTED;
    EnterCriticalSection(&object_lock);
    algorithm=find_locked(provider,OBJ_ALG);
    if (algorithm) {alg_id=algorithm->algorithm;alg_flags=algorithm->flags;}
    else if (!pseudo_algorithm(provider,&alg_id,&alg_flags))
        {LeaveCriticalSection(&object_lock);return ST_INVALID_HANDLE;}
    /* Keep the provider lock until insertion so Close cannot race creation. */
    if (!result || (!object_buffer && object_size) || (object_buffer && object_size<sizeof(*hash)) ||
        (secret_size && !secret) || (!(alg_flags&HMAC_FLAG) && secret)) {
        M98_NTSTATUS status=(object_buffer && object_size<sizeof(*hash))?
                           ST_BUFFER_TOO_SMALL:ST_INVALID_PARAMETER;
        LeaveCriticalSection(&object_lock);return status;
    }
    owned=object_buffer?0:1;
    hash=owned?(m98_hash_object *)HeapAlloc(process_heap,HEAP_ZERO_MEMORY,sizeof(*hash)):
               (m98_hash_object *)object_buffer;
    if (!hash) {LeaveCriticalSection(&object_lock);return ST_NO_MEMORY;}
    if (!owned) bytes_zero((BYTE *)hash,sizeof(*hash));
    hash->object.type=OBJ_HASH;hash->object.owned=owned;hash->object.algorithm=alg_id;
    hash->object.flags=alg_flags|flags;
    if (alg_flags&HMAC_FLAG) {
        bytes_zero(key,sizeof(key));bytes_zero(digest,sizeof(digest));
        if (key_size>64) {
            m98_hash_state temp;
            hash_init(&temp,alg_id);
            hash_update(&temp,alg_id,secret,secret_size);
            hash_final(&temp,alg_id,digest);
            wipe(&temp,sizeof(temp));
            key_size=digest_length(alg_id);
            bytes_copy(key,digest,key_size);
        } else if (key_size) bytes_copy(key,secret,key_size);
        for (i=0;i<64;i++) {hash->ipad[i]=key[i]^0x36;hash->opad[i]=key[i]^0x5c;}
        wipe(key,sizeof(key));wipe(digest,sizeof(digest));
    }
    prepare_hash(hash);
    token=register_locked(&hash->object);
    LeaveCriticalSection(&object_lock);
    if (!token) {
        wipe(hash,sizeof(*hash));
        if (owned) HeapFree(process_heap,0,hash);
        return ST_NO_MEMORY;
    }
    *result=(void *)(UINT_PTR)token;
    return ST_OK;
}

M98_NTSTATUS WINAPI m98_BCryptHashData(void *handle, BYTE *input, ULONG input_size, ULONG flags)
{
    m98_hash_object *hash;
    M98_NTSTATUS status;
    EnterCriticalSection(&object_lock);
    hash=(m98_hash_object *)find_locked(handle,OBJ_HASH);
    if (!hash || hash->finished) status=ST_INVALID_HANDLE;
    else if (flags || (input_size && !input)) status=ST_INVALID_PARAMETER;
    else {
        if (input_size) hash_update(&hash->state,hash->object.algorithm,input,input_size);
        status=ST_OK;
    }
    LeaveCriticalSection(&object_lock);
    return status;
}

M98_NTSTATUS WINAPI m98_BCryptFinishHash(void *handle, BYTE *output, ULONG output_size, ULONG flags)
{
    m98_hash_object *hash;
    M98_NTSTATUS status;
    BYTE inner[32];
    DWORD algorithm;
    EnterCriticalSection(&object_lock);
    hash=(m98_hash_object *)find_locked(handle,OBJ_HASH);
    if (!hash || hash->finished) status=ST_INVALID_HANDLE;
    else if (flags || !output || output_size!=digest_length(hash->object.algorithm))
        status=ST_INVALID_PARAMETER;
    else {
        algorithm=hash->object.algorithm;
        if (hash->object.flags&HMAC_FLAG) {
            m98_hash_state outer;
            hash_final(&hash->state,algorithm,inner);
            hash_init(&outer,algorithm);
            hash_update(&outer,algorithm,hash->opad,64);
            hash_update(&outer,algorithm,inner,digest_length(algorithm));
            hash_final(&outer,algorithm,output);
            wipe(&outer,sizeof(outer));wipe(inner,sizeof(inner));
        } else hash_final(&hash->state,algorithm,output);
        if (hash->object.flags&REUSABLE_FLAG) prepare_hash(hash);
        else {hash->finished=1;wipe(&hash->state,sizeof(hash->state));}
        status=ST_OK;
    }
    LeaveCriticalSection(&object_lock);
    return status;
}

M98_NTSTATUS WINAPI m98_BCryptDestroyHash(void *handle)
{
    m98_hash_object *hash;
    DWORD owned;
    EnterCriticalSection(&object_lock);
    hash=(m98_hash_object *)find_locked(handle,OBJ_HASH);
    if (!hash) {LeaveCriticalSection(&object_lock);return ST_INVALID_HANDLE;}
    unlink_locked(&hash->object);
    owned=hash->object.owned;
    LeaveCriticalSection(&object_lock);
    wipe(hash,sizeof(*hash));
    if (owned) HeapFree(process_heap,0,hash);
    return ST_OK;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)instance;(void)reserved;
    if (reason==DLL_PROCESS_ATTACH) {
        process_heap=GetProcessHeap();
        InitializeCriticalSection(&object_lock);
    } else if (reason==DLL_PROCESS_DETACH) {
        DeleteCriticalSection(&object_lock);
    }
    return TRUE;
}
