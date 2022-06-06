#include "Vmx.h"
#include "Logger.h"
#include <intrin.h>

VOID VmxSetFixedBits()
{
    CR0 ControlRegister0;
    CR4 ControlRegister4;

    ControlRegister0.Flags = __readcr0();
    ControlRegister4.Flags = __readcr4();

    // Set required fixed bits for cr0
    ControlRegister0.Flags |= __readmsr(IA32_VMX_CR0_FIXED0);
    ControlRegister0.Flags &= __readmsr(IA32_VMX_CR0_FIXED1);

    // Set required fixed bits for cr4
    ControlRegister4.Flags |= __readmsr(IA32_VMX_CR4_FIXED0);
    ControlRegister4.Flags &= __readmsr(IA32_VMX_CR4_FIXED1);


    // Apply to the processor
    __writecr0(ControlRegister0.Flags);
    __writecr4(ControlRegister4.Flags);
}

BOOL VmxEnterRootMode(PVMM_PROCESSOR_CONTEXT Context)
{
    // Enable VMXe in CR4 of the processor
	ArchEnableVmxe();

    // Ensure the required fixed bits are set in cr0 and cr4, as per the spec.
    VmxSetFixedBits();

    // Execute VMXON to bring processor to VMX mode
    // Check RFLAGS.CF == 0 to ensure successful execution
    unsigned char returnCode = __vmx_on((ULONGLONG*)&Context->VmxonRegionPhysical);
    if (returnCode != 0)
    {
        HxTLog("VMXON failed with code %d.\n", returnCode);
        return FALSE;
    }

    // And clear the VMCS before writing the configuration entries to it
    returnCode = __vmx_vmclear((ULONGLONG*)&Context->VmcsRegionPhysical);
    if (returnCode != 0)
    {
        HxTLog("VMCLEAR failed with code %d.\n", returnCode);
        return FALSE;
    }

    // Now load the blank VMCS
    returnCode = __vmx_vmptrld((ULONGLONG*)&Context->VmcsRegionPhysical);
    if (returnCode != 0)
    {
        HxTLog("VMPTRLD failed with code %d.\n", returnCode);
        return FALSE;
    }

    return TRUE;
}

BOOL VmxExitRootMode(PVMM_PROCESSOR_CONTEXT Context)
{
    HxTLog("Exiting VMX.\n");

    // Clear the VMCS before VMXOFF (Specification requires this)
    if (__vmx_vmclear((ULONGLONG*)&Context->VmcsRegionPhysical) != 0)
    {
        HxTLog("VMCLEAR failed.\n");
    }

    // Turn off VMX
    __vmx_off();

    // Turn off VMXe in CR4
    ArchDisableVmxe();

    // TODO: Reset fixed cr bits.

    return TRUE;
}

/*
 * Give a printout when VMX instructions or vmexits fail.
 *
 * Reads the instruction error from the VMCS.
 */
VOID VmxPrintErrorState(PVMM_PROCESSOR_CONTEXT Context)
{
    UINT64 FailureCode;

    UNREFERENCED_PARAMETER(Context);

    // TODO: Add register context

    // Read the failure code
    if (__vmx_vmread(VMCS_VM_INSTRUCTION_ERROR, &FailureCode) != 0)
    {
        HxTLog("VmxPrintErrorState: Failed to read error code.\n");
        return;
    }

    HxTLog("VmxPrintErrorState: VMLAUNCH Error = 0x%llx\n", FailureCode);
}

BOOL VmxLaunchProcessor(PVMM_PROCESSOR_CONTEXT Context)
{
    HxTLog("VmxLaunchProcessor: VMLAUNCH....\n");

    // Launch the VMCS! If this returns, there was an error.
    // Otherwise, execution continues in guest_resumes_here from vmxdefs.asm
    __vmx_vmlaunch();

    VmxPrintErrorState(Context);

    VmxExitRootMode(Context);

    return FALSE;
}
