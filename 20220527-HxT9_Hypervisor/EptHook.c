#include "EptHook.h"
#include "Ept.h"
#include "WindowsNT.h"
#include "Memory.h"
#include "Logger.h"

#define ZYDIS_STATIC_DEFINE
#include <Zydis/Zydis.h>

#define TRAMPOLINE_MAX_SIZE 0x100
#define TRAMPOLINE_JMP_SIZE 14

/*
CC614F E8 8c b1 ff ff->call cc12e0
dst - src - 5
*/

VOID EptHookWriteAbsoluteJump(PCHAR TargetBuffer, SIZE_T TargetAddress, BOOL IsX64)
{
    //push f1234567h                        68 67 45 23 f1
    //mov dword ptr [rsp+4], 5678h          c7 44 24 04 67 45 23 f1
    //ret                                   c3

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

VOID ParseTrampoline(UINT64 TrampolineBuffer, UINT64 TargetFunction, UINT64 TrampolineAddress, UINT64 TrampolineSize, BOOL IsX64) {
    ZydisDecoder decoder;
    ZydisDecodedInstruction instruction;
    ZydisDecodedOperand operand[ZYDIS_MAX_OPERAND_COUNT];
    UINT64 decodingAddress = TrampolineBuffer;
    UINT64 delta = TrampolineAddress - TargetFunction;
    UINT64 instructionTarget;
    PUCHAR rawInstruction;

    __debugbreak();

     if (!ZYAN_SUCCESS(ZydisDecoderInit(&decoder,
        IsX64 ? ZYDIS_MACHINE_MODE_LONG_64 : ZYDIS_MACHINE_MODE_LONG_COMPAT_32,
        IsX64 ? ZYDIS_STACK_WIDTH_64 : ZYDIS_STACK_WIDTH_32)))
        return FALSE;

    while (ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder, decodingAddress, TrampolineBuffer + TRAMPOLINE_MAX_SIZE - decodingAddress, &instruction, &operand, ZYDIS_MAX_OPERAND_COUNT, 0))) {
        rawInstruction = decodingAddress;

        if (instruction.attributes & ZYDIS_ATTRIB_IS_RELATIVE) {

            if (instruction.raw.disp.size == 8) {
                instructionTarget = TargetFunction + instruction.raw.disp.value;
                if (instructionTarget - TargetFunction <= TRAMPOLINE_JMP_SIZE) continue;

                EptHookWriteAbsoluteJump(TrampolineBuffer + TrampolineSize, instructionTarget, IsX64);
                *((BYTE*)rawInstruction + instruction.raw.disp.offset) = (TrampolineBuffer + TrampolineSize) - decodingAddress - instruction.length;

                TrampolineSize += 14;
            }
            else if (instruction.raw.disp.size == 32) {
                if (instruction.raw.disp.offset <= TRAMPOLINE_JMP_SIZE) continue;

                *((UINT32*)rawInstruction + instruction.raw.disp.offset) = instruction.raw.disp.value + delta;
            }

            for (int i = 0; i < 2; i++) {
                if (instruction.raw.imm[i].offset && instruction.raw.imm[i].is_relative) {
                    if (instruction.raw.imm[i].size == 8) {
                        instructionTarget = TargetFunction + (decodingAddress - TrampolineBuffer) + *(BYTE*)(rawInstruction + instruction.raw.imm[i].offset) + instruction.length;
                        if (instructionTarget - TargetFunction <= TRAMPOLINE_JMP_SIZE) continue;

                        EptHookWriteAbsoluteJump(TrampolineBuffer + TrampolineSize, instructionTarget, IsX64);
                        *(BYTE*)(rawInstruction + instruction.raw.imm[i].offset) = (TrampolineBuffer + TrampolineSize) - decodingAddress - instruction.length;

                        TrampolineSize += 14;
                    }
                    else if (instruction.raw.imm[i].size == 32) {
                        if (instruction.raw.imm[i].offset <= TRAMPOLINE_JMP_SIZE) continue;

                        *(UINT32*)(rawInstruction + instruction.raw.imm[i].offset) = instruction.raw.imm[i].value.u + delta;
                    }
                }
            }
        }

        decodingAddress += instruction.length;
    }
}

