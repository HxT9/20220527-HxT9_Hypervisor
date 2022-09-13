#include <Windows.h>
#include <stdio.h>

#if _WIN64
extern void asmHook(SIZE_T ecx_pwd, SIZE_T edx_HookData, SIZE_T eax_action, SIZE_T ebx_pid);
extern void asmCleanHook();
extern int asmTest();
#endif

typedef struct _HookData {
    unsigned __int64 FunctionToHook;
    unsigned __int64 HkFunction;
    unsigned __int64 TrampolineFunction;
} HookData, *PHookData;

typedef (*printFun)();
printFun pFTrampoline;

typedef (*testFun)();
testFun tFTrampoline;

void hkTest() {
    printf("HK Test");
    tFTrampoline();
}

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
    HookData hkData;

#if _WIN64
    __try {
        asmCleanHook();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
#endif

    printf("Tester\n");
    while (1) {
        printf("Pid                 0x%p\n", GetCurrentProcessId());
        printf("main                0x%p\n", &main);
        printf("origPrint           0x%p\n", &asmTest);
        printf("hookPrint           0x%p\n", &hkTest);
        tFTrampoline = VirtualAlloc(NULL, 0x100, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        __stosb(tFTrampoline, 0x90, 0x100);
        printf("printFunction Ref   0x%p\n", tFTrampoline);


        hkData.FunctionToHook = &asmTest;
        hkData.HkFunction = &hkTest;
        hkData.TrampolineFunction = tFTrampoline;

        printf("Calling print. Press Enter to continue\n");
        getch();

        printf("%d", asmTest());

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
        printf("%d", asmTest());

        getch();
    }
#if _WIN64
    asmCleanHook();
#endif
    system("pause");
}