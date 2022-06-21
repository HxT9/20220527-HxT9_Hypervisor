#include "Memory.h"
#include "WindowsNT.h"
#include "Vmm.h"
#include "Logger.h"

/**
 * @brief Get Index of VA on PMLx
 *
 * @param Level PMLx
 * @param Va Virtual Address
 * @return UINT64
 */
_Use_decl_annotations_ UINT64 MemoryMapperGetIndex(PAGING_LEVEL Level, UINT64 Va)
{
    UINT64 Result = Va;
    Result >>= 12 + Level * 9;

    return Result;
}

/**
 * @brief Get page offset
 *
 * @param Level PMLx
 * @param Va Virtual Address
 * @return UINT32
 */
_Use_decl_annotations_ UINT32 MemoryMapperGetOffset(PAGING_LEVEL Level, UINT64 Va)
{
    UINT64 Result = MemoryMapperGetIndex(Level, Va);
    Result &= (1 << 9) - 1; // 0x1ff

    return Result;
}

/**
 * @brief This function gets virtual address and returns its PTE of the virtual address
 * based on the specific cr3 but without switching to the target address
 * @details the TargetCr3 should be kernel cr3 as we will use it to translate kernel
 * addresses so the kernel functions to translate addresses should be mapped; thus,
 * don't pass a KPTI meltdown user cr3 to this function
 *
 * @param Va Virtual Address
 * @param Level PMLx
 * @param TargetCr3 kernel cr3 of target process
 * @return PPAGE_ENTRY virtual address of PTE based on cr3
 */
_Use_decl_annotations_ PT_ENTRY_64* MemoryMapperGetPteVaWithoutSwitchingByCr3(PVOID Va, PAGING_LEVEL Level, CR3 TargetCr3)
{
    CR3 Cr3;
    UINT64   TempCr3;
    PUINT64  Cr3Va;
    PUINT64  PdptVa;
    PUINT64  PdVa;
    PUINT64  PtVa;
    UINT32   Offset;

    Cr3.Flags = TargetCr3.Flags;

    //
    // Cr3 should be shifted 12 to the left because it's PFN
    //
    TempCr3 = Cr3.AddressOfPageDirectory << 12;

    //
    // we need VA of Cr3, not PA
    //
    Cr3Va = OsPhysicalToVirtual(TempCr3);

    //
    // Check for invalid address
    //
    if (Cr3Va == NULL)
    {
        return NULL;
    }

    Offset = MemoryMapperGetOffset(PagingLevelPageMapLevel4, Va);

    PT_ENTRY_64* Pml4e = &Cr3Va[Offset];

    if (!Pml4e->Present || Level == PagingLevelPageMapLevel4)
    {
        return Pml4e;
    }

    PdptVa = OsPhysicalToVirtual(Pml4e->PageFrameNumber << 12);

    //
    // Check for invalid address
    //
    if (PdptVa == NULL)
    {
        return NULL;
    }

    Offset = MemoryMapperGetOffset(PagingLevelPageDirectoryPointerTable, Va);

    PT_ENTRY_64* Pdpte = &PdptVa[Offset];

    if (!Pdpte->Present || Pdpte->LargePage || Level == PagingLevelPageDirectoryPointerTable)
    {
        return Pdpte;
    }

    PdVa = OsPhysicalToVirtual(Pdpte->PageFrameNumber << 12);

    //
    // Check for invalid address
    //
    if (PdVa == NULL)
    {
        return NULL;
    }

    Offset = MemoryMapperGetOffset(PagingLevelPageDirectory, Va);

    PT_ENTRY_64* Pde = &PdVa[Offset];

    if (!Pde->Present || Pde->LargePage || Level == PagingLevelPageDirectory)
    {
        return Pde;
    }

    PtVa = OsPhysicalToVirtual(Pde->PageFrameNumber << 12);

    //
    // Check for invalid address
    //
    if (PtVa == NULL)
    {
        return NULL;
    }

    Offset = MemoryMapperGetOffset(PagingLevelPageTable, Va);

    PT_ENTRY_64* Pt = &PtVa[Offset];

    return Pt;
}

/**
 * @brief Initialize the Memory Mapper
 * @details This function should be called in vmx non-root
 * in a IRQL <= APC_LEVEL
 *
 * @return VOID
 */
VOID MemoryMapperInitialize(PVMM_PROCESSOR_CONTEXT CpuContext)
{
    UINT64 TempPte;
    UINT64 Pte;
    CR3 currentCr3;

    currentCr3.Flags = __readcr3();

    //
    // Reserve the page from system va space
    //
    CpuContext->MemoryMapper.VirtualAddress = OsAllocateMappingAddress(PAGE_SIZE);

    //
    // Get the page's Page Table Entry
    //
    CpuContext->MemoryMapper.PteVirtualAddress = MemoryMapperGetPteVaWithoutSwitchingByCr3(CpuContext->MemoryMapper.VirtualAddress, PagingLevelPageTable, currentCr3);
}

