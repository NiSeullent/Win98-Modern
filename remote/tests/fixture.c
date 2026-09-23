/* Native Win98 child-process fixture for remote capture/exit/timeout tests. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static void output(DWORD stream,const char *text,DWORD length)
{
    DWORD written;
    WriteFile(GetStdHandle(stream),text,length,&written,NULL);
}

static int contains(const char *text,const char *word)
{
    DWORD i,j;
    for (i=0;text[i];i++) {
        for (j=0;word[j] && text[i+j]==word[j];j++) {}
        if (!word[j]) return 1;
    }
    return 0;
}

void mainCRTStartup(void)
{
    const char *command=GetCommandLineA();
    static const char out[]="STDOUT: remote fixture\r\n";
    static const char err[]="STDERR: remote fixture\r\n";
    output(STD_OUTPUT_HANDLE,out,sizeof(out)-1);
    output(STD_ERROR_HANDLE,err,sizeof(err)-1);
    if (contains(command," sleep")) Sleep(5000);
    if (contains(command," flood")) {
        char block[1024];
        DWORD i;
        for (i=0;i<sizeof(block);i++) block[i]='X';
        for (i=0;i<70;i++) output(STD_OUTPUT_HANDLE,block,sizeof(block));
    }
    ExitProcess(contains(command," fail")?7:0);
}
