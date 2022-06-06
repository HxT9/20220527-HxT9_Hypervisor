#pragma once
#include "VmmContext.h"

/**
* Return    1   OK
* Return    -1  Vmx not available on cpu
* Return    -2  Vmx not enabled
*/
BYTE IsVmxSupported();

PVMM_CONTEXT InitializeVmx();
BOOL TerminateVmx(PVMM_CONTEXT Context);

/*
 * Get the current CPU context object from the global context by reading the current processor number.
 */
PVMM_PROCESSOR_CONTEXT GetCurrentCPUContext(PVMM_CONTEXT Context);

VOID FreeVmmContext(PVMM_CONTEXT Context);

/*
 * This function is the main handler of all exits that take place during VMM execution.
 *
 * This function is initially called from EnterFromGuest (defined in vmxdefs.asm). During EnterFromGuest,
 * the state of the guest's registers are stored to the stack and given as a stack pointer in GuestRegisters.
 * The value of most guest gp registers can be accessed using the PGPREGISTER_CONTEXT, but GuestRSP must
 * be read out of the VMCS due to the fact that, during the switch from guest to host, the HostRSP value
 * replaced RSP. During the switch, GuestRSP was saved back to the guest area of the VMCS so we can access
 * it with a VMREAD.
 *
 * Defined in Section 27.2 RECORDING VM-EXIT INFORMATION AND UPDATING VM-ENTRY CONTROL FIELDS, exits have two main
 * exit fields, one which describes what kind of exit just took place (ExitReason) and why it took place (ExitQualification).
 * By reading these two values, the exit handler can know exactly what steps it should take to handle the exit properly.
 *
 * When the exit handler is called by the CPU, interrupts are disabled. In order to call certain kernel api functions
 * in Type 2, we will need to enable interrupts. Therefore, the first action the handler must take is to ensure execution
 * of the handler is not executing below DISPATCH_LEVEL. This is to prevent the dispatcher from context switching away
 * from our exit handler if we enable interrupts, potentially causing serious memory synchronization problems.
 *
 * Next, a VMEXIT_CONTEXT is initialized with the exit information, including certain guest registers (RSP, RIP, RFLAGS)
 * from the VMCS.
 *
 * This function is given two arguments from EnterFromGuest in vmxdefs.asm:
 *      - The GlobalContext, which was saved to the top of the HostStack
 *      - The guest register context, which was pushed onto the stack during EnterFromGuest.
 *
 */
BOOL HandleVmExit(PVMM_CONTEXT Context, PGPREGISTER_CONTEXT GuestRegisters);

/*
 * If we're at this point, that means EnterFromGuest failed to enter back to the guest.
 * Print out the error informaiton and some state of the processor for debugging purposes.
 */
BOOL HandleVmExitFailure(PVMM_CONTEXT Context, PGPREGISTER_CONTEXT GuestRegisters);