_Use_decl_annotations_ CR3 GetCr3FromProcessId(UINT32 ProcessId)
{
    PEPROCESS TargetEprocess;
    CR3  ProcessCr3 = { 0 };

    if (PsLookupProcessByProcessId(ProcessId, &TargetEprocess) != STATUS_SUCCESS)
    {
        //
        // There was an error, probably the process id was not found
        //
        return ProcessCr3;
    }

    //
    // Due to KVA Shadowing, we need to switch to a different directory table base
    // if the PCID indicates this is a user mode directory table base.
    //
    NT_KPROCESS* CurrentProcess = (NT_KPROCESS*)(TargetEprocess);
    ProcessCr3.Flags = CurrentProcess->DirectoryTableBase;

    ObDereferenceObject(TargetEprocess);

    return ProcessCr3;
}

_Use_decl_annotations_ CR3 SwitchCr3(CR3 TargetCr3)
{
    CR3 CurrentProcessCr3 = { 0 };

    //
    // Read the current cr3
    //
    CurrentProcessCr3.Flags = __readcr3();

    //
    // Change to a new cr3 (of target process)
    //
    __writecr3(TargetCr3.Flags);

    return CurrentProcessCr3;
}

_Use_decl_annotations_ CR3 SwitchToProcessCr3(UINT32 ProcessId)
{
    CR3 CurrentProcessCr3 = { 0 };
    CR3 ProcessCr3 = GetCr3FromProcessId(ProcessId);

    if (ProcessCr3.Flags) {

        //
        // Read the current cr3
        //
        CurrentProcessCr3.Flags = __readcr3();

        //
        // Change to a new cr3 (of target process)
        //
        __writecr3(ProcessCr3.Flags);
    }

    return CurrentProcessCr3;
}

_Use_decl_annotations_ UINT64 VirtualToPhysicalByCr3(PVOID VirtualAddress, CR3 TargetCr3)
{
    CR3 CurrentProcessCr3;
    UINT64   PhysicalAddress;

    //
    // Switch to new process's memory layout
    //
    CurrentProcessCr3 = SwitchCr3(TargetCr3);

    //
    // Validate if process id is valid
    //
    if (CurrentProcessCr3.Flags == NULL)
    {
        //
        // Pid is invalid
        //
        return NULL;
    }

    //
    // Read the physical address based on new cr3
    //
    PhysicalAddress = OsVirtualToPhysical(VirtualAddress);

    //
    // Restore the original process
    //
    SwitchCr3(CurrentProcessCr3);

    return PhysicalAddress;
}

/**
 * @brief Read memory safely by mapping the buffer using PTE
 * @param PaAddressToRead Physical address to read
 * @param BufferToSaveMemory buffer to save the memory
 * @param SizeToRead Size
 * @param PteVaAddress Virtual Address of PTE
 * @param MappingVa Mapping virtual address
 * @param InvalidateVpids whether invalidate based on VPIDs or not
 *
 * @return BOOLEAN returns TRUE if it was successfull and FALSE if there was error
 */
_Use_decl_annotations_ BOOLEAN ReadMemorySafeByPte(PHYSICAL_ADDRESS PaAddressToRead, PVOID BufferToSaveMemory, SIZE_T SizeToRead, UINT64 PteVaAddress, UINT64 MappingVa)
{
    PVOID       Va = MappingVa;
    PVOID       NewAddress;
    PT_ENTRY_64 PageEntry;
    PT_ENTRY_64* Pte = PteVaAddress;

    //
    // Copy the previous entry into the new entry
    //
    PageEntry.Flags = Pte->Flags;

    PageEntry.Present = 1;

    //
    // Generally we want each page to be writable
    //
    PageEntry.Write = 1;

    //
    // Do not flush this page from the TLB on CR3 switch, by setting the
    // global bit in the PTE.
    //
    PageEntry.Global = 1;

    //
    // Set the PFN of this PTE to that of the provided physical address,
    //
    PageEntry.PageFrameNumber = PaAddressToRead.QuadPart >> 12;

    //
    // Apply the page entry in a single instruction
    //
    Pte->Flags = PageEntry.Flags;

    //
    // Finally, invalidate the caches for the virtual address
    // It's not mandatory to invalidate the address in the VM nested-virtualization
    // because it will be automatically invalidated by the top hypervisor, however,
    // we should use invlpg in physical computers as it won't invalidate it automatically
    //
    __invlpg(Va);

    //
    // Compute the address
    //
    NewAddress = (PVOID)((UINT64)Va + (PAGE_4KB_OFFSET & (PaAddressToRead.QuadPart)));

    //
    // Move the address into the buffer in a safe manner
    //
    memcpy(BufferToSaveMemory, NewAddress, SizeToRead);

    //
    // Unmap Address
    //
    Pte->Flags = NULL;

    return TRUE;
}

