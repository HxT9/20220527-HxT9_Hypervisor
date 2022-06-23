#include <Windows.h>
#include <stdio.h>

#if _WIN64
extern void asmHook(SIZE_T ecx_pwd, SIZE_T edx_HookData, SIZE_T eax_action, SIZE_T ebx_pid);
extern void asmCleanHook();
extern void asmTest();
#endif

typedef struct _HookData {
    unsigned __int64 FunctionToHook;
    unsigned __int64 HkFunction;
    unsigned __int64 TrampolineFunction;
} HookData, *PHookData;

typedef (*printFun)();
printFun pFTrampoline;

DECLSPEC_NOINLINE void print() {
    int a = 1;
    int b = 2;
    int c = a ^ b;
    for (int i = 0; i < 10; i++) {
        a++;
    }
    c = a ^ b;

    printf("Print original %d\n", c);
}

void hkPrint() {
    printf("Print hooked");
    pFTrampoline();
}

int main()
{
    /*UINT64 printAddr = &print;
    pFTrampoline = VirtualAlloc(NULL, 0x100, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    BYTE tr[] = {0x68, 0, 0, 0, 0, 0xc7, 0x44, 0x24, 0x04, 0, 0, 0, 0, 0xc3};
    *((PUINT32)&tr[1]) = (UINT32)printAddr;
    *((PUINT32)&tr[9]) = (UINT32)(printAddr >> 32);
    memcpy(pFTrampoline, tr, sizeof(tr));

    printf("origPrint         0x%p\n", &print);
    printf("hookPrint         0x%p\n", &hkPrint);
    printf("printFunction Ref 0x%p\n", pFTrampoline);
    getch();

    hkPrint();
    return;*/

    HookData hkData;

#if _WIN64
    __try {
        asmCleanHook();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
#endif

    printf("Tester\n");
    while (1) {
        printf("Pid               0x%p\n", GetCurrentProcessId());
        printf("origPrint         0x%p\n", &print);
        printf("hookPrint         0x%p\n", &hkPrint);
        pFTrampoline = VirtualAlloc(NULL, 0x100, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        __stosb(pFTrampoline, 0x00, 0x100);
        printf("printFunction Ref 0x%p\n", pFTrampoline);


        hkData.FunctionToHook = &print;
        hkData.HkFunction = &hkPrint;
        hkData.TrampolineFunction = pFTrampoline;

        printf("Calling print. Press Enter to continue\n");
        getch();

        print();

        printf("Hooking\n");

        unsigned __int64 rax;
        unsigned __int64 rbx;
        unsigned __int64 rcx;
        unsigned __int64 rdx;
        rcx = 0x359309;
        rax = 1;
        rbx = GetCurrentProcessId();
        rdx = &hkData;

        Sleep(500);

        __try {
#if _WIN64
            asmHook(rcx, rdx, rax, rbx);
#endif
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}

        printf("Calling print. Enter to continue\n");
        getch();
        print();

        getch();
    }
#if _WIN64
    asmCleanHook();
#endif
    system("pause");
}