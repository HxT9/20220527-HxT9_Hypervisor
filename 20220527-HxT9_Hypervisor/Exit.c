#include "Exit.h"
#include "Vmcs.h"
#include "Utilities.h"
#include "Logger.h"
#include "WindowsNT.h"
#include "Ept.h"
#include "Memory.h"
#include "EptHook.h"
#include "Vmx.h"

typedef struct _HookData {
	unsigned __int64 FunctionToHook;
	unsigned __int64 HkFunction;
	unsigned __int64 TrampolineFunction;
} HookData, * PHookData;

VOID ExitHandleTerminateVmx(PVMM_PROCESSOR_CONTEXT ProcessorContext, PVMEXIT_CONTEXT ExitContext);

/*
 * Intialize fields of the exit context based on values read from the VMCS.
 */
VOID InitializeExitContext(PVMEXIT_CONTEXT ExitContext, PGPREGISTER_CONTEXT GuestRegisters)
{
	SIZE_T VmError = 0;

	OsZeroMemory(ExitContext, sizeof(VMEXIT_CONTEXT));

	/*
	 * Store pointer of the guest register context on the stack to the context for later access.
	 */
	ExitContext->GuestContext = GuestRegisters;

	/* By default, we increment RIP unless we're handling an EPT violation. */
	ExitContext->ShouldIncrementRIP = TRUE;

	/* By default, we continue execution. */
	ExitContext->ShouldStopExecution = FALSE;

	// Guest RSP at the time of exit
	VmxVmreadFieldToImmediate(VMCS_GUEST_RSP, &ExitContext->GuestContext->GuestRSP);

	// Guest RIP at the time of exit
	VmxVmreadFieldToImmediate(VMCS_GUEST_RIP, &ExitContext->GuestRIP);

	// Guest RFLAGS at the time of exit
	VmxVmreadFieldToImmediate(VMCS_GUEST_RFLAGS, &ExitContext->GuestFlags.RFLAGS);

	// The type of exit
	VmxVmreadFieldToRegister(VMCS_EXIT_REASON, &ExitContext->ExitReason);

	// Additional information about specific types of exits
	VmxVmreadFieldToImmediate(VMCS_EXIT_QUALIFICATION, &ExitContext->ExitQualification);

	// Length of the exiting instruction
	VmxVmreadFieldToImmediate(VMCS_VMEXIT_INSTRUCTION_LENGTH, &ExitContext->InstructionLength);

	// Information about the faulting instruction
	VmxVmreadFieldToImmediate(VMCS_VMEXIT_INSTRUCTION_INFO, &ExitContext->InstructionInformation);

	// Guest physical address during EPT exits
	VmxVmreadFieldToImmediate(VMCS_GUEST_PHYSICAL_ADDRESS, &ExitContext->GuestPhysicalAddress);
}

VOID ExitHandleCpuid(PVMM_PROCESSOR_CONTEXT ProcessorContext, PVMEXIT_CONTEXT ExitContext)
{
	INT32 CPUInfo[4];

	// Perform actual CPUID
	__cpuidex(CPUInfo, (int)ExitContext->GuestContext->GuestRAX, (int)ExitContext->GuestContext->GuestRCX);

	/*
	 * Give guest the results of the CPUID call.
	 */
	ExitContext->GuestContext->GuestRAX = CPUInfo[0];
	ExitContext->GuestContext->GuestRBX = CPUInfo[1];
	ExitContext->GuestContext->GuestRCX = CPUInfo[2];
	ExitContext->GuestContext->GuestRDX = CPUInfo[3];
}

VOID ExitHandleEptMisconfiguration(PVMM_PROCESSOR_CONTEXT ProcessorContext, PVMEXIT_CONTEXT ExitContext)
{
	UNREFERENCED_PARAMETER(ProcessorContext);

	HxTLog("EPT Misconfiguration! A field in the EPT paging structure was invalid. Faulting guest address: 0x%llX\n", ExitContext->GuestPhysicalAddress);

	ExitContext->ShouldIncrementRIP = FALSE;
	ExitContext->ShouldStopExecution = TRUE;

	// We can't continue now. EPT misconfiguration is a fatal exception that will probably crash the OS if we don't get out *now*.
}