/**
 * @brief Wrapper to read the memory safely by mapping the
 * buffer by physical address (It's a wrapper)
 *
 * @param TypeOfRead Type of read
 * @param AddressToRead Address to read
 * @param BufferToSaveMemory Destination to save
 * @param SizeToRead Size
 * @return BOOLEAN if it was successful the returns TRUE and if it was
 * unsuccessful then it returns FALSE
 */
_Use_decl_annotations_ BOOLEAN ReadVirtualMemory(PVMM_CONTEXT Context, UINT64 AddressToRead, UINT64 BufferToSaveMemory, SIZE_T SizeToRead, UINT32 TargetProcessId)
{
    UINT64                  AddressToCheck;
    PHYSICAL_ADDRESS        PhysicalAddress;
    CR3                     TargetProcessCr3;

    TargetProcessCr3 = GetCr3FromProcessId(TargetProcessId);
    if (TargetProcessCr3.Flags == NULL) {
        return FALSE;
    }

    //
    // Check to see if PTE and Reserved VA already initialized
    //
    if (GetCurrentCPUContext(Context)->MemoryMapper.VirtualAddress == NULL ||
        GetCurrentCPUContext(Context)->MemoryMapper.PteVirtualAddress == NULL)
    {
        HxTLog("MemoryMapper not initialized");
        return FALSE;
    }

    //
    // Check whether we should apply multiple accesses or not
    //
    AddressToCheck = (CHAR*)AddressToRead + SizeToRead - ((CHAR*)PAGE_ALIGN(AddressToRead));

    if (AddressToCheck > PAGE_SIZE)
    {
        //
        // Address should be accessed in more than one page
        //
        UINT64 PageCount = SizeToRead / PAGE_SIZE + 1;

        for (size_t i = 0; i <= PageCount; i++)
        {
            UINT64 ReadSize = 0;

            if (i == 0)
            {
                ReadSize = (UINT64)PAGE_ALIGN(AddressToRead + PAGE_SIZE) - AddressToRead;
            }
            else if (i == PageCount)
            {
                ReadSize = SizeToRead;
            }
            else
            {
                ReadSize = PAGE_SIZE;
            }

            //
            // One access is enough (page+size won't pass from the PAGE_ALIGN boundary)
            //
            PhysicalAddress.QuadPart = VirtualToPhysicalByCr3(AddressToRead, TargetProcessCr3);

            if (!ReadMemorySafeByPte(
                PhysicalAddress,
                BufferToSaveMemory,
                ReadSize,
                GetCurrentCPUContext(Context)->MemoryMapper.PteVirtualAddress,
                GetCurrentCPUContext(Context)->MemoryMapper.VirtualAddress))
            {
                return FALSE;
            }

            //
            // Apply the changes to the next addresses (if any)
            //
            SizeToRead = SizeToRead - ReadSize;
            AddressToRead = AddressToRead + ReadSize;
            BufferToSaveMemory = BufferToSaveMemory + ReadSize;
        }

        return TRUE;
    }
    else
    {
        //
        // One access is enough (page+size won't pass from the PAGE_ALIGN boundary)
        //
        PhysicalAddress.QuadPart = VirtualToPhysicalByCr3(AddressToRead, TargetProcessCr3);

        return ReadMemorySafeByPte(
            PhysicalAddress,
            BufferToSaveMemory,
            SizeToRead,
            GetCurrentCPUContext(Context)->MemoryMapper.PteVirtualAddress,
            GetCurrentCPUContext(Context)->MemoryMapper.VirtualAddress);
    }
}

/**
 * @brief Write memory safely by mapping the buffer using PTE
 *
 * @param SourceVA Source virtual address
 * @param PaAddressToWrite Destinaton physical address
 * @param SizeToWrite Size
 * @param PteVaAddress PTE of target virtual address
 * @param MappingVa Mapping Virtual Address
 * @param InvalidateVpids Invalidate VPIDs or not
 * @return BOOLEAN returns TRUE if it was successfull and FALSE if there was error
 */
