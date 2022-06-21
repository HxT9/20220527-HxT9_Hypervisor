#include "EptHook.h"
#include "Ept.h"
#include "WindowsNT.h"
#include "Memory.h"
#include "Logger.h"
#include "LengthDisassemblerEngine.h"

extern size_t __fastcall LDE(const void* lpData, unsigned int size);

VOID EptHookWriteAbsoluteJump(PCHAR TargetBuffer, SIZE_T TargetAddress, BOOL IsX64)
{
    if (IsX64) {
        /*
        48 B8 56 34 12 90 78 56 34 12   mov rax, 01234567890123456h
        FF E0                           jmp rax
        */
        TargetBuffer[0] = 0x48;
        TargetBuffer[1] = 0xB8;
        *((PSIZE_T)&TargetBuffer[2]) = TargetAddress;
        TargetBuffer[10] = 0xFF;
        TargetBuffer[11] = 0xE0;
    }else{
        /*
        B8 78 56 34 12                  mov eax, 012345678h
        FF E0                           jmp eax
        */
        TargetBuffer[0] = 0xB8;
        *((PUINT32)&TargetBuffer[1]) = (UINT32)TargetAddress;
        TargetBuffer[5] = 0xFF;
        TargetBuffer[6] = 0xE0;
    }

    return;

    int index = 0;
    //push f1234567h                        68 67 45 23 f1
    //mov dword ptr [rsp+4], 5678h          c7 44 24 04 67 45 23 f1
    //ret                                   c3
    "\x68\x34\x12\x00\x00\xc7\x44\x24\x04\x78\x56\x00\x00\xC3";

    //
    // push Lower 4-byte TargetAddress
    //
    TargetBuffer[0] = 0x68;

    //
    // Lower 4-byte TargetAddress
    //
    *((PUINT32)&TargetBuffer[1]) = (UINT32)TargetAddress;

    //If x86 place a ret
    TargetBuffer[5] = 0xC3;

    if (IsX64) {
        //
        // mov [rsp+4],High 4-byte TargetAddress
        //
        TargetBuffer[5] = 0xC7;
        TargetBuffer[6] = 0x44;
        TargetBuffer[7] = 0x24;
        TargetBuffer[8] = 0x04;

        //
        // High 4-byte TargetAddress
        //
        *((PUINT32)&TargetBuffer[9]) = (UINT32)(TargetAddress >> 32);

        //
        // ret
        //
        TargetBuffer[13] = 0xC3;
    }
}

BOOL EptHookInstructionMemory(PVMM_CONTEXT Context, PEPT_HOOKED_PAGE_DETAIL Hook, UINT32 TargetProcessId, PVOID TargetFunction, PVOID TargetFunctionInSafeMemory, PVOID HookFunction, PVOID TrampolineAddress, BOOL IsX64)
{
    __debugbreak();
    SIZE_T SizeOfHookedInstructions;
    SIZE_T OffsetIntoPage;
    PCHAR TrampolineBuffer;

    OffsetIntoPage = ADDRMASK_EPT_PML1_OFFSET((SIZE_T)TargetFunction);
    HxTLog("OffsetIntoPage: 0x%llx\n", OffsetIntoPage);

    if ((OffsetIntoPage + 14) > PAGE_SIZE - 1)
    {
        HxTLog("Function extends past a page boundary. We just don't have the technology to solve this.....\n");
        return FALSE;
    }

    /* Determine the number of instructions necessary to overwrite using Length Disassembler Engine */
    for (SizeOfHookedInstructions = 0; SizeOfHookedInstructions < 14; SizeOfHookedInstructions += ldisasm(TargetFunctionInSafeMemory, IsX64))
    {
        // Get the full size of instructions necessary to copy
    }

    HxTLog("Number of bytes of instruction mem: %d\n", SizeOfHookedInstructions);

    /* Build a trampoline */

    /* Allocate some executable memory for the trampoline */
    TrampolineBuffer = OsAllocateExecutableNonpagedMemory(SizeOfHookedInstructions + 14);
    ReadVirtualMemory(Context, TargetFunction, TrampolineBuffer, SizeOfHookedInstructions, TargetProcessId);

    Hook->Trampoline = TrampolineAddress;

    /* Add the absolute jump back to the original function. */

    EptHookWriteAbsoluteJump(&TrampolineBuffer[SizeOfHookedInstructions], (SIZE_T)TargetFunction + SizeOfHookedInstructions, IsX64);
    WriteVirtualMemory(Context, TrampolineAddress, TrampolineBuffer, SizeOfHookedInstructions + 14, TargetProcessId);
    OsFreeNonpagedMemory(TrampolineBuffer);

    HxTLog("Trampoline: 0x%llx\n", Hook->Trampoline);
    HxTLog("HookFunction: 0x%llx\n", HookFunction);

    /* Write the absolute jump to our shadow page memory to jump to our hook. */
    EptHookWriteAbsoluteJump(&Hook->FakePageContents[OffsetIntoPage], (SIZE_T)HookFunction, IsX64);

    return TRUE;
}

