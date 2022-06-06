#include "Exit.h"
#include "Vmcs.h"
#include "Utilities.h"
#include "Logger.h"
#include "WindowsNT.h"
#include "Ept.h"

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

	//Terminate Vmx
	if (ExitContext->GuestContext->GuestRAX == VMX_CPUID_TERMINATE_PWD) {
		ExitHandleTerminateVmx(ProcessorContext, ExitContext);
		return;
	}

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
	__debugbreak();
	if (ExitContext->GuestContext->GuestRCX == VMX_VMCALL_HOOK_PWD) {
		HxTLog("VmCall add page hook at 0x%p to 0x%p trampoline at 0x%p", ExitContext->GuestContext->GuestRDX, ExitContext->GuestContext->GuestRAX, (PVOID*)ExitContext->GuestContext->GuestRBX);
		EptAddPageHook(ProcessorContext, ExitContext->GuestContext->GuestRDX, ExitContext->GuestContext->GuestRAX, (PVOID*)ExitContext->GuestContext->GuestRBX);
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

extern void __lgdt(const void*);
extern void __lidt(const void*);
extern void ArchRestoreContext(CONTEXT* Registers);
VOID ExitHandleTerminateVmx(PVMM_PROCESSOR_CONTEXT ProcessorContext, PVMEXIT_CONTEXT ExitContext) {
	__debugbreak();
	SIZE_T value = 0;
	SIZE_T VmError = 0;
	SIZE_T GuestInstructionLength = 0;
	SEGMENT_DESCRIPTOR_REGISTER_64 GdtRegister;
	SEGMENT_DESCRIPTOR_REGISTER_64 IdtRegister;

	__vmx_vmread(VMCS_GUEST_GDTR_LIMIT, &GdtRegister.Limit);
	__vmx_vmread(VMCS_GUEST_GDTR_BASE, &GdtRegister.BaseAddress);

	__vmx_vmread(VMCS_GUEST_LDTR_LIMIT, &IdtRegister.Limit);
	__vmx_vmread(VMCS_GUEST_LDTR_BASE, &IdtRegister.BaseAddress);

	//__lgdt(&GdtRegister);
	//__lidt(&IdtRegister);

	__vmx_vmread(VMCS_GUEST_CR3, &value);
	__writecr3(value);

	ProcessorContext->RegistersContext.Rsp = ExitContext->GuestContext->GuestRSP;

	VmxVmreadFieldToImmediate(VMCS_VMEXIT_INSTRUCTION_LENGTH, &GuestInstructionLength);
	ProcessorContext->RegistersContext.Rip = ExitContext->GuestRIP + GuestInstructionLength;
	ProcessorContext->SpecialRegistersContext.RflagsRegister.Flags = ExitContext->GuestFlags.EFLAGS.Flags;

	__vmx_off();

	ArchRestoreContext(&ProcessorContext->RegistersContext);
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
		ExitHandleEptViolation(ProcessorContext, ExitContext);
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