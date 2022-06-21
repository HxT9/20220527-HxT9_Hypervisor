#pragma once
#include "ia-32/ia32.h"

typedef struct _VMM_CONTEXT VMM_CONTEXT, * PVMM_CONTEXT;
typedef struct _VMM_PROCESSOR_CONTEXT VMM_PROCESSOR_CONTEXT, * PVMM_PROCESSOR_CONTEXT;

#define PAGE_OFFSET(Va) ((PVOID)((ULONG_PTR)(Va) & (PAGE_SIZE - 1)))

#define PAGE_4KB_OFFSET ((UINT64)(1 << 12) - 1)
#define PAGE_2MB_OFFSET ((UINT64)(1 << 21) - 1)
#define PAGE_4MB_OFFSET ((UINT64)(1 << 22) - 1)
#define PAGE_1GB_OFFSET ((UINT64)(1 << 30) - 1)

typedef enum _PAGING_LEVEL
{
    PagingLevelPageTable = 0,
    PagingLevelPageDirectory,
    PagingLevelPageDirectoryPointerTable,
    PagingLevelPageMapLevel4
} PAGING_LEVEL;

typedef struct _NT_KPROCESS
{
    DISPATCHER_HEADER Header;
    LIST_ENTRY        ProfileListHead;
    ULONG_PTR         DirectoryTableBase;
    UCHAR             Data[1];
} NT_KPROCESS, * PNT_KPROCESS;

/**
 * @brief Memory mapper PTE and reserved virtual address
 *
 */
typedef struct _MEMORY_MAPPER_ADDRESSES
{
    UINT64 PteVirtualAddress; // The virtual address of PTE
    UINT64 VirtualAddress;     // The actual kernel virtual address to read or write
} MEMORY_MAPPER_ADDRESSES, * PMEMORY_MAPPER_ADDRESSES;

/**
 * @brief Converts pid to kernel cr3
 *
 * @details this function should NOT be called from vmx-root
 *
 * @param ProcessId ProcessId to switch
 * @return CR3_TYPE The cr3 of the target process
 */
_Use_decl_annotations_ CR3 GetCr3FromProcessId(UINT32 ProcessId);

/**
 * @brief Switch to another process's cr3
 *
 * @param TargetCr3 cr3 to switch
 * @return CR3_TYPE The cr3 of current process
 */
_Use_decl_annotations_ CR3 SwitchCr3(CR3 TargetCr3);

_Use_decl_annotations_ CR3 SwitchToProcessCr3(UINT32 ProcessId);

/**
 * @brief Converts Virtual Address to Physical Address based
 * on a specific process's kernel cr3
 *
 * @param VirtualAddress The target virtual address
 * @param TargetCr3 The target's process cr3
 * @return UINT64 Returns the physical address
 */
_Use_decl_annotations_ UINT64 VirtualToPhysicalByCr3(PVOID VirtualAddress, CR3 TargetCr3);

/**
 * @brief Initialize the Memory Mapper
 * @details This function should be called in vmx non-root
 * in a IRQL <= APC_LEVEL
 *
 * @return VOID
 */
VOID MemoryMapperInitialize(PVMM_PROCESSOR_CONTEXT CpuContext);

/**
 * @brief Wrapper to read the memory safely by mapping the
 * buffer by physical address (It's a wrapper)
 *
 * @param AddressToRead Address to read
 * @param BufferToSaveMemory Destination to save
 * @param SizeToRead Size
 * @return BOOLEAN if it was successful the returns TRUE and if it was
 * unsuccessful then it returns FALSE
 */
_Use_decl_annotations_ BOOLEAN ReadVirtualMemory(PVMM_CONTEXT Context, UINT64 AddressToRead, UINT64 BufferToSaveMemory, SIZE_T SizeToRead, UINT32 TargetProcessId);

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
_Use_decl_annotations_ BOOLEAN WriteVirtualMemory(PVMM_CONTEXT Context, UINT64 DestinationAddr, UINT64 Source, SIZE_T SizeToWrite, UINT32 TargetProcessId);