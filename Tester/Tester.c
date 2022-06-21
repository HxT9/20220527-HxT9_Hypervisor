#include <Windows.h>
#include <stdio.h>

extern void asmHook(SIZE_T ecx_pwd, SIZE_T edx_HookData, SIZE_T eax_action, SIZE_T ebx_pid);
extern void asmCleanHook();
extern void asmTest();

typedef struct _HookData {
    unsigned __int64 FunctionToHook;
    unsigned __int64 HkFunction;
    unsigned __int64 TrampolineFunction;
} HookData, *PHookData;

void (*printFun)();

void print() {
    printf("Print original\n");
}

void hkPrint() {
    DebugBreak();
    printf("Print hooked");
    printFun();
}

int main()
{
    HookData hkData;

    asmCleanHook();

    printf("Tester\n");
    while (1) {
        printf("Pid               0x%p\n", GetCurrentProcessId());
        printf("origPrint         0x%p\n", &print);
        printf("hookPrint         0x%p\n", &hkPrint);
        printFun = malloc(0x100);
        printf("printFunction Ref 0x%p\n", &printFun);


        hkData.FunctionToHook = &print;
        hkData.HkFunction = &hkPrint;
        hkData.TrampolineFunction = &printFun;

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
#ifdef x86
            __asm {
                mov ecx, 0x359309
                mov edx, origPrint
                mov eax, hookPrint
                mov ebx, printFunction
                vmcall
            }
#else
            asmHook(rcx, rdx, rax, rbx);
#endif
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}

        printf("Calling print\n");
        print();

        getch();
    }

    asmCleanHook();
    system("pause");
}