_Use_decl_annotations_ BOOLEAN MemoryMapperWriteMemorySafeByPte(PVOID SourceVA, PHYSICAL_ADDRESS PaAddressToWrite, SIZE_T SizeToWrite, UINT64 PteVaAddress, UINT64 MappingVa)
{
    PVOID       Va = MappingVa;
    PVOID       NewAddress;
    PT_ENTRY_64 PageEntry;
    PT_ENTRY_64* Pte = PteVaAddress;

    //
    // Copy the previous entry into the new entry
    //
    PageEntry.Flags = Pte->Flags;

    PageEntry.Present = 1;

    //
    // Generally we want each page to be writable
    //
    PageEntry.Write = 1;

    //
    // Do not flush this page from the TLB on CR3 switch, by setting the
    // global bit in the PTE.
    //
    PageEntry.Global = 1;

    //
    // Set the PFN of this PTE to that of the provided physical address.
    //
    PageEntry.PageFrameNumber = PaAddressToWrite.QuadPart >> 12;

    //
    // Apply the page entry in a single instruction
    //
    Pte->Flags = PageEntry.Flags;

    //
    // Finally, invalidate the caches for the virtual address.
    //
    __invlpg(Va);

    //
    // Compute the address
    //
    NewAddress = (PVOID)((UINT64)Va + (PAGE_4KB_OFFSET & (PaAddressToWrite.QuadPart)));

    //
    // Move the address into the buffer in a safe manner
    //
    memcpy(NewAddress, SourceVA, SizeToWrite);

    //
    // Unmap Address
    //
    Pte->Flags = NULL;

    return TRUE;
}

/**
 * @brief Write memory safely by mapping the buffer (It's a wrapper)
 *
 * @param TypeOfWrite Type of memory write
 * @param DestinationAddr Destination Address
 * @param Source Source Address
 * @param SizeToWrite Size
 * @param TargetProcessCr3 The process CR3 (might be null)
 * @param TargetProcessId The process PID (might be null)
 *
 * @return BOOLEAN returns TRUE if it was successfull and FALSE if there was error
 */
_Use_decl_annotations_ BOOLEAN WriteVirtualMemory(PVMM_CONTEXT Context, UINT64 DestinationAddr, UINT64 Source, SIZE_T SizeToWrite, UINT32 TargetProcessId)
{
    UINT64                  AddressToCheck;
    PHYSICAL_ADDRESS        PhysicalAddress;
    CR3                     TargetProcessCr3;

    TargetProcessCr3 = GetCr3FromProcessId(TargetProcessId);
    if (TargetProcessCr3.Flags == NULL) {
        return FALSE;
    }

    //
    // Check to see if PTE and Reserved VA already initialized
    //
    if (GetCurrentCPUContext(Context)->MemoryMapper.VirtualAddress == NULL ||
        GetCurrentCPUContext(Context)->MemoryMapper.PteVirtualAddress == NULL)
    {
        //
        // Not initialized
        //
        return FALSE;
    }

    //
    // Check whether it needs multiple accesses to different pages or no
    //
    AddressToCheck = (CHAR*)DestinationAddr + SizeToWrite - ((CHAR*)PAGE_ALIGN(DestinationAddr));

    if (AddressToCheck > PAGE_SIZE)
    {
        //
        // It need multiple accesses to different pages to access the memory
        //
        UINT64 PageCount = SizeToWrite / PAGE_SIZE + 1;

        for (SIZE_T i = 0; i <= PageCount; i++)
        {
            UINT64 WriteSize = 0;

            if (i == 0)
            {
                WriteSize = (UINT64)PAGE_ALIGN(DestinationAddr + PAGE_SIZE) - DestinationAddr;
            }
            else if (i == PageCount)
            {
                WriteSize = SizeToWrite;
            }
            else
            {
                WriteSize = PAGE_SIZE;
            }

            /*
            LogInfo("Addr From : %llx to Addr To : %llx | WriteSize : %llx\n",
                   DestinationAddr,
                   DestinationAddr + WriteSize,
                   WriteSize);
            */

            PhysicalAddress.QuadPart = VirtualToPhysicalByCr3(DestinationAddr, TargetProcessCr3);

            if (!MemoryMapperWriteMemorySafeByPte(
                Source,
                PhysicalAddress,
                WriteSize,
                GetCurrentCPUContext(Context)->MemoryMapper.PteVirtualAddress,
                GetCurrentCPUContext(Context)->MemoryMapper.VirtualAddress))
            {
                return FALSE;
            }

            SizeToWrite = SizeToWrite - WriteSize;
            DestinationAddr = DestinationAddr + WriteSize;
            Source = Source + WriteSize;
        }

        return TRUE;
    }
    else
    {
        //
        // One access is enough to write
        //
        PhysicalAddress.QuadPart = VirtualToPhysicalByCr3(DestinationAddr, TargetProcessCr3);

        return MemoryMapperWriteMemorySafeByPte(
            Source,
            PhysicalAddress,
            SizeToWrite,
            GetCurrentCPUContext(Context)->MemoryMapper.PteVirtualAddress,
            GetCurrentCPUContext(Context)->MemoryMapper.VirtualAddress);
    }
}