#include "WindowsNT.h"
#include "Logger.h"
#include "ArchIntel.h"

/*
 * Pool tag for memory allocations.
 */
#define HV_POOL_TAG (ULONG)'HxT9'

SIZE_T OsGetCPUCount()
{
	return (SIZE_T)KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
}

SIZE_T OsGetCurrentProcessorNumber()
{
	return (SIZE_T)KeGetCurrentProcessorNumberEx(NULL);
}

NTKERNELAPI _IRQL_requires_max_(APC_LEVEL) _IRQL_requires_min_(PASSIVE_LEVEL) _IRQL_requires_same_ VOID KeGenericCallDpc(_In_ PKDEFERRED_ROUTINE Routine, _In_opt_ PVOID Context);
void OsGenericCallDPC(PKDEFERRED_ROUTINE Routine, PVOID Context) {
	KeGenericCallDpc(Routine, Context);
}

NTKERNELAPI _IRQL_requires_(DISPATCH_LEVEL) _IRQL_requires_same_ LOGICAL KeSignalCallDpcSynchronize(_In_ PVOID SystemArgument2);
void OsSignalCallDpcSynchronize(_In_ PVOID SystemArgument2) {
	KeSignalCallDpcSynchronize(SystemArgument2);
}

NTKERNELAPI _IRQL_requires_(DISPATCH_LEVEL) _IRQL_requires_same_ VOID KeSignalCallDpcDone(_In_ PVOID SystemArgument1);
void OsSignalCallDpcDone(_In_ PVOID SystemArgument1) {
	KeSignalCallDpcDone(SystemArgument1);
}

VOID OsZeroMemory(PVOID VirtualAddress, SIZE_T Length)
{
	RtlZeroMemory(VirtualAddress, Length);
}

PVOID OsVirtualToPhysical(PVOID VirtualAddress)
{
	return MmGetPhysicalAddress(VirtualAddress).QuadPart;
}

PVOID OsPhysicalToVirtual(PVOID PhysicalAddressIn)
{
	PHYSICAL_ADDRESS PhysicalAddress;
	PhysicalAddress.QuadPart = (ULONG64)PhysicalAddressIn;

	return (PVOID)MmGetVirtualForPhysical(PhysicalAddress);
}

VOID OsCaptureContext(CONTEXT* ContextRecord)
{
	ArchCaptureContext(ContextRecord);
}

DECLSPEC_NORETURN NTSYSAPI VOID RtlRestoreContext(_In_ PCONTEXT ContextRecord, _In_opt_ struct _EXCEPTION_RECORD* ExceptionRecord);
VOID OsRestoreContext(CONTEXT* ContextRecord)
{
	RtlRestoreContext(ContextRecord, NULL);
}

PVOID OsAllocateNonpagedMemory(SIZE_T NumberOfBytes)
{
	PVOID Output;

	Output = ExAllocatePoolWithTag(NonPagedPoolNx, NumberOfBytes, HV_POOL_TAG);

	return Output;
}

VOID OsFreeNonpagedMemory(PVOID MemoryPointer)
{
	ExFreePoolWithTag(MemoryPointer, HV_POOL_TAG);
}

PVOID OsAllocateContiguousAlignedPages(SIZE_T NumberOfPages)
{
	PHYSICAL_ADDRESS MaxSize;
	PVOID Output;

	// Allocate address anywhere in the OS's memory space
	MaxSize.QuadPart = MAXULONG64;

	Output = MmAllocateContiguousMemory(NumberOfPages * PAGE_SIZE, MaxSize);

	return Output;
}

VOID OsFreeContiguousAlignedPages(PVOID PageRegionAddress)
{
	MmFreeContiguousMemory(PageRegionAddress);
}

PVOID OsAllocateExecutableNonpagedMemory(SIZE_T NumberOfBytes)
{
	PVOID Output;

	Output = ExAllocatePoolWithTag(NonPagedPool, NumberOfBytes, HV_POOL_TAG);

	if (Output == NULL)
	{
		HxTLog("OsAllocateExecutableNonpagedMemory: Out of memory!\n");
	}

	return Output;
}