/**
 * @brief The main function that performs EPT page hook with hidden detours and monitor
 * @details This function returns false in VMX Non-Root Mode if the VM is already initialized
 * This function have to be called through a VMCALL in VMX Root Mode
 *
 * @param TargetAddress The address of function or memory address to be hooked
 * @param HookFunction The function that will be called when hook triggered
 * @param ProcessCr3 The process cr3 to translate based on that process's cr3
 * @param UnsetRead Hook READ Access
 * @param UnsetWrite Hook WRITE Access
 * @param UnsetExecute Hook EXECUTE Access
 * @return BOOLEAN Returns true if the hook was successfull or false if there was an error
 */
BOOLEAN EptHookAddHook(PVMM_CONTEXT Context, PVOID TargetAddress, PVOID HookFunction, PVOID TrampolineAddress, UINT32 TargetProcessId, BOOL IsX64)
{
    INVEPT_DESCRIPTOR       Descriptor;
    SIZE_T                  PhysicalBaseAddress;
    PVOID                   VirtualTarget;
    PVOID                   TargetBuffer;
    UINT64                  TargetAddressInSafeMemory;
    UINT64                  PageOffset;
    PEPT_PML1_ENTRY         TargetPage;
    PEPT_HOOKED_PAGE_DETAIL HookedPage;
    ULONG                   LogicalCoreIndex;
    CR3                     TargetProcessCr3;
    PLIST_ENTRY             TempList = 0;
    PEPT_HOOKED_PAGE_DETAIL HookedEntry = NULL;

    //
    // Translate the page from a physical address to virtual so we can read its memory.
    // This function will return NULL if the physical address was not already mapped in
    // virtual memory.
    //
    VirtualTarget = PAGE_ALIGN(TargetAddress);

    //
    // Here we have to change the CR3, it is because we are in SYSTEM process
    // and if the target address is not mapped in SYSTEM address space (e.g
    // user mode address of another process) then the translation is invalid
    //

    TargetProcessCr3 = GetCr3FromProcessId(TargetProcessId);
    if (TargetProcessCr3.Flags == NULL) {
        return FALSE;
    }

    //
    // Find cr3 of target core
    //
    PhysicalBaseAddress = (SIZE_T)VirtualToPhysicalByCr3(VirtualTarget, TargetProcessCr3);

    if (!PhysicalBaseAddress)
    {
        HxTLog("EptHookAddHook, Invalid physical base address");
        return FALSE;
    }

    //
    // try to see if we can find the address
    //
    TempList = &Context->EptState->HookedPagesList;

    while (&Context->EptState->HookedPagesList != TempList->Flink)
    {
        TempList = TempList->Flink;
        HookedEntry = CONTAINING_RECORD(TempList, EPT_HOOKED_PAGE_DETAIL, PageHookList);

        if (HookedEntry->PhysicalBaseAddress == PhysicalBaseAddress)
        {
            //
            // Means that we find the address and don't support multiple hook in on page
            //
            HxTLog("EptHookAddHook, This page already has a hook");
            return FALSE;
        }
    }

    if (!EptSplitLargePage(Context, PhysicalBaseAddress))
    {
        return FALSE;
    }

    //
    // Pointer to the page entry in the page table
    //
    TargetPage = EptGetPml1Entry(Context->EptState->EptPageTable, PhysicalBaseAddress);

    //
    // Ensure the target is valid
    //
    if (!TargetPage)
    {
        HxTLog("EptHookAddHook, Failed to get pml1 page of target address");
        return FALSE;
    }

    //
    // Save the detail of hooked page to keep track of it
    //
    HookedPage = (PEPT_HOOKED_PAGE_DETAIL)OsAllocateNonpagedMemory(sizeof(EPT_HOOKED_PAGE_DETAIL));

    if (!HookedPage)
    {
        HxTLog("EptHookAddHook, Could not allocate EPT_HOOKED_PAGE_DETAIL");
        return FALSE;
    }

    //
    // Save the original permissions of the page
    //
    HookedPage->OriginalEntry = *TargetPage;

    //Also save page for our fake entries
    HookedPage->ExecuteEntry = *TargetPage;
    HookedPage->RWEntry = *TargetPage;

    //
    // Save the virtual address
    //
    HookedPage->VirtualAddress = TargetAddress;

    //
    // Save the physical address
    //
    HookedPage->PhysicalBaseAddress = PhysicalBaseAddress;

    //
    // Fake page content physical address
    //
    HookedPage->PhysicalBaseAddressOfFakePageContents = (SIZE_T)OsVirtualToPhysical(&HookedPage->FakePageContents[0]) / PAGE_SIZE;

    //
    // Save the entry address
    //
    HookedPage->EntryAddress = TargetPage;

    //
    // In execution hook, we have to make sure to unset read, write because
    // an EPT violation should occur for these cases and we can swap the original page
    //
    HookedPage->ExecuteEntry.ReadAccess = 0;
    HookedPage->ExecuteEntry.WriteAccess = 0;
    HookedPage->ExecuteEntry.ExecuteAccess = 1;

#ifdef DEBUGGING
    HookedPage->ExecuteEntry.ReadAccess = 1;
    HookedPage->ExecuteEntry.WriteAccess = 1;
#endif

    HookedPage->RWEntry.ReadAccess = 1;
    HookedPage->RWEntry.WriteAccess = 1;
    HookedPage->RWEntry.ExecuteAccess = 0;

    //
    // Also set the current pfn to fake page
    //
    HookedPage->ExecuteEntry.PageFrameNumber = HookedPage->PhysicalBaseAddressOfFakePageContents;

    //
    // Copy the content to the fake page
    // The following line can't be used in user mode addresses
    // RtlCopyBytes(&HookedPage->FakePageContents, VirtualTarget, PAGE_SIZE);
    //
    ReadVirtualMemory(Context, VirtualTarget, &HookedPage->FakePageContents, PAGE_SIZE, TargetProcessId);

    //
    // Compute new offset of target offset into a safe bufferr
    // It will be used to compute the length of the detours
    // address because we might have a user mode code
    //
    TargetAddressInSafeMemory = &HookedPage->FakePageContents;
    TargetAddressInSafeMemory = PAGE_ALIGN(TargetAddressInSafeMemory);
    PageOffset = PAGE_OFFSET(TargetAddress);
    TargetAddressInSafeMemory = TargetAddressInSafeMemory + PageOffset;

    //
    // Create Hook
    //
    if (!EptHookInstructionMemory(Context, HookedPage, TargetProcessId, TargetAddress, TargetAddressInSafeMemory, HookFunction, TrampolineAddress, IsX64))
    {
        OsFreeNonpagedMemory(HookedPage);

        HxTLog("Error building ept hook\n");
        return FALSE;
    }

    //
    // Add it to the list
    //
    InsertHeadList(&Context->EptState->HookedPagesList, &(HookedPage->PageHookList));

    //
    // Apply the hook to EPT
    //
    EptSetPML1AndInvalidateTLB(Context, TargetPage, HookedPage->ExecuteEntry, InveptSingleContext);

    return TRUE;
}

BOOLEAN EptClearHooks(PVMM_CONTEXT Context) {
    PLIST_ENTRY TempList = 0;

    TempList = &Context->EptState->HookedPagesList;
    while (&Context->EptState->HookedPagesList != TempList->Flink) {
        TempList = TempList->Flink;
        PEPT_HOOKED_PAGE_DETAIL HookedEntry = CONTAINING_RECORD(TempList, EPT_HOOKED_PAGE_DETAIL, PageHookList);
        EptSetPML1AndInvalidateTLB(Context, HookedEntry->EntryAddress, HookedEntry->OriginalEntry, InveptSingleContext);

        OsFreeNonpagedMemory(HookedEntry);

        RemoveEntryList(TempList);
        TempList = &Context->EptState->HookedPagesList;
    }

    return TRUE;
}