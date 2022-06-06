#pragma once

/*
 * Get the number of CPUs on the system
 */
SIZE_T OsGetCPUCount();

/*
 * Get the current CPU number of the processor executing this function.
 */
SIZE_T OsGetCurrentProcessorNumber();

/*
* Generate a DPC that makes all processors execute the broadcast function.
*/
void OsGenericCallDPC(PKDEFERRED_ROUTINE Routine, PVOID Context);

void OsSignalCallDpcSynchronize(_In_ PVOID SystemArgument2);

void OsSignalCallDpcDone(_In_ PVOID SystemArgument1);

/*
 * Zero out Length bytes of a region of memory.
 */
VOID OsZeroMemory(PVOID VirtualAddress, SIZE_T Length);

/*
 * Convert a virtual address to a physical address.
 */
PVOID OsVirtualToPhysical(PVOID VirtualAddress);

/*
 * Convert a physical address to a virtual address.
 */
PVOID OsPhysicalToVirtual(PVOID PhysicalAddressIn);

/*
 * OS-dependent version of RtlCaptureContext from NT.
 */
VOID OsCaptureContext(CONTEXT* ContextRecord);

/*
 * OS-dependent version of RtlRestoreContext from NT.
 */
VOID OsRestoreContext(CONTEXT* ContextRecord);

/*
 * Allocate generic, nonpaged r/w memory.
 *
 * Returns NULL if the bytes could not be allocated.
 */
PVOID OsAllocateNonpagedMemory(SIZE_T NumberOfBytes);

/*
 * Free memory allocated with OsAllocateNonpagedMemory.
 */
VOID OsFreeNonpagedMemory(PVOID MemoryPointer);

/*
 * Allocate a number of page-aligned, contiguous pages of memory and return a pointer to the region.
 *
 * Returns NULL if the pages could not be allocated.
 */
PVOID OsAllocateContiguousAlignedPages(SIZE_T NumberOfPages);

/*
 * Free a region of pages allocated by OsAllocateContiguousAlignedPages.
 */
VOID OsFreeContiguousAlignedPages(PVOID PageRegionAddress);

/*
 * Allocate generic, nonpaged, executable r/w memory.
 *
 * Returns NULL if the bytes could not be allocated.
 */
PVOID OsAllocateExecutableNonpagedMemory(SIZE_T NumberOfBytes);