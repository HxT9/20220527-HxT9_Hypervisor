#pragma once
#include "ia-32/ia32.h"
#include "Settings.h"
#include "ArchIntel.h"
#include "Ept.h"
#include "Memory.h"

typedef struct _VMM_CONTEXT VMM_CONTEXT, * PVMM_CONTEXT;

typedef struct _VMM_HOST_STACK_REGION
{
	/*
	 * Above the context pointer is the actual host stack which will be used by the exit handler
	 * for general operation.
	 */
	CHAR HostStack[VMM_SETTING_STACK_SPACE];

	/*
	 * Top of the host stack must always have a pointer to the global context.
	 * This allows the exit handler to access the global context after the host area is loaded.
	 * In order to follow the x64 calling convention, must ensure that the end of rsp is 8 after entering HOST.
	 * So we need a 16-byte aligned member in front of GlobalContext.
	 */
	DECLSPEC_ALIGN(16) UINT64 Alignment;
	PVMM_CONTEXT GlobalContext;

} VMM_HOST_STACK_REGION, * PVMM_HOST_STACK_REGION;

typedef struct _VMM_PROCESSOR_CONTEXT {
	PVMM_CONTEXT GlobalContext;

	BOOL Launched;

	VMXON* VmxonRegion;
	PVOID VmxonRegionPhysical;

	VMCS* VmcsRegion;
	PVOID VmcsRegionPhysical;

	VMX_MSR_BITMAP* MsrBitmap;
	PVOID MsrBitmapPhysical;

	CONTEXT RegistersContext;
	IA32_SPECIAL_REGISTERS SpecialRegistersContext;

	VMM_HOST_STACK_REGION HostStack;

	MEMORY_MAPPER_ADDRESSES MemoryMapper;

	UINT64 ExitRSP;
	UINT64 ExitRIP;
}VMM_PROCESSOR_CONTEXT, *PVMM_PROCESSOR_CONTEXT;

typedef struct _VMM_CONTEXT {
	SIZE_T ProcessorCount;
	SIZE_T SuccessfulInitializationsCount;
	PVMM_PROCESSOR_CONTEXT* ProcessorContext;
	IA32_VMX_BASIC_REGISTER VmxCapabilities;
	SIZE_T SystemDirectoryTableBase;
	PEPT_STATE EptState;
}VMM_CONTEXT, *PVMM_CONTEXT;