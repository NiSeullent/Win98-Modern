/* Host/Win98 guest direct-call smoke for the app-local BCRYPT hash subset. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>

typedef LONG status_t;
typedef status_t (WINAPI *open_fn)(void **,LPCWSTR,LPCWSTR,ULONG);
typedef status_t (WINAPI *close_fn)(void *,ULONG);
typedef status_t (WINAPI *property_fn)(void *,LPCWSTR,BYTE *,ULONG,ULONG *,ULONG);
typedef status_t (WINAPI *create_fn)(void *,void **,BYTE *,ULONG,BYTE *,ULONG,ULONG);
typedef status_t (WINAPI *data_fn)(void *,BYTE *,ULONG,ULONG);
typedef status_t (WINAPI *finish_fn)(void *,BYTE *,ULONG,ULONG);
typedef status_t (WINAPI *destroy_fn)(void *);

#define SUCCESS ((status_t)0)
#define INVALID_HANDLE ((status_t)0xc0000008)
#define INVALID_PARAMETER ((status_t)0xc000000d)
#define BUFFER_TOO_SMALL ((status_t)0xc0000023)
#define NOT_SUPPORTED ((status_t)0xc00000bb)
#define NOT_FOUND ((status_t)0xc0000225)
#define HMAC_FLAG 8UL
#define REUSABLE_FLAG 32UL

static open_fn open_algorithm;
static close_fn close_algorithm;
static property_fn get_property;
static create_fn create_hash;
static data_fn hash_data;
static finish_fn finish_hash;
static destroy_fn destroy_hash;

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

static void verify_hex(const BYTE *bytes,DWORD count,const char *expected)
{
    static const char digits[]="0123456789abcdef";
    DWORD i;
    for (i=0;i<count;i++) {
        if (digits[bytes[i]>>4]!=expected[i*2] ||
            digits[bytes[i]&15]!=expected[i*2+1]) fail("incorrect digest");
    }
    if (expected[count*2]) fail("vector length");
}

static void vector(LPCWSTR name,const BYTE *input,DWORD count,const char *expected,DWORD digest_size)
{
    void *algorithm=NULL,*hash=NULL;
    BYTE output[32];
    if (open_algorithm(&algorithm,name,NULL,0)!=SUCCESS || !algorithm)
        fail("open hash provider");
    if (create_hash(algorithm,&hash,NULL,0,NULL,0,0)!=SUCCESS || !hash)
        fail("create hash object");
    if (hash_data(hash,(BYTE *)input,count/2,0)!=SUCCESS ||
        hash_data(hash,(BYTE *)(input+count/2),count-count/2,0)!=SUCCESS)
        fail("append split input");
    if (finish_hash(hash,output,digest_size,0)!=SUCCESS)
        fail("finish hash");
    verify_hex(output,digest_size,expected);
    if (hash_data(hash,(BYTE *)input,count,0)!=INVALID_HANDLE ||
        finish_hash(hash,output,digest_size,0)!=INVALID_HANDLE)
        fail("non-reusable hash after finish");
    if (destroy_hash(hash)!=SUCCESS || destroy_hash(hash)!=INVALID_HANDLE ||
        close_algorithm(algorithm,0)!=SUCCESS ||
        close_algorithm(algorithm,0)!=INVALID_HANDLE)
        fail("handle destruction and stale handles");
}

static void property_tests(void)
{
    void *algorithm=NULL,*hash=NULL;
    BYTE output[32],caller_storage[512];
    ULONG size=0,value=0,needed=0;
    DWORD i;
    if (open_algorithm(&algorithm,L"SHA256",NULL,0)!=SUCCESS) fail("property provider");
    if (get_property(algorithm,L"HashDigestLength",NULL,0,&size,0)!=SUCCESS || size!=4)
        fail("property size query");
    if (get_property(algorithm,L"HashDigestLength",(BYTE *)&value,3,&size,0)!=BUFFER_TOO_SMALL ||
        size!=4 || value!=0) fail("small property output");
    if (get_property(algorithm,L"HashDigestLength",(BYTE *)&value,4,&size,0)!=SUCCESS ||
        value!=32 || size!=4) fail("digest length property");
    if (get_property(algorithm,L"HashBlockLength",(BYTE *)&value,4,&size,0)!=SUCCESS ||
        value!=64) fail("hash block length property");
    if (get_property(algorithm,L"ObjectLength",(BYTE *)&value,4,&size,0)!=SUCCESS ||
        value>sizeof(caller_storage) || value<128) fail("hash object length property");
    needed=value;
    if (get_property(algorithm,L"AlgorithmName",output,sizeof(output),&size,0)!=SUCCESS ||
        size!=14 || ((WCHAR *)output)[0]!=L'S' || ((WCHAR *)output)[6]!=0)
        fail("algorithm name property");
    if (get_property(algorithm,L"NotAProperty",output,sizeof(output),&size,0)!=NOT_SUPPORTED ||
        get_property(algorithm,NULL,output,sizeof(output),&size,0)!=INVALID_PARAMETER ||
        get_property(algorithm,L"ObjectLength",output,sizeof(output),NULL,0)!=INVALID_PARAMETER)
        fail("invalid property");
    if (create_hash(algorithm,&hash,caller_storage,needed-1,NULL,0,0)!=BUFFER_TOO_SMALL)
        fail("short caller-owned object");
    if (create_hash(algorithm,&hash,caller_storage,needed,NULL,0,0)!=SUCCESS)
        fail("caller-owned object");
    if (get_property(hash,L"HashDigestLength",(BYTE *)&value,4,&size,0)!=SUCCESS || value!=32)
        fail("hash property");
    if (hash_data(hash,NULL,0,0)!=SUCCESS ||
        hash_data(hash,NULL,1,0)!=INVALID_PARAMETER ||
        hash_data(hash,(BYTE *)"x",1,1)!=INVALID_PARAMETER ||
        finish_hash(hash,output,31,0)!=INVALID_PARAMETER ||
        finish_hash(hash,NULL,32,0)!=INVALID_PARAMETER)
        fail("hash argument validation");
    if (finish_hash(hash,output,32,0)!=SUCCESS) fail("empty SHA256");
    verify_hex(output,32,"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    if (destroy_hash(hash)!=SUCCESS) fail("destroy caller-owned object");
    for (i=0;i<needed;i++) if (caller_storage[i]!=0) fail("wipe caller-owned object");
    if (close_algorithm(algorithm,0)!=SUCCESS) fail("close property provider");
}

static void hmac_and_reuse_tests(void)
{
    void *algorithm=NULL,*hash=NULL;
    BYTE key[100],output[32];
    DWORD i;
    for (i=0;i<sizeof(key);i++) key[i]=0x0b;
    if (open_algorithm(&algorithm,L"SHA256",NULL,HMAC_FLAG)!=SUCCESS)
        fail("HMAC provider");
    if (create_hash(algorithm,&hash,NULL,0,key,20,REUSABLE_FLAG)!=SUCCESS)
        fail("reusable HMAC object");
    if (hash_data(hash,(BYTE *)"Hi There",8,0)!=SUCCESS ||
        finish_hash(hash,output,32,0)!=SUCCESS) fail("first HMAC");
    verify_hex(output,32,"b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    if (hash_data(hash,(BYTE *)"Hi There",8,0)!=SUCCESS ||
        finish_hash(hash,output,32,0)!=SUCCESS) fail("reused HMAC");
    verify_hex(output,32,"b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    if (destroy_hash(hash)!=SUCCESS || close_algorithm(algorithm,0)!=SUCCESS)
        fail("HMAC cleanup");
    for (i=0;i<sizeof(key);i++) key[i]='a';
    if (open_algorithm(&algorithm,L"SHA256",NULL,HMAC_FLAG)!=SUCCESS ||
        create_hash(algorithm,&hash,NULL,0,key,sizeof(key),0)!=SUCCESS ||
        hash_data(hash,(BYTE *)"abc",3,0)!=SUCCESS ||
        finish_hash(hash,output,32,0)!=SUCCESS) fail("long-key HMAC-SHA256");
    verify_hex(output,32,"c661b9f4ae9d1a9e9bd6ad956d01d63f91809f83039431a3a362ab665652adfe");
    if (destroy_hash(hash)!=SUCCESS || close_algorithm(algorithm,0)!=SUCCESS)
        fail("long-key HMAC cleanup");
    for (i=0;i<sizeof(key);i++) key[i]=0x0b;
    if (open_algorithm(&algorithm,L"MD5",NULL,HMAC_FLAG)!=SUCCESS ||
        create_hash(algorithm,&hash,NULL,0,key,16,0)!=SUCCESS ||
        hash_data(hash,(BYTE *)"Hi There",8,0)!=SUCCESS ||
        finish_hash(hash,output,16,0)!=SUCCESS) fail("HMAC-MD5");
    verify_hex(output,16,"9294727a3638bb1c13f48ef8158bfc9d");
    if (destroy_hash(hash)!=SUCCESS || close_algorithm(algorithm,0)!=SUCCESS)
        fail("HMAC-MD5 cleanup");
    if (open_algorithm(&algorithm,L"SHA256",NULL,0)!=SUCCESS) fail("plain provider");
    if (create_hash(algorithm,&hash,NULL,0,key,1,0)!=INVALID_PARAMETER ||
        create_hash(algorithm,&hash,NULL,0,NULL,1,0)!=INVALID_PARAMETER ||
        create_hash(algorithm,&hash,NULL,0,NULL,0,1)!=NOT_SUPPORTED)
        fail("unsupported secret or flag");
    if (close_algorithm(algorithm,0)!=SUCCESS) fail("plain provider cleanup");
}

static void pseudo_handle_tests(void)
{
    static const DWORD handles[4]={0x21UL,0x41UL,0x91UL,0xb1UL};
    static const DWORD lengths[4]={16,32,16,32};
    static const char *digests[4]={
        "900150983cd24fb0d6963f7d28e17f72",
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "9294727a3638bb1c13f48ef8158bfc9d",
        "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"
    };
    DWORD i;
    for (i=0;i<4;i++) {
        void *provider=(void *)(UINT_PTR)handles[i],*hash=NULL;
        BYTE output[32],key[20];
        ULONG size=0,value=0;
        DWORD j;
        for (j=0;j<sizeof(key);j++) key[j]=0x0b;
        if (get_property(provider,L"HashDigestLength",(BYTE *)&value,sizeof(value),&size,0)!=SUCCESS ||
            value!=lengths[i] || size!=sizeof(value)) fail("pseudo-handle property");
        if (close_algorithm(provider,0)!=INVALID_HANDLE)
            fail("pseudo-handle must not be closed");
        if (i<2) {
            if (create_hash(provider,&hash,NULL,0,NULL,0,0)!=SUCCESS ||
                hash_data(hash,(BYTE *)"abc",3,0)!=SUCCESS ||
                finish_hash(hash,output,lengths[i],0)!=SUCCESS)
                fail("plain pseudo-handle hash");
        } else {
            if (create_hash(provider,&hash,NULL,0,key,i==2?16:20,0)!=SUCCESS ||
                hash_data(hash,(BYTE *)"Hi There",8,0)!=SUCCESS ||
                finish_hash(hash,output,lengths[i],0)!=SUCCESS)
                fail("HMAC pseudo-handle hash");
        }
        verify_hex(output,lengths[i],digests[i]);
        if (destroy_hash(hash)!=SUCCESS) fail("pseudo-handle hash cleanup");
    }
    if (get_property((void *)(UINT_PTR)0x31UL,L"HashDigestLength",NULL,0,NULL,0)!=INVALID_HANDLE ||
        create_hash((void *)(UINT_PTR)0x31UL,NULL,NULL,0,NULL,0,0)!=INVALID_HANDLE)
        fail("unsupported pseudo-handle");
}

void mainCRTStartup(void)
{
    HMODULE shim=LoadLibraryA("bcrypt.dll");
    BYTE block[1000],output[32];
    void *algorithm=NULL,*hash=NULL;
    DWORD i;
    if (!shim) fail("load app-local bcrypt.dll");
    open_algorithm=(open_fn)GetProcAddress(shim,"BCryptOpenAlgorithmProvider");
    close_algorithm=(close_fn)GetProcAddress(shim,"BCryptCloseAlgorithmProvider");
    get_property=(property_fn)GetProcAddress(shim,"BCryptGetProperty");
    create_hash=(create_fn)GetProcAddress(shim,"BCryptCreateHash");
    hash_data=(data_fn)GetProcAddress(shim,"BCryptHashData");
    finish_hash=(finish_fn)GetProcAddress(shim,"BCryptFinishHash");
    destroy_hash=(destroy_fn)GetProcAddress(shim,"BCryptDestroyHash");
    if (!open_algorithm || !close_algorithm || !get_property || !create_hash ||
        !hash_data || !finish_hash || !destroy_hash ||
        GetProcAddress(shim,"BCryptCreateHash@28")) fail("exact undecorated exports");
    if (open_algorithm(NULL,L"SHA256",NULL,0)!=INVALID_PARAMETER ||
        open_algorithm(&algorithm,NULL,NULL,0)!=INVALID_PARAMETER ||
        open_algorithm(&algorithm,L"AES",NULL,0)!=NOT_FOUND ||
        open_algorithm(&algorithm,L"SHA256",L"Unknown provider",0)!=NOT_FOUND ||
        open_algorithm(&algorithm,L"SHA256",NULL,1)!=NOT_SUPPORTED ||
        close_algorithm((void *)0xdeadbeef,0)!=INVALID_HANDLE)
        fail("open and close validation");
    for (i=0;i<sizeof(block);i++) block[i]='a';
    vector(L"SHA256",(const BYTE *)"abc",3,
           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",32);
    vector(L"MD5",(const BYTE *)"abc",3,
           "900150983cd24fb0d6963f7d28e17f72",16);
    vector(L"MD5",(const BYTE *)"",0,
           "d41d8cd98f00b204e9800998ecf8427e",16);
    vector(L"SHA256",block,55,
           "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318",32);
    vector(L"SHA256",block,56,
           "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a",32);
    vector(L"SHA256",block,64,
           "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb",32);
    vector(L"MD5",block,56,
           "3b0c8ac703f828b04c6c197006d17218",16);
    vector(L"MD5",block,65,
           "c743a45e0d2e6a95cb859adae0248435",16);
    property_tests();hmac_and_reuse_tests();pseudo_handle_tests();
    if (open_algorithm(&algorithm,L"SHA256",NULL,0)!=SUCCESS ||
        create_hash(algorithm,&hash,NULL,0,NULL,0,0)!=SUCCESS)
        fail("million byte provider");
    for (i=0;i<1000;i++) if (hash_data(hash,block,sizeof(block),0)!=SUCCESS)
        fail("million byte update");
    if (finish_hash(hash,output,32,0)!=SUCCESS) fail("million byte finish");
    verify_hex(output,32,"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    if (destroy_hash(hash)!=SUCCESS || close_algorithm(algorithm,0)!=SUCCESS)
        fail("million byte cleanup");
    report("PASS: BCrypt SHA256/MD5, HMAC, reusable hash, buffers, handles, pseudo-handles\r\n");
    FreeLibrary(shim);
    ExitProcess(0);
}