SIZE_T calcTrampolineSize(UINT64 TargetFunction, INT64 MinSize, BOOL IsX64) {
    ZydisDecoder decoder;
    INT64 totalSize = 0;
    if (IsX64) {
        if (!ZYAN_SUCCESS(ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64)))
            return 0;
    }
    else {
        if (!ZYAN_SUCCESS(ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_COMPAT_32, ZYDIS_STACK_WIDTH_32)))
            return 0;
    }

    ZydisDecodedInstruction instruction;
    ZydisDecodedOperand operand[ZYDIS_MAX_OPERAND_COUNT];
    while (ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder, TargetFunction, (ZyanUSize)(PAGE_SIZE - (UINT64)PAGE_OFFSET(TargetFunction)), &instruction, &operand, ZYDIS_MAX_OPERAND_COUNT, 0))) {
        TargetFunction += instruction.length;
        totalSize += instruction.length;

        if (totalSize >= MinSize) break;
    }
    return totalSize;
}

BOOL EptHookInstructionMemory(PVMM_CONTEXT Context, PEPT_HOOKED_PAGE_DETAIL Hook, UINT32 TargetProcessId, PVOID TargetFunction, PVOID TargetFunctionInSafeMemory, PVOID HookFunction, PVOID TrampolineAddress, BOOL IsX64)
{
    SIZE_T SizeOfHookedInstructions;
    SIZE_T OffsetIntoPage;
    PCHAR TrampolineBuffer;

    OffsetIntoPage = ADDRMASK_EPT_PML1_OFFSET((SIZE_T)TargetFunction);
    HxTLog("OffsetIntoPage: 0x%llx\n", OffsetIntoPage);

    if ((OffsetIntoPage + TRAMPOLINE_JMP_SIZE) > PAGE_SIZE - 1)
    {
        HxTLog("Function extends past a page boundary. We just don't have the technology to solve this.....\n");
        return FALSE;
    }

    SizeOfHookedInstructions = calcTrampolineSize(TargetFunctionInSafeMemory, TRAMPOLINE_JMP_SIZE, IsX64);
    /* Determine the number of instructions necessary to overwrite using Length Disassembler Engine */
    /*for (SizeOfHookedInstructions = 0; SizeOfHookedInstructions < 14; SizeOfHookedInstructions += LDE(TargetFunctionInSafeMemory, (IsX64 ? 64 : 0)))
    {
        // Get the full size of instructions necessary to copy
    }*/
    if (!SizeOfHookedInstructions) return FALSE;

    HxTLog("Number of bytes of instruction mem: %d\n", SizeOfHookedInstructions);

    /* Build a trampoline */

    /* Allocate some executable memory for the trampoline */
    TrampolineBuffer = OsAllocateExecutableNonpagedMemory(TRAMPOLINE_MAX_SIZE);
    __stosb(TrampolineBuffer, 0x90, 0x100);
    ReadVirtualMemory(Context, TargetFunction, TrampolineBuffer, SizeOfHookedInstructions, TargetProcessId);

    Hook->Trampoline = TrampolineAddress;

    /* Add the absolute jump back to the original function. */

    EptHookWriteAbsoluteJump(&TrampolineBuffer[SizeOfHookedInstructions], (SIZE_T)TargetFunction + SizeOfHookedInstructions, IsX64);
    ParseTrampoline(TrampolineBuffer, TargetFunction, TrampolineAddress, SizeOfHookedInstructions + TRAMPOLINE_JMP_SIZE, IsX64);
    WriteVirtualMemory(Context, TrampolineAddress, TrampolineBuffer, TRAMPOLINE_MAX_SIZE, TargetProcessId);
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

#if 1
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

BOOLEAN EptClearHooks(PVMM_CONTEXT Context, BOOL InvalidateTLB) {
    PLIST_ENTRY TempList = 0;

    TempList = &Context->EptState->HookedPagesList;
    while (&Context->EptState->HookedPagesList != TempList->Flink) {
        TempList = TempList->Flink;
        PEPT_HOOKED_PAGE_DETAIL HookedEntry = CONTAINING_RECORD(TempList, EPT_HOOKED_PAGE_DETAIL, PageHookList);
        if(InvalidateTLB)
            EptSetPML1AndInvalidateTLB(Context, HookedEntry->EntryAddress, HookedEntry->OriginalEntry, InveptSingleContext);
        else
            EptSetPML1(Context, HookedEntry->EntryAddress, HookedEntry->OriginalEntry);

        OsFreeNonpagedMemory(HookedEntry);

        RemoveEntryList(TempList);
        TempList = &Context->EptState->HookedPagesList;
    }

    return TRUE;
}