VOID ExitHandleVmcall(PVMM_PROCESSOR_CONTEXT ProcessorContext, PVMEXIT_CONTEXT ExitContext)
{
	SIZE_T Size;
	DWORD Pid = 0;
	__int32 Value;
	BOOL IsX64 = FALSE;
	PEPROCESS PeProc;
	HookData hkData;

	if (ExitContext->GuestContext->GuestRCX == VMX_VMCALL_HOOK_PWD) {
		__debugbreak();
		//HxTLog("VmCall add page hook at 0x%p to 0x%p trampoline at 0x%p", ExitContext->GuestContext->GuestRDX, ExitContext->GuestContext->GuestRAX, (PVOID*)ExitContext->GuestContext->GuestRBX);
		//EptAddPageHook(ProcessorContext, ExitContext->GuestContext->GuestRDX, ExitContext->GuestContext->GuestRAX, (PVOID*)ExitContext->GuestContext->GuestRBX);

		switch ((__int32)ExitContext->GuestContext->GuestRAX) {
		case 1: //Hook
			//RBX Pid
			//RDX Address to hook
			//Stack Hook Function
			//Stack + 8 Trampoline space
			Pid = ExitContext->GuestContext->GuestRBX;
			ReadVirtualMemory(ProcessorContext->GlobalContext, ExitContext->GuestContext->GuestRDX, &hkData, sizeof(HookData), Pid);

			if (NT_SUCCESS(PsLookupProcessByProcessId(Pid, &PeProc))) {

				IsX64 = !OsIsWow64Process(PeProc);
				if (EptHookAddHook(ProcessorContext->GlobalContext, hkData.FunctionToHook, hkData.HkFunction, hkData.TrampolineFunction, Pid, IsX64)) {
					HxTLog("Hook ok\n");
				}
				else {
					HxTLog("Hook failed\n");
				}

				ObDereferenceObject(PeProc);
			}

			break;
		case 2: //EPT Reset Hooks
			EptClearHooks(ProcessorContext->GlobalContext, TRUE);
			break;
		case 99:
			ExitHandleTerminateVmx(ProcessorContext, ExitContext);
			return;
			break;
		}
	}

	//
	// Set the CF flag, which is how VMX instructions indicate failure
	//
	ExitContext->GuestFlags.EFLAGS.Flags |= 0x1; // VM_FAIL_INVALID

	//
	// RFLAGs is actually restored from the VMCS, so update it here
	//
	__vmx_vmwrite(VMCS_GUEST_RFLAGS, ExitContext->GuestFlags.EFLAGS.Flags);
}

VOID ExitHandleVmx(PVMM_PROCESSOR_CONTEXT ProcessorContext, PVMEXIT_CONTEXT ExitContext)
{
	//
	// Set the CF flag, which is how VMX instructions indicate failure
	//
	ExitContext->GuestFlags.EFLAGS.Flags |= 0x1; // VM_FAIL_INVALID

	//
	// RFLAGs is actually restored from the VMCS, so update it here
	//
	__vmx_vmwrite(VMCS_GUEST_RFLAGS, ExitContext->GuestFlags.EFLAGS.Flags);
}

extern void AsmReloadGdtr(void* GdtBase, unsigned long GdtLimit);
extern void AsmReloadIdtr(void* GdtBase, unsigned long GdtLimit);
VOID RestoreRegisters()
{
	UINT64 FsBase;
	UINT64 GsBase;
	UINT64 GdtrBase;
	UINT64 GdtrLimit;
	UINT64 IdtrBase;
	UINT64 IdtrLimit;

	//
	// Restore FS Base
	//
	__vmx_vmread(VMCS_GUEST_FS_BASE, &FsBase);
	__writemsr(IA32_FS_BASE, FsBase);

	//
	// Restore Gs Base
	//
	__vmx_vmread(VMCS_GUEST_GS_BASE, &GsBase);
	__writemsr(IA32_GS_BASE, GsBase);

	//
	// Restore GDTR
	//
	__vmx_vmread(VMCS_GUEST_GDTR_BASE, &GdtrBase);
	__vmx_vmread(VMCS_GUEST_GDTR_LIMIT, &GdtrLimit);

	AsmReloadGdtr(GdtrBase, GdtrLimit);

	//
	// Restore IDTR
	//
	__vmx_vmread(VMCS_GUEST_IDTR_BASE, &IdtrBase);
	__vmx_vmread(VMCS_GUEST_IDTR_LIMIT, &IdtrLimit);

	AsmReloadIdtr(IdtrBase, IdtrLimit);
}
VOID ExitHandleTerminateVmx(PVMM_PROCESSOR_CONTEXT ProcessorContext, PVMEXIT_CONTEXT ExitContext) {
	UINT64 GuestRSP = 0, GuestRIP = 0, GuestCr3 = 0, ExitInstructionLength = 0;
	
	//
	// According to SimpleVisor :
	//  	Our callback routine may have interrupted an arbitrary user process,
	//  	and therefore not a thread running with a system-wide page directory.
	//  	Therefore if we return back to the original caller after turning off
	//  	VMX, it will keep our current "host" CR3 value which we set on entry
	//  	to the PML4 of the SYSTEM process. We want to return back with the
	//  	correct value of the "guest" CR3, so that the currently executing
	//  	process continues to run with its expected address space mappings.
	//

	__vmx_vmread(VMCS_GUEST_CR3, &GuestCr3);
	__writecr3(GuestCr3);

	//
	// Read guest rsp and rip
	//
	__vmx_vmread(VMCS_GUEST_RIP, &GuestRIP);
	__vmx_vmread(VMCS_GUEST_RSP, &GuestRSP);

	//
	// Read instruction length
	//
	__vmx_vmread(VMCS_VMEXIT_INSTRUCTION_LENGTH, &ExitInstructionLength);
	GuestRIP += ExitInstructionLength;

	//
	// Set the previous register states
	//
	ProcessorContext->ExitRSP = GuestRSP;
	ProcessorContext->ExitRIP = GuestRIP;

	ExitContext->ShouldStopExecution = TRUE;

	//
	// Restore the previous FS, GS , GDTR and IDTR register as patchguard might find the modified
	//
	RestoreRegisters();

	VmxExitRootMode(ProcessorContext);
}

