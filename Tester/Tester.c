#include <Windows.h>
#include <stdio.h>

extern void asmHook(SIZE_T ecx_pwd, SIZE_T edx_functionToHook, SIZE_T eax_hkFunction, SIZE_T ebx_trampoline);

void (*printFun)();

void print() {
    printf("Print original\n");
}

void hkPrint() {
    printf("Print hooked");
    printFun();
}

int main()
{
    SIZE_T origPrint = (SIZE_T)print;
    SIZE_T hookPrint = (SIZE_T)hkPrint;
    SIZE_T printFunction = (SIZE_T)&printFun;

    printf("Tester\n");
    printf("origPrint         0x%p\n", origPrint);
    printf("hookPrint         0x%p\n", hookPrint);
    printf("printFunction Ref 0x%p\n", printFunction);

    printf("Calling print\n");
    print();

    printf("Hooking\n");

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
        asmHook(0x359309, origPrint, hookPrint, printFunction);
#endif
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}

    printf("Calling print\n");
    print();
}