VOID ExitHandleUnknownExit(PVMM_PROCESSOR_CONTEXT ProcessorContext, PVMEXIT_CONTEXT ExitContext)
{
	UNREFERENCED_PARAMETER(ProcessorContext);

	HxTLog("Unknown exit reason! An exit was made but no handler was configured to handle it. Reason: 0x%llX\n", ExitContext->ExitReason.BasicExitReason);

	// Try to keep executing, despite the unknown exit.
	ExitContext->ShouldIncrementRIP = TRUE;
}

/**
 * Dispatch to the correct handler function given the exit code.
 */
BOOL ExitDispatchFunction(PVMM_PROCESSOR_CONTEXT ProcessorContext, PVMEXIT_CONTEXT ExitContext)
{
	SIZE_T VmError = 0;
	SIZE_T GuestInstructionLength;

	/*
	 * Choose an appropriate function to handle our exit.
	 */
	switch (ExitContext->ExitReason.BasicExitReason)
	{
		/*
		 * The following instructions cause VM exits when they are executed in VMX non-root operation: CPUID, GETSEC,1
		 * INVD, and XSETBV. This is also true of instructions introduced with VMX, which include: INVEPT, INVVPID,
		 * VMCALL, VMCLEAR, VMLAUNCH, VMPTRLD, VMPTRST, VMRESUME, VMXOFF, and VMXON.
		 *
		 * GETSEC will never exit because we will never run in SMX mode.
		 */
	case VMX_EXIT_REASON_EXECUTE_CPUID:
		ExitHandleCpuid(ProcessorContext, ExitContext);
		break;
	case VMX_EXIT_REASON_EXECUTE_INVD:
		__wbinvd();
		break;
	case VMX_EXIT_REASON_EXECUTE_XSETBV:
		_xsetbv((UINT32)ExitContext->GuestContext->GuestRCX,
			ExitContext->GuestContext->GuestRDX << 32 |
			ExitContext->GuestContext->GuestRAX);
		break;
	case VMX_EXIT_REASON_EPT_MISCONFIGURATION:
		ExitHandleEptMisconfiguration(ProcessorContext, ExitContext);
		break;
	case VMX_EXIT_REASON_EPT_VIOLATION:
		EptHandleEptViolation(ProcessorContext, ExitContext);
		break;
	case VMX_EXIT_REASON_EXECUTE_VMCALL:
		ExitHandleVmcall(ProcessorContext, ExitContext);
		break;
	case VMX_EXIT_REASON_EXECUTE_VMCLEAR:
	case VMX_EXIT_REASON_EXECUTE_VMLAUNCH:
	case VMX_EXIT_REASON_EXECUTE_VMPTRLD:
	case VMX_EXIT_REASON_EXECUTE_VMPTRST:
	case VMX_EXIT_REASON_EXECUTE_VMREAD:
	case VMX_EXIT_REASON_EXECUTE_VMRESUME:
	case VMX_EXIT_REASON_EXECUTE_VMWRITE:
	case VMX_EXIT_REASON_EXECUTE_VMXOFF:
	case VMX_EXIT_REASON_EXECUTE_VMXON:
		ExitHandleVmx(ProcessorContext, ExitContext);
		break;
	default:
		ExitHandleUnknownExit(ProcessorContext, ExitContext);
		break;
	}

	if (ExitContext->ShouldStopExecution)
	{
		HxTLog("ExitDispatchFunction: Leaving VMX mode.\n");
		return FALSE;
	}

	/* If we're an 'instruction' exit, we need to act like a fault handler and move the instruction pointer forward. */
	if (ExitContext->ShouldIncrementRIP)
	{
		VmxVmreadFieldToImmediate(VMCS_VMEXIT_INSTRUCTION_LENGTH, &GuestInstructionLength);

		ExitContext->GuestRIP += GuestInstructionLength;

		VmxVmwriteFieldFromImmediate(VMCS_GUEST_RIP, ExitContext->GuestRIP);
	}

	return TRUE;
}